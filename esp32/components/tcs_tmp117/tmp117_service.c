/**
 * tmp117_service.c — TMP117 体温采样服务实现（R1c；结构与 m207_service 同型：
 * 核 1 定节拍任务+纯函数健康机/均值+掉线沿报警粘滞+注入 API+板上自测）
 * 官方查证：检索 components.espressif.com 2026-10-26，组件库 TMP117 仅 solderedelectronics GPL-3.0-only 第三方件→合法拒绝（F5 证据 docs/evidence/官方库检索/2026-10-26_components_espressif_com.md）；I2C 用官方 driver/i2c_master（IDF v5.5 新 API）；实现参考 IDF examples/peripherals/i2c/i2c_basic
 */
#include "tmp117_service.h"
#include "tmp117_pure.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "param_registry.h"     /* g_tcs */
#include "control.h"            /* tcs_alarm_raise / ALM_BODY_SENSOR */
#include "post.h"               /* H6.7 post_body_sanity_ok */

#define TAG "TMP117"

/* 注入能力开关（R1 修复 P0-06 / AD-5）：只有 SIM 或 R1e 取证构建才编译注入实现。
 * 生产固件里 `body` 命令与 s_sim_on 分支都不存在——一条命令注入假体温的通道被移除。*/
#if CONFIG_TCS_SIM || CONFIG_TCS_R1E_PROBE
#define TCS_SIM_ENABLED 1
#define SIM_ACTIVE()    (s_sim_on)
#else
#define TCS_SIM_ENABLED 0
#define SIM_ACTIVE()    (0u)          /* 编译期常 0：分支与注入实现一起消失 */
#endif

#define TMP117_ADDR7        0x48
#define TMP117_REG_TEMP     0x00
#define TMP117_REG_DEVID    0x0F
#define TMP117_DEVID_EXPECT 0x0117u
#define TMP117_SAMPLE_MS    100u
#define TMP117_I2C_TIMEOUT_MS 10
#define TMP117_LOST_PERIOD_MS 1000u   /* 失联降频：错满 10 次后 1s 试联 */

static i2c_master_bus_handle_t s_bus;
static i2c_master_dev_handle_t s_dev;
static TaskHandle_t s_task;
static uint8_t s_started;

static tmp117_health_t s_health;
static tmp117_avg_t    s_avg;
static float    s_last_valid_c;
static uint32_t s_last_valid_ms;
static uint8_t  s_have_valid;

#if TCS_SIM_ENABLED
static volatile float   s_sim_val;
static volatile uint8_t s_sim_on;
#endif

static uint32_t s_tick;                 /* 100ms 拍计数（dt 计算）*/
static uint32_t s_stat_err;             /* 累计通信/合理性错误 */
static uint32_t s_stat_samples;         /* 累计入窗样本 */
static uint32_t s_tick_max_us;          /* 拍耗时峰值（perf 预算 500µs）*/
static uint32_t s_lost_edges, s_recover_edges;

/* ====== 掉线沿（H743 同语义：布防门=WP-A.5；未布防只记沿不报警）====== */
static void on_sensor_lost(void)
{
    if (!post_armed()) {
        ESP_LOGW(TAG, "LOST (unarmed, log only)");
        return;
    }
    tcs_alarm_raise(ALM_BODY_SENSOR);
    s_lost_edges++;
    ESP_LOGW(TAG, "LOST (%u err-run) -> ALM_BODY_SENSOR (sticky)",
             (unsigned)TMP117_ERR_TRIP);
}

/* ====== 单样本接受：H6.7 合理性（25~45°C 且 |Δ|≤2°C/s）====== */
static int sample_accept(float c, uint32_t tick_now)
{
    if (!s_have_valid) return 1;
    float dt = (float)(tick_now - s_last_valid_ms) * (TMP117_SAMPLE_MS / 1000.0f);
    return post_body_sanity_ok(s_last_valid_c, c, dt);
}

