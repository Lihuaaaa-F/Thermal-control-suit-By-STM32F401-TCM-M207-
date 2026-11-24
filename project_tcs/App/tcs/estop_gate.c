/* estop_gate.c — 见 estop_gate.h。纯逻辑，不要在这里 include 任何 HAL/LVGL 头。*/
#include "estop_gate.h"

estop_gate_fire_t estop_gate_step(estop_gate_state_t *st, uint8_t pressed_in_zone,
                                  uint8_t estop_dlg_open, uint8_t on_home,
                                  uint32_t now, uint32_t beat, uint32_t pressed_reads)
{
    if (!pressed_in_zone) {
        st->in_zone = 0;      /* 离开热区（含触摸读取失败）→ 下次按下重新计 */
        return ESTOP_GATE_IDLE;
    }
    if (!st->in_zone) {
        st->in_zone = 1;
        st->fired   = 0;
        st->since   = now;
        st->beat0   = beat;
        st->reads0  = pressed_reads;
    }
    if (st->fired) return ESTOP_GATE_IDLE;
    uint32_t held = now - st->since;          /* 无符号减法，时基回绕安全 */
    if (held < TCS_ESTOP_GRACE_MS) return ESTOP_GATE_IDLE;

    uint8_t lvgl_alive = (uint8_t)(beat != st->beat0);
    if (on_home) {
        if (!lvgl_alive) {
            st->fired = 1;
            return ESTOP_GATE_FIRE_DEAD;
        }
        if (held >= TCS_ESTOP_FORCE_HOLD_MS && !estop_dlg_open) {
            st->fired = 1;
            return ESTOP_GATE_FIRE_HOLD;
        }
        return ESTOP_GATE_IDLE;
    }

    /* 其它页面：只有界面没有在处理这次按压时，长按才算数。*/
    uint8_t handled = (uint8_t)(lvgl_alive && pressed_reads != st->reads0);
    if (held >= TCS_ESTOP_FORCE_HOLD_MS && !handled) {
        st->fired = 1;
        return ESTOP_GATE_FIRE_HOLD_UNHANDLED;
    }
    return ESTOP_GATE_IDLE;
}
