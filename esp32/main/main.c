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
/* ============ 诊断: BUSY 波形追踪(复位/PON/POF 各段电平轨迹,定命令通路) ============ */
static void trace_bus(const char *phase, int ms)
{
    char trace[201];
    int n = 0;
    int last = -1;
    for (int i = 0; i < ms / 10 && n < 198; i++) {
        int lv = gpio_get_level((gpio_num_t)CONFIG_EPAPER_BUSY_GPIO);
        if (lv != last) {
            n += snprintf(trace + n, sizeof(trace) - n, "%s%d@%ld ", lv ? "H" : "L", lv, (long)(i * 10));
            last = lv;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    trace[n] = 0;
    ESP_LOGI(TAG, "[TRACE] %s: %s", phase, n ? trace : "(全程无跳变)");
}
static void test_bus_trace(void)
{
    gpio_set_level((gpio_num_t)CONFIG_EPAPER_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)CONFIG_EPAPER_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level((gpio_num_t)CONFIG_EPAPER_RST_GPIO, 1);
    trace_bus("复位后 2s", 2000);

    epaper_write_cmd(0x04);   /* PON */
    trace_bus("PON 后 2s", 2000);

    epaper_write_cmd(0x00); epaper_write_data(0x8F);   /* PSR */
    trace_bus("PSR 后 1s", 1000);

    epaper_write_cmd(0x02);   /* POF */
    trace_bus("POF 后 2s", 2000);

    /* 判别实验: 悬空 vs 被驱动(芯片 BUSY 为推挽输出,弱上下拉改变不了被驱动线) */
    gpio_set_pull_mode((gpio_num_t)CONFIG_EPAPER_BUSY_GPIO, GPIO_PULLDOWN_ONLY);
    vTaskDelay(pdMS_TO_TICKS(50));
    int high_pd = 0;
    for (int i = 0; i < 100; i++) {
        high_pd += gpio_get_level((gpio_num_t)CONFIG_EPAPER_BUSY_GPIO);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    gpio_set_pull_mode((gpio_num_t)CONFIG_EPAPER_BUSY_GPIO, GPIO_PULLUP_ONLY);
    vTaskDelay(pdMS_TO_TICKS(50));
    int high_pu = 0;
    for (int i = 0; i < 100; i++) {
        high_pu += gpio_get_level((gpio_num_t)CONFIG_EPAPER_BUSY_GPIO);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    gpio_set_pull_mode((gpio_num_t)CONFIG_EPAPER_BUSY_GPIO, GPIO_FLOATING);
    ESP_LOGI(TAG, "[TRACE] 判别: 下拉时高电平 %d/100, 上拉时高电平 %d/100", high_pd, high_pu);
    if (high_pd < 5 && high_pu > 95) {
        ESP_LOGW(TAG, "[TRACE] 判定: BUSY 悬空! 上下拉都钉死了 —— 屏未接通/该线断,S0 实为未过");
    } else if (high_pd > 95 && high_pu < 5) {
        ESP_LOGI(TAG, "[TRACE] 判定: BUSY 被驱动为低(芯片在,处于忙/异常态)");
    } else {
        ESP_LOGI(TAG, "[TRACE] 判定: BUSY 有驱动迹象(上下拉未完全钉死)");
    }
    ESP_LOGI(TAG, "[TRACE] 判读: PON/PSR 段有新跳变=命令通路通;仅复位段动=命令没到芯片(查 DIN/CLK/CS/DC)");
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

    ESP_ERROR_CHECK(epaper_init());

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
    test_bus_trace();
#elif CONFIG_EPAPER_TEST_MODE == 6
    test_restart10();
#elif CONFIG_EPAPER_TEST_MODE == 0
    app_run();
#endif

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
