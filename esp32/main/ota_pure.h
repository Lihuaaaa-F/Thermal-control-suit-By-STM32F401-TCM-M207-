/**
 * ota_pure.h — OTA 自拷贝的纯逻辑（脱离 IDF 也能编译；主机用例 esp32/tests/test_ota_pure.c）
 *
 * 与硬件绑定的部分（分区读写、esp_ota_*、任务、重启）留在 main.c；这里只放"逻辑"本身，
 * 交付固件与主机用例编译的是同一份代码。S 线 S-G e3b。
 */
#pragma once
#include <stdint.h>

/* ====== 单实例闸门（X03-F-12 = P1-15 的 ②）======
 * OTA 拷贝任务同一时刻只允许一例在跑。闸门在 app_ota_trigger() 里用"比较并交换"拿到：
 * 拿到的那一例才去建拷贝任务；其余请求直接回绝（HTTP 409）。拿到之后，所有没走到重启的
 * 出口都必须调 ota_gate_leave()，否则一次失败之后 OTA 永远被拒。 */
int  ota_gate_try_enter(void);   /* 1 = 拿到（此刻起只有这一例）；0 = 已有一例在进行 */
void ota_gate_leave(void);       /* 放开。只由拿到过的那一例调用 */
int  ota_gate_busy(void);        /* 1 = 有一例在进行 */

/* ====== 运行镜像的长度（M08-F-12）======
 * esp_app 镜像 = 24 字节镜像头 + N ×（8 字节段头 + 段数据），段与段首尾相接；最后一段之后是
 * 校验字节所在的 16 字节块，再后面是 32 字节 SHA-256（镜像头第 23 字节 hash_appended 为 1 时才有）。
 * 长度按 IDF 的算法算（bootloader_support/src/esp_image_format.c process_segments / process_checksum /
 * process_appended_hash_and_sig），与 esp_image_get_metadata().image_len 同值。
 *
 * 段头一个接一个排在各自段数据的后面，所以不能只读开头一小块：要顺着读下去。每一步都先在
 * 分区范围内校验偏移，再读；读函数不会收到越界的请求。 */
typedef int (*ota_read_fn)(uint32_t off, uint8_t *dst, uint32_t len, void *ctx);
#define OTA_IMG_OK         0    /* *len_out = 镜像全长 */
#define OTA_IMG_EREAD     (-1)  /* 分区读失败 */
#define OTA_IMG_EMAGIC    (-2)  /* 第 0 字节不是 0xE9：不是 esp_app 镜像 */
#define OTA_IMG_ESEGS     (-3)  /* 段数为 0 */
#define OTA_IMG_EBOUND    (-4)  /* 段头或段数据越过分区末尾（或 32 位回绕）*/
#define OTA_IMG_ETOOLONG  (-5)  /* 校验块 / SHA-256 放不下 */
int ota_image_len(uint32_t part_size, ota_read_fn rd, void *ctx, uint32_t *len_out);
