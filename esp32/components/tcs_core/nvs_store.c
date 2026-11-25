/**
 * nvs_store.c — NVS 持久化实现（R1a）
 * 键：命名空间 "tcs"、key "reg_img"（blob 256B；与 softap 的 ssid/pass 同命名空间不同键）。
 * 写放大（M10-F-07，照 IDF v5.5.5 源码改正；原注释写的是"每次 commit 全量 256B blob 重写"）：
 *   一次 nvs_registry_commit = nvs_set_blob 写 1 个 BLOB_DATA item（跨 9 个 entry：1 个头 + 8×32B 数据）
 *   + 1 个 BLOB_IDX entry，共 10 个 entry。nvs_commit() 在 v5.5.5 是 no-op（nvs_api.cpp:411-414
 *   "no-op for now, to be used when intermediate cache is added"），写失败只会由 nvs_set_blob 返回，
 *   所以这里不再调用它、日志也不再把它说成错误来源。审计表见 docs/results/R1.md。
 *
 * 文件分两段：安全记录的编解码是纯函数（主机单测 tests/test_tcs_core.c 的 N 系列），
 * 只有 ESP_PLATFORM 段里的读写用到 NVS 与日志——与 control.c 同样的零 HAL 纪律。
 */
#include "nvs_store.h"
#include <string.h>

/* ====== 安全记录的编解码（纯函数）======
 * v1（R1 修复 P0-08）：{magic 0x53, ver 0x01, latch, ~latch}
 * v2（X04-F-03）  ：{magic 0x53, ver 0x02, latch, ~latch, alm_lo, alm_hi, ~alm_lo, ~alm_hi}
 *   报警位随安全锁存一起落盘。落盘发生在 trip / ack（锁存变化的时刻），存的是那一刻的报警位——
 *   "上次出事时屏上 / 网页上亮着什么"。只作证据读出（status 的 nvalm=），**不**回灌成实时报警：
 *   上一轮的报警到这一轮未必还成立，回灌只会让人去确认一个不存在的故障。
 *   （锁存位本身仍然恢复成实时安全态，那是 P0-08 的口径，没变。）*/
#define SAFETY_REC_MAGIC   0x53u
#define SAFETY_REC_VER1    0x01u
#define SAFETY_REC_VER2    0x02u
#define SAFETY_REC_SIZE    8u      /* v2 的长度；v1 是前 4 字节 */

int safety_rec_encode(uint8_t out[SAFETY_REC_SIZE], uint8_t latch, uint16_t alarm)
{
    if (!out) return -1;
    out[0] = SAFETY_REC_MAGIC;
    out[1] = SAFETY_REC_VER2;
    out[2] = latch;
    out[3] = (uint8_t)~latch;
    out[4] = (uint8_t)(alarm & 0xFFu);
    out[5] = (uint8_t)(alarm >> 8);
    out[6] = (uint8_t)~(alarm & 0xFFu);
    out[7] = (uint8_t)~(alarm >> 8);
    return 0;
}

int safety_rec_decode(const uint8_t *b, size_t len, uint8_t *latch, uint16_t *alarm)
{
    if (latch) *latch = 0xFF;            /* 调用方即使不查返回值也拿到"未知"，不会当成"无锁存" */
    if (alarm) *alarm = 0;
    if (!b || !latch) return -1;
    if (len < 4u || b[0] != SAFETY_REC_MAGIC) return -1;
    if ((uint8_t)~b[2] != b[3]) return -1;
    if (b[1] == SAFETY_REC_VER1) {
        *latch = b[2];                   /* 老记录：没有报警位快照 */
        return 0;
    }
    if (b[1] != SAFETY_REC_VER2 || len < SAFETY_REC_SIZE) return -1;
    {
        uint8_t lo = b[4], hi = b[5];
        if ((uint8_t)~lo != b[6] || (uint8_t)~hi != b[7]) return -1;
        *latch = b[2];
        if (alarm) *alarm = (uint16_t)(lo | ((uint16_t)hi << 8));
    }
    return 0;
}

#ifdef ESP_PLATFORM
#include "nvs.h"
#include "esp_log.h"

static const char *TAG = "tcs_nvs";
#define NS_TCS   "tcs"
#define KEY_IMG  "reg_img"

