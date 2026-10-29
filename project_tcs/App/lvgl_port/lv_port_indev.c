/* lv_port_indev.c — G4：FT5446 原始采样 → LVGL 指针 indev
 * 2026-10-29 增测试钩子（源内固化）：
 *   g_tap_inject[4] = {seq, touched, x, y} 合成一次 150ms 按压（触摸走查/热区边界点）
 *   g_tap_drop      = 1 时 read_cb 恒返回 RELEASED（TL-13 indev 丢帧注入）
 * 正常运行时 g_tap_inject[0] 恒 0、g_tap_drop 恒 0，路径与原来一致。 */
#include "lv_port_indev.h"
#include "tcs_lvgl.h"
#include "ft5446.h"

static uint32_t s_seq;
static uint32_t s_end_ms;      /* 合成按压截止时刻（0=无） */
static int32_t  s_x, s_y;

static void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);
    data->state = LV_INDEV_STATE_RELEASED;

    if (g_tap_drop) return;    /* TL-13：整条链路丢帧 */

    uint32_t seq = g_tap_inject[0];
    if (seq != s_seq) {
        s_seq = seq;
        if (g_tap_inject[1]) {
            s_x = (int32_t)g_tap_inject[2];
            s_y = (int32_t)g_tap_inject[3];
            s_end_ms = lv_tick_get() + 150u;
        }
    }
    if (s_end_ms && (int32_t)(s_end_ms - lv_tick_get()) > 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = s_x;
        data->point.y = s_y;
        return;
    }
    s_end_ms = 0;

    touch_event_t ev;
    if(ft5446_read(&ev) != 0) return;
    if(!ev.valid) return;
    if(ev.touched) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = ev.x;
        data->point.y = ev.y;
    }
}

void lv_port_indev_init(void)
{
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, lv_display_get_default());
}
