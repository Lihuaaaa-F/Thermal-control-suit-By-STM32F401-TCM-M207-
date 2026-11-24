/* tui.c — 实时 UI 层：页管理 / 顶栏状态条 / 报警横幅 / 弹窗 / 提示条 / 刷新节拍
 * 页面与横幅的"显示什么"全部由 ui_model 决定（纯逻辑、可单测）；这里只负责画。 */
#include "tui.h"
#include "ui_common.h"
#include "link_snap.h"
#include "control.h"
#include <stdio.h>
#include <string.h>

/* 主循环在"快照带来状态变化"时写（ms）：用来量"状态变化 → 上屏"的真实延迟 */
volatile uint32_t g_ui_state_change_ms;
volatile uint32_t g_ui_state_change_seq;
volatile uint32_t g_ui_stat[8];
volatile uint32_t g_ui_probe[80];

static tui_page_t s_page = TUI_HOME;
static int        s_diag_mode;
static lv_obj_t * s_scr;
static lv_obj_t * s_mask;               /* 模态遮罩（弹窗在场时暂停刷新）*/
static int        s_pending = -2;       /* -2 无 / -1 重建当前 / >=0 切页 */
static int        s_rebuilt;
static uint32_t   s_last_tick;
static ui_in_t    s_in;
static ui_link_view_t s_lv;

/* ---- 横幅 ---- */
#define BN_X      208
#define BN_W      440      /* 右边给顶栏状态条留出 660..1000 */
#define BN_H      56
static ui_banner_t s_bn;
static uint8_t     s_bn_have;
static lv_obj_t *  s_bn_layer;
static lv_obj_t *  s_bn_detail;   /* 横幅第二行：主控断开时每秒只改它的文字，不重建横幅 */
static ui_ack_t    s_ack;
static ui_bn_kind_t s_ack_kind;

/* ---- 顶栏状态条（每一页都在）---- */
static ui_pill_t s_pctrl, s_pmod;
static char      s_pctrl_text[24], s_pmod_text[24];
static uint8_t   s_strip_valid;

/* ---- 提示条 ---- */
#define UI_TOAST_MS  3500u     /* 一句 20 字左右的提示，3.5s 够读一遍 */
static lv_obj_t *s_toast_box, *s_toast_label;
static uint32_t  s_toast_until;

void tui_diag_set_mode(int mode) { s_diag_mode = mode; }
tui_page_t tui_current(void) { return s_page; }
int tui_last_was_rebuild(void) { int v = s_rebuilt; s_rebuilt = 0; return v; }

void tui_input(const ui_in_t **in, const ui_link_view_t **lv)
{
    *in = &s_in;
    *lv = &s_lv;
}

/* ================= 提示条 ================= */

void ui_toast(const char *text)
{
    if (!text || !*text) return;
    if (!s_toast_box) {
        s_toast_box = ui_box(lv_layer_top(), 0, 0, 10, 56, TKS_HERO, 12);
        s_toast_label = ui_text_center(s_toast_box, &tcs_font_lv24, TKS_WHITE, "");
    }
    lv_obj_set_width(s_toast_box, ui_text_w(&tcs_font_lv24, text) + 48);
    /* 横幅槽位（8..64）正下方：不挡横幅，更够不着底栏急停键 */
    lv_obj_align(s_toast_box, LV_ALIGN_CENTER, 0, 100 - 300);
    ui_lab_set(s_toast_label, text);
    lv_obj_set_hidden(s_toast_box, false);
    s_toast_until = lv_tick_get() + UI_TOAST_MS;
    g_ui_stat[4]++;
}

static void toast_tick(uint32_t now)
{
    if (s_toast_box && !lv_obj_is_hidden(s_toast_box) &&
        (int32_t)(now - s_toast_until) >= 0)
        lv_obj_set_hidden(s_toast_box, true);
}

/* ================= 顶栏状态条 ================= */

