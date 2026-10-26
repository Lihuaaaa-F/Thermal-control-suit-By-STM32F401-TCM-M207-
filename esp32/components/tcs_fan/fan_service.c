/**
 * fan_service.c — 风扇驱动实现（R1d；LEDC PWM，低=开）
 * 官方查证：检索 components.espressif.com 2026-10-26，无风扇 PWM 官方件外需求；PWM 用官方 driver/ledc；板载 RGB 已换官方组件 espressif/led_strip v3.1.1（F1，commit b741ef2）；实现参考 IDF examples/peripherals/ledc/ledc_basic
 */
#include "fan_service.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_log.h"

#include "param_registry.h"     /* g_tcs.fan_on */

#define TAG "FAN"

#define FAN_LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE
#define FAN_LEDC_TIMER      LEDC_TIMER_0
#define FAN_LEDC_CHANNEL    LEDC_CHANNEL_0

static uint8_t  s_pct;          /* 当前设定 0-100 */
static uint32_t s_set_cnt;      /* 设定次数（变更路径统计）*/
static uint8_t  s_ledc_ready;   /* LEDC 通道已配置（WS2812 借脚前停/还逻辑的门）*/

/* 低=开：低电平占空比 = pct% → LEDC duty（高电平计数）= (100-pct)/100 × 满 */
static uint32_t pct_to_duty(uint8_t pct)
{
    return (uint32_t)(FAN_DUTY_MAX - pct) * ((1 << LEDC_TIMER_10_BIT) - 1)
           / FAN_DUTY_MAX;
}

/* ====== WS2812 清灯（官方 ESP32-S3-DevKitC-1：v1.0 RGB=GPIO48 / v1.1 RGB=GPIO38，
 * "Addressable RGB LED"；两代都在板载，风扇 PWM 会被灯珠当噪声数据锁存随机亮色）======
 * 发任意 RGB 帧后**必须把脚收成输出高**：浮空的 DIN 会采集噪声重新锁存（清了白清）；
 * 恒高=无数据=灯保持最后一帧颜色。真风扇光耦耐受 ~30µs 低脉冲，无害。*/
static void ws2812_send_frame(int gpio_num, uint8_t g, uint8_t r, uint8_t b)
{
    /* 若目标是风扇脚（48），先停 LEDC 让出引脚，发完恢复 */
    uint8_t on_fan_pin = (gpio_num == FAN_GPIO) && s_ledc_ready;
    if (on_fan_pin) ledc_stop(FAN_LEDC_SPEED_MODE, FAN_LEDC_CHANNEL, 1);

    /* 官方 led_strip 驱动（手搓 RMT 编码被证伪：0.2/0.3/0.4µs T0H 全被读成 1）*/
    led_strip_handle_t strip = NULL;
    led_strip_config_t scfg = {
        .strip_gpio_num = gpio_num,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rcfg = {
        .resolution_hz = 10000000,          /* 官方默认 10MHz */
        .mem_block_symbols = 64,
    };
    esp_err_t e = led_strip_new_rmt_device(&scfg, &rcfg, &strip);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "led_strip new g%d fail %s", gpio_num, esp_err_to_name(e));
        goto restore;
    }
    ESP_LOGI(TAG, "ws2812 clear g%d (r=%u g=%u b=%u)", gpio_num, r, g, b);
    e = led_strip_set_pixel(strip, 0, r, g, b);
    if (e != ESP_OK) ESP_LOGE(TAG, "set_pixel fail %s", esp_err_to_name(e));
    e = led_strip_refresh(strip);           /* 阻塞发送 */
    if (e != ESP_OK) ESP_LOGE(TAG, "refresh fail %s", esp_err_to_name(e));
    led_strip_del(strip);
    /* 关键：锁存=帧后数据线保持低 ≥50µs（reset）；拉低 2ms 再停泊高 */
    gpio_config_t io = { .pin_bit_mask = 1ULL << gpio_num,
                         .mode = GPIO_MODE_OUTPUT, .intr_type = GPIO_INTR_DISABLE };
    gpio_config(&io);
    gpio_set_level(gpio_num, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
restore:
    if (on_fan_pin) {                      /* 恢复 LEDC（恒高=duty0）*/
        ledc_channel_config_t ccfg = {
            .speed_mode = FAN_LEDC_SPEED_MODE, .channel = FAN_LEDC_CHANNEL,
            .timer_sel = FAN_LEDC_TIMER, .intr_type = LEDC_INTR_DISABLE,
            .gpio_num = FAN_GPIO, .duty = pct_to_duty(s_pct), .hpoint = 0,
        };
        ledc_channel_config(&ccfg);
    } else {
        gpio_set_level(gpio_num, 1);       /* 非风扇脚停泊高：无数据=保持 */
    }
}

void fan_ws2812_black_frame(int gpio_num)
{
    ws2812_send_frame(gpio_num, 0, 0, 0);
}

/* 可视自证：红→黑（用户眼见红灯闪一下再灭 = 帧时序与引脚双确认）*/
void fan_ws2812_ledtest(int gpio_num)
{
    ws2812_send_frame(gpio_num, 0, 0xFF, 0);
    vTaskDelay(pdMS_TO_TICKS(700));
    ws2812_send_frame(gpio_num, 0, 0, 0);
}

int fan_service_start(void)
{
    fan_ws2812_black_frame(48);            /* 开机清板载 RGB：v1.0 脚 */
    fan_ws2812_black_frame(38);            /* v1.1 脚（官方文档 user_guide_v1.1）*/
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
    s_ledc_ready = 1;
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
