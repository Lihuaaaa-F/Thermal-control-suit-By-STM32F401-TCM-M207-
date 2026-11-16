/* tui.c — G5 实时 UI 层：页管理 / 报警横幅（v1.2 驻留规则）/ 急停与模式弹窗 / 500ms 刷新 */
#include "tui.h"
#include "ui_common.h"
#include "control.h"
#include <stdio.h>
#include <string.h>

static tui_page_t s_page = TUI_HOME;
static int s_diag_mode;
void tui_diag_set_mode(int mode) { s_diag_mode = mode; }
static lv_obj_t * s_scr;                 /* 单屏复用：切页=清屏重建 */
static lv_obj_t * s_mask;                /* 模态遮罩 */
static int s_pending = -2;               /* -2 无 / -1 重建当前 / >=0 切页 */
static int s_rebuilt;                    /* 上一拍执行过整页重建（预算归因用） */
static uint32_t s_last_refresh;

/* ---------------- 报警横幅（优先级：ESTOP>OVERTEMP>BODY>M207>EEPROM） ---------------- */
volatile uint32_t g_ui_probe[80];
static lv_obj_t * s_bn_box;
static int s_bn_kind = -1;               /* -1=无报警 0..4=种类；变化才重建内容 */

typedef struct { uint16_t bit; const char * msg; const char * act; uint8_t red; } alm_row_t;
static const alm_row_t s_alm[] = {
    { ALM_ESTOP,       "急停已触发",       "已断电制动 · 点击横幅确认恢复",  1 },
    { ALM_OVERTEMP,    "水温过高",         "已停止加热 · 点击横幅确认恢复",  1 },
    { ALM_SENSOR_OPEN, "水温传感器断线",   "已停止加热 · 检查传感器接线后确认", 1 },
    { ALM_BODY_SENSOR, "体温传感器失联",   "检查传感器接头 · 点击横幅确认恢复", 0 },
    { ALM_M207,        "M207 通信失联",    "检查通信线缆 · 点击横幅确认恢复", 0 },
    { ALM_EEPROM,      "配置校验失败",     "已恢复默认参数 · 请重新设定",   0 },
};
#define ALM_N  (sizeof(s_alm)/sizeof(s_alm[0]))

static int alarm_pick(uint16_t bits, int * extra)
{
    int total = 0, first = -1;
    for (unsigned i = 0; i < ALM_N; i++)
        if (bits & s_alm[i].bit) { total++; if (first < 0) first = (int)i; }
    *extra = total > 1 ? total - 1 : 0;
    return first;
}

static void banner_ack_cb(lv_event_t * e)
{
    LV_UNUSED(e);
    tcs_alarm_ack();
}

void tui_banner_build(lv_obj_t * parent)
{
    s_bn_box = NULL;
    int extra = 0;
    int k = alarm_pick(g_tcs.alarm_bits, &extra);
    s_bn_kind = k;
    if (k < 0) return;
    lv_color_t bg = s_alm[k].red ? TKS_ALMBG : TKS_WARNBG;
    lv_color_t tx = s_alm[k].red ? TKS_ALMTX : TKS_WARNTX;
    s_bn_box = ui_rrect(parent, 208, 8, 552, 56, 12, bg, TKS_BG);
    lv_obj_set_clickable(s_bn_box, true);
    lv_obj_add_event_cb(s_bn_box, banner_ack_cb, LV_EVENT_CLICKED, NULL);
    if (s_alm[k].red) ui_icon_sq20(parent, 226, 26, tx);
    else              ui_icon_tri20(parent, 226, 26, tx, bg);
    ui_lab(parent, 258, 12, &tcs_font_lv24, tx, s_alm[k].msg);

    /* U-6：M207 失联且模块回过错误码时，第二行改成人话原因（禁止裸代码上屏） */
    const char * act = s_alm[k].act;
    char actbuf[64];
#if TCS_USE_LVGL
    extern volatile uint8_t g_m207_last_errcode;
    extern const char * m207_errcode_text(uint8_t code);
    uint8_t ec = g_m207_last_errcode;
    if (s_alm[k].bit == ALM_M207 && ec >= 2 && ec <= 7) {
        snprintf(actbuf, sizeof actbuf, "模块返回：%s", m207_errcode_text(ec));
        act = actbuf;
    }
#endif
    ui_lab(parent, 258, 38, &tcs_font_lv24, tx, act);
    if (extra) {
        char cnt[24];
        snprintf(cnt, sizeof(cnt), "还有%d条", extra);
        int cw = ui_text24_w(cnt) + 16;
        int cx0 = 208 + 552 - cw - 12;
        ui_outline(parent, cx0, 12, cx0 + cw - 1, 28, tx);
        ui_lab(parent, cx0 + 8, 14, &tcs_font_lv24, tx, cnt);
    }
}

