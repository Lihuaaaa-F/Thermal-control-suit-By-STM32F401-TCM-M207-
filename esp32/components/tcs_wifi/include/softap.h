/**
 * softap.h — WiFi AP（TCS-<mac后4位> / 12345678，NVS 可改，任务书 §4.1）
 */
#pragma once
#include <stddef.h>

void softap_start(void);

/* 当前生效的热点名与密码（NVS 覆盖优先；否则 TCS-<MAC 后 4 位> / 12345678）。
 * 拷进调用方缓冲（含 NUL）；放得下才拷。返回 0=成功，-1=尚未启动或放不下。*/
int  softap_credentials(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);
