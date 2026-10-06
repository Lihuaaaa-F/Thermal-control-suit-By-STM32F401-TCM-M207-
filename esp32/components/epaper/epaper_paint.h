#pragma once
/* paint 层: 双平面帧缓冲绘制(1bit/像素;bit=1 白/无色,bit=0 黑[黑平面]/红[红平面])
 * 坐标约定: plane[y*(W/8) + x/8], 掩码 0x80>>(x&7), 原点左上,宽 128 高 296 */
#include <stdint.h>
#include <stdbool.h>

#define PAINT_W 128
#define PAINT_H 296
#define PAINT_STRIDE (PAINT_W / 8)

void paint_fill_rect(uint8_t *plane, int x, int y, int w, int h, bool ink);
void paint_clear(uint8_t *plane, bool white);        /* white=true 全 0xFF */
/* 七段数码管风格大数字(温度显示用,免字库,渲染确定性高) */
void paint_draw_7seg(uint8_t *plane, int x, int y, int h, char ch);  /* 支持 0-9 '-' 'C' */
void paint_draw_dot(uint8_t *plane, int x, int y, int h);            /* 小数点(基线方点) */
/* 渲染温度: tenths=温度×10(如 256 表示 25.6°C), 输出 "25.6C", 返回占用宽度 */
int paint_render_temp(uint8_t *plane, int x, int y, int h, int tenths);
