/* estop_gate.h — 急停屏入口兜底的判定（纯函数，主机可测：tests/test_site_logic.c）
 *
 * 背景（2026-11-23/24 现场）：规范是"急停二次确认（弹窗）"，正常路径由 LVGL 弹窗。
 * 这里只管不依赖 LVGL 的原始坐标兜底（D-L7②）：
 *   - LVGL 心跳停了（卡死 / 低水位跳过 handler）→ 任何页面，按下后下一个节拍直接急停；
 *   - LVGL 活着但按住 ≥600ms 仍无弹窗（indev 半死）→ 只在首页直接急停
 *     （用户 2026-11-24 确认保留长按直接急停；其它页面这块区域是别的控件，调参页的"+"就在这里）。
 * 每次按压只触发一次。本文件不碰任何寄存器，输入由调用方取好再传进来。*/
#ifndef ESTOP_GATE_H
#define ESTOP_GATE_H

#include <stdint.h>

/* 热区 = 首页底栏"急停"键（屏幕绝对坐标）。首页的按钮与点击区也用这组宏，防止两处漂移。*/
#define TCS_ESTOP_ZONE_X   768u
#define TCS_ESTOP_ZONE_Y   472u
#define TCS_ESTOP_ZONE_W   216u
#define TCS_ESTOP_ZONE_H   112u

/* 按下后至少跨这么久再看心跳：watch 在 poll 之前调用，要给 LVGL 一个主循环节拍。*/
#define TCS_ESTOP_GRACE_MS        5u
/* 按住这么久还没弹窗 → 判 indev 半死。*/
#define TCS_ESTOP_FORCE_HOLD_MS   600u

typedef enum {
    ESTOP_GATE_IDLE = 0,   /* 没按下 / 还在等 / 本次按压已触发过 */
    ESTOP_GATE_FIRE_DEAD,  /* LVGL 心跳停了 */
    ESTOP_GATE_FIRE_HOLD   /* 首页长按无弹窗 */
} estop_gate_fire_t;

typedef struct {
    uint8_t  in_zone;   /* 上一步是否已在热区内 */
    uint8_t  fired;     /* 本次按压是否已触发 */
    uint32_t since;     /* 本次按压起始时刻（ms）*/
    uint32_t beat0;     /* 本次按压起始时的 LVGL 心跳 */
} estop_gate_state_t;

/* 每个主循环节拍调用一次。
 * pressed_in_zone：当前有触摸（合成或真实）且落在热区内。
 * dialog_open / on_home：仅在 LVGL 活着时有意义（卡死时调用方传什么都不会用到 on_home）。
 * now：毫秒时基；beat：LVGL 渲染心跳（每次真正执行 handler 加 1）。
 * 返回本次是否要触发急停；触发后直到离开热区前不再返回触发。*/
estop_gate_fire_t estop_gate_step(estop_gate_state_t *st, uint8_t pressed_in_zone,
                                  uint8_t dialog_open, uint8_t on_home,
                                  uint32_t now, uint32_t beat);

#endif /* ESTOP_GATE_H */
