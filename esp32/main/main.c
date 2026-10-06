/* 调温服 ESP32-S3 N16R8 无线端骨架 (ESP-IDF v5.x)
 *
 * 日常闭环一条命令(skill 脚本,含 preflight/首错提取/stale 拦截):
 *   bash ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh all
 *
 * 引脚红线(写码前必读): skill platforms/esp32/SKILL.md 第 5 节
 *   - GPIO33-37 被 Octal PSRAM 占用,GPIO26-32 是 Flash,均不可外用
 *   - 温度模拟采样走 ADC1(GPIO1-10);ADC2 与 WiFi 互斥
 *
 * WiFi/BLE 场景 recipes: ~/.zcode/skills/esp-dev-skill/repos/esp-idf/recipes/
 *   wifi_sta.md / softap.md / ble_peripheral.md / nvs_storage.md
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "epaper.h"

static const char *TAG = "TCS";   // 分类前缀规范见根内核第 3 节

void app_main(void)
{
    /* NVS 必须最先初始化(WiFi 凭据/BLE 配网/业务配置都存这里);
     * 不处理这两个返回值是高频坑 #3 */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_LOGI(TAG, "[SYSTEM] boot ok, free heap=%u", (unsigned)esp_get_free_heap_size());

    /* 墨水屏总线初始化(微雪 2.9" V2;刷屏驱动待屏到手后移植) */
    ESP_ERROR_CHECK(epaper_init());

    /* TODO(显示): 屏到手后移植 EPD_2in9_V2 -> epaper_clear() 白屏验收
     * TODO(WiFi): recipes/wifi_sta.md —— 事件循环 + esp_wifi_start,凭据存 NVS
     * TODO(BLE):  recipes/ble_peripheral.md —— NimBLE 广播,接收配网写入 NVS
     * TODO(业务): 收 STM32 端温控帧 -> 上云/下发控制指令 */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
