#pragma once
/* UC8151 芯片命令层(微雪 2.9" B 三色)——从 porting-ref/EPD_2in9bc.c 移植
 * 移植原则: 命令序列照参考,工程缺陷照修(参考的 ReadBusy 是无界死等 → 这里全部带超时)
 *
 * 实测事实(2026-10-07 真板): BUSY 空闲=高、忙=低(参考驱动 wait-until-HIGH 为证);
 * 显示编码: 平面 bit=1 → 白/无色, bit=0 → 黑(黑平面)/红(红平面); Clear=双平面全 0xFF
 */
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define UC8151_BUSY_TIMEOUT_MS   3200    /* PON/POF 等 */
#define UC8151_REFRESH_TIMEOUT_MS 30000  /* 三色全刷规格 ~15s;低温/首刷留双倍余量 */

esp_err_t uc8151_bc_reset(void);                                  /* 硬复位(200/2/200ms 参考时序) */
esp_err_t uc8151_bc_wait_idle(uint32_t timeout_ms, bool require_busy);               /* 等 BUSY 回空闲(极性自校准),超时返回 ESP_ERR_TIMEOUT */
esp_err_t uc8151_bc_init(void);                                   /* 复位+寄存器序列(含 PON) */
esp_err_t uc8151_bc_display(const uint8_t *black, const uint8_t *red, uint32_t plane_bytes); /* 全刷 ~15s */
esp_err_t uc8151_bc_clear(uint32_t plane_bytes);                  /* 全白 */
esp_err_t uc8151_bc_sleep(void);                                  /* POF+深睡 0xA5(唤醒只能靠复位重 init) */