static void led_colors(ui_led_t led, lv_color_t *bg, lv_color_t *fg, lv_color_t *dot)
{
    *dot = TKS_GREEN;
    switch (led) {
        case UI_LED_GREEN:  *bg = TKS_CARD;   *fg = TKS_TX;    *dot = TKS_GREEN;  break;
        case UI_LED_GRAY:   *bg = TKS_CARD;   *fg = TKS_TX2;   *dot = TKS_GRAY;   break;
        case UI_LED_YELLOW: *bg = TKS_WARNBG; *fg = TKS_WARNTX; *dot = TKS_YELLOW; break;
        default:            *bg = TKS_ALMBG;  *fg = TKS_ALMTX;  *dot = TKS_RED;    break;
    }
}

static void strip_update(int force)
{
    char a[24], b[24];
    ui_led_t la, lb;
    ui_status_ctrl(&s_lv, a, sizeof a, &la);
    ui_status_module(&s_lv, b, sizeof b, &lb);
    if (!force && s_strip_valid && strcmp(a, s_pctrl_text) == 0 && strcmp(b, s_pmod_text) == 0)
        return;
    lv_color_t bg, fg, dot;
    /* 两枚都右对齐到 x=1000：先放"模块"，再按它的宽度放"主控"。
     * 宽度用实测文字宽算，不读 lv_obj_get_width：刚改完尺寸时布局还没刷新，读到的是旧值
     * （2026-11-24 真机：二级页上两枚胶囊就是这样被放到了屏幕外）。*/
    led_colors(lb, &bg, &fg, &dot);
    ui_pill_set(&s_pmod, b, bg, fg, UI_PILL_DOT_R, dot);
    int wb = ui_pill_width(&tcs_font_lv24, b, UI_PILL_DOT_R);
    ui_pill_move(&s_pmod, 1000 - wb, 16);
    led_colors(la, &bg, &fg, &dot);
    ui_pill_set(&s_pctrl, a, bg, fg, UI_PILL_DOT_R, dot);
    int wa = ui_pill_width(&tcs_font_lv24, a, UI_PILL_DOT_R);
    ui_pill_move(&s_pctrl, 1000 - wb - 12 - wa, 16);
    snprintf(s_pctrl_text, sizeof s_pctrl_text, "%s", a);
    snprintf(s_pmod_text, sizeof s_pmod_text, "%s", b);
    s_strip_valid = 1;
}

static void strip_build(void)
{
    s_pctrl = ui_pill(s_scr, 700, 16, 40, &tcs_font_lv24);
    s_pmod  = ui_pill(s_scr, 836, 16, 40, &tcs_font_lv24);
    s_strip_valid = 0;
    strip_update(1);
}

/* ================= 报警横幅 ================= */

static void banner_tap(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!s_bn_have) return;
    g_ui_stat[3]++;
    if (s_bn.tap == UI_BN_TAP_ACK) {
        /* 发确认；结果由下一帧主控快照给出（ui_ack_poll），这里不抢先清屏上的报警 */
        s_ack_kind = s_bn.kind;
        tcs_alarm_ack();
        ui_ack_start(&s_ack, &s_in);
    } else {
        const char *h = ui_banner_tap_hint(&s_bn);
        if (h) ui_toast(h);
    }
}

