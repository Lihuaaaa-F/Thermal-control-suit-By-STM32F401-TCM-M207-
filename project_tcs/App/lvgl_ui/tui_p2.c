/* tui_p2.c — 温度曲线页：近 30 分钟两路水温（60 点 × 30s），叠硬切断阈值线
 * 数据源 = control 侧持久环形缓冲（跨页持续采样）。只在来了新格子时重设折线，
 * 当前值与"超过阈值"提示每拍就地更新；主控断开时折线转灰、右端不再写"现在"。
 *
 * 摆放口径：tcs_hist_snapshot 返回"旧→新"的线性序，长度 n，**从下标 0 起填**。
 * 横轴右端是"现在"，所以 n 个样本要贴着右端放（下标 60-n .. 59），左边留空。
 * 2026-11-24 真机：旧实现把"最后 n 个下标"当成有效点，数据没攒满 30 分钟时
 * 画出来的全是数组尾部的 0（整条线趴在 0°C 那条线上）。
 * 链路不在的格子是 NaN（见 tcs_hist_tick）：这一格不画，折线在这里断开。 */
#include "tui.h"
#include "ui_common.h"
#include "control.h"          /* tcs_hist_*：曲线数据源；ALM_OVERTEMP_HARD_C：硬切断阈值 */
#include <stdio.h>
#include <string.h>

#define HIST_N   60
#define CARD_X   32
#define CARD_Y   72
#define CARD_W   960
#define CARD_H   456
#define PLOT_X   72
#define PLOT_Y   116
#define PLOT_W   888
#define PLOT_H   376
#define Y_MAX    60                  /* 纵轴 0..60°C：水温全量程，阈值线在图内 */
#define THRESH_C ((int)ALM_OVERTEMP_HARD_C)   /* 图上那条线就是固件的硬切断阈值，不另立口径 */

static lv_obj_t *s_chart, *s_over, *s_v1, *s_v2, *s_now, *s_v3;
static lv_obj_t *s_body;             /* 图表区容器：有横幅时整体下移（tui_p2_set_banner_room）*/
static lv_chart_series_t *s_ser1, *s_ser2, *s_ser3;   /* ser3 = 体温（m1 模式页，2026-11-26）*/
static float s_h1[HIST_N], s_h2[HIST_N], s_hb[HIST_N];   /* tcs_hist_snapshot(_body) 的线性序（旧→新，前 n 个有效）*/
static uint32_t s_drawn;             /* 上次画上去时的格子总数（来了新格子就变）*/
static uint8_t s_built, s_grayed;    /* grayed：折线当前是否转灰（主控断开）*/

static void clk_back(lv_event_t *e) { LV_UNUSED(e); tui_request(TUI_HOME); }

static int y_of(int v) { return PLOT_Y + (Y_MAX - v) * PLOT_H / Y_MAX; }

/* 右对齐的标签：文字变了才改，并按新宽度重新贴右缘 */
static void right_label(lv_obj_t *l, int right_x, const char *text)
{
    if (strcmp(lv_label_get_text(l), text) == 0) return;
    lv_label_set_text(l, text);
    lv_obj_set_x(l, right_x - ui_text24_w(text));
}

/* 折线上第 i 格（0=30 分钟前，59=现在）对应哪个样本；返回 -1 = 这一格还没有数据 */
static int sample_of(uint32_t n, int i)
{
    if (i < (int)(HIST_N - n)) return -1;
    return i - (int)(HIST_N - n);
}

static void redraw_series(uint32_t n, uint32_t total)
{
    for (int i = 0; i < HIST_N; i++) {
        int k = sample_of(n, i);
        int v1 = LV_CHART_POINT_NONE, v2 = LV_CHART_POINT_NONE;
        if (k >= 0) {
            /* 纵轴从 0 起：负值与 NaN（链路不在的格子）都不画，折线在此断开 */
            if (s_h1[k] >= 0.0f) v1 = (int)(s_h1[k] + 0.5f);
            if (s_h2[k] >= 0.0f) v2 = (int)(s_h2[k] + 0.5f);
        }
        lv_chart_set_value_by_id(s_chart, s_ser1, (uint16_t)i, v1);
        lv_chart_set_value_by_id(s_chart, s_ser2, (uint16_t)i, v2);
        /* m1（模式页）：体温线。探头没接 / 失联的格子是 NaN，同样断开；体温 35–40 落在 0–60 纵轴的上部 */
        if (k >= 0 && s_hb[k] >= 0.0f)
            lv_chart_set_value_by_id(s_chart, s_ser3, (uint16_t)i, (int)(s_hb[k] + 0.5f));
        else
            lv_chart_set_value_by_id(s_chart, s_ser3, (uint16_t)i, LV_CHART_POINT_NONE);
    }
    lv_chart_refresh(s_chart);
    s_drawn = total;
}

