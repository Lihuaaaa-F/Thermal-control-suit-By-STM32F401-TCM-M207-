/**
 * nvs_store.c — NVS 持久化实现（R1a）
 * 键：命名空间 "tcs"、key "reg_img"（blob 256B；与 softap 的 ssid/pass 同命名空间不同键）。
 * 写放大：每次 commit 全量 256B blob 重写（NVS 磨损均衡分页）；审计表见 docs/results/R1.md。
 */
#include "nvs_store.h"
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "tcs_nvs";
#define NS_TCS   "tcs"
#define KEY_IMG  "reg_img"

int eeprom_ready(void) { return 1; }   /* NVS 在 app_main nvs_flash_init 后恒可用 */

int eeprom_read_image(uint8_t img[EE_IMG_SIZE])
{
    nvs_handle_t h;
    if (nvs_open(NS_TCS, NVS_READONLY, &h) != ESP_OK) return -1;
    size_t len = EE_IMG_SIZE;
    esp_err_t r = nvs_get_blob(h, KEY_IMG, img, &len);
    nvs_close(h);
    return (r == ESP_OK && len == EE_IMG_SIZE) ? 0 : -1;
}

int nvs_registry_commit(const uint8_t img[EE_IMG_SIZE])
{
    nvs_handle_t h;
    if (nvs_open(NS_TCS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs open fail");
        return -1;
    }
    esp_err_t r = nvs_set_blob(h, KEY_IMG, img, EE_IMG_SIZE);
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "nvs commit fail=%s", esp_err_to_name(r));
        return -1;
    }
    return 0;
}

/* ====== 安全锁存（R1 修复 P0-08）======
 * 布局 {magic, ver, latch, ~latch}：4 字节，只在 trip/ack 时写，写放大可忽略。
 * 校验失败按"无锁存"处理并告警——读不出不能静默认为安全，但也不能因坏记录拒绝启动
 * （启动后 control_poll 会用实时源重新判 OVERTEMP/BODY/M207）。 */
#define KEY_SAFETY   "safety"
#define SAFETY_MAGIC 0x53u
#define SAFETY_VER   0x01u

int safety_state_save(uint8_t latch)
{
    uint8_t b[4] = { SAFETY_MAGIC, SAFETY_VER, latch, (uint8_t)~latch };
    nvs_handle_t h;
    if (nvs_open(NS_TCS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t r = nvs_set_blob(h, KEY_SAFETY, b, sizeof(b));
    if (r == ESP_OK) r = nvs_commit(h);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "safety save fail=%s", esp_err_to_name(r));
        return -1;
    }
    return 0;
}

uint8_t safety_state_load(void)
{
    uint8_t b[4] = { 0 };
    size_t len = sizeof(b);
    nvs_handle_t h;
    if (nvs_open(NS_TCS, NVS_READONLY, &h) != ESP_OK) return 0;   /* 首次上电 */
    esp_err_t r = nvs_get_blob(h, KEY_SAFETY, b, &len);
    nvs_close(h);
    if (r != ESP_OK || len != sizeof(b)) return 0;
    if (b[0] != SAFETY_MAGIC || b[1] != SAFETY_VER || (uint8_t)~b[2] != b[3]) {
        ESP_LOGW(TAG, "safety blob invalid, treat as cleared");
        return 0;
    }
    if (b[2]) ESP_LOGW(TAG, "safety latch restored=0x%02x (prev reset)", b[2]);
    return b[2];
}
