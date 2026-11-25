/**
 * tcs_bootrec.c — 上次运行记录：落点与刷新（X04-F-03 / R5-13；说明见 tcs_bootrec.h）
 *
 * 为什么要有它：2026-11-24 现场 ESP32 运行 1264 s 时自行复位一次，复位原因无法事后取证——
 * `status` 里的 rst= 只有有人及时敲才看得到，启动日志又在 USB 串口重枚举期间丢失。
 */
#include "tcs_bootrec.h"
#include "boot_record.h"
#include "control.h"
#include "nvs_store.h"

#include <inttypes.h>
#include <string.h>
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
#include "esp_core_dump.h"
#define TCS_COREDUMP_SUMMARY 1
#else
#define TCS_COREDUMP_SUMMARY 0
#endif

static const char *TAG = "TCS";

/* 与 IDF 的 esp_reset_reason_t 对表：boot_record.h 里的数值任何一边漂了，这里编译不过 */
_Static_assert(ESP_RST_UNKNOWN == BOOTREC_RST_UNKNOWN && ESP_RST_POWERON == BOOTREC_RST_POWERON
               && ESP_RST_EXT == BOOTREC_RST_EXT && ESP_RST_SW == BOOTREC_RST_SW
               && ESP_RST_PANIC == BOOTREC_RST_PANIC && ESP_RST_INT_WDT == BOOTREC_RST_INT_WDT
               && ESP_RST_TASK_WDT == BOOTREC_RST_TASK_WDT && ESP_RST_WDT == BOOTREC_RST_WDT
               && ESP_RST_BROWNOUT == BOOTREC_RST_BROWNOUT && ESP_RST_USB == BOOTREC_RST_USB
               && ESP_RST_CPU_LOCKUP == BOOTREC_RST_CPU_LOCKUP,
               "boot_record.h reset-reason numbers drifted away from esp_reset_reason_t");

/* 存放处：RTC 慢速存储的 NOINIT 段。软复位 / panic / 看门狗复位都保得住；冷上电是随机内容，
 * bootrec_start 用魔数 + 版本 + 长度 + CRC 识破（tests/test_tcs_core.c 的 B2/B4）。*/
static RTC_NOINIT_ATTR boot_record_t s_rec;
static bootrec_prev_t s_prev;           /* 上电时抄出的上一轮记录，之后只读 */
static uint8_t s_prev_ready;
static uint8_t s_core_dump_present;     /* flash 里留着上一次异常的 core dump */
static TimerHandle_t s_timer;

static void refresh_once(void)
{
    bootrec_refresh(&s_rec, (uint32_t)(esp_timer_get_time() / 1000000LL), g_tcs.alarm_bits,
                    control_safety_latched(), (uint8_t)control_state());
}

/* 定时器任务里跑：只写 24 字节 RAM + 算一次 CRC，微秒级 */
static void refresh_cb(TimerHandle_t t)
{
    (void)t;
    refresh_once();
}

/* flash 里有没有上一次异常留下的 core dump；有就尽量把任务名 / PC / 回溯解出来直接打进日志——
 * 现场没有工具也能知道"死在哪个任务"。完整导出用 `idf.py -p <端口> coredump-info` 读取。*/
static void report_core_dump(void)
{
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
    size_t addr = 0, size = 0;
    esp_err_t ce = esp_core_dump_image_get(&addr, &size);
    if (ce == ESP_ERR_NOT_FOUND) {
        ESP_LOGI(TAG, "[SYSTEM] core dump: none stored");
        return;
    }
    if (ce != ESP_OK) {
        ESP_LOGW(TAG, "[SYSTEM] core dump: check failed (%s)", esp_err_to_name(ce));
        return;
    }
    s_core_dump_present = 1;
    ESP_LOGW(TAG, "[SYSTEM] core dump from the previous crash is stored in flash: %u bytes @0x%08x "
             "(read it with: idf.py coredump-info)", (unsigned)size, (unsigned)addr);
#if TCS_COREDUMP_SUMMARY
    {
        esp_core_dump_summary_t s;
        if (esp_core_dump_get_summary(&s) == ESP_OK) {
            char task[17];
            uint32_t n = s.exc_bt_info.depth;
            if (n > 4) n = 4;
            memcpy(task, s.exc_task, sizeof s.exc_task);
            task[sizeof s.exc_task] = '\0';
            ESP_LOGW(TAG, "[SYSTEM] core dump summary: task='%s' pc=0x%08" PRIx32 " cause=%" PRIu32
                     " bt=0x%08" PRIx32 " 0x%08" PRIx32 " 0x%08" PRIx32 " 0x%08" PRIx32 "%s",
                     task, s.exc_pc, s.ex_info.exc_cause,
                     n > 0 ? s.exc_bt_info.bt[0] : 0u, n > 1 ? s.exc_bt_info.bt[1] : 0u,
                     n > 2 ? s.exc_bt_info.bt[2] : 0u, n > 3 ? s.exc_bt_info.bt[3] : 0u,
                     s.exc_bt_info.corrupted ? " (backtrace corrupted)" : "");
        }
    }
#endif
#else
    ESP_LOGI(TAG, "[SYSTEM] core dump: disabled in this build");
#endif
}

void tcs_bootrec_boot(void)
{
    esp_reset_reason_t rr = esp_reset_reason();
    uint32_t seq = bootrec_start(&s_rec, (uint8_t)rr, &s_prev);
    s_prev_ready = 1;
    /* 启动第一行就带复位原因（X04-F-03）。"上一轮怎么结束的"看本轮的复位原因；
     * "上一轮是怎么起来的、跑了多久"看上一轮的记录。*/
    ESP_LOGI(TAG, "[SYSTEM] boot #%" PRIu32 " reset reason=%d (%s)",
             seq, (int)rr, bootrec_reset_name((int)rr));
    if (s_prev.valid) {
        ESP_LOGI(TAG, "[SYSTEM] last run: started by %d (%s), ran %" PRIu32 "s, "
                 "last refresh alarm=0x%04X latch=0x%02X state=%u",
                 (int)s_prev.reset_reason, bootrec_reset_name((int)s_prev.reset_reason),
                 s_prev.uptime_s, (unsigned)s_prev.alarm, (unsigned)s_prev.latch,
                 (unsigned)s_prev.ctrl_state);
    } else {
        ESP_LOGI(TAG, "[SYSTEM] last run: no record (cold power-on / first run / corrupted)");
    }
    if (bootrec_is_crash((int)rr))
        ESP_LOGW(TAG, "[SYSTEM] the previous run ended abnormally: %s", bootrec_reset_name((int)rr));
    report_core_dump();
}

esp_err_t tcs_bootrec_start_refresh(void)
{
    if (s_timer) return ESP_OK;
    refresh_once();
    s_timer = xTimerCreate("tcs_brec", pdMS_TO_TICKS(1000), pdTRUE, NULL, refresh_cb);
    if (!s_timer) return ESP_ERR_NO_MEM;
    if (xTimerStart(s_timer, 0) != pdPASS) return ESP_FAIL;
    return ESP_OK;
}

size_t tcs_bootrec_status(char *out, size_t cap)
{
    /* 格式化在纯模块里（主机用例 test_tcs_core.c 的 B10）；这里只把数据递过去。
     * 没跑过 tcs_bootrec_boot()（不该发生）时传 NULL → 输出 prst=-1，不会读到未初始化的 s_prev。*/
    return bootrec_format_prev(s_prev_ready ? &s_prev : NULL, safety_state_last_alarm(),
                               s_core_dump_present, out, cap);
}
