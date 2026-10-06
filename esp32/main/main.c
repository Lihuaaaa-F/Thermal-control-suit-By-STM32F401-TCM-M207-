/* 调温服 ESP32-S3 N16R8 无线端 (ESP-IDF v5.5)
 *
 * 日常闭环: bash ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh all
 * 门禁测试: Kconfig EPAPER_TEST_MODE(menuconfig 切换) —— docs/epaper-workflow.md
 *
 * 引脚红线: GPIO33-37(Octal PSRAM) 26-32(Flash) 19/20(USB) 0/3/45/46(strap) 不可外用;
 *          GPIO1-10 预留 ADC1(NTC)。墨水屏: 11/12/13/14/21/39。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "driver/gpio.h"
#include "epaper.h"
#include "epaper_io.h"
#include "epaper_paint.h"

static const char *TAG = "TCS";

#if CONFIG_EPAPER_TEST_MODE == 0
/* ============ 正常 app: display_task + change-driven(工作流 §6) ============ */
typedef struct { int tenths; } disp_msg_t;
static QueueHandle_t s_disp_q;

static void display_task(void *arg)
{
    static int last_shown = INT32_MAX;   /* change-driven: 与上次显示差 <0.5°C 则不刷 */
    uint8_t *black = heap_caps_malloc(EPAPER_PLANE_BYTES, MALLOC_CAP_DMA);
    uint8_t *red   = heap_caps_malloc(EPAPER_PLANE_BYTES, MALLOC_CAP_DMA);
    configASSERT(black && red);
    disp_msg_t m;
    while (1) {
        if (xQueueReceive(s_disp_q, &m, portMAX_DELAY) != pdTRUE) continue;
        if (last_shown != INT32_MAX && abs(m.tenths - last_shown) < 5) {
            ESP_LOGI(TAG, "[DISPLAY] change-driven skip(%d, 差<0.5°C)", m.tenths);
            continue;
        }
        int64_t t0 = esp_timer_get_time();
        paint_clear(black, true);
        paint_clear(red, true);
        paint_render_temp(black, 8, 80, 120, m.tenths);
        int64_t render_us = esp_timer_get_time() - t0;
        esp_err_t e = epaper_draw_full(black, red);
        ESP_LOGI(TAG, "[DISPLAY] %s render=%lldms total=%lldms temp=%d",
                 esp_err_to_name(e), render_us / 1000,
                 (esp_timer_get_time() - t0) / 1000, m.tenths);
        last_shown = m.tenths;
        epaper_deep_sleep();
    }
}