/* 上电读镜像（P1-12）：ESP_OK = 读到；ESP_ERR_NVS_NOT_FOUND = 首次上电（从没保存过）；
 * 其它 = 读失败（镜像损坏 / 长度不对 / NVS 异常）。原先这两种情况合成一句 "(defaults, first boot)"。*/
esp_err_t nvs_registry_load(uint8_t img[EE_IMG_SIZE])
{
    nvs_handle_t h;
    esp_err_t ro = nvs_open(NS_TCS, NVS_READONLY, &h);
    if (ro == ESP_ERR_NVS_NOT_FOUND) return ESP_ERR_NVS_NOT_FOUND;
    if (ro != ESP_OK) return ro;
    size_t len = EE_IMG_SIZE;
    esp_err_t r = nvs_get_blob(h, KEY_IMG, img, &len);
    nvs_close(h);
    if (r == ESP_OK && len != EE_IMG_SIZE) r = ESP_ERR_NVS_INVALID_LENGTH;
    return r;
}

int nvs_registry_commit(const uint8_t img[EE_IMG_SIZE])
{
    nvs_handle_t h;
    if (nvs_open(NS_TCS, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "nvs open fail");
        return -1;
    }
    esp_err_t r = nvs_set_blob(h, KEY_IMG, img, EE_IMG_SIZE);
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "nvs write fail=%s", esp_err_to_name(r));
        return -1;
    }
    return 0;
}

/* ====== 安全锁存（R1 修复 P0-08）======
 * 布局见文件头的 v1 / v2。只在 trip/ack 时写，写放大可忽略。
 * 校验失败按"未知"处理并告警（返回 0xFF，上层保守进 SAFE、等人工确认）——读不出不能静默认为安全，
 * 但也不能因坏记录拒绝启动。首次上电（NOT_FOUND）才返回 0。*/
#define KEY_SAFETY   "safety"

/* 上电读回的报警位快照（v2 记录才有；v1 / 读失败 / 无记录 = 0）。只作证据，不回灌。*/
static uint16_t s_last_alarm;

int safety_state_save(uint8_t latch, uint16_t alarm)
{
    uint8_t b[SAFETY_REC_SIZE];
    nvs_handle_t h;
    (void)safety_rec_encode(b, latch, alarm);
    if (nvs_open(NS_TCS, NVS_READWRITE, &h) != ESP_OK) return -1;
    esp_err_t r = nvs_set_blob(h, KEY_SAFETY, b, sizeof(b));   /* nvs_commit 在 v5.5.5 是 no-op，不调 */
    nvs_close(h);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "safety save fail=%s", esp_err_to_name(r));
        return -1;
    }
    return 0;
}

uint16_t safety_state_last_alarm(void) { return s_last_alarm; }

uint8_t safety_state_load(void)
{
    uint8_t b[SAFETY_REC_SIZE] = { 0 };
    size_t len = sizeof(b);
    nvs_handle_t h;
    /* R-05：区分"首次上电"与"读回失败/坏记录"。
     * 首次上电（NOT_FOUND）→ 0（无锁存）；其它任何异常 → 0xFF（未知，上层按最坏情况进 SAFE）。*/
    esp_err_t ro = nvs_open(NS_TCS, NVS_READONLY, &h);
    if (ro == ESP_ERR_NVS_NOT_FOUND) return 0;          /* 命名空间都还没建 = 首次上电 */
    if (ro != ESP_OK) {
        ESP_LOGE(TAG, "safety load open fail=%s", esp_err_to_name(ro));
        return 0xFF;
    }
    esp_err_t r = nvs_get_blob(h, KEY_SAFETY, b, &len);
    nvs_close(h);
    if (r == ESP_ERR_NVS_NOT_FOUND) return 0;           /* 有命名空间但没这个键 = 首次上电 */
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "safety load read fail=%s", esp_err_to_name(r));
        return 0xFF;
    }
    {
        uint8_t latch;
        uint16_t alarm;
        if (safety_rec_decode(b, len, &latch, &alarm) != 0) {
            ESP_LOGE(TAG, "safety record invalid (len=%u) -> unknown (enter SAFE)", (unsigned)len);
            return 0xFF;
        }
        s_last_alarm = alarm;
        if (latch) ESP_LOGW(TAG, "safety latch restored=0x%02x (prev reset)", latch);
        return latch;
    }
}
#endif /* ESP_PLATFORM */
