/**
 * control_service.c — control FSM 服务任务实现（R1e）
 *
 * ops 装配在 main（fan→tcs_fan / tec→tcs_m207）；本文件只管节拍/wdt/布防步进/perf。
 * M207 上电宽限语义沿用 control.c（5s）；body 传感器宽限由布防机制天然承载
 * （UNARMED 期 sensor 异常只记沿不进 SAFE，H743 WP-A.5 同款）。
 */
#include "control_service.h"
#include "control.h"
#include "post.h"
#include "param_registry.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#define TAG "CTRL"

static TaskHandle_t s_task;
static uint8_t s_started;
/* M09-F-12 / X05-F-05（S 线 e3a）：停喂狗的注入口只在取证构建里存在。此前实现体无条件编译，
 * 只是因为没人调用才被链接器丢掉；现在变量、使用点、函数与头文件声明四处同一个条件。*/
#if CONFIG_TCS_R1E_PROBE
static volatile uint8_t s_hang_test;   /* E-20：置 1 后本任务停喂狗（F12 注入口）*/
#endif
static uint32_t s_poll_max_us;      /* control_poll 段耗时峰值（预算 100µs）*/
static uint32_t s_polls;
static post_arm_ctx_t s_arm_ctx;    /* H6 布防三态机上下文 */

/* 节拍：任务 100ms 一拍（U 线 E1，原 500ms）。
 *   每拍：control_poll（故障发现 / 自愈计时 / 退出 SAFE 都是 100ms 粒度）+ 布防步进；
 *   参数落盘不在本任务里（M09-F-07）：由 persist_worker 每 500ms 冲洗一次（2s 防抖逻辑在 param_registry 内）。
 *   SAFE 期间给模块重申 TCSW=0 的频率由 control.c 内部限在 500ms，不随本节拍变密。
 * 背景：原来整个任务 500ms 一拍，post_step_100ms 里的 100ms 闸永远不触发——
 * "10 个健康样本回布防"实际约 5s（设计 1s）；通信类故障自愈要 2.0–2.5s，
 * 之后还要再等一整拍才退出 SAFE。*/
#define CTRL_TICK_MS        100u
#define CTRL_PERSIST_TICK_MS 500u   /* 落盘冲洗周期（persist_worker，M09-F-07：不再在控制任务里做）*/
/* 布防步进的最小间隔（半拍）：正常拍间隔有 ±1 个系统 tick 的抖动，按整拍闸会误吞正常的拍 */
#define CTRL_ARM_STEP_MIN_MS 50u

/* post 步进（H743 post_poll 272-288 行同语义；无 UI 角标——屏角标 R2 起）*/
static void post_step(uint32_t now)
{
    /* 最小间隔闸：任务被拖慢后 vTaskDelayUntil 会连着补跑错过的拍，不闸的话
     * "10 个健康样本 = 1s"会被压缩（m207_service 对同类问题也是这么处理的）。*/
    static uint32_t s_last_ms;
    if (now - s_last_ms < CTRL_ARM_STEP_MIN_MS) return;
    s_last_ms = now;

    /* 健康/已确认都要把"探头开路"算进去：M207 在线但温度不可用不算健康 */
    uint8_t healthy = (g_tcs.body_sensor_ok && g_tcs.m207_online
                       && !g_tcs.m207_sensor_open) ? 1u : 0u;
    uint8_t acked = (!(g_tcs.alarm_bits & ALM_BODY_SENSOR) &&
                     !(g_tcs.alarm_bits & ALM_M207) &&
                     !(g_tcs.alarm_bits & ALM_SENSOR_OPEN)) ? 1u : 0u;
    uint8_t mode08 = (uint8_t)param_get(PARAM_ID_COMMISSIONED);
    /* M09-F-06：可布防 = 双源健康 **且** 上电参数镜像校验通过。ESP32 没有 H743 的六项 POST 电池，
     * 以"镜像完好 + 双源健康"替代正典的"POST 过"（正典 0x08 那一行同提交注明）。
     * 原先 post_pass := healthy：镜像损坏 / 版本不匹配的机器照样开启完整 FMEA 布防。*/
    uint8_t post_pass = (uint8_t)(healthy && param_boot_image_ok());
    post_arm_t prev = post_arm_state();
    post_arm_t st = post_arm_step(&s_arm_ctx, mode08, post_pass, healthy, acked);
    post_arm_commit(st);
    if (st == POST_ARM_ARMED_ONLINE && prev == POST_ARM_LOST_ALARM)
        ESP_LOGI(TAG, "arm LOST->ARMED (10 healthy samples = 1s)");
    else if (st == POST_ARM_LOST_ALARM && prev == POST_ARM_ARMED_ONLINE)
        ESP_LOGI(TAG, "arm ARMED->LOST (source lost)");
    else if (st == POST_ARM_ARMED_ONLINE && prev == POST_ARM_UNARMED_ABSENT)
        ESP_LOGI(TAG, "arm UNARMED->ARMED (sources healthy)");
}

