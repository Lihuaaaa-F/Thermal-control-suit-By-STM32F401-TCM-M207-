/**
 * ft5446.h — FT5446/FT5206 电容触摸驱动（M3，软件 I2C bit-bang）
 *
 * 抄官方 LVGL 例程 ctiic.c + ft5206.c（FT5206 与 FT5446 同系列，协议一致）：
 *   - SCL=PD12 推挽、SDA=PD13 开漏+上拉（软件 bit-bang，非硬件 I2C）
 *   - 7bit 地址 0x38（写 0x70 / 读 0x71）
 *   - TP_INT=PH7 输入上拉（轮询）、TP_RST=PH6 推挽（低电平有效复位）
 *
 * WP-M2（v10.11）坐标映射定版【T-13】：
 *   本工程首版映射（标准字节序+X 镜像）与 GS 验证版差一个 90° 旋转，为 D1 头号
 *   嫌疑（总师预对拍 2026-10-19：GS 版 2026-07-28 真手指实测"raw 直接当屏幕像素，
 *   无需仿射"=交换 X/Y 不镜像）。本版恢复 GS 验证映射，变换收敛到唯一纯函数
 *   ft5446_map_raw()（PC 单测逐点断言 + raw 级注入走全链，封死注入绕过变换层盲区）。
 *
 * 帧级幽灵过滤已移除：H1 跳距过滤由触摸会话状态机（touch_session.c）按"整会话"
 * 判定取代，跨会话绝不惩罚。本驱动只负责：raw 读取 + 坏包防御 + 映射 + raw 日志。
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
#define FT5446_TP1_REG          0x03    /* 第1触点数据地址（6B：XY+weight+area）*/
#define FT5446_ID_G_MODE        0xA4    /* 中断模式控制（0=轮询）*/
#define FT5446_ID_G_THGROUP     0x80    /* 触摸有效值 */
#define FT5446_ID_G_PERIODACTIVE 0x88   /* 激活状态周期 */
#define FT5446_ID_G_LIB_VERSION 0xA1    /* 版本号（T-11：入日志头）*/

/* ====== 触摸样本（驱动输出 = 会话机输入）====== */
typedef struct {
    uint8_t touched;    /* 1=有触摸（n>0 且首触点 contact 事件）*/
    uint8_t valid;      /* 0=坏包/越界（驱动已计数，会话机防御性忽略）*/
    uint8_t injected;   /* 1=注入样本（T-13 raw 级注入 / 屏坐标注入）*/
    uint8_t contacts;   /* 原始触点数 n（reg 0x02 低 4 位）*/
    uint8_t touch_id;   /* 触点 id（buf[2] 高 4 位）*/
    uint8_t weight;     /* 触点重量（reg 0x07）*/
    uint8_t area;       /* 触点面积（reg 0x08，幽灵判定面积恒定项）*/
    int     x;          /* 屏幕像素 X（ft5446_map_raw 输出，0..LCD_WIDTH-1）*/
    int     y;          /* 屏幕像素 Y（0..LCD_HEIGHT-1）*/
} touch_event_t;

/* ====== 坐标映射（唯一出口，PC 单测逐点断言）======
 * GS 验证版（2026-07-28 本屏真手指实测）：交换 X/Y，不镜像。
 * 面板原生报点轴与横屏显示差 90°：screen_x = raw_y，screen_y = raw_x。
 * 之后走仿射校准（默认恒等）并 clamp 到屏内。*/
void ft5446_map_raw(uint16_t xr, uint16_t yr, int *sx, int *sy);

/* ====== raw 日志（M0：时间戳/触点数/raw xy/事件/id/重量/面积，RAM 环形）====== */
#define FTLOG_RING_N  4096u

typedef struct {
    uint32_t t_ms;      /* millis() */
    int16_t  xr, yr;    /* 原始报点 */
    uint8_t  n;         /* 触点数（0=抬起标记）*/
    uint8_t  ev;        /* 事件旗标（buf[0] 高 4 位）*/
    uint8_t  id;        /* 触点 id */
    uint8_t  src;       /* bit0=注入样本 */
} ftlog_ent_t;          /* 12B × 4096 = 48KB */

void ftlog_clear(void);
void ftlog_enable(uint8_t on);
uint32_t ftlog_count(void);          /* 累计入环条数（环形覆盖不减少）*/
const ftlog_ent_t *ftlog_at(uint32_t idx);   /* idx < ftlog_count()，老→新 */
uint16_t ft5446_fw_version(void);    /* T-11：IC 固件版本寄存器值（init 时锁存）*/

/* ====== raw 级注入（TCS_SIM 构建，T-13：携带 raw 坐标走完整变换链）====== */
void ft5446_raw_inject(int down, uint16_t xr, uint16_t yr);

/* ====== API ====== */

/* 初始化：I2C bit-bang GPIO + 复位 FT5446 + 配寄存器。返回 0=成功 <0=失败。*/
int ft5446_init(void);

/* 触摸是否就绪（init 成功为 1）。*/
int touch_ready(void);

/* 读当前触摸样本（轮询）。只读+映射+日志，不做会话级过滤（会话机负责）。
 * 返回 0=样本有效；-1=未就绪。无触摸时 ev->touched=0 仍返回 0（供会话机结算）。*/
int ft5446_read(touch_event_t *ev);

/* 触摸校准：仿射参数 x_s=a*x+b, y_s=c*y+d（映射后微调，默认恒等）。*/
void ft5446_set_calibration(float a, float b, float c, float d);

#endif /* FT5446_H */
