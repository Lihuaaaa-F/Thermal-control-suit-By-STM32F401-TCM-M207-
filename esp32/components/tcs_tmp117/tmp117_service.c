/**
 * tmp117_service.c — TMP117 体温采样服务 IDF 外壳（R1c；S-G e2 起只剩与硬件绑定的部分）
 *
 * 一拍的决策——读器件、样本合理性、均值窗、健康机、掉线/恢复沿——在 tmp117_core.c
 * （主机可编译，逐拍可测）；本文件只有：I2C0 总线与器件句柄、100ms 采样任务、注入命令、上电自测。
 * 结构与 m207_service / m207_core 同型：核 1 定节拍任务 + 纯函数健康机/均值 + 掉线沿报警粘滞。
 * 官方查证：检索 components.espressif.com 2026-10-26，组件库 TMP117 仅 solderedelectronics GPL-3.0-only 第三方件→合法拒绝（F5 证据 docs/evidence/官方库检索/2026-10-26_components_espressif_com.md）；I2C 用官方 driver/i2c_master（IDF v5.5 新 API）；实现参考 IDF examples/peripherals/i2c/i2c_basic
 */
#include "tmp117_service.h"
#include "tmp117_core.h"
#include "tmp117_pure.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#include "param_registry.h"     /* g_tcs */
#include "control.h"            /* tcs_alarm_raise / ALM_BODY_SENSOR */
#include "post.h"               /* H6.7 post_body_sanity_ok */

#define TAG "TMP117"

#define TMP117_ADDR7        0x48
#define TMP117_I2C_TIMEOUT_MS 10

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static TaskHandle_t s_task;
static uint8_t s_started;
static tmp117_core_t s_core;
static uint32_t s_tick_max_us;          /* 拍耗时峰值（perf 预算 500µs）*/

/* ====== I2C 出入口（交给 tmp117_core）====== */
static int io_read_reg(void *ctx, uint8_t reg, uint8_t out[2])
{
    (void)ctx;
    return (i2c_master_transmit_receive(s_dev, &reg, 1, out, 2,
                                        TMP117_I2C_TIMEOUT_MS) == ESP_OK) ? 0 : -1;
}
static int io_write_reg(void *ctx, uint8_t reg, uint16_t val)
{
    (void)ctx;
    uint8_t b[3] = { reg, (uint8_t)(val >> 8), (uint8_t)val };
    return (i2c_master_transmit(s_dev, b, 3, TMP117_I2C_TIMEOUT_MS) == ESP_OK) ? 0 : -1;
}

/* ====== 采样任务（核 1；100ms 节拍）====== */
static void tmp117_task(void *arg)
{
    esp_task_wdt_add(NULL);   /* R1 P1-07：关键任务纳入 TWDT（原来只有 ctrl_svc）*/
    (void)arg;
    uint32_t p_win = 0;
    for (;;) {
        esp_task_wdt_reset();
        /* 失联降频：错满 TRIP 且非 sim → 1s 试联（F4 恢复检测 ≤1s 首样，3 成 ≤1.3s）*/
        vTaskDelay(pdMS_TO_TICKS(tmp117_core_period_ms(&s_core)));
        int64_t t0 = esp_timer_get_time();
        tmp117_core_tick(&s_core, (uint32_t)((uint64_t)t0 / 1000u));
        int32_t dt = (int32_t)esp_timer_get_time() - (int32_t)t0;
        if (dt > 0 && (uint32_t)dt > s_tick_max_us) s_tick_max_us = (uint32_t)dt;

        if (++p_win >= 100) {              /* 10s perf 行（第八章预算 500µs）*/
            p_win = 0;
            ESP_LOGI(TAG, "[TMP117P] tick_max=%luus (budget 500) err=%lu samples=%lu "
                     "lost=%lu rec=%lu sim=%u",
                     (unsigned long)s_tick_max_us, (unsigned long)s_core.stat_err,
                     (unsigned long)s_core.stat_samples, (unsigned long)s_core.lost_edges,
                     (unsigned long)s_core.recover_edges,
                     (unsigned)tmp117_core_sim_active(&s_core));
        }
    }
}

