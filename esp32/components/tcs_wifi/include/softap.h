/**
 * softap.h — WiFi AP（TCS-<mac后4位> / 12345678，NVS 可改，任务书 §4.1）
 */
#pragma once
#include <stddef.h>

void softap_start(void);

/* 当前生效的热点名与密码（NVS 覆盖优先；否则 TCS-<MAC 后 4 位> / 12345678）。
 * 拷进调用方缓冲（含 NUL）；放得下才拷。返回 0=成功，-1=尚未启动或放不下。*/
int  softap_credentials(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);

/* 修改热点名称/口令：写 NVS("tcs") 并立即应用到 AP（已连客户端会断开）。
 * 返回 0=成功；-1=未启动/参数为空；-2=长度非法（名称 1..32、口令 8..63）；-3=NVS 写失败；-4=应用失败。*/
int  softap_set_credentials(const char *ssid, const char *pass);
