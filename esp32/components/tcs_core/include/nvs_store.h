/**
 * nvs_store.h — 参数注册表 NVS 持久化（R1a，任务书 v11.1）
 *
 * 256B 镜像格式与 AT24C02 v2 布局完全同构（magic 0xA6/ver 2/记录/CRC8@0xF8），
 * 校验逻辑复用 param_serialize/param_deserialize——NVS 只当"一片不磨损超限的 EEPROM"。
 * eeprom_* 别名满足 param_registry.c 的 param_load_eeprom 外部引用（双向同源，Q5）。
 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "param_registry.h"

int nvs_registry_commit(const uint8_t img[EE_IMG_SIZE]);  /* hooks.eeprom_commit 落点 */

/* ====== 安全记录的编解码（纯函数，主机单测；X04-F-03 起为 v2）======
 * v1：{magic 0x53, ver 0x01, latch, ~latch}
 * v2：{magic 0x53, ver 0x02, latch, ~latch, alm_lo, alm_hi, ~alm_lo, ~alm_hi}
 * decode 两种都认（v1 没有报警位快照，alarm 记 0）。坏记录（魔数 / 版本 / 长度 / 反码任一不符）
 * 返回 -1，并把 *latch 置成 0xFF（"未知"），不会被当成"无锁存"。*/
#define SAFETY_REC_SIZE   8u
int      safety_rec_encode(uint8_t out[SAFETY_REC_SIZE], uint8_t latch, uint16_t alarm);
int      safety_rec_decode(const uint8_t *b, size_t len, uint8_t *latch, uint16_t *alarm);

/* ====== 安全锁存持久化（R1 修复 P0-08）======
 * 独立小 blob，与参数镜像分开：参数被擦除不应连带解除安全态。
 * 返回 0 成功；-1 失败（调用方决定是否重试/告警）。alarm 是落盘那一刻的报警位快照，
 * 只作证据（safety_state_last_alarm 读出），不回灌成实时报警。 */
int     safety_state_save(uint8_t latch, uint16_t alarm);
uint8_t safety_state_load(void);      /* 无记录（首次上电）→ 0；读失败 / 校验失败 → 0xFF（"未知"），并留日志 */
uint16_t safety_state_last_alarm(void);   /* 上电读回的报警位快照；v1 记录 / 读失败 / 无记录 → 0 */

/* param_registry.c 的 param_load_eeprom() 外部引用（NVS 实现）*/
int eeprom_ready(void);
int eeprom_read_image(uint8_t img[EE_IMG_SIZE]);

#ifdef ESP_PLATFORM
#include "esp_err.h"
/* 上电读参数镜像（P1-12）：ESP_OK = 读到；ESP_ERR_NVS_NOT_FOUND = 首次上电（从没保存过）；
 * 其它 = 读失败。app_main 据此把"读失败 / 镜像校验不过"与"首次上电"拆成不同的日志。*/
esp_err_t nvs_registry_load(uint8_t img[EE_IMG_SIZE]);
#endif
