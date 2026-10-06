#pragma once
/* 微雪 2.9" V2 墨水屏(UC8151 级驱动, 296x128, SPI)基础接口
 *
 * 当前状态: 总线/GPIO 真实初始化已实现(屏到手前可编译烧录);
 *           刷屏操作为桩 —— 到手后把微雪 wiki 的 EPD_2in9_V2 命令序列移植进来
 *           (Init/ Clear/ Display/ DisplayPartBaseImage 等,唯一入口改这里)。
 *
 * 硬件接线(8 线): VCC->3V3, GND->GND, DIN->MOSI, CLK->SCLK,
 *                 CS->CS, DC->DC, RST->RST, BUSY->BUSY(Kconfig 可改)
 */
#include <stdint.h>
#include "esp_err.h"

#define EPAPER_WIDTH_PX   128
#define EPAPER_HEIGHT_PX  296
#define EPAPER_FB_BYTES   (EPAPER_WIDTH_PX * EPAPER_HEIGHT_PX / 8) /* 4736B */

/* SPI 总线(SCLK/MOSI/CS) + DC/RST/BUSY 控制脚初始化;可重复调用安全(幂等) */
esp_err_t epaper_init(void);

/* ---- 以下待驱动移植(返回 ESP_ERR_NOT_SUPPORTED) ---- */
esp_err_t epaper_clear(void);                        /* 全屏刷白(约 4s) */
esp_err_t epaper_draw_full(const uint8_t *fb);       /* 全刷一帧, fb=EPAPER_FB_BYTES, 1bit/pixel */
esp_err_t epaper_draw_partial(const uint8_t *fb);    /* 局部快刷(常温可用,低温变慢) */
esp_err_t epaper_deep_sleep(void);                   /* 刷完进深度睡眠(墨水屏保命习惯) */