void tui_banner_refresh(void)
{
    int extra = 0;
    int k = alarm_pick(g_tcs.alarm_bits, &extra);
    if (k != s_bn_kind) tui_request_rebuild();
}

/* ---------------- 弹窗 ---------------- */
static void mask_click_cb(lv_event_t * e) { LV_UNUSED(e); }

void tui_dlg_close(void)
{
    if (s_mask) { lv_obj_delete(s_mask); s_mask = NULL; }
}

int tui_dlg_active(void) { return s_mask != NULL; }

static void dlg_estop_ok(lv_event_t * e)
{
    LV_UNUSED(e);
    tcs_emergency_stop();
    tui_dlg_close();
    tui_request_rebuild();   /* 延后重建，显示 SAFE 态（红横幅/急停图标） */
}

static void dlg_cancel(lv_event_t * e)
{
    LV_UNUSED(e);
    tui_dlg_close();
}

static void build_dialog(const char * title, const char * body, const char * ok,
                         uint8_t ok_red, lv_event_cb_t ok_cb)
{
    if (s_mask) return;
    s_mask = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(s_mask);
    lv_obj_set_size(s_mask, 1024, 600);
    lv_obj_set_pos(s_mask, 0, 0);
    lv_obj_set_style_bg_opa(s_mask, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(s_mask, mask_click_cb, LV_EVENT_CLICKED, NULL);
    ui_mask_dots(s_mask);

    ui_card(s_mask, 256, 136, 512, 320, TKS_WHITE, TKS_BORDER);   /* G5-Z30：对齐 mock 白底 */
    ui_text32(s_mask, 296, 168, title, ok_red ? TKS_RED : TKS_TX);
    ui_text24(s_mask, 296, 232, body, TKS_TX);

    ui_rrect(s_mask, 296, 344, 192, 88, 8, ok_red ? TKS_RED : TKS_BLUE, TKS_WHITE);
    ui_text24(s_mask, 296 + (192 - ui_text24_w(ok)) / 2, 344 + 32, ok, TKS_WHITE);
    lv_obj_t * ok_btn = lv_obj_create(s_mask);
    lv_obj_remove_style_all(ok_btn);
    lv_obj_set_pos(ok_btn, 296, 344);
    lv_obj_set_size(ok_btn, 192, 88);
    lv_obj_set_clickable(ok_btn, true);
    lv_obj_add_event_cb(ok_btn, ok_cb, LV_EVENT_CLICKED, NULL);

    ui_rrect(s_mask, 536, 344, 192, 88, 8, TKS_WHITE, TKS_WHITE);
    ui_hline(s_mask, 542, 722, 344, TKS_BLUE);
    ui_hline(s_mask, 542, 722, 344 + 87, TKS_BLUE);
    ui_vline(s_mask, 536, 350, 425, TKS_BLUE);
    ui_vline(s_mask, 727, 350, 425, TKS_BLUE);
    ui_text24(s_mask, 536 + (192 - 48) / 2, 344 + 32, "取消", TKS_BLUE);
    lv_obj_t * cancel = lv_obj_create(s_mask);
    lv_obj_remove_style_all(cancel);
    lv_obj_set_pos(cancel, 536, 344);
    lv_obj_set_size(cancel, 192, 88);
    lv_obj_set_clickable(cancel, true);
    lv_obj_add_event_cb(cancel, dlg_cancel, LV_EVENT_CLICKED, NULL);
}

void tui_dlg_estop(void)
{
    build_dialog("确认急停？", "TEC 将全部停止，风扇全开。", "确认急停", 1, dlg_estop_ok);
}

static void dlg_mode_ok(lv_event_t * e)
{
    LV_UNUSED(e);
    param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_TOUCH);
    tui_dlg_close();
    tui_request_rebuild();
}

void tui_dlg_mode(void)
{
    build_dialog("切换到自动模式？", "自动模式按体温闭环控制制冷。", "切换", 0, dlg_mode_ok);
}

