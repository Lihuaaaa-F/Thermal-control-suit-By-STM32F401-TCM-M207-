/* lv_port_indev.c — G4：FT5446 原始采样 → LVGL 指针 indev
 * 2026-10-29 增测试钩子（源内固化）：
 *   g_tap_inject[4] = {seq, mode, x, y}
 *     mode 1 = 合成一次 150ms 按压（触摸走查/热区边界点）
 *     mode 2 = 持续按住（PRESSED 直到 mode 改 0；2026-11-15 UI 单栈后用于 sim touchh/r）
 *   g_tap_drop      = 1 时 read_cb 恒返回 RELEASED（TL-13 indev 丢帧注入）
 * 正常运行时 g_tap_inject[0] 恒 0、g_tap_drop 恒 0，路径与原来一致。
 * 2026-11-24（S 线 C01）：这两个钩子只在台架/仿真档（TCS_PROFILE >= 1）编译；
 * 交付档的 read_cb 里只有真实触摸这一条路。
 * 2026-11-24 晚：每向 LVGL 报告一次"按下"给 g_indev_pressed_reads 加 1。急停屏入口判定
 * （estop_gate.c 规则 C）在别的页面上靠"按住期间这个数有没有变"区分"界面在处理这次按压"
 * 和"触摸没送进 LVGL"。计的是读的次数而不是按压的次数：只要手指还按着它就一直变，
 * 不受 watch 与 LVGL 读回调谁先谁后的影响。 */
#include "lv_port_indev.h"
#include "tcs_lvgl.h"
#include "ft5446.h"

volatile uint32_t g_indev_pressed_reads;

#if TCS_PROFILE >= 1
static uint32_t s_seq;
static uint32_t s_end_ms;      /* 合成按压截止时刻（0=无） */
static int32_t  s_x, s_y;
#endif

static void report_pressed(lv_indev_data_t * data, int32_t x, int32_t y)
{
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = (lv_coord_t)x;
    data->point.y = (lv_coord_t)y;
    g_indev_pressed_reads++;
}

static void touch_read_cb(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);
    data->state = LV_INDEV_STATE_RELEASED;

#if TCS_PROFILE >= 1
    if (g_tap_drop) return;    /* TL-13：整条链路丢帧 */

    uint32_t seq = g_tap_inject[0];
    if (seq != s_seq) {
        s_seq = seq;
        s_x = (int32_t)g_tap_inject[2];
        s_y = (int32_t)g_tap_inject[3];
        if (g_tap_inject[1] == 1) {
            s_end_ms = lv_tick_get() + 150u;   /* 单次合成按压 */
        } else {
            s_end_ms = 0;
        }
    }
    if (g_tap_inject[1] == 2) {
        /* 持续按住：直到外部把 mode 改回 0（sim touchr）*/
        report_pressed(data, s_x, s_y);
        return;
    }
    if (s_end_ms && (int32_t)(s_end_ms - lv_tick_get()) > 0) {
        report_pressed(data, s_x, s_y);
        return;
    }
    s_end_ms = 0;
#endif

    touch_event_t ev;
    if(ft5446_read(&ev) != 0) return;
    if(!ev.valid) return;
    if(ev.touched) report_pressed(data, ev.x, ev.y);
}

void lv_port_indev_init(void)
{
    lv_indev_t * indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, lv_display_get_default());
}