static void banner_build(const ui_banner_t *b)
{
    /* 首页占顶栏槽位（y=8）；二级页顶栏被"返回/标题/状态条"占着，横幅落在其下方（y=64）*/
    int dy = (s_page == TUI_HOME) ? 0 : 56;
    lv_color_t bg = b->red ? TKS_ALMBG : TKS_WARNBG;
    lv_color_t tx = b->red ? TKS_ALMTX : TKS_WARNTX;
    s_bn_layer = ui_box(s_scr, 0, 0, 1024, 600, TKS_BG, 0);
    lv_obj_set_style_bg_opa(s_bn_layer, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(s_bn_layer, false);
    lv_obj_t *box = ui_box(s_bn_layer, BN_X, 8 + dy, BN_W, BN_H, bg, 12);
    lv_obj_set_clickable(box, true);
    lv_obj_add_event_cb(box, banner_tap, LV_EVENT_CLICKED, NULL);
    if (b->red) ui_icon_sq20(s_bn_layer, BN_X + 18, 26 + dy, tx);
    else        ui_icon_tri20(s_bn_layer, BN_X + 18, 26 + dy, tx, bg);
    ui_text24(s_bn_layer, BN_X + 50, 12 + dy, b->title, tx);
    s_bn_detail = ui_text24(s_bn_layer, BN_X + 50, 38 + dy, b->detail, tx);
    if (b->extra) {
        /* "还有 N 条"：放第一行右端（标题都短，第二行留给说明）*/
        char cnt[24];
        snprintf(cnt, sizeof(cnt), "还有%d条", b->extra);
        int cw = ui_text24_w(cnt) + 16;
        int cx0 = BN_X + BN_W - cw - 12;
        ui_outline(s_bn_layer, cx0, 12 + dy, cx0 + cw - 1, 32 + dy, tx);
        ui_text24(s_bn_layer, cx0 + 8, 10 + dy, cnt, tx);
    }
}

static void banner_sync(void)
{
    ui_banner_t want;
    ui_banner_select(&s_in, &s_lv, &want);
    /* 确认在途：横幅保持不动。点击瞬间本地先清报警位、随后快照又把旧位回灌，再过一两拍
     * 主控处理完才真正消失——不冻结的话横幅会"消失→回来→消失"闪一下（2026-11-24 真机）。*/
    if (s_ack.waiting && s_bn_have) return;
    if (want.kind == UI_BN_NONE) {
        if (s_bn_layer) { lv_obj_delete(s_bn_layer); s_bn_layer = NULL; }
        s_bn_have = 0;
        s_bn_detail = NULL;
        return;
    }
    if (s_bn_have && want.kind == s_bn.kind && want.red == s_bn.red &&
        want.extra == s_bn.extra && strcmp(want.title, s_bn.title) == 0) {
        /* 只有第二行变了（主控断开时的"数据停在 N 秒前"每秒变一次）：只改这一行文字。
         * 整条横幅删了重建会让那块区域每秒重画一次，看着一闪一闪。*/
        if (strcmp(want.detail, s_bn.detail) != 0) {
            snprintf(s_bn.detail, sizeof s_bn.detail, "%s", want.detail);
            if (s_bn_detail) ui_lab_set(s_bn_detail, s_bn.detail);
        }
        return;
    }
    if (s_bn_layer) lv_obj_delete(s_bn_layer);
    s_bn_detail = NULL;
    s_bn = want;
    s_bn_have = 1;
    banner_build(&s_bn);
    g_ui_stat[2]++;
}

static void ack_poll(void)
{
    uint16_t still = 0;
    ui_ack_result_t r = ui_ack_poll(&s_ack, &s_in, &still);
    if (r == UI_ACK_WAIT) return;
    switch (r) {
        case UI_ACK_ALL_CLEAR:
            /* 急停会把设备切到手动：如实告诉使用者下一步（急停复位后不自动重启）*/
            if (s_ack_kind == UI_BN_ESTOP)
                ui_toast("急停已解除 · 现在是手动模式，点 切自动 恢复自动温控");
            else if (s_ack_kind == UI_BN_OVERTEMP)
                ui_toast("超温已解除，可以继续使用");
            else
                ui_toast("已恢复");
            break;
        case UI_ACK_PARTIAL: {
            const char *why = ui_ack_reason(still, &s_lv);
            ui_toast(why ? why : "还有报警没解除");
            break;
        }
        case UI_ACK_NO_REPLY:
            ui_toast("主控没有响应，请检查连接");
            break;
        default:
            break;
    }
}

/* ================= 弹窗 ================= */

static void mask_click_cb(lv_event_t *e) { LV_UNUSED(e); }

void tui_dlg_close(void)
{
    if (s_mask) { lv_obj_delete(s_mask); s_mask = NULL; }
}

int tui_dlg_active(void) { return s_mask != NULL; }

static void dlg_estop_ok(lv_event_t *e)
{
    LV_UNUSED(e);
    tcs_emergency_stop();
    tui_dlg_close();
    ui_toast("已急停：TEC 全停、风扇全速");
}

static void dlg_cancel(lv_event_t *e)
{
    LV_UNUSED(e);
    tui_dlg_close();
}

/* 标题 + 正文 + 两个键（确认键随 danger 取红/蓝，取消键统一幽灵样式）*/
static void build_dialog(const char *title, const char *body, const char *ok,
                         int danger, lv_event_cb_t ok_cb)
{
    if (s_mask) return;
    s_mask = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_mask);
    lv_obj_set_size(s_mask, 1024, 600);
    lv_obj_set_pos(s_mask, 0, 0);
    lv_obj_set_style_bg_opa(s_mask, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_mask, mask_click_cb, LV_EVENT_CLICKED, NULL);
    ui_mask_dots(s_mask);

    ui_card(s_mask, 256, 136, 512, 320, TKS_WHITE, TKS_BORDER);
    ui_text32(s_mask, 296, 168, title, danger ? TKS_RED : TKS_TX);
    ui_text24(s_mask, 296, 232, body, TKS_TX);
    ui_button(s_mask, 296, 344, 192, 88, ok, danger ? UI_BTN_DANGER : UI_BTN_PRIMARY,
              &tcs_font_lv24, ok_cb, NULL);
    ui_button(s_mask, 536, 344, 192, 88, "取消", UI_BTN_GHOST, &tcs_font_lv24, dlg_cancel, NULL);
}

