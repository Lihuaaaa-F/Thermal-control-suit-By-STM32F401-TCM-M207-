/* ui_page_state.h — 页面"要不要整页重建"的判定（纯函数，主机可测：tests/test_site_logic.c）
 *
 * 2026-11-24 现场：主页体温卡的样式（黄框 / 灰字 / "失联"角标）取决于体温传感器在线态，
 * 但原来的重建判据不比较它。未布防时传感器掉线/恢复不置报警位，没人触发重建，
 * 结果 sensor=1、无报警的主页仍挂着红色"失联"角标。
 * 规则：建页时把"建页读取过的每个状态字段"记成快照；刷新时逐字段比较，不同即重建。
 * 新增一个"建页时读取"的字段，就必须加进这里，否则会重演同一个问题。*/
#ifndef UI_PAGE_STATE_H
#define UI_PAGE_STATE_H

#include <stdint.h>

/* 首页（tui_p0）建页时读取的全部状态字段，每个都归一成 0/1。*/
typedef struct {
    uint8_t mode;    /* g_tcs.mode != 0 */
    uint8_t fan;     /* g_tcs.fan_on */
    uint8_t tec1;    /* g_tcs.tec1_on */
    uint8_t tec2;    /* g_tcs.tec2_on */
    uint8_t estop;   /* g_tcs.alarm_bits & ALM_ESTOP */
    uint8_t alm;     /* g_tcs.alarm_bits != 0 */
    uint8_t body;    /* g_tcs.body_sensor_ok */
    uint8_t m207;    /* g_tcs.m207_online */
} ui_p0_state_t;

/* 任一字段不同返回 1。两个指针都必须非空。*/
uint8_t ui_p0_state_changed(const ui_p0_state_t *was, const ui_p0_state_t *now);

/* 调参页（tui_p1）建页时读取的状态：选中行、步进、4 个参数的当前值。
 * 数值文字 / 单位位置 / 滑条填充与拇指位置都由它们决定。*/
typedef struct {
    uint8_t sel;     /* 当前选中行 0..3 */
    float   step;    /* 步进 0.1 / 1.0 */
    float   val[4];  /* TC1_SET / TC2_SET / BODY_HOT / BODY_COLD */
} ui_p1_state_t;

/* 任一项不同返回 1（浮点按 != 比较，值均为有限数）。两个指针都必须非空。*/
uint8_t ui_p1_state_changed(const ui_p1_state_t *was, const ui_p1_state_t *now);

#endif /* UI_PAGE_STATE_H */