/* ====== 采样任务（核 1；100ms 节拍）====== */
static void tmp117_task(void *arg)
{
    (void)arg;
    uint8_t reg = TMP117_REG_TEMP;
    uint8_t buf[2];
    uint32_t p_win = 0;
    for (;;) {
        /* 失联降频：错满 TRIP 且非 sim → 1s 试联（F4 恢复检测 ≤1s 首样，3 成 ≤1.3s）*/
        uint32_t period = (s_health.err_run >= TMP117_ERR_TRIP && !SIM_ACTIVE())
                          ? TMP117_LOST_PERIOD_MS : TMP117_SAMPLE_MS;
        vTaskDelay(pdMS_TO_TICKS(period));
        s_tick++;
        int32_t t0 = (int32_t)esp_timer_get_time();
        int ok = 0;

#if TCS_SIM_ENABLED
        if (s_sim_on) {
            tmp117_avg_push(&s_avg, s_sim_val);
            g_tcs.body_temp = tmp117_avg_mean(&s_avg);
            s_last_valid_c = s_sim_val;
            s_last_valid_ms = s_tick;
            s_have_valid = 1;
            s_stat_samples++;
            ok = 1;
        } else
#endif
        if (s_started && s_dev) {
            if (i2c_master_transmit_receive(s_dev, &reg, 1, buf, 2,
                                            TMP117_I2C_TIMEOUT_MS) == ESP_OK) {
                int16_t raw = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
                float c = tmp117_raw_to_c(raw);
                if (sample_accept(c, s_tick)) {
                    tmp117_avg_push(&s_avg, c);
                    g_tcs.body_temp = tmp117_avg_mean(&s_avg);
                    s_last_valid_c = c;
                    s_last_valid_ms = s_tick;
                    s_have_valid = 1;
                    s_stat_samples++;
                    ok = 1;
                } else {
                    s_stat_err++;
                    ESP_LOGW(TAG, "implausible sample %.2f dropped (H6.7)",
                             (double)c);
                }
            } else {
                s_stat_err++;
            }
        }
        uint8_t ok_before = s_health.ok;
        tmp117_health_step(&s_health, ok, on_sensor_lost);
        if (ok_before && !s_health.ok) {
            g_tcs.body_sensor_ok = 0;      /* 掉线沿 */
        } else if (!ok_before && s_health.ok) {
            g_tcs.body_sensor_ok = 1;      /* 恢复沿（数据流恢复；报警位保持）*/
            s_recover_edges++;
            ESP_LOGW(TAG, "RECOVERED (data flow; alarm stays until ack)");
        }
        int32_t dt = (int32_t)esp_timer_get_time() - t0;
        if (dt > 0 && (uint32_t)dt > s_tick_max_us) s_tick_max_us = (uint32_t)dt;

        if (++p_win >= 100) {              /* 10s perf 行（第八章预算 500µs）*/
            p_win = 0;
            ESP_LOGI(TAG, "[TMP117P] tick_max=%luus (budget 500) err=%lu samples=%lu "
                     "lost=%lu rec=%lu sim=%u",
                     (unsigned long)s_tick_max_us, (unsigned long)s_stat_err,
                     (unsigned long)s_stat_samples, (unsigned long)s_lost_edges,
                     (unsigned long)s_recover_edges, (unsigned)SIM_ACTIVE());
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

    /* 上电探活：读 DEVID（0x0F 期望 0x0117）。硬件未接=失败留痕，不阻塞服务。*/
    uint8_t idreg = TMP117_REG_DEVID;
    uint8_t idbuf[2] = {0};
    if (i2c_master_transmit_receive(s_dev, &idreg, 1, idbuf, 2,
                                    TMP117_I2C_TIMEOUT_MS) == ESP_OK) {
        uint16_t id = (uint16_t)(((uint16_t)idbuf[0] << 8) | idbuf[1]);
        ESP_LOGI(TAG, "DEVID=0x%04X (%s)", id,
                 id == TMP117_DEVID_EXPECT ? "OK" : "UNEXPECTED");
    } else {
        ESP_LOGW(TAG, "DEVID probe NACK — 硬件未接（T1-T3 待接，Q12 标注）");
    }

    if (xTaskCreatePinnedToCore(tmp117_task, "tmp117_svc", 4096, NULL,
                                5, &s_task, 1) != pdPASS) return -1;
    s_started = 1;
    ESP_LOGI(TAG, "service started i2c0 sda=8 scl=9 100k core=1");
    return 0;
}

/* ====== 读侧 ====== */
uint8_t tmp117_body_ok(void) { return s_health.ok; }
float   tmp117_body_temp(void) { return g_tcs.body_temp; }

/* ====== 注入（E-21/E-22；仅 SIM / R1e 取证构建）====== */
uint8_t tmp117_sim_active(void) { return (uint8_t)SIM_ACTIVE(); }   /* 状态行永远可用 */
#if TCS_SIM_ENABLED
void tmp117_sim_set(float v)
{
    s_sim_val = v;
    s_sim_on = 1;
    ESP_LOGW(TAG, "[SIM] body temp forced %.2f", (double)v);
}

void tmp117_sim_disable(void)
{
    s_sim_on = 0;
    ESP_LOGW(TAG, "[SIM] body temp OFF (real I2C path; absent=loss path)");
}
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

    /* 健康机：10 错掉线沿→3 成功恢复数据→报警粘滞由 control 侧保证 */
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

    /* 服务状态复位（boottest 独占于服务启动前）*/
    tmp117_health_init(&s_health);
    tmp117_avg_init(&s_avg);
    s_have_valid = 0;
    g_tcs.body_sensor_ok = 1;
    g_tcs.body_temp = 0.0f;

    ESP_LOGI(TAG, "[TB117] RESULT fail=%d %s", s_lb_fail,
             s_lb_fail ? "FAIL" : "PASS");
}