void tui_dlg_estop(void)
{
    build_dialog("确认急停？", "TEC 将全部停止，风扇全开。", "确认急停", 1, dlg_estop_ok);
}

static void dlg_mode_ok(lv_event_t *e)
{
    LV_UNUSED(e);
    (void)param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_TOUCH);
    tui_dlg_close();
}

void tui_dlg_mode(void)
{
    build_dialog("切换到自动模式？", "自动模式按体温闭环控制制冷。", "切换", 0, dlg_mode_ok);
}

/* ================= 页管理 ================= */

void tui_goto(tui_page_t p)
{
    s_page = p;
    tui_dlg_close();
    lv_obj_clean(s_scr);
    /* 旧横幅随整屏清掉；状态条与页面内容由下面的构建重建 */
    s_bn_layer = NULL;
    s_bn_detail = NULL;
    s_bn_have = 0;
    switch (p) {
        case TUI_HOME:  tui_p0_build(s_scr); break;
        case TUI_TUNE:  tui_p1_build(s_scr); break;
        case TUI_CHART: tui_p2_build(s_scr); break;
        case TUI_CONN:  tui_p3_build(s_scr); break;
        case TUI_DIAG:  tui_diag_build(s_scr, s_diag_mode); break;
        default: break;
    }
    if (p != TUI_DIAG) strip_build();
    s_rebuilt = 1;
    g_ui_stat[0]++;
}

void tui_request(tui_page_t p) { s_pending = (int)p; }
void tui_request_rebuild(void) { s_pending = -1; }

/* ================= 走查命令 ================= */

volatile uint32_t g_ui_cmd;

