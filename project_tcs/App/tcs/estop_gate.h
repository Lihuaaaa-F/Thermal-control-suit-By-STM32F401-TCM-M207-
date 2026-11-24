/* estop_gate.h — 急停屏入口兜底的判定（纯函数，主机可测：tests/test_site_logic.c）
 *
 * 背景（2026-11-23/24 现场）：规范是"急停二次确认（弹窗）"，正常路径由 LVGL 弹窗。
 * 这里只管不依赖 LVGL 的原始坐标兜底（D-L7②）。热区 = 首页底栏"急停"键的矩形。
 *
 * 规则（2026-11-24 晚，两条线各写了一版之后合一；每条后面是理由）：
 *  A  首页 + LVGL 卡死（按压期间渲染心跳一次都没推进）→ 过了宽限期就触发。
 *     首页热区就是急停键本身，不存在"按到别的控件"；卡死时弹窗这条路已经不可用。
 *  B  首页 + LVGL 活着 + 按住 ≥600ms + "确认急停"弹窗没有开着 → 触发（长按直接急停，
 *     用户 2026-11-24 点名保留）。"确认急停"弹窗开着说明界面在正常响应，让位给它。
 *     开着的是别的弹窗（比如"切自动"）→ 不让位：急停不该被任何弹窗挡住（U 线独立复核 P2-8）。
 *  C  其它页面：界面在正常处理这次按压时绝不触发——这块区域放的是别的控件，调参页的"+"就在这里。
 *     只有"界面没有在处理这次按压"——渲染心跳没推进，或者 LVGL 的触摸读回调自按下起一次都没有
 *     报告过按下（indev 半死）——并且按住 ≥600ms 才触发。
 *     理由：界面冻在别的页面上时，用户既回不了首页也按不了任何东西；屏幕上的急停不能因此够不着，
 *     直到断电重启。要求按住 600ms 是为了不让一下误碰就停机。
 *     U 线的版本是"其它页面一律不触发"；这一条两线判断不同，取"够得着"（S 线结果文档 §4 D7）。
 *
 * 每次按压只触发一次。本文件不碰任何寄存器，输入由调用方取好再传进来。*/
#ifndef ESTOP_GATE_H
#define ESTOP_GATE_H

#include <stdint.h>

/* 按下后至少跨这么久再看心跳：watch 在 poll 之前调用，要给 LVGL 一个主循环节拍。*/
#define TCS_ESTOP_GRACE_MS        5u
/* 按住这么久：首页 = 直接急停；其它页面 = 在界面没处理这次按压的前提下直接急停。*/
#define TCS_ESTOP_FORCE_HOLD_MS   600u

typedef enum {
    ESTOP_GATE_IDLE = 0,             /* 没按下 / 还在等 / 本次按压已触发过 */
    ESTOP_GATE_FIRE_DEAD,            /* 首页：LVGL 心跳停了 */
    ESTOP_GATE_FIRE_HOLD,            /* 首页：按住 600ms，没有"确认急停"弹窗 */
    ESTOP_GATE_FIRE_HOLD_UNHANDLED   /* 其它页面：按住 600ms，界面没有在处理这次按压 */
} estop_gate_fire_t;

typedef struct {
    uint8_t  in_zone;   /* 上一步是否已在热区内 */
    uint8_t  fired;     /* 本次按压是否已触发 */
    uint32_t since;     /* 本次按压起始时刻（ms）*/
    uint32_t beat0;     /* 本次按压起始时的 LVGL 渲染心跳 */
    uint32_t reads0;    /* 本次按压起始时"LVGL 读回调报告按下"的累计次数 */
} estop_gate_state_t;

/* 每个主循环节拍调用一次。
 * pressed_in_zone：当前有触摸（合成或真实）且落在热区内。
 * estop_dlg_open：当前开着的是"确认急停"弹窗（别的弹窗传 0）。
 * on_home：当前在首页。
 * now：毫秒时基；beat：LVGL 渲染心跳（每次真正执行 handler 加 1）。
 * pressed_reads：LVGL 触摸读回调累计报告过多少次"按下"（每读到一次按下都加 1，不是每次按压加 1）。
 *   用"读次数有没有变"而不是"按压事件有没有来"：watch 先于 LVGL 的读回调运行，
 *   按压事件计数可能在登记之前就已经加过了；读次数只要手指还按着就会一直变，不受先后顺序影响。
 * 返回本次是否要触发急停；触发后直到离开热区前不再返回触发。*/
estop_gate_fire_t estop_gate_step(estop_gate_state_t *st, uint8_t pressed_in_zone,
                                  uint8_t estop_dlg_open, uint8_t on_home,
                                  uint32_t now, uint32_t beat, uint32_t pressed_reads);

#endif /* ESTOP_GATE_H */
