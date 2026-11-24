/* tui_p1.c — G5 实时调参页（4 参 SCR1：TC1_SET/TC2_SET/BODY_HOT/BODY_COLD；编辑即生效） */
#include "tui.h"
#include "ui_common.h"
#include "ui_page_state.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#define ROW_X 32
#define ROW_W 960
#define ROW_H 88
#define ROW_Y0 56
#define ROW_GAP 16
#define TRACK_X (ROW_X + 320)
#define TRACK_W 432

static const uint16_t s_ids[4] = {
    PARAM_ID_TC1_SET, PARAM_ID_TC2_SET, PARAM_ID_BODY_HOT, PARAM_ID_BODY_COLD
};
static int s_sel;
static float s_step = 0.1f;
/* 建页时的状态快照。2026-11-24 全页面排查：本页的数值文字、单位位置、滑条填充与拇指位置
 * 都取自参数值/选中行/步进，但原来的刷新只改数值文字——外部（网页/镜像）改参后文字变了、
 * 滑条不动；数值位数变化（5.0→40.1）后单位压到末位数字上。现在任何一项变了就整页重建。*/
static ui_p1_state_t s_st1;

static ui_p1_state_t p1_state_now(void)
{
    ui_p1_state_t s;
    s.sel  = (uint8_t)s_sel;
    s.step = s_step;
    for (int i = 0; i < 4; i++) s.val[i] = param_get(s_ids[i]);
    return s;
}

static int row_y(int i) { return ROW_Y0 + i * (ROW_H + ROW_GAP); }

static void apply_param(uint16_t id, float v)
{
    const param_desc_t * p = param_find(id);
    if (!p) return;
    float step = (p->step > 0.0f) ? p->step : 0.1f;
    v = p->min + roundf((v - p->min) / step) * step;
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    (void)param_set(id, v, PARAM_SRC_TOUCH);
}

static void set_from_track(int i, int px)
{
    const param_desc_t * p = param_find(s_ids[i]);
    if (!p) return;
    float frac = (float)(px - (TRACK_X + 40)) / (float)(TRACK_W - 80);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    apply_param(s_ids[i], p->min + frac * (p->max - p->min));
    tui_request_rebuild();
}

static void clk_row(lv_event_t * e) { s_sel = (int)(intptr_t)lv_event_get_user_data(e); tui_request_rebuild(); }
static void clk_back(lv_event_t * e) { LV_UNUSED(e); tui_request(TUI_HOME); }
static void clk_step(lv_event_t * e)
{
    LV_UNUSED(e);
    s_step = (s_step < 0.5f) ? 1.0f : 0.1f;
    tui_request_rebuild();
}
static void clk_key(lv_event_t * e)
{
    intptr_t d = (intptr_t)lv_event_get_user_data(e);
    const param_desc_t * p = param_find(s_ids[s_sel]);
    if (p) apply_param(s_ids[s_sel], p->value + (float)d * s_step);
    tui_request_rebuild();
}
static void clk_track(lv_event_t * e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    lv_indev_t * in = lv_indev_active();
    if (!in) return;
    lv_point_t pt;
    lv_indev_get_point(in, &pt);
    set_from_track(i, (int)pt.x);
}

static lv_obj_t * click_zone(lv_obj_t * p, int x, int y, int w, int h,
                             lv_event_cb_t cb, void * ud)
{
    lv_obj_t * z = lv_obj_create(p);
    lv_obj_remove_style_all(z);
    lv_obj_set_pos(z, x, y);
    lv_obj_set_size(z, w, h);
    lv_obj_set_clickable(z, true);
    lv_obj_add_event_cb(z, cb, LV_EVENT_CLICKED, ud);
    return z;
}

