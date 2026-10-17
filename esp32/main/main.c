/* main.c — 调温服 ESP32-S3 无线端（任务书 §4）
 *
 * ESP32 = 纯桥 + Web 服务器：不存业务参数、不做决策（H743 唯一数据源）。
 * 启动序：NVS → event loop → SoftAP(TCS-XXXX/12345678) → 链路任务(UART1 230400) → HTTP/WS :80
 * OTA（WP9 首期）：esp_ota 写入接口自测（从 factory 启动时，向 ota_0 写 4KB 校验后放弃）。
 *
 * 日常闭环：bash ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh all
 * 引脚红线：GPIO17(TX)→H743 PA3(RX)，GPIO18(RX)←H743 PD5(TX)；17/18 非保留脚 ✓
 */
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

#include "softap.h"
#include "link_task.h"
#include "http_server.h"

static const char *TAG = "TCS";

/* ====== WP9：esp_ota 写入接口自测（写 ota_0 → end 校验失败 → abort，验证管线）====== */
static void ota_selftest_once(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running && running->subtype != ESP_PARTITION_SUBTYPE_APP_FACTORY) {
        ESP_LOGW(TAG, "ota_selftest SKIP (running from %s, only from factory)",
                 running->label);
        return;
    }
    const esp_partition_t *ota0 = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, "ota_0");
    if (!ota0) {
        ESP_LOGE(TAG, "ota_selftest FAIL: ota_0 partition not found");
        return;
    }
    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(ota0, 4096, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_selftest FAIL: esp_ota_begin=%s", esp_err_to_name(err));
        return;
    }
    static const uint8_t pattern[256] = {1};
    for (int i = 0; i < 16; i++) {
        err = esp_ota_write(handle, pattern, sizeof(pattern));
        if (err != ESP_OK) break;
    }
    esp_err_t end_err = esp_ota_end(handle);   /* 非 app 镜像 → 预期校验失败 */
    if (end_err != ESP_OK) {
        ESP_LOGI(TAG, "ota_selftest PIPE OK: wrote 4KB, end rejects non-app as expected (%s)",
                 esp_err_to_name(end_err));
        esp_ota_abort(handle);
    } else {
        /* 理论不可达：pattern 不是合法 app 镜像 */
        ESP_LOGW(TAG, "ota_selftest: end accepted pattern (unexpected)");
    }
    ESP_LOGI(TAG, "ota_selftest DONE: partition %s @0x%08lx size=%dKB",
             ota0->label, (unsigned long)ota0->address, ota0->size / 1024);
}

static void ota_test_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(5000));   /* 等 AP/链路日志先出 */
    ota_selftest_once();
    vTaskDelete(NULL);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "=== 调温服 ESP32-S3 bridge boot ===");
    softap_start();
    link_start();
    web_start();
    ESP_LOGI(TAG, "WiFi AP + link + web all up");

    xTaskCreate(ota_test_task, "ota_test", 6144, NULL, 3, NULL);
}
