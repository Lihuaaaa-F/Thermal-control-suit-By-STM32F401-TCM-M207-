/* tui_p2.c — G5 温控曲线页（UI 规范 v1.2 §A；30min/60 点/30s 采样；阈值 45°C；越界红字）
 * 数据源（阶段-1）：页面内环形缓冲，30s 采样（进入页面后累积）；control 侧持久环形缓冲=G5 后续项。 */
#include "tui.h"
#include "ui_common.h"
#include "control.h"
#include <stdio.h>
#include <string.h>

#define HIST_N 60
static float s_h1[HIST_N], s_h2[HIST_N];
static int s_head, s_fill_n;
static lv_obj_t * s_chart, * s_ser1, * s_ser2, * s_over_txt;
static uint32_t s_last_sample;
static int s_started;

static void clk_back(lv_event_t * e) { LV_UNUSED(e); tui_goto(TUI_HOME); }

/* 数据源=control 侧 60 点环形（G5 阶段-2：跨页持久采样） */
static void push_sample(void)
{
    tcs_hist_snapshot(s_h1, s_h2, &s_fill_n);
    LV_UNUSED(s_head);
}

static void redraw_series(void)
{
    if (!s_chart) return;
    /* 环形 → 线序（旧→新） */
    for (int i = 0; i < HIST_N; i++) {
        int idx = (s_head - s_fill_n + i + HIST_N) % HIST_N;
        int v1 = (i >= HIST_N - s_fill_n) ? (int)(s_h1[idx] + 0.5f) : LV_CHART_POINT_NONE;
        int v2 = (i >= HIST_N - s_fill_n) ? (int)(s_h2[idx] + 0.5f) : LV_CHART_POINT_NONE;
        lv_chart_set_value_by_id(s_chart, s_ser1, (uint16_t)i, v1);
        lv_chart_set_value_by_id(s_chart, s_ser2, (uint16_t)i, v2);
    }
    lv_chart_refresh(s_chart);
    /* 越界红字（UI 规范 v1.2 §A：值 >45 顶格+红字标注） */
    int over = (g_tcs.m207_online && (g_tcs.tc1_act > 45.0f || g_tcs.tc2_act > 45.0f));
    if (s_over_txt) {
        if (over) lv_obj_clear_flag(s_over_txt, LV_OBJ_FLAG_HIDDEN);
        else      lv_obj_add_flag(s_over_txt, LV_OBJ_FLAG_HIDDEN);
    }
}

void tui_p2_build(lv_obj_t * scr)
{
    ui_btn(scr, 24, 8, 120, 40, "‹返回", 5, 24);
    lv_obj_t * z = lv_obj_create(scr);
    lv_obj_remove_style_all(z);
    lv_obj_set_pos(z, 24, 0);
    lv_obj_set_size(z, 120, 56);
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z, clk_back, LV_EVENT_CLICKED, NULL);

    ui_text32(scr, 176, 16, "温控曲线 · 近30min", TKS_TX);
    ui_dot8(scr, 700, 34, TKS_TX);
    ui_text24(scr, 712, 22, "贴身水箱", TKS_TX2);
    ui_dot8(scr, 830, 34, TKS_TX2);
    ui_text24(scr, 842, 22, "环境水箱", TKS_TX2);
    ui_vline(scr, 960, 20, 48, TKS_RED);
    ui_text24(scr, 968, 22, "阈值", TKS_RED);

    ui_card(scr, 32, 72, 960, 456, TKS_CARD, TKS_BORDER);

    s_chart = lv_chart_create(scr);
    lv_obj_set_pos(s_chart, 56, 88);
    lv_obj_set_size(s_chart, 896, 408);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, HIST_N);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, -10, 50);
    lv_chart_set_div_line_count(s_chart, 6, 6);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 0, 0);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);
    s_ser1 = lv_chart_add_series(s_chart, TKS_TX, LV_CHART_AXIS_PRIMARY_Y);
    s_ser2 = lv_chart_add_series(s_chart, TKS_TX2, LV_CHART_AXIS_PRIMARY_Y);

    ui_aux20(scr, 40, 84, "50", TKS_TX2);
    ui_aux20(scr, 34, 470, "-10", TKS_TX2);
    ui_text24(scr, 40, 496, "30min前", TKS_TX2);
    ui_text24(scr, 896, 496, "现在", TKS_TX2);

    static lv_point_precise_t th_pts[2];
    th_pts[0].x = 56; th_pts[0].y = 88 + (int)((50 - 45) * 408.0 / 60.0);
    th_pts[1].x = 952; th_pts[1].y = th_pts[0].y;
    lv_obj_t * th = lv_line_create(scr);
    lv_line_set_points(th, th_pts, 2);
    lv_obj_set_style_line_color(th, TKS_RED, 0);
    lv_obj_set_style_line_width(th, 2, 0);
    lv_obj_set_style_line_dash_gap(th, 6, 0);
    lv_obj_set_style_line_dash_width(th, 6, 0);

    s_over_txt = lv_label_create(scr);
    lv_obj_remove_style_all(s_over_txt);
    lv_obj_set_style_text_font(s_over_txt, &tcs_font_lv24, 0);
    lv_obj_set_style_text_color(s_over_txt, TKS_RED, 0);
    ui_lab_set(s_over_txt, "越界");
    lv_obj_set_pos(s_over_txt, 64, 96);
    lv_obj_add_flag(s_over_txt, LV_OBJ_FLAG_HIDDEN);

    if (!s_started) { s_started = 1; push_sample(); }
    redraw_series();
}

void tui_p2_refresh(void)
{
    uint32_t now = lv_tick_get();
    if (s_fill_n == 0 || now - s_last_sample >= 5000u) {   /* 页面 5s 拉取（control 侧 30s 采样不变） */
        s_last_sample = now;
        push_sample();
    }
    redraw_series();   /* 越界红字随数据即时更新 */
}