static void build_row(lv_obj_t * scr, int i)
{
    int y = row_y(i);
    int sel = (i == s_sel);
    const param_desc_t * p = param_find(s_ids[i]);
    if (!p) return;
    lv_color_t bg = sel ? TKS_PRESS : TKS_CARD;
    lv_color_t bd = sel ? TKS_BLUE : TKS_BORDER;
    ui_card(scr, ROW_X, y, ROW_W, ROW_H, bg, bd);
    if (sel) ui_box(scr, ROW_X + 1, y + 1, 4, ROW_H - 5, TKS_BLUE, 0);

    ui_text24(scr, ROW_X + 24, y + 12, p->name, TKS_TX);
    char vs[24];
    snprintf(vs, sizeof(vs), "%.1f", (double)p->value);
    lv_obj_t * vl = lv_label_create(scr);
    lv_obj_remove_style_all(vl);
    lv_obj_set_style_text_font(vl, &tcs_font_lv32, 0);
    lv_obj_set_style_text_color(vl, TKS_HERO, 0);
    lv_obj_set_style_bg_color(vl, bg, 0);          /* G5-Z25：不透明化 */
    lv_obj_set_style_bg_opa(vl, LV_OPA_COVER, 0);
    ui_lab_set(vl, vs);
    lv_obj_set_pos(vl, ROW_X + 24, y + 44);
    if (p->unit[0]) ui_aux20(scr, ROW_X + 24 + 32 * (int)strlen(vs) + 4, y + 52, p->unit, TKS_TX2);

    /* 滑条（方形轨道；拇指 80 圆硬边） */
    ui_box(scr, TRACK_X, y + 36, TRACK_W, 16, TKS_TRACK, 0);
    float frac = (p->max > p->min) ? (p->value - p->min) / (p->max - p->min) : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int fw = (int)(frac * (float)(TRACK_W - 1)) + 1;
    if (fw > 4) ui_box(scr, TRACK_X, y + 36, fw, 16, TKS_TRACKFILL, 0);
    int bx = TRACK_X + 40 + (int)(frac * (float)(TRACK_W - 80)) - 40;
    ui_rrect(scr, bx, y + 4, 80, 80, 40, sel ? TKS_BLUE : TKS_GRAY, bg);

    /* ± 迷你键（命中=绘制 64×64 @y+12，G5 清账④口径） */
    ui_btn(scr, ROW_X + 792, y + 12, 64, 64, "-", sel ? 0 : 1, 32);
    ui_btn(scr, ROW_X + 872, y + 12, 64, 64, "+", sel ? 0 : 1, 32);

    click_zone(scr, ROW_X, y, ROW_W, ROW_H, clk_row, (void *)(intptr_t)i);
    click_zone(scr, TRACK_X, y + 20, TRACK_W, 48, clk_track, (void *)(intptr_t)i);
    click_zone(scr, ROW_X + 792, y + 12, 64, 64, clk_key, (void *)(intptr_t)-1);
    click_zone(scr, ROW_X + 872, y + 12, 64, 64, clk_key, (void *)(intptr_t)1);
}

void tui_p1_build(lv_obj_t * scr)
{
    s_st1 = p1_state_now();
    ui_btn(scr, 24, 8, 120, 40, "‹返回", 5, 24);
    click_zone(scr, 24, 0, 120, 56, clk_back, NULL);   /* 命中区扩整列（规范 #6） */

    const param_desc_t * p = param_find(s_ids[s_sel]);
    char ctx[96];
    if (p) snprintf(ctx, sizeof(ctx), "选中：%s · 范围 %.1f~%.1f°C", p->name,
                    (double)p->min, (double)p->max);
    else   snprintf(ctx, sizeof(ctx), "调参");
    ui_text24(scr, 176, 22, ctx, TKS_TX2);

    for (int i = 0; i < 4; i++) build_row(scr, i);

    ui_btn(scr, 32, 472, 96, 96, "-", 0, 32);
    ui_btn(scr, 896, 472, 96, 96, "+", 0, 32);
    char sl[24];
    snprintf(sl, sizeof(sl), "步进%.1f", (double)s_step);
    ui_btn(scr, 160, 488, 180, 64, sl, 0, 24);
    click_zone(scr, 32, 472, 96, 96, clk_key, (void *)(intptr_t)-1);
    click_zone(scr, 896, 472, 96, 96, clk_key, (void *)(intptr_t)1);
    click_zone(scr, 160, 488, 180, 64, clk_step, NULL);
}

void tui_step_param(int dir)
{
    const param_desc_t * p = param_find(s_ids[s_sel]);
    if (!p) return;
    float step = (p->step > 0.0f) ? p->step : 0.1f;
    if (step < s_step) step = s_step;
    float v = p->value + (float)dir * step;
    v = p->min + roundf((v - p->min) / step) * step;
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    param_set(s_ids[s_sel], v, PARAM_SRC_TOUCH);
}
void tui_step_toggle(void) { s_step = (s_step < 0.5f) ? 1.0f : 0.1f; }
void tui_sel_next(void) { s_sel = (s_sel + 1) % 4; }
void tui_slider_center(void)
{
    const param_desc_t * p = param_find(s_ids[s_sel]);
    if (!p) return;
    apply_param(s_ids[s_sel], p->min + (p->max - p->min) * 0.5f);
}

void tui_p1_refresh(void)
{
    /* 选中行 / 步进 / 任一参数值与建页时不同 → 整页重建（单位位置与滑条一起重画）。
     * 判定在 ui_page_state.c（纯函数，单测 tests/test_site_logic.c）。*/
    ui_p1_state_t now = p1_state_now();
    if (ui_p1_state_changed(&s_st1, &now)) tui_request_rebuild();
}
