/**
 * link_info.h — 屏侧保存的主控设备信息（0x22 INFO：热点名/密码/网址/固件版本）
 *
 * 纯逻辑、零 HAL：主机单测 tests/test_link_info.c。
 * 主控每 3.2s 推一次；屏单独重启后最多 3.2s 拿到，连接页在此之前显示"—"。
 */
#ifndef LINK_INFO_H
#define LINK_INFO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char    ssid[33];
    char    pass[65];
    char    url[40];
    char    fw[24];
    uint8_t valid;            /* 收到过至少一帧合法 INFO */
} tcs_link_info_t;

/* 解析并保存一帧 0x22 载荷（格式见 protocol/tcs_proto.h）。载荷非法/放不下 → 不改动旧值，返回 -1。*/
int link_info_store(const char *pl, size_t len);

const tcs_link_info_t *link_info(void);

/* 每成功保存一帧 +1：连接页据此判断要不要重画 */
uint32_t link_info_gen(void);

/* 手机"扫码连热点"的二维码内容：WIFI:T:WPA;S:<ssid>;P:<pass>;;
 * SSID/密码里的 \\ ; , : " 按 WIFI 二维码规范加反斜杠转义。
 * 没有有效热点名 → 退回网址（或默认 http://192.168.4.1）。返回字节数，放不下返回 0。*/
size_t link_info_wifi_qr(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* LINK_INFO_H */
