/**
 * fan_service.c — 风扇驱动实现（R1d；LEDC PWM，低=开）
 */
#include "fan_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "param_registry.h"     /* g_tcs.fan_on */

#define TAG "FAN"

#define FAN_LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE
#define FAN_LEDC_TIMER      LEDC_TIMER_0
#define FAN_LEDC_CHANNEL    LEDC_CHANNEL_0

static uint8_t  s_pct;          /* 当前设定 0-100 */
static uint32_t s_set_cnt;      /* 设定次数（变更路径统计）*/

/* 低=开：低电平占空比 = pct% → LEDC duty（高电平计数）= (100-pct)/100 × 满 */
static uint32_t pct_to_duty(uint8_t pct)
{
    return (uint32_t)(FAN_DUTY_MAX - pct) * ((1 << LEDC_TIMER_10_BIT) - 1)
           / FAN_DUTY_MAX;
}

int fan_service_start(void)
{
    ledc_timer_config_t tcfg = {
        .speed_mode      = FAN_LEDC_SPEED_MODE,
        .timer_num       = FAN_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz         = FAN_FREQ_HZ,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc timer fail %s", esp_err_to_name(err));
        return -1;
    }
    ledc_channel_config_t ccfg = {
        .speed_mode = FAN_LEDC_SPEED_MODE,
        .channel    = FAN_LEDC_CHANNEL,
        .timer_sel  = FAN_LEDC_TIMER,
        .intr_type  = LEDC_INTR_DISABLE,
        .gpio_num   = FAN_GPIO,
        .duty       = pct_to_duty(0),   /* 上电=关（引脚恒高，F13 安全默认）*/
        .hpoint     = 0,
    };
    err = ledc_channel_config(&ccfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ledc channel fail %s", esp_err_to_name(err));
        return -1;
    }
    s_pct = 0;
    g_tcs.fan_on = 0;
    ESP_LOGI(TAG, "service started gpio=%u ledc %ukHz/10bit low=on duty=0(off)",
             (unsigned)FAN_GPIO, (unsigned)(FAN_FREQ_HZ / 1000));
    return 0;
}

void fan_set_duty(uint8_t pct)
{
    if (pct > FAN_DUTY_MAX) pct = FAN_DUTY_MAX;
    s_pct = pct;
    s_set_cnt++;
    ledc_set_duty(FAN_LEDC_SPEED_MODE, FAN_LEDC_CHANNEL, pct_to_duty(pct));
    ledc_update_duty(FAN_LEDC_SPEED_MODE, FAN_LEDC_CHANNEL);
    g_tcs.fan_on = (pct > 0) ? 1u : 0u;
}

void fan_set_on(uint8_t on)
{
    fan_set_duty(on ? FAN_DUTY_MAX : 0);
}

uint8_t fan_get_duty(void) { return s_pct; }

/* ====== 上电自测：两态电平回读（软件子集；电压两态=万用表挂账用户）====== */
static int s_fail;
#define FAN_CHECK(cond, name) do { \
    if (!(cond)) { ESP_LOGE(TAG, "[FANLB] FAIL %s", name); s_fail++; } \
    else { ESP_LOGI(TAG, "[FANLB] %s PASS", name); } \
} while (0)

void fan_lb_boottest(void)
{
    s_fail = 0;
    ESP_LOGI(TAG, "[FANLB] === boottest start ===");
    fan_service_start();

    fan_set_duty(0);
    vTaskDelay(pdMS_TO_TICKS(10));
    FAN_CHECK(gpio_get_level(FAN_GPIO) == 1, "duty 0 -> pin high (OFF)");

    fan_set_duty(100);
    vTaskDelay(pdMS_TO_TICKS(10));
    FAN_CHECK(gpio_get_level(FAN_GPIO) == 0, "duty 100 -> pin low (ON)");

    fan_set_duty(0);
    vTaskDelay(pdMS_TO_TICKS(10));
    FAN_CHECK(gpio_get_level(FAN_GPIO) == 1, "back to 0 -> high");
    FAN_CHECK(g_tcs.fan_on == 0, "g_tcs.fan_on mirrors OFF");

    ESP_LOGI(TAG, "[FANLB] RESULT fail=%d %s", s_fail,
             s_fail ? "FAIL" : "PASS");
}