int tmp117_service_start(void)
{
    if (s_started) return -1;
    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = 0,
        .sda_io_num = GPIO_NUM_8,
        .scl_io_num = GPIO_NUM_9,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   /* 模块板载上拉则并联无害 */
    };
    esp_err_t err = i2c_new_master_bus(&bus_cfg, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus new failed %s", esp_err_to_name(err));
        return -1;
    }
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TMP117_ADDR7,
        .scl_speed_hz = 100000,
    };
    err = i2c_master_bus_add_device(s_bus, &dev_cfg, &s_dev);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c dev add failed %s", esp_err_to_name(err));
        return -1;
    }

    /* 身份核对、写配置、回读校验都在内核的第一拍里做（M13-F-08 / M13-F-02）：
     * DEVID 不是 0x0117 就不读它的温度；配置不符就不采样。硬件未接 = 身份读失败 → 计错 →
     * 1s 后掉线沿 ALM_BODY_SENSOR（F4 预期行为），不阻塞服务。*/
    tmp117_core_init(&s_core, &(tmp117_io_t){
        .read_reg = io_read_reg,
        .write_reg = io_write_reg,
    }, NULL);

    if (xTaskCreatePinnedToCore(tmp117_task, "tmp117_svc", 4096, NULL,
                                5, &s_task, 1) != pdPASS) return -1;
    s_started = 1;
    ESP_LOGI(TAG, "service started i2c0 sda=8 scl=9 100k core=1");
    return 0;
}

/* ====== 读侧 ====== */
uint8_t tmp117_body_ok(void) { return tmp117_core_ok(&s_core); }
float   tmp117_body_temp(void) { return g_tcs.body_temp; }

/* ====== 注入（E-21/E-22；仅 SIM / R1e 取证构建）====== */
uint8_t tmp117_sim_active(void) { return tmp117_core_sim_active(&s_core); }   /* 状态行永远可用 */
#if TCS_SIM_ENABLED
void tmp117_sim_set(float v) { tmp117_core_sim_set(&s_core, v); }
void tmp117_sim_disable(void) { tmp117_core_sim_disable(&s_core); }
#endif /* TCS_SIM_ENABLED */

/* ====== 上电自测：纯逻辑+健康机全路径板上取证（服务启动前独占）====== */
static int s_lb_fail;
#define TB_CHECK(cond, name) do { \
    if (!(cond)) { ESP_LOGE(TAG, "[TB117] FAIL %s", name); s_lb_fail++; } \
    else { ESP_LOGI(TAG, "[TB117] %s PASS", name); } \
} while (0)

void tmp117_lb_boottest(void)
{
    s_lb_fail = 0;
    ESP_LOGI(TAG, "[TB117] === boottest start ===");

    /* 纯逻辑：raw→°C 锚点（0=0.000 / 0x0190=400→3.125 / 0xFE70=-400→-3.125）*/
    TB_CHECK(tmp117_raw_to_c(0) == 0.0f, "raw 0");
    TB_CHECK(tmp117_raw_to_c(400) > 3.124f && tmp117_raw_to_c(400) < 3.126f,
             "raw 400 -> 3.125");
    TB_CHECK(tmp117_raw_to_c(-400) < -3.124f && tmp117_raw_to_c(-400) > -3.126f,
             "raw -400 -> -3.125");

    /* 均值窗口：8 推入+回绕 */
    tmp117_avg_t a;
    tmp117_avg_init(&a);
    for (int i = 1; i <= 8; i++) tmp117_avg_push(&a, (float)i);
    TB_CHECK(tmp117_avg_mean(&a) > 4.499f && tmp117_avg_mean(&a) < 4.501f,
             "avg 1..8 = 4.5");
    tmp117_avg_push(&a, 100.0f);       /* 回绕覆盖最老样本 */
    TB_CHECK(tmp117_avg_mean(&a) > 16.87f && tmp117_avg_mean(&a) < 16.88f,
             "avg wrap = (100,2..8)/8 = 16.875");

    /* 健康机：10 错掉线沿→3 成成功恢复数据→报警粘滞由 control 侧保证 */
    tmp117_health_t h;
    tmp117_health_init(&h);
    for (int i = 0; i < 9; i++) tmp117_health_step(&h, 0, NULL);
    TB_CHECK(h.ok == 1, "9 errs still ok");
    tmp117_health_step(&h, 0, NULL);
    TB_CHECK(h.ok == 0, "10th err trips");
    for (int i = 0; i < 2; i++) tmp117_health_step(&h, 1, NULL);
    TB_CHECK(h.ok == 0, "2 oks still down");
    tmp117_health_step(&h, 1, NULL);
    TB_CHECK(h.ok == 1, "3rd ok recovers");

    /* 服务状态在 tmp117_service_start → tmp117_core_init 里复位；这里先把真值置为"不就绪"
     * M06-F-06：未采到样本前一律"不就绪" */
    g_tcs.body_sensor_ok = 0;
    g_tcs.body_temp = 0.0f;

    ESP_LOGI(TAG, "[TB117] RESULT fail=%d %s", s_lb_fail,
             s_lb_fail ? "FAIL" : "PASS");
}