static void app_run(void)
{
    s_disp_q = xQueueCreate(8, sizeof(disp_msg_t));
    xTaskCreate(display_task, "display", 4096, NULL, 5, NULL);
    /* 演示序列: 25.0 首刷 → 25.2 跳过 → 26.0 再刷(change-driven 生效即两项日志) */
    disp_msg_t seq[] = {{250}, {252}, {260}};
    for (int i = 0; i < 3; i++) {
        xQueueSend(s_disp_q, &seq[i], portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(18000));   /* 全刷 ~15s,留完成间隔 */
    }
    ESP_LOGI(TAG, "[APP] demo 序列完成,后续生产者(WiFi/BLE/STM32)经同一队列接入");
}
#elif CONFIG_EPAPER_TEST_MODE == 2
/* ============ S3 门: 三色条(上黑/中白/下红),兼任屏版本终审 ============ */
static uint8_t s_black[EPAPER_PLANE_BYTES], s_red[EPAPER_PLANE_BYTES];
static void test_bars(void)
{
    paint_clear(s_black, true);
    paint_clear(s_red, true);
    paint_fill_rect(s_black, 0, 0, PAINT_W, PAINT_H / 3, true);              /* 上 1/3 黑 */
    paint_fill_rect(s_red, 0, PAINT_H * 2 / 3, PAINT_W, PAINT_H / 3, true);  /* 下 1/3 红 */
    int64_t t0 = esp_timer_get_time();
    esp_err_t e = epaper_draw_full(s_black, s_red);
    ESP_LOGI(TAG, "[TEST] S3 三色条: %s, 耗时 %lld ms —— 期望上黑/中白/下红(红缺失=版本错配)",
             esp_err_to_name(e), (esp_timer_get_time() - t0) / 1000);
}
#elif CONFIG_EPAPER_TEST_MODE == 3
/* ============ S2 门: 白屏 + 深睡→自动唤醒→再白屏循环 ============ */
static void test_white_sleepwake(void)
{
    int64_t t0 = esp_timer_get_time();
    esp_err_t e1 = epaper_clear();
    int64_t d1 = (esp_timer_get_time() - t0) / 1000;
    ESP_LOGI(TAG, "[TEST] S2 白屏#1: %s, %lld ms", esp_err_to_name(e1), d1);

    ESP_ERROR_CHECK(epaper_deep_sleep());
    ESP_LOGI(TAG, "[TEST] S2 深睡完成,触发自动唤醒再刷");
    t0 = esp_timer_get_time();
    esp_err_t e2 = epaper_clear();
    ESP_LOGI(TAG, "[TEST] S2 白屏#2(唤醒后): %s, %lld ms —— 两者皆 ESP_OK 且耗时接近 = sleep/wake 循环通过",
             esp_err_to_name(e2), (esp_timer_get_time() - t0) / 1000);
}
#elif CONFIG_EPAPER_TEST_MODE == 4
/* ============ S4 门: 老化 N 次全刷 + 逐次计时(Kconfig EPAPER_AGING_COUNT) ============ */
static void test_aging(void)
{
    int fail = 0;
    int64_t worst = 0, sum = 0;
    for (int i = 1; i <= CONFIG_EPAPER_AGING_COUNT; i++) {
        int64_t t0 = esp_timer_get_time();
        esp_err_t e = epaper_clear();
        int64_t ms = (esp_timer_get_time() - t0) / 1000;
        if (e != ESP_OK) fail++;
        if (ms > worst) worst = ms;
        sum += ms;
        ESP_LOGI(TAG, "[TEST] S4 aging %d/%d: %s %lld ms", i, CONFIG_EPAPER_AGING_COUNT,
                 esp_err_to_name(e), ms);
    }
    ESP_LOGI(TAG, "[TEST] S4 汇总: %d 次, 失败 %d, 最长 %lld ms, 平均 %lld ms, SPI=%d Hz",
             CONFIG_EPAPER_AGING_COUNT, fail, worst, sum / CONFIG_EPAPER_AGING_COUNT,
             CONFIG_EPAPER_SPI_HZ);
    epaper_deep_sleep();
}
#elif CONFIG_EPAPER_TEST_MODE == 6
/* ============ S6 门: 重启 x10(NVS 计数),每轮走一遍显示路径 ============ */
static void test_restart10(void)
{
    nvs_handle_t h;
    ESP_ERROR_CHECK(nvs_open("tcs", NVS_READWRITE, &h));
    uint32_t cnt = 0;
    nvs_get_u32(h, "boot_cnt", &cnt);
    cnt++;
    nvs_set_u32(h, "boot_cnt", cnt);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "[TEST] S6 第 %u/10 次启动", (unsigned)cnt);

    static uint8_t s_black[EPAPER_PLANE_BYTES], s_red[EPAPER_PLANE_BYTES];
    paint_clear(s_black, true);
    paint_clear(s_red, true);
    paint_render_temp(s_black, 8, 80, 120, (int)cnt * 10);   /* 显示启动次数(如 3.0C) */
    ESP_ERROR_CHECK(epaper_draw_full(s_black, s_red));
    epaper_deep_sleep();

    if (cnt < 10) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
    ESP_LOGI(TAG, "[TEST] S6 完成: 连续 10 次启动显示路径全部正常");
}
#elif CONFIG_EPAPER_TEST_MODE == 5
/* ============ 接线发现模式(裸屏直连无示波器时的自动化穷举) ============
 * 前提: 面板 6 信号线接在这 6 个 devkit 引脚上但顺序未知(错位/颠倒场景)。
 * 阶段1: 每个候选 RST 引脚打复位脉冲,其余引脚上拉输入找 LOW 忙相 -> 定 RST/BUSY 对
 * 阶段2: 剩余 4 引脚的 24 种 CLK/MOSI/CS/DC 排列逐个发 PON,BUSY 出现忙相即命中
 * 全程无命中 => 电源级问题(VCC/GND/接触),只能人工处理 */
#include "driver/spi_master.h"
static const gpio_num_t WIRING[6] = {11, 12, 13, 14, 21, 39};   /* 原 CLK MOSI CS DC RST BUSY 位置 */
static const char *WNAME[6] = {"CLK", "MOSI", "CS", "DC", "RST", "BUSY"};

