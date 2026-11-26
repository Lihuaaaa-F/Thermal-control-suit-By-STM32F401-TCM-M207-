/**
 * ota_pure.c — OTA 自拷贝的纯逻辑（说明见 ota_pure.h）
 */
#include "ota_pure.h"
#include <stdbool.h>
#include <stdatomic.h>

/* ====== 单实例闸门 ===== */
static atomic_bool s_ota_busy;          /* 静态存储，上电即 false（ATOMIC_VAR_INIT 在 C17 已弃用，主机编译器也不一定有）*/

/* 比较并交换：只有把 false 换成 true 的那一例算拿到，其余的看到 true、直接回绝。
 * 与 IDF 自带组件的写法相同（components/esp_adc/adc_filter.c）；IDF 为 xtensa 提供了
 * <stdatomic.h> 的实现（components/newlib/src/stdatomic.c），不需要额外链接。 */
int ota_gate_try_enter(void)
{
    bool expected = false;
    return atomic_compare_exchange_strong(&s_ota_busy, &expected, true) ? 1 : 0;
}

void ota_gate_leave(void)
{
    atomic_store(&s_ota_busy, false);
}

int ota_gate_busy(void)
{
    return atomic_load(&s_ota_busy) ? 1 : 0;
}

/* ====== 镜像长度 ===== */
#define OTA_IMG_HDR_LEN  24u     /* esp_image_header_t */
#define OTA_SEG_HDR_LEN   8u     /* esp_image_segment_header_t：load_addr(4) + data_len(4) */
#define OTA_SHA256_LEN   32u     /* ESP_IMAGE_HASH_LEN */

int ota_image_len(uint32_t part_size, ota_read_fn rd, void *ctx, uint32_t *len_out)
{
    if (!rd) return OTA_IMG_EREAD;
    if (part_size < OTA_IMG_HDR_LEN) return OTA_IMG_EBOUND;
    uint8_t hdr[OTA_IMG_HDR_LEN];
    if (rd(0, hdr, sizeof hdr, ctx) != 0) return OTA_IMG_EREAD;
    if (hdr[0] != 0xE9) return OTA_IMG_EMAGIC;
    unsigned segs = hdr[1];
    if (segs == 0) return OTA_IMG_ESEGS;

    /* 不变量：off 始终 ≤ part_size（下面每一步都先比后加），所以 part_size - off 不会下溢 */
    uint32_t off = OTA_IMG_HDR_LEN;
    for (unsigned i = 0; i < segs; i++) {
        if (part_size - off < OTA_SEG_HDR_LEN) return OTA_IMG_EBOUND;   /* 段头放不下 */
        uint8_t sh[OTA_SEG_HDR_LEN];
        if (rd(off, sh, sizeof sh, ctx) != 0) return OTA_IMG_EREAD;
        uint32_t len = (uint32_t)sh[4] | ((uint32_t)sh[5] << 8)
                     | ((uint32_t)sh[6] << 16) | ((uint32_t)sh[7] << 24);
        off += OTA_SEG_HDR_LEN;
        if (len > part_size - off) return OTA_IMG_EBOUND;               /* 段数据越过分区末尾 / 回绕 */
        off += len;
    }
    /* 段后：校验字节所在的 16 字节块，再是 32 字节 SHA-256（镜像头第 23 字节 hash_appended）*/
    uint32_t end = ((off + 1u) + 15u) & ~15u;
    if (hdr[23]) end += OTA_SHA256_LEN;
    if (end > part_size) return OTA_IMG_ETOOLONG;
    *len_out = end;
    return OTA_IMG_OK;
}
