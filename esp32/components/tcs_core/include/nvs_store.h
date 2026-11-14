/**
 * nvs_store.h — 参数注册表 NVS 持久化（R1a，任务书 v11.1）
 *
 * 256B 镜像格式与 AT24C02 v2 布局完全同构（magic 0xA6/ver 2/记录/CRC8@0xF8），
 * 校验逻辑复用 param_serialize/param_deserialize——NVS 只当"一片不磨损超限的 EEPROM"。
 * eeprom_* 别名满足 param_registry.c 的 param_load_eeprom 外部引用（双向同源，Q5）。
 */
#pragma once
#include <stdint.h>
#include "param_registry.h"

int nvs_registry_commit(const uint8_t img[EE_IMG_SIZE]);  /* hooks.eeprom_commit 落点 */

/* ====== 安全锁存持久化（R1 修复 P0-08）======
 * 独立小 blob（4B），与参数镜像分开：参数被擦除不应连带解除安全态。
 * 返回 0 成功；-1 失败（调用方决定是否重试/告警）。 */
int     safety_state_save(uint8_t latch);
uint8_t safety_state_load(void);      /* 无记录/校验失败 → 0（视为无锁存），并留日志 */

/* param_registry.c 的 param_load_eeprom() 外部引用（NVS 实现）*/
int eeprom_ready(void);
int eeprom_read_image(uint8_t img[EE_IMG_SIZE]);