/* M09-F-07：参数落盘移出控制任务。原先每 5 拍在控制任务里同步做 nvs_set_blob（NVS 页满触发 GC 时
 * 可达数十到数百毫秒），100ms 的控制节拍被 flash 阻塞拖长。现在控制任务只改参数（置脏），
 * 落盘由本任务每 500ms 调一次 param_persist_flush（2s 防抖逻辑不变，在 param_registry 内；
 * 写路径与它同一把锁，见 M10-F-03）。不订阅 TWDT：它本来就可能被 flash 拖住。
 * 安全锁存的落盘（safety_state_save）不在此列——那必须同步、可靠。*/
static void persist_worker(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(CTRL_PERSIST_TICK_MS));
        param_persist_flush(control_ms_from_us(esp_timer_get_time()));
    }
}

static void control_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    esp_task_wdt_add(NULL);         /* 订阅 TWDT（3s；F3/F12）*/
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(CTRL_TICK_MS));
        int64_t t0_us = esp_timer_get_time();
        uint32_t now_ms = control_ms_from_us(t0_us);
        control_poll(now_ms);
        post_step(now_ms);
        /* 参数落盘已移到 persist_worker（M09-F-07）：控制任务里不再有任何 NVS 调用 */
        int32_t dt = (int32_t)(esp_timer_get_time() - t0_us);
        if (dt > 0 && (uint32_t)dt > s_poll_max_us) s_poll_max_us = (uint32_t)dt;
        s_polls++;
        /* 任务看门狗 CONFIG_ESP_TASK_WDT_TIMEOUT_S（本工程 3 s，PANIC=y：超时即 panic 复位）。
         * 控制任务 100ms 一拍、每拍喂一次，离超时还有 30 倍余量。*/
#if CONFIG_TCS_R1E_PROBE
        if (!s_hang_test) esp_task_wdt_reset();   /* E-20：取证构建里置 1 后停喂，验证 TWDT 复位 */
#else
        esp_task_wdt_reset();
#endif
    }
}

uint8_t control_service_armed(void) { return post_armed(); }
#if CONFIG_TCS_R1E_PROBE
void control_service_hang_test(uint8_t on) { s_hang_test = on; }
#endif

esp_err_t control_service_start(void)
{
    if (s_started) return ESP_ERR_INVALID_STATE;
    post_arm_init(&s_arm_ctx);
    /* P1-12：返回值由 app_main 汇总（svc_status）；失败照旧走 fail-safe */
    if (xTaskCreatePinnedToCore(control_task, "ctrl_svc", 4096, NULL,
                                4, &s_task, 1) != pdPASS) return ESP_ERR_NO_MEM;
    s_started = 1;
    if (xTaskCreate(persist_worker, "p_persist", 4096, NULL, 2, NULL) != pdPASS)
        ESP_LOGE(TAG, "persist worker did not start: parameter changes stay in RAM until the next reboot");
    ESP_LOGI(TAG, "service started core=1 %ums tick (poll+arm-step), wdt 3s",
             (unsigned)CTRL_TICK_MS);
    return ESP_OK;
}

/* perf 读侧（console perf 命令用）*/
uint32_t control_service_poll_max_us(void) { return s_poll_max_us; }
uint32_t control_service_polls(void) { return s_polls; }