/* ---------------- 页管理 ---------------- */
void tui_goto(tui_page_t p)
{
    s_page = p;
    tui_dlg_close();
    lv_obj_clean(s_scr);
    switch (p) {
        case TUI_HOME:  tui_p0_build(s_scr); break;
        case TUI_TUNE:  tui_p1_build(s_scr); break;
        case TUI_CHART: tui_p2_build(s_scr); break;
        case TUI_CONN:  tui_p3_build(s_scr); break;
        case TUI_DIAG:  tui_diag_build(s_scr, s_diag_mode); break;
    }
}

tui_page_t tui_current(void) { return s_page; }

volatile uint32_t g_ui_cmd;

void tui_request(tui_page_t p)   { s_pending = (int)p; }

static void ui_cmd_dispatch(uint32_t cmd)
{
    switch (cmd) {
        case 1: tui_dlg_estop(); break;
        case 2: tui_dlg_mode(); break;
        case 3: tui_request(TUI_TUNE); break;
        case 4: tui_request(TUI_CHART); break;
        case 5: tui_request(TUI_CONN); break;
        case 6: control_manual_fan(g_tcs.fan_on ? 0 : 1); tui_request_rebuild(); break;
        case 7: param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_TOUCH); tui_request_rebuild(); break;
        case 8: tcs_alarm_ack(); tui_request_rebuild(); break;
        case 9: tui_step_param(+1); tui_request_rebuild(); break;
        case 10: tui_step_param(-1); tui_request_rebuild(); break;
        case 11: tui_step_toggle(); tui_request_rebuild(); break;
        case 12: tui_sel_next(); tui_request_rebuild(); break;
        case 13: tui_request(TUI_HOME); break;
        case 15: tui_dlg_close(); break;                  /* 关弹窗（走查用） */
        case 16: tui_dlg_close(); tui_dlg_estop(); break; /* 关旧弹窗→开新弹窗 */
        case 14: tui_slider_center(); tui_request_rebuild(); break;
        /* —— G5 诊断页（VL-12 / TL-18；只走查用）—— */
        case 100: tui_diag_set_mode(0); tui_request(TUI_DIAG); break;   /* VL-12+U-6 */
        case 101: tui_diag_set_mode(1); tui_request(TUI_DIAG); break;   /* TL-18 24px */
        case 102: tui_diag_set_mode(2); tui_request(TUI_DIAG); break;   /* TL-18 32px */
        case 103: tui_diag_set_mode(3); tui_request(TUI_DIAG); break;   /* TL-18 48/96px */
        default: break;
    }
}
void tui_request_rebuild(void)   { s_pending = -1; }

void tui_tick(void)
{
    /* ⓪ 走查命令消费（probe 写入） */
    if (g_ui_cmd) {
        uint32_t c = g_ui_cmd;
        g_ui_cmd = 0;
        ui_cmd_dispatch(c);
    }
    /* ① 延后导航/重建（在 lv_timer_handler 之外的上下文执行——LVGL 安全模式） */
    if (s_pending != -2 && !s_mask) {        /* 模态期间保留请求（M03-F-11：原来被吞掉）*/
        int p = s_pending;
        s_pending = -2;
        if (p < 0) tui_goto(s_page);
        else       tui_goto((tui_page_t)p);
        s_rebuilt = 1;
    }
    /* ② 500ms 数据刷新（仅文本更新；需要重建的页自行 request） */
    uint32_t now = lv_tick_get();
    if (now - s_last_refresh >= 500u) {
        s_last_refresh = now;
        if (s_mask) return;
        switch (s_page) {
            case TUI_HOME:  tui_p0_refresh(); break;
            case TUI_TUNE:  tui_p1_refresh(); break;
            case TUI_CHART: tui_p2_refresh(); break;
            default: break;
        }
    }
}

void tui_init(void)
{
    lv_obj_t * act = lv_screen_active();
    ui_screen_init(act);
    s_scr = lv_obj_create(NULL);
    ui_screen_init(s_scr);
    lv_screen_load(s_scr);
#ifndef TUI_BOOT_PAGE
#define TUI_BOOT_PAGE 0
#endif
    tui_goto((tui_page_t)TUI_BOOT_PAGE);   /* 验证变体：-DTUI_BOOT_PAGE=1/2/3 */
    s_last_refresh = lv_tick_get();
}

void tui_poll(void) { }

int tui_last_was_rebuild(void)          /* 读取并清零（预算埋点归因） */
{
    int v = s_rebuilt;
    s_rebuilt = 0;
    return v;
}
