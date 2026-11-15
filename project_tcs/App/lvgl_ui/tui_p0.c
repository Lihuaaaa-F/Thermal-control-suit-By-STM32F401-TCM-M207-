/* tui_p0.c — G5 实时主页（数据绑定 g_tcs/param；点卡进曲线页，横幅点击确认恢复） */
#include "tui.h"
#include "ui_common.h"
#include "control.h"
#include <stdio.h>

static lv_obj_t * s_v1, * s_v2, * s_set1, * s_set2, * s_body;
static uint8_t s_st_mode, s_st_fan, s_st_tec1, s_st_tec2, s_st_estop, s_st_alm;

static void clk_tune(lv_event_t * e)  { LV_UNUSED(e); tui_request(TUI_TUNE); }
static void clk_chart(lv_event_t * e) { LV_UNUSED(e); tui_request(TUI_CHART); }
static void clk_conn(lv_event_t * e)  { LV_UNUSED(e); tui_request(TUI_CONN); }
static void clk_estop(lv_event_t * e) { LV_UNUSED(e); tui_dlg_estop(); }
static void clk_fan(lv_event_t * e)
{
    LV_UNUSED(e);
    control_manual_fan(g_tcs.fan_on ? 0 : 1);
    tui_request_rebuild();
}
static void clk_mode(lv_event_t * e)
{
    LV_UNUSED(e);
    if (g_tcs.mode) param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_TOUCH);   /* 切手动=即时 */
    else            tui_dlg_mode();
    tui_request_rebuild();
}

static lv_obj_t * click_zone(lv_obj_t * p, int x, int y, int w, int h, lv_event_cb_t cb)
{
    lv_obj_t * z = lv_obj_create(p);
    lv_obj_remove_style_all(z);
    lv_obj_set_pos(z, x, y);
    lv_obj_set_size(z, w, h);
    lv_obj_set_clickable(z, true);
    lv_obj_add_event_cb(z, cb, LV_EVENT_CLICKED, NULL);
    return z;
}

