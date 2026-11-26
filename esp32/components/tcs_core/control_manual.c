/**
 * control_manual.c — 手动模式两路 TEC 输出映射（m1"模式"页）。设计见 control_manual.h。
 */
#include "control_manual.h"
#include "param_registry.h"
#include "control.h"          /* TEC_COOL_TARGET_C / TEC_HEAT_TARGET_C（安全常量，不进参数表）*/

static control_manual_ops_t s_ops;
static uint8_t s_applied[2];       /* 已下发的方向；下标 0 = TEC1，1 = TEC2 */
static uint8_t s_fan_linked;       /* 1 = 风扇正被联动开着 */
static uint8_t s_fan_user;         /* 联动之前用户的风扇选择 */

static uint16_t pid_of(uint8_t idx)
{
    return idx ? PARAM_ID_TEC2_MANUAL : PARAM_ID_TEC1_MANUAL;
}

static uint8_t tec_on(uint8_t idx)
{
    return idx ? (uint8_t)(g_tcs.tec2_on ? 1u : 0u) : (uint8_t)(g_tcs.tec1_on ? 1u : 0u);
}

void control_manual_init(const control_manual_ops_t *ops)
{
    if (ops) s_ops = *ops;
    s_applied[0] = TEC_DIR_PAUSE;
    s_applied[1] = TEC_DIR_PAUSE;
    s_fan_linked = 0;
    s_fan_user = 0;
}

/* 风扇联动：任一路制冷 / 制热 → 风扇开；两路都暂停 → 恢复用户原来的选择 */
static void fan_link(void)
{
    uint8_t active = (uint8_t)((s_applied[0] != TEC_DIR_PAUSE) || (s_applied[1] != TEC_DIR_PAUSE));
    if (active) {
        if (!s_fan_linked) {
            s_fan_linked = 1;
            s_fan_user = (uint8_t)(g_tcs.fan_on ? 1u : 0u);   /* 只在进入联动的那一刻记 */
        }
        /* 联动期间风扇必须开：别的入口（网页的风扇切换）把它关了，下一拍就开回来 */
        if (!g_tcs.fan_on && s_ops.fan_set) s_ops.fan_set(1);
    } else if (s_fan_linked) {
        s_fan_linked = 0;
        if (s_ops.fan_set) s_ops.fan_set(s_fan_user);
    }
}

void control_manual_poll(void)
{
    for (uint8_t i = 0; i < 2u; i++) {
        uint8_t req = (uint8_t)param_get(pid_of(i));
        if (req > TEC_DIR_HEAT) req = TEC_DIR_PAUSE;      /* 防御：表里只会有 0..2 */
        if (req != s_applied[i]) {
            /* 只在参数变化时下发：先设目标再开（与自动状态机同一顺序）；暂停只发关 */
            if (req != TEC_DIR_PAUSE) {
                if (s_ops.tec_temp)
                    s_ops.tec_temp((uint8_t)(i + 1u),
                                   (req == TEC_DIR_COOL) ? TEC_COOL_TARGET_C : TEC_HEAT_TARGET_C);
                if (s_ops.tec_sw) s_ops.tec_sw((uint8_t)(i + 1u), 1);
            } else {
                if (s_ops.tec_sw) s_ops.tec_sw((uint8_t)(i + 1u), 0);
            }
            s_applied[i] = req;
            continue;
        }
        /* 已下发"开"，模块回读却是关（模块被断电重启后复联重同步、网页 / 台架把它关了）：
         * 不自动重开——把参数也置回暂停，屏上如实显示暂停；用户重新点才会再输出。*/
        if (s_applied[i] != TEC_DIR_PAUSE && !tec_on(i)) {
            if (param_get(pid_of(i)) != 0.0f)
                (void)param_set(pid_of(i), 0.0f, PARAM_SRC_KERNEL);
            s_applied[i] = TEC_DIR_PAUSE;
        }
    }
    fan_link();
}

void control_manual_reset(int restore_fan)
{
    for (uint8_t i = 0; i < 2u; i++) {
        if (s_applied[i] != TEC_DIR_PAUSE && s_ops.tec_sw) s_ops.tec_sw((uint8_t)(i + 1u), 0);
        s_applied[i] = TEC_DIR_PAUSE;
        if (param_get(pid_of(i)) != 0.0f)
            (void)param_set(pid_of(i), 0.0f, PARAM_SRC_KERNEL);
    }
    if (s_fan_linked) {
        s_fan_linked = 0;
        if (restore_fan && s_ops.fan_set) s_ops.fan_set(s_fan_user);
    }
}

int control_manual_check(int manual_mode, int in_safe, int latched)
{
    if (!manual_mode) return PARAM_REJECT_NOT_MANUAL;
    if (in_safe || latched) return PARAM_REJECT_SAFE;
    return 0;
}

uint8_t control_manual_dir(uint8_t ch)
{
    return (ch == 1u || ch == 2u) ? s_applied[ch - 1u] : TEC_DIR_PAUSE;
}

uint8_t control_manual_fan_pref(void)
{
    if (s_fan_linked) return s_fan_user;
    return (uint8_t)(g_tcs.fan_on ? 1u : 0u);
}