/* 图例：圆点 + 名称 + 当前值（值每拍就地改）*/
static void legend(lv_obj_t *scr, int x, lv_color_t dot, const char *name, lv_obj_t **val)
{
    ui_box(scr, x, 94, 10, 10, dot, 5);
    ui_text24(scr, x + 20, 86, name, TKS_TX);
    *val = ui_aux20(scr, x + 20 + ui_text24_w(name) + 8, 90, "--.-", TKS_TX2);
}

/* 顶栏（返回 + 标题）直接画在屏幕上不动；图表区画在容器 s_body 里，有横幅时容器整体下移。
 * 坐标一律按屏幕绝对位置写（容器在 (0,0)）。*/
void tui_p2_build(lv_obj_t *scr)
{
    s_built = 0;
    s_grayed = 0xFF;
    ui_button(scr, 24, 8, 120, 40, "‹ 返回", UI_BTN_GHOST, &tcs_font_lv24, clk_back, NULL);
    ui_text32(scr, 176, 16, "温控曲线", TKS_TX);
    ui_text24(scr, 320, 22, "近 30 分钟", TKS_TX2);

    s_body = ui_box(scr, 0, 0, 1024, 600, TKS_BG, 0);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_TRANSP, 0);
    lv_obj_t *body = s_body;
    ui_card(body, CARD_X, CARD_Y, CARD_W, CARD_H, TKS_CARD, TKS_BORDER);
    legend(body, PLOT_X, TKS_BLUE, "贴身水箱", &s_v1);
    legend(body, PLOT_X + 300, TKS_TX2, "环境水箱", &s_v2);
    legend(body, PLOT_X + 600, TKS_YELLOW, "体温", &s_v3);   /* m1（模式页）：体温卡点进来也有曲线 */

    /* 纵轴刻度：0 / 15 / 30 / 45 / 60 */
    for (int v = 0; v <= Y_MAX; v += 15) {
        char t[8];
        snprintf(t, sizeof t, "%d", v);
        ui_aux20(body, PLOT_X - 12 - ui_aux20_w(t), y_of(v) - 14, t, TKS_TX2);
    }
    /* 硬切断阈值：红色虚线 + 标注 */
    {
        static lv_point_precise_t th[2];
        char lab[24];
        snprintf(lab, sizeof lab, "%d°C 阈值", THRESH_C);
        th[0].x = PLOT_X; th[0].y = y_of(THRESH_C);
        th[1].x = PLOT_X + PLOT_W; th[1].y = th[0].y;
        lv_obj_t *line = lv_line_create(body);
        lv_line_set_points(line, th, 2);
        lv_obj_set_style_line_color(line, TKS_RED, 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_dash_width(line, 6, 0);
        lv_obj_set_style_line_dash_gap(line, 6, 0);
        lv_obj_set_clickable(line, false);
        ui_text24(body, PLOT_X + PLOT_W - ui_text24_w(lab), y_of(THRESH_C) - 30, lab, TKS_RED);
    }

    s_chart = lv_chart_create(body);
    lv_obj_set_pos(s_chart, PLOT_X, PLOT_Y);
    lv_obj_set_size(s_chart, PLOT_W, PLOT_H);
    lv_chart_set_type(s_chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(s_chart, HIST_N);
    lv_chart_set_range(s_chart, LV_CHART_AXIS_PRIMARY_Y, 0, Y_MAX);
    lv_chart_set_div_line_count(s_chart, Y_MAX / 15 - 1, 0);
    lv_obj_set_style_bg_opa(s_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_chart, 0, 0);
    lv_obj_set_style_pad_all(s_chart, 0, 0);
    lv_obj_set_style_line_width(s_chart, 2, LV_PART_ITEMS);
    lv_obj_set_style_size(s_chart, 0, 0, LV_PART_INDICATOR);
    lv_obj_set_clickable(s_chart, false);
    s_ser1 = lv_chart_add_series(s_chart, TKS_BLUE, LV_CHART_AXIS_PRIMARY_Y);
    s_ser2 = lv_chart_add_series(s_chart, TKS_TX2, LV_CHART_AXIS_PRIMARY_Y);
    s_ser3 = lv_chart_add_series(s_chart, TKS_YELLOW, LV_CHART_AXIS_PRIMARY_Y);   /* m1：体温 */

    ui_text24(body, PLOT_X, PLOT_Y + PLOT_H + 4, "30 分钟前", TKS_TX2);
    s_now = ui_text24(body, PLOT_X + PLOT_W - ui_text24_w("现在"), PLOT_Y + PLOT_H + 4, "现在", TKS_TX2);

    /* "超过阈值"：贴在阈值线下方靠右，平时隐藏 */
    {
        char over[24];
        snprintf(over, sizeof over, "超过 %d°C", THRESH_C);
        s_over = ui_text24(body, PLOT_X + PLOT_W - ui_text24_w(over), y_of(THRESH_C) + 6, over, TKS_RED);
    }
    lv_obj_set_hidden(s_over, true);

    s_drawn = 0xFFFFFFFFu;
    s_built = 1;
    (void)tui_p2_update();
}

int tui_p2_update(void)
{
    if (!s_built) return 1;
    uint32_t n, total = tcs_hist_count(), nb = 0;
    tcs_hist_snapshot(s_h1, s_h2, &n);
    (void)tcs_hist_snapshot_body(s_hb, &nb);      /* m1（模式页）：与两路水温同一套格子，total 一起推进 */
    /* 折线：格子总数变了才重设（原来每 500ms 全量重设 120 个点并整图刷新）。
     * 不能比"最后一个样本的数值"：水温稳定时数值不变，新点要等它变了才出现。*/
    if (total != s_drawn) redraw_series(n, total);

    /* 图例里的当前值 + 越界提示：每拍就地更新 */
    const ui_in_t *in;
    const ui_link_view_t *lv;
    tui_input(&in, &lv);
    char b[24];
    int ok = (lv->module == UI_SRC_OK);
    if (ok) {
        snprintf(b, sizeof b, "%.1f", (double)in->tc1_act);
        ui_lab_set(s_v1, b);
        snprintf(b, sizeof b, "%.1f", (double)in->tc2_act);
        ui_lab_set(s_v2, b);
    } else {
        ui_lab_set(s_v1, "--.-");
        ui_lab_set(s_v2, "--.-");
    }
    /* 体温当前值（m1）：探头没接 / 失联 → --.- */
    if (lv->body == UI_SRC_OK) {
        snprintf(b, sizeof b, "%.1f", (double)in->body);
        ui_lab_set(s_v3, b);
    } else {
        ui_lab_set(s_v3, "--.-");
    }
    lv_obj_set_hidden(s_over, !(ok && (in->tc1_act >= THRESH_C || in->tc2_act >= THRESH_C)));

    /* 主控断开：折线转灰；右端不再写"现在"，改说数据停在多久前（不把旧数据贴在"现在"上）*/
    if (lv->stale != s_grayed) {
        s_grayed = lv->stale;
        lv_chart_set_series_color(s_chart, s_ser1, s_grayed ? TKS_GRAY : TKS_BLUE);
        lv_chart_set_series_color(s_chart, s_ser2, s_grayed ? TKS_GRAY : TKS_TX2);
        lv_chart_set_series_color(s_chart, s_ser3, s_grayed ? TKS_GRAY : TKS_YELLOW);   /* m1：体温线 */
    }
    if (lv->stale) {
        uint32_t s = lv->down_ms / 1000u;
        if (s < 60u) snprintf(b, sizeof b, "%us 前", (unsigned)s);
        else         snprintf(b, sizeof b, "%u 分钟前", (unsigned)(s / 60u));
    } else {
        snprintf(b, sizeof b, "现在");
    }
    right_label(s_now, PLOT_X + PLOT_W, b);
    return 0;
}

/* 二级页的横幅占 y 64..120：图表区整体下移 72px 让开（卡片到 144..600，正好放得下）。
 * 只动容器，不重建任何对象。*/
void tui_p2_set_banner_room(int room)
{
    if (s_body) lv_obj_set_y(s_body, room ? 72 : 0);
}