static lv_obj_t * value_label(lv_obj_t * p, int x, int y, lv_color_t c,
                              const lv_font_t * f, const char * txt)
{
    lv_obj_t * l = lv_label_create(p);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    /* G5-Z25：数值 label 不透明化（bg=卡底）——变值时 label 连背景一起重画，
     * 消除双写+PARTIAL 组合下的旧帧残影/黑带（矩阵实验实锤） */
    lv_obj_set_style_bg_color(l, TKS_CARD, 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    ui_lab_set(l, txt);
    lv_obj_set_pos(l, x, y);
    return l;
}

static void tc_card(lv_obj_t * p, int cx, const char * title, uint8_t tec_on,
                    float act, int lost, lv_obj_t ** val_out)
{
    ui_card(p, cx, 88, 460, 240, TKS_CARD, TKS_BORDER);
    ui_text32(p, cx + 24, 108, title, TKS_TX);
    ui_rrect(p, cx + 348, 112, 88, 40, 20, tec_on ? TKS_GREEN : TKS_GRAY, TKS_CARD);
    ui_text24(p, cx + 356, 120, tec_on ? "●运行" : "○停止", TKS_WHITE);

    char big[16];
    if (lost) snprintf(big, sizeof(big), "--.-");
    else      snprintf(big, sizeof(big), "%.1f", (double)act);
    *val_out = value_label(p, cx + 24, 160, lost ? TKS_GRAY : TKS_HERO, &tcs_font_lv96, big);
    lv_point_t sz;
    lv_text_get_size(&sz, big, &tcs_font_lv96, 0, 0, 1000, LV_TEXT_FLAG_NONE);
    ui_text32(p, cx + 24 + (int)sz.x + 8, 224, "°C", TKS_TX2);
    ui_dot8(p, cx + 423, 173, lost ? TKS_GRAY : TKS_GREEN);

    ui_text24(p, cx + 24, 272, "设定", TKS_TX2);
    lv_obj_t * sp = value_label(p, cx + 24 + 56, 268, TKS_TX, &tcs_font_lv32, "00.0");
    if (cx == 32) s_set1 = sp; else s_set2 = sp;
    lv_point_t sv;
    lv_text_get_size(&sv, "00.0", &tcs_font_lv32, 0, 0, 1000, LV_TEXT_FLAG_NONE);
    ui_text32(p, cx + 24 + 56 + (int)sv.x + 4, 268, "°C", TKS_TX2);

    click_zone(p, cx, 88, 460, 240, clk_chart);   /* P2 入口：点 hero 卡看 30min 曲线 */
}

void tui_p0_build(lv_obj_t * scr)
{
    s_st_mode = (uint8_t)(g_tcs.mode != 0);
    s_st_fan = g_tcs.fan_on;
    s_st_tec1 = g_tcs.tec1_on;
    s_st_tec2 = g_tcs.tec2_on;
    s_st_estop = (uint8_t)((g_tcs.alarm_bits & ALM_ESTOP) ? 1 : 0);
    s_st_alm = (uint8_t)(g_tcs.alarm_bits != 0);

    /* 顶栏 */
    ui_rrect(scr, 24, 16, 160, 40, 20, TKS_CARD, TKS_BG);
    ui_hline(scr, 30, 178, 16, TKS_BORDER);
    ui_hline(scr, 30, 178, 55, TKS_BORDER);
    ui_vline(scr, 24, 22, 50, TKS_BORDER);
    ui_vline(scr, 183, 22, 50, TKS_BORDER);
    ui_dot8(scr, 76, 36, g_tcs.mode ? TKS_GREEN : TKS_GRAY);
    ui_text24(scr, 88, 24, g_tcs.mode ? "自动" : "手动", TKS_TX);

    tui_banner_build(scr);
    if (!g_tcs.alarm_bits) {
        ui_dot8(scr, 780, 31, g_tcs.m207_online ? TKS_GREEN : TKS_GRAY);
        ui_aux20(scr, 796, 18, "M207", TKS_TX2);
        ui_dot8(scr, 863, 31, g_tcs.esp_link_ok ? TKS_GREEN : TKS_GRAY);
        ui_aux20(scr, 876, 18, "ESP", TKS_TX2);
    }
    ui_icon_estop24(scr, 940, 20, TKS_GRAY, (g_tcs.alarm_bits & ALM_ESTOP) != 0);

    int m207_lost = !g_tcs.m207_online;
    tc_card(scr, 32, "贴身水箱", g_tcs.tec1_on, g_tcs.tc1_act, m207_lost, &s_v1);
    tc_card(scr, 532, "环境水箱", g_tcs.tec2_on, g_tcs.tc2_act, m207_lost, &s_v2);

    /* 体温卡 */
    int body_lost = !g_tcs.body_sensor_ok;
    ui_card(scr, 32, 352, 460, 96, TKS_CARD, body_lost ? TKS_YELLOW : TKS_BORDER);
    ui_text24(scr, 56, 364, "体温", TKS_TX);
    char bb[16];
    if (body_lost) snprintf(bb, sizeof(bb), "--.-");
    else           snprintf(bb, sizeof(bb), "%.1f", (double)g_tcs.body_temp);
    s_body = value_label(scr, 152, 372, body_lost ? TKS_GRAY : TKS_HERO, &tcs_font_lv48, bb);
    lv_point_t bz;
    lv_text_get_size(&bz, bb, &tcs_font_lv48, 0, 0, 1000, LV_TEXT_FLAG_NONE);
    ui_aux20(scr, 152 + (int)bz.x + 6, 400, "°C", TKS_TX2);
    ui_dot8(scr, 333, 397, body_lost ? TKS_GRAY : TKS_GREEN);
    if (body_lost) {
        ui_rrect(scr, 380, 380, 88, 40, 20, TKS_WARNBG, TKS_CARD);
        ui_icon_tri20(scr, 392, 388, TKS_WARNTX, TKS_WARNBG);
        ui_text24(scr, 416, 388, "失联", TKS_WARNTX);
    }

    /* 风扇卡 */
    ui_card(scr, 532, 352, 460, 96, TKS_CARD, TKS_BORDER);
    ui_text24(scr, 556, 364, "风扇", TKS_TX);
    ui_text24(scr, 556, 402, g_tcs.fan_on ? "运行中" : "已停止", TKS_TX2);
    ui_rrect(scr, 840, 368, 128, 64, 32, g_tcs.fan_on ? TKS_GREEN : TKS_GRAY, TKS_CARD);
    int fb = 840 + (g_tcs.fan_on ? 64 : 0);
    ui_rrect(scr, fb, 368, 64, 64, 32, TKS_WHITE, g_tcs.fan_on ? TKS_GREEN : TKS_GRAY);
    ui_text24(scr, fb + 20, 388, g_tcs.fan_on ? "开" : "关", TKS_TX);
    click_zone(scr, 532, 352, 460, 96, clk_fan);

    /* 底栏 */
    ui_btn(scr, 40, 472, 208, 96, "调参", 0, 32);
    ui_btn(scr, 272, 472, 208, 96, g_tcs.mode ? "切手动" : "切自动", 0, 32);
    ui_btn(scr, 504, 472, 208, 96, "连接", 0, 32);
    ui_btn(scr, 768, 472, 216, 112, "急停", 2, 32);
    click_zone(scr, 40, 472, 208, 96, clk_tune);
    click_zone(scr, 272, 472, 208, 96, clk_mode);
    click_zone(scr, 504, 472, 208, 96, clk_conn);
    click_zone(scr, 768, 472, 216, 112, clk_estop);
}

void tui_p0_refresh(void)
{
    /* 状态字段变化 → 整页重建（低频）；仅数值变化 → 原地改文本（无整页重绘） */
    if (s_st_mode != (uint8_t)(g_tcs.mode != 0) || s_st_fan != g_tcs.fan_on ||
        s_st_tec1 != g_tcs.tec1_on || s_st_tec2 != g_tcs.tec2_on ||
        s_st_estop != (uint8_t)((g_tcs.alarm_bits & ALM_ESTOP) ? 1 : 0) ||
        s_st_alm != (uint8_t)(g_tcs.alarm_bits != 0)) {
        tui_request_rebuild();
        return;
    }
    char b[16];
    if (s_v1) {
        if (!g_tcs.m207_online) snprintf(b, sizeof(b), "--.-");
        else                    snprintf(b, sizeof(b), "%.1f", (double)g_tcs.tc1_act);
        ui_lab_set(s_v1, b);
    }
    if (s_v2) {
        if (!g_tcs.m207_online) snprintf(b, sizeof(b), "--.-");
        else                    snprintf(b, sizeof(b), "%.1f", (double)g_tcs.tc2_act);
        ui_lab_set(s_v2, b);
    }
    if (s_set1) { snprintf(b, sizeof(b), "%.1f", (double)param_get(PARAM_ID_TC1_SET)); ui_lab_set(s_set1, b); }
    if (s_set2) { snprintf(b, sizeof(b), "%.1f", (double)param_get(PARAM_ID_TC2_SET)); ui_lab_set(s_set2, b); }
    if (s_body) {
        if (!g_tcs.body_sensor_ok) snprintf(b, sizeof(b), "--.-");
        else                       snprintf(b, sizeof(b), "%.1f", (double)g_tcs.body_temp);
        ui_lab_set(s_body, b);
    }
}
