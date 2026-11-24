/* tui_p2.c — 温度曲线页：近 30 分钟两路水温（60 点 × 30s），叠 45°C 阈值线
 * 数据源 = control 侧持久环形缓冲（跨页持续采样）。只在来了新样本时重设折线，
 * 当前值与"超过 45°C"提示每拍就地更新。
 *
 * 摆放口径：tcs_hist_snapshot 返回"旧→新"的线性序，长度 n，**从下标 0 起填**。
 * 横轴右端是"现在"，所以 n 个样本要贴着右端放（下标 60-n .. 59），左边留空。
 * 2026-11-24 真机：旧实现把"最后 n 个下标"当成有效点，数据没攒满 30 分钟时
 * 画出来的全是数组尾部的 0（整条线趴在 0°C 那条线上）。 */
#include "tui.h"
#include "ui_common.h"
#include "control.h"          /* tcs_hist_snapshot：曲线数据源 */
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
#define THRESH_C 45

static lv_obj_t *s_chart, *s_over, *s_v1, *s_v2;
static lv_chart_series_t *s_ser1, *s_ser2;
static float s_h1[HIST_N], s_h2[HIST_N];   /* tcs_hist_snapshot 的线性序（旧→新，前 n 个有效）*/
static uint32_t s_drawn;             /* 上次画上去的样本数 */
static float s_last1, s_last2;       /* …以及最后一个样本（数量没变时也可能被改写）*/
static uint8_t s_built;

static void clk_back(lv_event_t *e) { LV_UNUSED(e); tui_request(TUI_HOME); }

static int y_of(int v) { return PLOT_Y + (Y_MAX - v) * PLOT_H / Y_MAX; }

/* 折线上第 i 格（0=30 分钟前，59=现在）对应哪个样本；返回 -1 = 这一格还没有数据 */
static int sample_of(uint32_t n, int i)
{
    if (i < (int)(HIST_N - n)) return -1;
    return i - (int)(HIST_N - n);
}

static void redraw_series(uint32_t n)
{
    for (int i = 0; i < HIST_N; i++) {
        int k = sample_of(n, i);
        int v1 = LV_CHART_POINT_NONE, v2 = LV_CHART_POINT_NONE;
        if (k >= 0) {
            if (s_h1[k] >= 0.0f) v1 = (int)(s_h1[k] + 0.5f);   /* 纵轴从 0 起：负值不画 */
            if (s_h2[k] >= 0.0f) v2 = (int)(s_h2[k] + 0.5f);
        }
        lv_chart_set_value_by_id(s_chart, s_ser1, (uint16_t)i, v1);
        lv_chart_set_value_by_id(s_chart, s_ser2, (uint16_t)i, v2);
    }
    lv_chart_refresh(s_chart);
    s_drawn = n;
    s_last1 = n ? s_h1[n - 1] : 0.0f;
    s_last2 = n ? s_h2[n - 1] : 0.0f;
}

/* 图例：圆点 + 名称 + 当前值（值每拍就地改）*/
static void legend(lv_obj_t *scr, int x, lv_color_t dot, const char *name, lv_obj_t **val)
{
    ui_box(scr, x, 94, 10, 10, dot, 5);
    ui_text24(scr, x + 20, 86, name, TKS_TX);
    *val = ui_aux20(scr, x + 20 + ui_text24_w(name) + 8, 90, "--.-", TKS_TX2);
}

void tui_p2_build(lv_obj_t *scr)
{
    s_built = 0;
    ui_button(scr, 24, 8, 120, 40, "‹ 返回", UI_BTN_GHOST, &tcs_font_lv24, clk_back, NULL);
    ui_text32(scr, 176, 16, "温控曲线", TKS_TX);
    ui_text24(scr, 320, 22, "近 30 分钟", TKS_TX2);

    ui_card(scr, CARD_X, CARD_Y, CARD_W, CARD_H, TKS_CARD, TKS_BORDER);
    legend(scr, PLOT_X, TKS_BLUE, "贴身水箱", &s_v1);
    legend(scr, PLOT_X + 300, TKS_TX2, "环境水箱", &s_v2);

    /* 纵轴刻度：0 / 15 / 30 / 45 / 60 */
    for (int v = 0; v <= Y_MAX; v += 15) {
        char t[8];
        snprintf(t, sizeof t, "%d", v);
        ui_aux20(scr, PLOT_X - 12 - ui_aux20_w(t), y_of(v) - 14, t, TKS_TX2);
    }
    /* 45°C 阈值：红色虚线 + 标注 */
    {
        static lv_point_precise_t th[2];
        th[0].x = PLOT_X; th[0].y = y_of(THRESH_C);
        th[1].x = PLOT_X + PLOT_W; th[1].y = th[0].y;
        lv_obj_t *line = lv_line_create(scr);
        lv_line_set_points(line, th, 2);
        lv_obj_set_style_line_color(line, TKS_RED, 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_style_line_dash_width(line, 6, 0);
        lv_obj_set_style_line_dash_gap(line, 6, 0);
        lv_obj_set_clickable(line, false);
        ui_text24(scr, PLOT_X + PLOT_W - ui_text24_w("45°C 阈值"), y_of(THRESH_C) - 30,
                  "45°C 阈值", TKS_RED);
    }

    s_chart = lv_chart_create(scr);
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

    ui_text24(scr, PLOT_X, PLOT_Y + PLOT_H + 4, "30 分钟前", TKS_TX2);
    ui_text24(scr, PLOT_X + PLOT_W - ui_text24_w("现在"), PLOT_Y + PLOT_H + 4, "现在", TKS_TX2);

    /* "超过 45°C"：贴在阈值线上方靠右，平时隐藏 */
    s_over = ui_text24(scr, PLOT_X + PLOT_W - ui_text24_w("超过 45°C"), y_of(THRESH_C) + 6,
                       "超过 45°C", TKS_RED);
    lv_obj_set_hidden(s_over, true);

    s_drawn = 0xFFFFFFFFu;
    s_built = 1;
    (void)tui_p2_update();
}

int tui_p2_update(void)
{
    if (!s_built) return 1;
    uint32_t n;
    tcs_hist_snapshot(s_h1, s_h2, &n);
    /* 折线：只有"来了新样本"才重设（原来每 500ms 全量重设 120 个点并整图刷新）*/
    if (n != s_drawn || (n && (s_h1[n - 1] != s_last1 || s_h2[n - 1] != s_last2)))
        redraw_series(n);

    /* 图例里的当前值 + 越界提示：每拍就地更新 */
    const ui_in_t *in;
    const ui_link_view_t *lv;
    tui_input(&in, &lv);
    char b[16];
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
    lv_obj_set_hidden(s_over, !(ok && (in->tc1_act > THRESH_C || in->tc2_act > THRESH_C)));
    return 0;
}
