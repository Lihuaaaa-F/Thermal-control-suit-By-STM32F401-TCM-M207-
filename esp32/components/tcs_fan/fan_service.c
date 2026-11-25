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
#include "esp_rom_sys.h"      /* esp_rom_delay_us：引脚电平采样间隔 */
#include "esp_timer.h"        /* "命令-回读"周期核对 */

#include "param_registry.h"     /* g_tcs.fan_on */

#define TAG "FAN"

#define FAN_LEDC_SPEED_MODE LEDC_LOW_SPEED_MODE
#define FAN_LEDC_TIMER      LEDC_TIMER_0
#define FAN_LEDC_CHANNEL    LEDC_CHANNEL_0

/* 头文件里的占空比换算按 10 位满量程写死；LEDC 分辨率若改了，这里编译期就报 */
_Static_assert(((1 << LEDC_TIMER_10_BIT) - 1) == FAN_DUTY_FULL,
               "FAN_DUTY_FULL must match LEDC_TIMER_10_BIT");

static uint8_t  s_pct;          /* 当前设定 0-100 */
static uint32_t s_set_cnt;      /* 设定次数（变更路径统计）*/
static uint32_t s_err_cnt;      /* LEDC 写失败次数（R1 M13-F-05：失败必须可见）*/
static uint8_t  s_ledc_ready;   /* LEDC 通道已配置（WS2812 借脚前停/还逻辑的门）*/

/* 低=开：低电平占空比 = pct% → LEDC duty（高电平计数）= (100-pct)/100 × 满。
 * 换算在头文件里（fan_duty_of_pct，主机可测），这里只是别名。 */
#define pct_to_duty(p) fan_duty_of_pct(p)

static void fan_check_start(void);   /* "命令-回读"周期核对（实现在文件后部）*/

/* ====== WS2812 清灯（官方 ESP32-S3-DevKitC-1：v1.0 RGB=GPIO48 / v1.1 RGB=GPIO38，
 * "Addressable RGB LED"；两代都在板载，风扇 PWM 会被灯珠当噪声数据锁存随机亮色）======
 * 发任意 RGB 帧后**必须把脚收成输出高**：浮空的 DIN 会采集噪声重新锁存（清了白清）；
 * 恒高=无数据=灯保持最后一帧颜色。真风扇光耦耐受 ~30µs 低脉冲，无害。*/
static void ws2812_send_frame(int gpio_num, uint8_t g, uint8_t r, uint8_t b)
{
    /* 若目标是风扇脚（FAN_GPIO = 42），先停 LEDC 让出引脚，发完恢复 */
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
        /* 借脚期间 RMT 驱动过这个脚；还回去之后把回读用的输入缓冲重新打开 */
        gpio_input_enable(FAN_GPIO);
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
    /* 开机清板载 RGB。这两脚（v1.0 板 = 48，v1.1 板 = 38）只是 RGB 灯珠，不是风扇：
     * 风扇 PWM 在 FAN_GPIO = 42（v2.1 换脚，见头文件）。 */
    fan_ws2812_black_frame(48);
    fan_ws2812_black_frame(38);
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
    /* "命令-回读"要读引脚电平：显式打开输入缓冲，不依赖焊盘复位后的默认值
     * （只开输入缓冲，不改输出：引脚仍由 LEDC 驱动）。 */
    err = gpio_input_enable(FAN_GPIO);
    if (err != ESP_OK)
        ESP_LOGW(TAG, "gpio_input_enable(%d) fail %s — pin level readback unavailable",
                 FAN_GPIO, esp_err_to_name(err));
    fan_check_start();
    ESP_LOGI(TAG, "service started gpio=%u ledc %ukHz/10bit low=on duty=0(off)",
             (unsigned)FAN_GPIO, (unsigned)(FAN_FREQ_HZ / 1000));
    return 0;
}

/* ====== "命令-回读"对账（S-G e2：X04-F-05 ②、M13-F-06）====== */
static uint32_t s_mismatch;          /* 周期核对发现回读与命令不符的累计次数 */
static fan_act_t s_act_last = FAN_ACT_UNKNOWN;
static esp_timer_handle_t s_chk_timer;

fan_act_t fan_get_act(void)
{
    int lo = 1, hi = 0;
    if (!s_ledc_ready) return FAN_ACT_UNKNOWN;
    /* 连续采样引脚电平：16 点 × 3µs ≈ 48µs，覆盖 25kHz（40µs）的一个整周期。
     * 恒定电平时 lo == hi；在翻转时 lo=0、hi=1。 */
    for (int i = 0; i < FAN_ACT_SAMPLES; i++) {
        int l = gpio_get_level(FAN_GPIO);
        if (l < lo) lo = l;
        if (l > hi) hi = l;
        esp_rom_delay_us(3);
    }
    /* ledc_get_duty 读的是 LEDC 通道的硬件占空比寄存器，不是软件影子 */
    return fan_act_classify(s_pct, ledc_get_duty(FAN_LEDC_SPEED_MODE, FAN_LEDC_CHANNEL),
                            FAN_DUTY_FULL, lo, hi);
}

const char *fan_act_name(fan_act_t a)
{
    switch (a) {
    case FAN_ACT_OFF:      return "off";
    case FAN_ACT_ON:       return "on";
    case FAN_ACT_PWM:      return "pwm";
    case FAN_ACT_MISMATCH: return "MISMATCH";
    default:               return "unknown";
    }
}

uint32_t fan_mismatch_count(void) { return s_mismatch; }

static void fan_check_cb(void *arg)
{
    fan_act_t a = fan_get_act();
    (void)arg;
    if (a == FAN_ACT_MISMATCH) {
        s_mismatch++;
        if (s_mismatch == 1u || (s_mismatch % 60u) == 0u)
            ESP_LOGE(TAG, "fan readback mismatch: cmd=%u%% but LEDC duty / pin level disagree "
                         "(count=%lu) — ESP32-side output path is suspect",
                     (unsigned)s_pct, (unsigned long)s_mismatch);
    }
    s_act_last = a;
}

static void fan_check_start(void)
{
    const esp_timer_create_args_t ta = {
        .callback = fan_check_cb,
        .name = "fan_chk",
    };
    if (s_chk_timer) return;                 /* 只起一次 */
    if (esp_timer_create(&ta, &s_chk_timer) != ESP_OK ||
        esp_timer_start_periodic(s_chk_timer, 1000000ull) != ESP_OK) {
        s_chk_timer = NULL;
        ESP_LOGW(TAG, "fan readback timer not started — fan_get_act() still callable on demand");
    }
}

void fan_set_duty(uint8_t pct)
{
    if (pct > FAN_DUTY_MAX) pct = FAN_DUTY_MAX;
    /* R1 修复 M13-F-05：判 LEDC 返回值。失败时 fan_on 记 0（显示与实际一致）并留日志，
     * 不再"命令回声 = 输出状态"。*/
    esp_err_t e1 = ledc_set_duty(FAN_LEDC_SPEED_MODE, FAN_LEDC_CHANNEL, pct_to_duty(pct));
    esp_err_t e2 = ledc_update_duty(FAN_LEDC_SPEED_MODE, FAN_LEDC_CHANNEL);
    if (e1 != ESP_OK || e2 != ESP_OK) {
        s_err_cnt++;
        ESP_LOGE(TAG, "ledc set/update fail %s/%s (pct=%u)",
                 esp_err_to_name(e1), esp_err_to_name(e2), (unsigned)pct);
        s_pct = 0;
        g_tcs.fan_on = 0;
        return;
    }
    s_pct = pct;
    s_set_cnt++;
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