static void ui_cmd_dispatch(uint32_t cmd)
{
    switch (cmd) {
        case 1:  tui_dlg_estop(); break;
        case 2:  tui_dlg_mode(); break;
        case 3:  tui_request(TUI_TUNE); break;
        case 4:  tui_request(TUI_CHART); break;
        case 5:  tui_request(TUI_CONN); break;
        case 6:  control_fan_toggle(); break;          /* 风扇状态由就地更新跟上，不重建 */
        case 7:  (void)param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_TOUCH); break;
        case 8:  banner_tap(NULL); break;              /* 与点击横幅同一条路径 */
        case 9:  tui_step_param(+1); break;
        case 10: tui_step_param(-1); break;
        case 11: tui_step_toggle(); tui_request_rebuild(); break;
        case 12: tui_sel_next(); tui_request_rebuild(); break;
        case 13: tui_request(TUI_HOME); break;
        case 14: tui_slider_center(); tui_request_rebuild(); break;
        case 15: tui_dlg_close(); break;
        case 16: tui_dlg_close(); tui_dlg_estop(); break;
        /* —— 诊断页（只走查用）—— */
        case 100: tui_diag_set_mode(0); tui_request(TUI_DIAG); break;
        case 101: tui_diag_set_mode(1); tui_request(TUI_DIAG); break;
        case 102: tui_diag_set_mode(2); tui_request(TUI_DIAG); break;
        case 103: tui_diag_set_mode(3); tui_request(TUI_DIAG); break;
        default: break;
    }
}

/* ================= 节拍 ================= */

static void input_refresh(void)
{
    uint32_t age, seq;
    uint8_t have;
    tcs_link_snap_info(&age, &seq, &have);
    memset(&s_in, 0, sizeof s_in);
    s_in.now_ms = lv_tick_get();
    s_in.snap_age_ms = have ? age : UI_LINK_NEVER;
    s_in.snap_seq = have ? seq : 0;
    /* 屏端 g_tcs 就是最近一帧快照的镜像（snap_apply / snap_reassert 维护）*/
    s_in.m207_online = g_tcs.m207_online;
    s_in.body_ok = g_tcs.body_sensor_ok;
    s_in.sensor_open = (g_tcs.alarm_bits & ALM_SENSOR_OPEN) ? 1u : 0u;
    s_in.alarm = g_tcs.alarm_bits;
    s_in.mode = g_tcs.mode;
    s_in.fan = g_tcs.fan_on;
    s_in.tec1 = g_tcs.tec1_on;
    s_in.tec2 = g_tcs.tec2_on;
    s_in.tc1_act = g_tcs.tc1_act;
    s_in.tc2_act = g_tcs.tc2_act;
    s_in.body = g_tcs.body_temp;
    s_lv = ui_link_view(&s_in);
}

static int page_update(void)
{
    switch (s_page) {
        case TUI_HOME:  return tui_p0_update();
        case TUI_TUNE:  return tui_p1_update();
        case TUI_CHART: return tui_p2_update();
        case TUI_CONN:  return tui_p3_update();
        default:        return 0;
    }
}

void tui_tick(void)
{
    /* ⓪ 走查命令消费（probe 写入）*/
    if (g_ui_cmd) {
        uint32_t c = g_ui_cmd;
        g_ui_cmd = 0;
        ui_cmd_dispatch(c);
    }
    /* ① 延后导航/重建：在 lv_timer_handler 之外的上下文执行（LVGL 回调里不能删/建屏）*/
    if (s_pending != -2 && !s_mask) {
        int p = s_pending;
        s_pending = -2;
        if (p < 0) tui_goto(s_page);
        else       tui_goto((tui_page_t)p);
    }
    /* ② 周期刷新 */
    uint32_t now = lv_tick_get();
    if (now - s_last_tick < UI_TICK_MS) return;
    s_last_tick = now;
    input_refresh();
    strip_update(0);
    if (!s_mask) {
        if (page_update()) tui_goto(s_page);
        banner_sync();
        ack_poll();
    }
    toast_tick(now);
}

void tui_init(void)
{
    lv_obj_t *act = lv_screen_active();
    ui_screen_init(act);
    s_scr = lv_obj_create(NULL);
    ui_screen_init(s_scr);
    lv_screen_load(s_scr);
    memset(&s_in, 0, sizeof s_in);
    s_in.snap_age_ms = UI_LINK_NEVER;
    s_lv = ui_link_view(&s_in);
    tui_goto(TUI_HOME);
    s_last_tick = lv_tick_get();
}
