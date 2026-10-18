/**
 * ft5446.h — FT5446/FT5206 电容触摸驱动（M3，软件 I2C bit-bang）
 *
 * 抄官方 LVGL 例程 ctiic.c + ft5206.c（FT5206 与 FT5446 同系列，协议一致）：
 *   - SCL=PD12 推挽、SDA=PD13 开漏+上拉（软件 bit-bang，非硬件 I2C）
 *   - 7bit 地址 0x38（写 0x70 / 读 0x71）
 *   - TP_INT=PH7 输入上拉（轮询）、TP_RST=PH6 推挽（低电平有效复位）
 *   - 坐标 X:0~1023 Y:0~599，默认横屏 X 软件镜像（屏排线方向与显示相反）
 *
 * 触摸坐标 → 屏幕像素的映射：仿射校准（g_cal_a/b/c/d），M3b 校准后调。
 */
#ifndef FT5446_H
#define FT5446_H

#include <stdint.h>
#ifndef TCS_UNIT_TEST
#include "stm32h7xx_hal.h"
#endif

/* ====== FT5446 参数（抄 ft5206.h）====== */
#define FT5446_CMD_WR           0x70    /* 写命令（0x38<<1，末位0）*/
#define FT5446_CMD_RD           0x71    /* 读命令（末位1）*/
#define FT5446_DEVIDE_MODE      0x00    /* 模式控制 */
#define FT5446_REG_NUM_FINGER   0x02    /* 触点状态寄存器 */
#define FT5446_TP1_REG          0x03    /* 第1触点数据地址 */
#define FT5446_ID_G_MODE        0xA4    /* 中断模式控制（0=轮询）*/
#define FT5446_ID_G_THGROUP     0x80    /* 触摸有效值 */
#define FT5446_ID_G_PERIODACTIVE 0x88   /* 激活状态周期 */
#define FT5446_ID_G_LIB_VERSION 0xA1    /* 版本号 */

/* ====== 触摸事件 ====== */
typedef struct {
    int     touched;        /* 1=有触摸，0=无 */
    int     x;              /* 屏幕像素 X（已校准，0..LCD_WIDTH-1）*/
    int     y;              /* 屏幕像素 Y（已校准，0..LCD_HEIGHT-1）*/
} touch_event_t;

/* ====== 纯逻辑（PC 单测，H1 幽灵触点过滤）====== */
#define FT5446_GHOST_JUMP_PX  200   /* 坐标突跳阈值：相邻帧位移 >200px 判幽灵 */
/* 与上一有效点的位移是否可信（H1：>200px/帧 丢弃）。首点（have=0）恒可信。 */
uint8_t ft5446_ghost_jump_ok(int have, int px, int py, int x, int y);

/* ====== API ====== */

/* 初始化：I2C bit-bang GPIO + 复位 FT5446 + 配寄存器。返回 0=成功 <0=失败。*/
int ft5446_init(void);

/* 触摸是否就绪（init 成功为 1）。*/
int touch_ready(void);

/* 读当前触摸点（轮询方式）。不就绪或没触摸 → ev->touched=0。返回 0=成功。*/
int ft5446_read(touch_event_t *ev);

/* 触摸校准：仿射参数 x_s=a*xr+b, y_s=c*yr+d。M3b 校准后调。*/
void ft5446_set_calibration(float a, float b, float c, float d);

#endif /* FT5446_H */