static bool busy_low_pulse(gpio_num_t busy, int ms)
{
    for (int i = 0; i < ms / 5; i++) {
        if (gpio_get_level(busy) == 0) return true;   /* 开漏忙=拉低 */
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    return false;
}

static void wire_discovery(void)
{
    /* 全候选拉高输入(除当前 RST 输出外) */
    for (int i = 0; i < 6; i++) {
        gpio_config_t io = { .pin_bit_mask = 1ULL << WIRING[i], .mode = GPIO_MODE_INPUT,
                             .pull_up_en = GPIO_PULLUP_ENABLE };
        gpio_config(&io);
    }
    /* 阶段 1: 找 RST/BUSY 对 */
    int rst_i = -1, busy_i = -1;
    for (int r = 0; r < 6 && rst_i < 0; r++) {
        gpio_set_pull_mode(WIRING[r], GPIO_FLOATING);
        gpio_config_t io = { .pin_bit_mask = 1ULL << WIRING[r], .mode = GPIO_MODE_OUTPUT };
        gpio_config(&io);
        gpio_set_level(WIRING[r], 1); vTaskDelay(pdMS_TO_TICKS(200));
        gpio_set_level(WIRING[r], 0); vTaskDelay(pdMS_TO_TICKS(2));
        gpio_set_level(WIRING[r], 1);
        for (int b = 0; b < 6; b++) {
            if (b == r) continue;
            if (busy_low_pulse(WIRING[b], 1200)) {
                rst_i = r; busy_i = b;
                ESP_LOGI(TAG, "[DISCOVER] RST=GPIO%d(%s) BUSY=GPIO%d(位置%s)",
                         WIRING[r], WNAME[r], WIRING[b], WNAME[b]);
                break;
            }
        }
        if (rst_i < 0) {
            gpio_set_pull_mode(WIRING[r], GPIO_PULLUP_ONLY);
            gpio_config_t io = { .pin_bit_mask = 1ULL << WIRING[r], .mode = GPIO_MODE_INPUT,
                                 .pull_up_en = GPIO_PULLUP_ENABLE };
            gpio_config(&io);
        }
    }
    if (rst_i < 0) {
        ESP_LOGW(TAG, "[DISCOVER] 阶段1 无命中: 6 引脚上均无复位应答 => 电源级问题(VCC/GND 反接/未接/接触不良),需人工核线");
        return;
    }
    /* 阶段 2: 4 根线全排列找 SPI */
    int rest[4], n = 0;
    for (int i = 0; i < 6; i++) if (i != rst_i && i != busy_i) rest[n++] = i;
    int perm[4][4] = {{0,1,2,3},{0,1,3,2},{0,2,1,3},{0,2,3,1},{0,3,1,2},{0,3,2,1},
                      {1,0,2,3},{1,0,3,2},{1,2,0,3},{1,2,3,0},{1,3,0,2},{1,3,2,0},
                      {2,0,1,3},{2,0,3,1},{2,1,0,3},{2,1,3,0},{2,3,0,1},{2,3,1,0},
                      {3,0,1,2},{3,0,2,1},{3,1,0,2},{3,1,2,0},{3,2,0,1},{3,2,1,0}};
    spi_bus_config_t bus; spi_device_handle_t dev = NULL; int found = -1;
    for (int p = 0; p < 24 && found < 0; p++) {
        int clk = rest[perm[p][0]], mosi = rest[perm[p][1]], cs = rest[perm[p][2]], dc = rest[perm[p][3]];
        memset(&bus, 0, sizeof(bus));
        bus.mosi_io_num = WIRING[mosi]; bus.sclk_io_num = WIRING[clk];
        bus.quadwp_io_num = -1; bus.quadhd_io_num = -1;
        if (spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) continue;
        spi_device_interface_config_t d = { .clock_speed_hz = 1000000, .mode = 0, .spics_io_num = -1, .queue_size = 2 };
        if (spi_bus_add_device(SPI2_HOST, &d, &dev) == ESP_OK) {
            gpio_set_direction(WIRING[cs], GPIO_MODE_OUTPUT); gpio_set_level(WIRING[cs], 1);
            gpio_set_direction(WIRING[dc], GPIO_MODE_OUTPUT); gpio_set_level(WIRING[dc], 0);
            uint8_t pon = 0x04;
            spi_transaction_t t1 = { .length = 8, .tx_buffer = &pon };
            gpio_set_level(WIRING[cs], 0);
            if (spi_device_polling_transmit(dev, &t1) == ESP_OK) {}
            gpio_set_level(WIRING[cs], 1);
            if (busy_low_pulse(WIRING[busy_i], 1200)) {
                found = p;
                ESP_LOGI(TAG, "[DISCOVER] 命中: CLK=GPIO%d MOSI=GPIO%d CS=GPIO%d DC=GPIO%d RST=GPIO%d BUSY=GPIO%d",
                         WIRING[clk], WIRING[mosi], WIRING[cs], WIRING[dc], WIRING[rst_i], WIRING[busy_i]);
            }
            spi_bus_remove_device(dev); dev = NULL;
        }
        spi_bus_free(SPI2_HOST);
    }
    if (found < 0) {
        ESP_LOGW(TAG, "[DISCOVER] 阶段2 无命中: RST/BUSY 对存在但 24 种 SPI 排列均无应答 => 查 DIN/CLK 接触或芯片供电");
    } else {
        ESP_LOGI(TAG, "[DISCOVER] 完成上方映射后: 改 Kconfig 默认引脚 -> 重建 -> TEST_MODE=1 过 S1");
    }
}
#endif

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_LOGI(TAG, "[SYSTEM] boot ok, free heap=%u", (unsigned)esp_get_free_heap_size());

#if CONFIG_EPAPER_TEST_MODE != 5
    ESP_ERROR_CHECK(epaper_init());
#endif

#if CONFIG_EPAPER_TEST_MODE == 1
    epaper_bus_test_t bus;
    ESP_ERROR_CHECK(epaper_selftest_bus(&bus));
#elif CONFIG_EPAPER_TEST_MODE == 2
    test_bars();
#elif CONFIG_EPAPER_TEST_MODE == 3
    test_white_sleepwake();
#elif CONFIG_EPAPER_TEST_MODE == 4
    test_aging();
#elif CONFIG_EPAPER_TEST_MODE == 5
    wire_discovery();
#elif CONFIG_EPAPER_TEST_MODE == 6
    test_restart10();
#elif CONFIG_EPAPER_TEST_MODE == 0
    app_run();
#endif

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
