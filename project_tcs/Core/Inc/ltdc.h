/**
 * ltdc.h — LTDC + 外部 SDRAM 显存驱动（M2 显示）
 *
 * 1024×600 RGB565，显存放外部 SDRAM @0xC0000000（FMC Bank1 Region1）。
 * 双 buffer：front @ 0xC0000000, back @ 0xC012C000（每帧 1024*600*2=1228800=0x12C000）。
 *
 * 引脚/时序/MPU 全部照抄官方例程（实验12 LTDC LCD RGB 屏）原文核实值，见 §2.2/§2.3/§2.8。
 */
#ifndef LTDC_H
#define LTDC_H

#include <stdint.h>
#include <stddef.h>
#include "stm32h7xx_hal.h"

/* ====== 显示参数（1024×600 RGB565）====== */
#define LCD_WIDTH       1024
#define LCD_HEIGHT      600
#define LCD_PIXELBYTES  2          /* RGB565 = 2B/像素 */
#define LCD_FBSIZE      (LCD_WIDTH * LCD_HEIGHT * LCD_PIXELBYTES)  /* 1228800 B = 0x12C000 */

/* ====== 显存地址（外部 SDRAM，FMC Bank1 Region1）====== */
#define SDRAM_BASE_ADDR   0xC0000000U
#define LCD_FRONT_BUF     ((uint16_t *)SDRAM_BASE_ADDR)
#define LCD_BACK_BUF      ((uint16_t *)(SDRAM_BASE_ADDR + LCD_FBSIZE))

/* 颜色 RGB565 */
#define RGB565(r,g,b)  ((((r)&0xF8)<<8) | (((g)&0xFC)<<3) | (((b)&0xF8)>>3))
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     RGB565(255,0,0)
#define COLOR_GREEN   RGB565(0,255,0)
#define COLOR_BLUE    RGB565(0,0,255)
#define COLOR_YELLOW  RGB565(255,255,0)
#define COLOR_CYAN    RGB565(0,255,255)
#define COLOR_MAGENTA RGB565(255,0,255)
#define COLOR_GRAY    RGB565(128,128,128)
#define COLOR_DKGRAY  RGB565(64,64,64)
#define COLOR_ORANGE  RGB565(255,128,0)

/* ====== API ====== */

/* 初始化：MPU+D-Cache write-through → SDRAM（PLL2 已在 SystemClock_Config 配）→
 * PLL3（LTDC PCLK=50M）→ LTDC 寄存器+引脚 → 复位序列 → 背光 → 清屏。
 * 返回 0=成功，<0=失败（SDRAM 不通/LTDC 配置失败）。失败时不点屏但协议层照跑。*/
int ltdc_init(void);

/* 显存是否就绪（ltdc_init 成功后为 1）。UI 层据此决定是否刷新屏幕。*/
int lcd_ready(void);

/* SDRAM 读写测试：写 0xAA55+i 模式再读回，验证 SDRAM 通。
 * 返回 1=通 0=不通。M2 点屏前先跑这个排错。 */
int sdram_selftest(void);

/* 清屏（整个 back buffer 填 color，DMA2D R2M 加速）*/
void gfx_clear(uint16_t color);

/* 画点（back buffer）*/
void gfx_draw_pixel(int x, int y, uint16_t color);

/* 画水平线 */
void gfx_draw_hline(int x0, int x1, int y, uint16_t color);

/* 画垂直线 */
void gfx_draw_vline(int x, int y0, int y1, uint16_t color);

/* 画矩形（边框）*/
void gfx_draw_rect(int x0, int y0, int x1, int y1, uint16_t color);

/* 填充矩形（DMA2D R2M 加速）*/
void gfx_fill_rect(int x0, int y0, int x1, int y1, uint16_t color);

/* 画 8×16 ASCII 字符（内置字库，M2 简易显示用）*/
void gfx_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg);

/* 画字符串 */
void gfx_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg);

/* back → front 翻转（WP-C.5 真双缓冲：LTDC CFBAR 乒乓 + 消隐期重载，零拷贝）*/
void gfx_flip(void);

/* 整页重绘后同步另一缓冲（乒乓局部重绘语义保障；日常局部重绘勿调）*/
void gfx_sync_buffers(void);

/* K3 v9.5：小更新直绘后矩形同步——gfx_use_shown_buffer() 直绘显示缓冲完成后调用，
 * 把该 rect 从显示缓冲拷入隐藏缓冲。仅切页/弹窗才 flip，其余更新零翻转零闪屏。*/
void gfx_sync_rect(int x0, int y0, int x1, int y1);

/* 选定后续绘制目标缓冲（WP-C.5 引入，v9.5 K3 直绘路径主用）*/
void gfx_use_shown_buffer(void);
void gfx_use_hidden_buffer(void);

#endif /* LTDC_H */
