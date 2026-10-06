#pragma once
/* 微雪 2.9" (B) 三色墨水屏(红/黑/白, UC8151 类, 296x128, SPI)驱动组件接口
 * 型号: WFT0290CZ10; 蓝本: porting-ref/(EPD_2in9bc | EPD_2in9b_V4)
 *
 * 三色屏物理: 全刷 ~15s; 黑白局部 ~1.8s(仅黑白内容); 无 0.3s 快刷。
 * 感知性能靠 app 层 change-driven"少刷"策略 —— docs/epaper-workflow.md §6
 *
 * 接线 8 线(指模块/驱动板排针; 裸屏 FPC 实为 24 脚须经含升压电路的驱动板,
 *           见 porting-ref/2.9inch-e-paper-b-v3-specification.pdf):
 *           VCC->3V3(勿接5V) | GND | DIN->GPIO11 | CLK->GPIO12 | CS->GPIO13
 *           DC->GPIO14 | RST->GPIO21 | BUSY->GPIO39 (Kconfig 可改)
 */
#include <stdint.h>
#include "esp_err.h"

#define EPAPER_WIDTH_PX    128
#define EPAPER_HEIGHT_PX   296
#define EPAPER_PLANE_BYTES (EPAPER_WIDTH_PX * EPAPER_HEIGHT_PX / 8) /* 4736B/平面 */

/* SPI 总线(SCLK/MOSI/CS) + DC/RST/BUSY 控制脚初始化; 幂等 */
esp_err_t epaper_init(void);

/* ---- S1 门: 总线活化自检(真实实现, 屏未接也有意义) ---- */
typedef enum {
    EPAPER_BUS_OK = 0,        /* 复位脉冲后 BUSY 拉高又回落: 面板活着 */
    EPAPER_BUS_NO_ACTIVITY,   /* BUSY 恒低: 未接线或面板无响应 */
    EPAPER_BUS_STUCK_BUSY,    /* BUSY 拉高 3s 不回落: 面板忙死/接线错 */
} epaper_bus_test_t;
esp_err_t epaper_selftest_bus(epaper_bus_test_t *out);

/* ---- 刷屏 API(chip 层 uc8151_bc.c 已实现;屏幕未通电时将以超时/无忙相错误呈现) ---- */
esp_err_t epaper_clear(void);   /* 全白 */
/* 三色全刷 ~15s: black_plane/red_plane 各 EPAPER_PLANE_BYTES, 1bit/像素 */
esp_err_t epaper_draw_full(const uint8_t *black_plane, const uint8_t *red_plane);
esp_err_t epaper_deep_sleep(void);
