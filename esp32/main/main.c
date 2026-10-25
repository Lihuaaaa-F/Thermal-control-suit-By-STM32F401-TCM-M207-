/* main.c — 调温服 ESP32-S3 无线端（任务书 v5.1 WP-F）
 *
 * ESP32 = 纯桥 + Web 服务器：不存业务参数、不做决策（H743 唯一数据源）。
 * 启动序：NVS → event loop → SoftAP(TCS-XXXX/12345678) → 回环自测(WP-F.1)
 *        → 链路任务(UART1 230400，含内置注册表快照+演示模式 WP-F.2/F3) → HTTP/WS :80
 * OTA（WP-F.4）：/api/ota 或 WS {"t":"ota"} 触发——运行镜像自拷贝到对槽+切换；
 *        m=1 直接把 boot 指回对槽（回滚），两次重启日志即证据。
 *
 * 日常闭环：bash ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh all
 * 引脚红线：GPIO17(TX)→H743 PA3(RX)，GPIO18(RX)←H743 PD5(TX)；17/18 非保留脚 ✓
 */
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_app_desc.h"
#include "esp_system.h"

#include "softap.h"
#include "link_task.h"
#include "http_server.h"
#include "param_registry.h"
#include "nvs_store.h"

static const char *TAG = "TCS";

/* ====== WP-F.4 OTA：镜像实际长度（解析 image header/segments，避免整槽 3MB 拷贝）====== */
static uint32_t ota_image_size(const esp_partition_t *p)
{
    uint8_t hdr[256];
    ESP_ERROR_CHECK(esp_partition_read(p, 0, hdr, sizeof(hdr)));
    if (hdr[0] != 0xE9) return p->size;          /* 非 app 镜像 → 整槽兜底 */
    uint8_t segs = hdr[1];
    uint32_t off = 24;                            /* image header 24B */
    uint32_t end = off;
    for (int i = 0; i < segs && i < 16; i++) {
        uint32_t len = hdr[off + 4] | (hdr[off + 5] << 8)
                     | (hdr[off + 6] << 16) | (hdr[off + 7] << 24);
        off += 8 + len;
        end = off;
    }
    end = (end + 15u) & ~15u;                     /* 对齐 + checksum 字节 */
    end += 1 + 32;                                /* hash 尾 */
    return (end <= p->size) ? end : p->size;
}

static void ota_copy_task(void *arg)
{
    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!next) { ESP_LOGE(TAG, "ota FAIL: no next slot"); vTaskDelete(NULL); return; }
    uint32_t len = ota_image_size(run);
    ESP_LOGI(TAG, "ota: copy %s@0x%08lx img=%luKB -> %s",
             run->label, (unsigned long)run->address,
             (unsigned long)(len / 1024), next->label);
    esp_ota_handle_t h = 0;
    esp_err_t err = esp_ota_begin(next, len, &h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "ota begin=%s", esp_err_to_name(err)); vTaskDelete(NULL); return; }
    static uint8_t buf[8192];
    for (uint32_t off = 0; off < len; off += sizeof(buf)) {
        uint32_t chunk = (len - off > sizeof(buf)) ? sizeof(buf) : (len - off);
        ESP_ERROR_CHECK(esp_partition_read(run, off, buf, chunk));
        err = esp_ota_write(h, buf, chunk);
        if (err != ESP_OK) { ESP_LOGE(TAG, "ota write=%s", esp_err_to_name(err)); vTaskDelete(NULL); return; }
    }
    err = esp_ota_end(h);
    if (err != ESP_OK) { ESP_LOGE(TAG, "ota end=%s", esp_err_to_name(err)); vTaskDelete(NULL); return; }
    ESP_ERROR_CHECK(esp_ota_set_boot_partition(next));
    ESP_LOGI(TAG, "ota OK: boot -> %s, restart in 1s", next->label);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/* m=0：自拷贝到对槽并切换；m=1：回滚（boot 指回对槽，不拷贝）*/
void app_ota_trigger(int rollback)
{
    if (rollback) {
        const esp_partition_t *run = esp_ota_get_running_partition();
        const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
        if (!next) { ESP_LOGE(TAG, "rollback FAIL: no next slot"); return; }
        ESP_LOGI(TAG, "ota ROLLBACK: boot %s -> %s, restart in 1s",
                 run->label, next->label);
        ESP_ERROR_CHECK(esp_ota_set_boot_partition(next));
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }
    xTaskCreate(ota_copy_task, "ota_copy", 6144, NULL, 4, NULL);
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

    const esp_partition_t *run = esp_ota_get_running_partition();
    const esp_app_desc_t *desc = esp_app_get_description();
    ESP_LOGI(TAG, "=== 调温服 ESP32-S3 bridge boot ===");

    /* R1a（任务书 v11.1）：注册表唯一真值迁至 ESP32。本步只起服务+恢复 NVS；
     * 链路/网页数据源切换在 R2，屏端语义不变。 */
    param_registry_init(&(param_hooks_t){ .eeprom_commit = nvs_registry_commit });
    {
        uint8_t img[EE_IMG_SIZE];
        if (eeprom_read_image(img) == 0 && param_deserialize(img) == 0)
            ESP_LOGI(TAG, "R1a registry: %d params restored from NVS", param_count());
        else
            ESP_LOGI(TAG, "R1a registry: %d params loaded (defaults, first boot)",
                     param_count());
    }
    ESP_LOGI(TAG, "boot part=%s proj=%s ver=%s", run->label, desc->project_name, desc->version);

    softap_start();
    link_loopback_run();          /* WP-F.1：真机自环自测（独占 UART1，~2.5s，证据=[LPBK]）*/
    link_start();
    web_start();
    ESP_LOGI(TAG, "WiFi AP + loopback + link + web all up");
}
