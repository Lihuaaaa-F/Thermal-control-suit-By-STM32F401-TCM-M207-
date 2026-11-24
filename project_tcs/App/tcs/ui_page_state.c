/* ui_page_state.c — 见 ui_page_state.h。纯逻辑，不要 include 任何 HAL/LVGL 头。*/
#include "ui_page_state.h"

uint8_t ui_p0_state_changed(const ui_p0_state_t *was, const ui_p0_state_t *now)
{
    if (!was || !now) return 1;
    return (uint8_t)(was->mode != now->mode ||
                     was->fan  != now->fan  ||
                     was->tec1 != now->tec1 ||
                     was->tec2 != now->tec2 ||
                     was->estop != now->estop ||
                     was->alm  != now->alm  ||
                     was->body != now->body ||
                     was->m207 != now->m207);
}

uint8_t ui_p1_state_changed(const ui_p1_state_t *was, const ui_p1_state_t *now)
{
    if (!was || !now) return 1;
    if (was->sel != now->sel || was->step != now->step) return 1;
    for (int i = 0; i < 4; i++)
        if (was->val[i] != now->val[i]) return 1;
    return 0;
}
