/* tui_p1.c — 调参页：4 个常用参数（贴身/环境水箱设定、体温过热/过冷阈值）
 * 每行 = 名称 / 当前值 / 滑条（两端标范围）/ 本行的 − ＋ 键；改动立即生效并同步到主控。
 *
 * 全部就地更新（U 线 S3/S5）：选行、改值、别处（网页/镜像）改参都只动对应的文字与滑块，
 * 不整页重建。拖动滑条时文字与滑块即时跟手，落值按 100ms 节流，松手补最后一笔。
 * − ＋ 键永远作用于**自己所在的那一行**（旧实现作用在"选中行"上，点第 3 行的 + 会改第 1 行）。 */
#include "tui.h"
#include "ui_common.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- 版面（px）---- */
#define ROW_X       32
#define ROW_W       960
#define ROW_H       88
#define ROW_Y0      56
#define ROW_GAP     16
#define TRACK_X     (ROW_X + 320)
#define TRACK_W     432
#define TRACK_Y     36                  /* 相对行顶 */
#define TRACK_H     16
#define THUMB_D     32
#define KEY_XM      (ROW_X + 792)
#define KEY_XP      (ROW_X + 872)
#define KEY_Y       12
#define KEY_WH      64
#define DRAG_PERIOD_MS  100u             /* 拖动落值的最小间隔 */

static const uint16_t s_ids[4] = {
    PARAM_ID_TC1_SET, PARAM_ID_TC2_SET, PARAM_ID_BODY_HOT, PARAM_ID_BODY_COLD
};

typedef struct {
    ui_button_t zone;          /* 整行可点：选中 */
    lv_obj_t   *bar;           /* 选中指示条 */
    lv_obj_t   *val, *unit;
    lv_obj_t   *slider;        /* 滑条命中区（可拖）*/
    lv_obj_t   *fill, *thumb;
    ui_button_t minus, plus;
    lv_obj_t   *sym[2][2];     /* − ＋ 的矢量笔画（选中时换色）*/
    int         y;
} row_t;

static row_t     s_row[4];
static float     s_shown[4];         /* 每行上次画上去的值（与注册表比对，变了才重画）*/
static lv_obj_t *s_scr;              /* 四行的父对象（= s_rows）*/
static lv_obj_t *s_rows;             /* 四行容器：有横幅时整体下移（tui_p1_set_banner_room）*/
static lv_obj_t *s_hint;             /* 顶栏那句用法/禁用提示 */
static uint8_t   s_hint_off;         /* s_hint 当前写的是"主控断了不能调参" */
static uint8_t   s_room;             /* 当前是否给横幅让了位 */
static int       s_sel;
static float     s_step = 0.1f;
static uint8_t   s_built;
static ui_button_t s_big_minus, s_big_plus, s_step_btn;
static int       s_drag_row = -1;    /* 正在拖动的行，-1 = 无 */
static float     s_drag_v;           /* 拖动中的待落值 */
static uint32_t  s_drag_last_ms;

static int row_y(int i) { return ROW_Y0 + i * (ROW_H + ROW_GAP); }
static const param_desc_t *pd(int i) { return param_find(s_ids[i]); }

/* ============ 换算 ============ */

static float quantize(const param_desc_t *p, float v)
{
    float step = (p->step > 0.0f) ? p->step : 0.1f;
    v = p->min + roundf((v - p->min) / step) * step;
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    return v;
}

static float frac_of(const param_desc_t *p, float v)
{
    if (p->max <= p->min) return 0.0f;
    float f = (v - p->min) / (p->max - p->min);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return f;
}

static int thumb_x(float frac) { return TRACK_X + THUMB_D / 2 + (int)(frac * (TRACK_W - THUMB_D)); }

/* ============ 画一行 ============ */

static void row_show(int i, float v)
{
    row_t *r = &s_row[i];
    const param_desc_t *p = pd(i);
    if (!p) return;
    char b[24];
    snprintf(b, sizeof b, "%.1f", (double)v);
    ui_lab_set(r->val, b);
    lv_obj_set_x(r->unit, ROW_X + 24 + ui_text32_w(b) + 4);
    float f = frac_of(p, v);
    int tx = thumb_x(f);
    lv_obj_set_width(r->fill, tx - TRACK_X);
    lv_obj_set_pos(r->thumb, tx - THUMB_D / 2, r->y + TRACK_Y + TRACK_H / 2 - THUMB_D / 2);
    s_shown[i] = v;
}

static void row_refresh(int i)
{
    const param_desc_t *p = pd(i);
    if (p) row_show(i, p->value);
}

static void select_paint(void)
{
    for (int i = 0; i < 4; i++) {
        row_t *r = &s_row[i];
        int sel = (i == s_sel);
        lv_color_t c = sel ? TKS_BLUE : TKS_GRAY;
        ui_button_set_style(&r->zone, sel ? UI_BTN_CARD_SEL : UI_BTN_CARD);
        if (sel) lv_obj_set_hidden(r->bar, false);
        else     lv_obj_set_hidden(r->bar, true);
        lv_obj_set_style_bg_color(r->thumb, c, 0);
        lv_obj_set_style_bg_color(r->fill, sel ? TKS_TRACKFILL : TKS_TRACK, 0);
        for (int k = 0; k < 2; k++) for (int a = 0; a < 2; a++)
            if (r->sym[k][a]) lv_obj_set_style_bg_color(r->sym[k][a], sel ? TKS_BLUE : TKS_TX2, 0);
    }
}

/* ============ 落值 ============ */

#define HINT_ON  "拖滑条或按加减键，改动立即生效"
#define HINT_OFF "主控连接中断，现在不能调参"

/* 主控在线才能改参：屏端注册表只是镜像，帧发不出去时 param_set 照样返回 0，本地值
 * 会在下一次镜像泵时被主控打回去——使用者看到的是"改了又自己变回去"。
 * 所以主控断了就明说现在不能改，而不是让他白点。（独立复核 P2-4）*/
static int link_up(void)
{
    const ui_in_t *in;
    const ui_link_view_t *lv;
    tui_input(&in, &lv);
    return lv->link == UI_LINK_UP;
}

static void apply_value(int i, float v)
{
    const param_desc_t *p = pd(i);
    if (!p) return;
    if (!link_up()) { ui_toast(HINT_OFF); return; }
    int rv = param_set(s_ids[i], quantize(p, v), PARAM_SRC_TOUCH);
    if (rv == -2) ui_toast("没有改成：过冷阈值必须低于过热阈值");
    else if (rv < 0) ui_toast("这个参数现在不能修改");
    row_refresh(i);
}

/* ============ 滑条拖动 ============ */

static void slider_point(int i, int final)
{
    lv_indev_t *in = lv_indev_active();
    const param_desc_t *p = pd(i);
    if (!in || !p) return;
    lv_point_t pt;
    lv_indev_get_point(in, &pt);
    float f = (float)(pt.x - (TRACK_X + THUMB_D / 2)) / (float)(TRACK_W - THUMB_D);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    s_drag_v = quantize(p, p->min + f * (p->max - p->min));
    uint32_t now = lv_tick_get();
    if (final || now - s_drag_last_ms >= DRAG_PERIOD_MS) {
        s_drag_last_ms = now;
        apply_value(i, s_drag_v);
    } else if (s_drag_v != s_shown[i]) {
        row_show(i, s_drag_v);            /* 先跟手显示，到了节流点再落值 */
    }
}

static void slider_event(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    switch (lv_event_get_code(e)) {
        case LV_EVENT_PRESSED:  s_drag_row = i; s_drag_last_ms = lv_tick_get() - DRAG_PERIOD_MS; slider_point(i, 0); break;
        case LV_EVENT_PRESSING: if (s_drag_row == i) slider_point(i, 0); break;
        case LV_EVENT_RELEASED: if (s_drag_row == i) slider_point(i, 1); s_drag_row = -1; break;
        case LV_EVENT_PRESS_LOST: if (s_drag_row == i) row_refresh(i); s_drag_row = -1; break;
        default: break;
    }
}

/* ============ 点击 ============ */

static void clk_back(lv_event_t *e) { LV_UNUSED(e); tui_request(TUI_HOME); }

static void select_row(int i)
{
    if (s_sel == i) return;
    s_sel = i;
    select_paint();
}

static void clk_row(lv_event_t *e) { select_row((int)(intptr_t)lv_event_get_user_data(e)); }

static void clk_key(lv_event_t *e)
{
    intptr_t ud = (intptr_t)lv_event_get_user_data(e);
    int i = (int)(ud >> 4);
    int dir = (int)(ud & 0xF) ? 1 : -1;
    select_row(i);
    const param_desc_t *p = pd(i);
    if (p) apply_value(i, p->value + (float)dir * s_step);
}

static void clk_big(lv_event_t *e)
{
    int dir = (int)(intptr_t)lv_event_get_user_data(e);
    const param_desc_t *p = pd(s_sel);
    if (p) apply_value(s_sel, p->value + (float)dir * s_step);
}

static void step_button_text(void)
{
    char b[24];
    snprintf(b, sizeof b, "步进 %.1f", (double)s_step);
    ui_button_set_text(&s_step_btn, b);
}

static void clk_step(lv_event_t *e)
{
    LV_UNUSED(e);
    tui_step_toggle();
    step_button_text();
}

/* 矢量 − / ＋：不依赖字库里的符号，粗细位置都可控 */
static void make_symbol(lv_obj_t *btn, int plus, int arm, int thick, lv_obj_t *out[2])
{
    out[0] = ui_box(btn, 0, 0, arm, thick, TKS_BLUE, thick / 2);
    lv_obj_align(out[0], LV_ALIGN_CENTER, 0, 0);
    out[1] = 0;
    if (plus) {
        out[1] = ui_box(btn, 0, 0, thick, arm, TKS_BLUE, thick / 2);
        lv_obj_align(out[1], LV_ALIGN_CENTER, 0, 0);
    }
}

/* ============ 构建 ============ */

static void build_row(int i)
{
    row_t *r = &s_row[i];
    const param_desc_t *p = pd(i);
    int y = row_y(i);
    r->y = y;
    if (!p) return;

    r->zone = ui_button(s_scr, ROW_X, y, ROW_W, ROW_H, "", UI_BTN_CARD, &tcs_font_lv24,
                        clk_row, (void *)(intptr_t)i);
    ui_box(s_scr, ROW_X + 1, y + ROW_H - 3, ROW_W - 2, 2, TKS_CLINE, 0);   /* 卡片底饰线 */
    r->bar = ui_box(s_scr, ROW_X + 1, y + 1, 4, ROW_H - 5, TKS_BLUE, 0);

    ui_text24(s_scr, ROW_X + 24, y + 12, p->name, TKS_TX);
    r->val = ui_text32(s_scr, ROW_X + 24, y + 44, "", TKS_HERO);
    lv_obj_set_style_bg_color(r->val, TKS_CARD, 0);
    lv_obj_set_style_bg_opa(r->val, LV_OPA_COVER, 0);
    r->unit = ui_aux20(s_scr, ROW_X + 24, y + 52, p->unit, TKS_TX2);

    /* 滑条：命中区比轨道高（好按），轨道/已选段/滑块都是它的子对象 */
    r->slider = ui_box(s_scr, TRACK_X, y + 12, TRACK_W, 64, TKS_BG, 0);
    lv_obj_set_style_bg_opa(r->slider, LV_OPA_TRANSP, 0);
    lv_obj_set_clickable(r->slider, true);
    ui_box(r->slider, 0, TRACK_Y - 12, TRACK_W, TRACK_H, TKS_TRACK, TRACK_H / 2);
    r->fill = ui_box(r->slider, 0, TRACK_Y - 12, 1, TRACK_H, TKS_TRACK, TRACK_H / 2);
    r->thumb = ui_box(s_scr, 0, 0, THUMB_D, THUMB_D, TKS_GRAY, THUMB_D / 2);
    int codes[] = { LV_EVENT_PRESSED, LV_EVENT_PRESSING, LV_EVENT_RELEASED, LV_EVENT_PRESS_LOST };
    for (unsigned k = 0; k < sizeof codes / sizeof codes[0]; k++)
        lv_obj_add_event_cb(r->slider, slider_event, codes[k], (void *)(intptr_t)i);
    /* 两端的范围刻度 */
    {
        char lo[16], hi[16];
        snprintf(lo, sizeof lo, "%.1f", (double)p->min);
        snprintf(hi, sizeof hi, "%.1f", (double)p->max);
        ui_aux20(s_scr, TRACK_X, y + 60, lo, TKS_TX2);
        ui_aux20(s_scr, TRACK_X + TRACK_W - ui_aux20_w(hi), y + 60, hi, TKS_TX2);
    }

    r->minus = ui_button(s_scr, KEY_XM, y + KEY_Y, KEY_WH, KEY_WH, "", UI_BTN_GHOST,
                         &tcs_font_lv24, clk_key, (void *)(intptr_t)(i << 4));
    make_symbol(r->minus.obj, 0, 28, 4, r->sym[0]);
    r->plus = ui_button(s_scr, KEY_XP, y + KEY_Y, KEY_WH, KEY_WH, "", UI_BTN_GHOST,
                        &tcs_font_lv24, clk_key, (void *)(intptr_t)((i << 4) | 1));
    make_symbol(r->plus.obj, 1, 28, 4, r->sym[1]);
    row_refresh(i);
}

/* 顶栏（返回 + 标题 + 提示）直接画在屏幕上不动；四行画在容器 s_rows 里，底栏画在 s_body 上。
 * 坐标一律按屏幕绝对位置写（容器都在 (0,0)）。*/
void tui_p1_build(lv_obj_t *scr)
{
    memset(s_row, 0, sizeof s_row);
    for (int i = 0; i < 4; i++) s_shown[i] = NAN;
    if (s_sel < 0 || s_sel > 3) s_sel = 0;
    s_drag_row = -1;
    s_built = 0;
    s_room = 0;

    /* 顶栏：返回 + 标题 + 一句用法（右边是常驻状态条）*/
    ui_button(scr, 24, 8, 120, 40, "‹ 返回", UI_BTN_GHOST, &tcs_font_lv24, clk_back, NULL);
    ui_text32(scr, 176, 16, "调参", TKS_TX);
    s_hint_off = 0xFF;
    s_hint = ui_text24(scr, 280, 22, HINT_ON, TKS_TX2);

    lv_obj_t *body = ui_box(scr, 0, 0, 1024, 600, TKS_BG, 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    s_rows = ui_box(body, 0, 0, 1024, 600, TKS_BG, 0);
    lv_obj_set_style_bg_opa(s_rows, LV_OPA_TRANSP, 0);
    s_scr = s_rows;

    for (int i = 0; i < 4; i++) build_row(i);
    select_paint();

    /* 底栏：对"选中行"生效的大 − ＋ 与步进切换 */
    lv_obj_t *sym[2];
    s_big_minus = ui_button(body, 32, 472, 96, 96, "", UI_BTN_PRIMARY, &tcs_font_lv32,
                            clk_big, (void *)(intptr_t)-1);
    make_symbol(s_big_minus.obj, 0, 36, 5, sym);
    s_big_plus = ui_button(body, 896, 472, 96, 96, "", UI_BTN_PRIMARY, &tcs_font_lv32,
                           clk_big, (void *)(intptr_t)1);
    make_symbol(s_big_plus.obj, 1, 36, 5, sym);
    /* 步进键宽 176：《现行UI规范》§底栏写的是 步进(160,488,176,64)，并把"步进 180"列为不落 8px 网格的偏差
     * （审查条目 M03-F-13）。2026-11-24 之前是 180。 */
    s_step_btn = ui_button(body, 160, 488, 176, 64, "", UI_BTN_PRIMARY, &tcs_font_lv24, clk_step, NULL);
    step_button_text();
    s_built = 1;
    (void)tui_p1_update();
}

/* 二级页的横幅占 y 64..120（会盖住第一行的滑条）。有横幅时：四行整体下移 72px（从 128 起，
 * 末行到 528），底栏压扁挪到最底下（532..588）；没有横幅时回到原位。
 * 只改位置和尺寸，不重建任何对象——就地更新手里的句柄全部照旧有效。*/
void tui_p1_set_banner_room(int room)
{
    if (!s_built || (uint8_t)(room ? 1 : 0) == s_room) return;
    s_room = (uint8_t)(room ? 1 : 0);
    lv_obj_set_y(s_rows, room ? 72 : 0);
    lv_obj_set_pos(s_big_minus.obj, 32, room ? 532 : 472);
    lv_obj_set_height(s_big_minus.obj, room ? 56 : 96);
    lv_obj_set_pos(s_big_plus.obj, 896, room ? 532 : 472);
    lv_obj_set_height(s_big_plus.obj, room ? 56 : 96);
    lv_obj_set_pos(s_step_btn.obj, 160, room ? 536 : 488);
    lv_obj_set_size(s_step_btn.obj, 176, room ? 48 : 64);
}

/* 别处（网页 / 控制台 / 镜像）改了参数：只重画值变了的行；顶栏提示跟着链路状态走 */
int tui_p1_update(void)
{
    if (!s_built) return 1;
    for (int i = 0; i < 4; i++) {
        if (s_drag_row == i) continue;                 /* 拖动中的行由滑条事件自己刷新 */
        const param_desc_t *p = pd(i);
        if (p && p->value != s_shown[i]) row_refresh(i);
    }
    uint8_t off = link_up() ? 0u : 1u;
    if (off != s_hint_off) {
        s_hint_off = off;
        ui_lab_set(s_hint, off ? HINT_OFF : HINT_ON);
        lv_obj_set_style_text_color(s_hint, off ? TKS_RED : TKS_TX2, 0);
    }
    return 0;
}

/* ---- 走查钩子（g_ui_cmd 9..14；与真实触摸走同一套 apply_value）----
 * 2026-11-24（S 线 C01）：只有台架/仿真档编译。tui_step_toggle 不在此列——
 * 调参页的步进按钮（上面）自己也在调它，三档都要。
 * U 线 S7：只在本页时有效：别的页上 s_row 里的对象早已被删，不能碰。*/
#if TCS_PROFILE >= 1
void tui_step_param(int dir)
{
    if (!s_built || tui_current() != TUI_TUNE) return;
    const param_desc_t *p = pd(s_sel);
    if (!p) return;
    float step = (p->step > 0.0f) ? p->step : 0.1f;
    if (step < s_step) step = s_step;
    apply_value(s_sel, p->value + (float)dir * step);
}

void tui_sel_next(void)    { if (!s_built || tui_current() != TUI_TUNE) return; s_sel = (s_sel + 1) % 4; select_paint(); }

void tui_slider_center(void)
{
    if (!s_built || tui_current() != TUI_TUNE) return;
    const param_desc_t *p = pd(s_sel);
    if (p) apply_value(s_sel, p->min + (p->max - p->min) * 0.5f);
}
#endif /* TCS_PROFILE >= 1：走查钩子 */

void tui_step_toggle(void) { if (s_built && tui_current() == TUI_TUNE) s_step = (s_step < 0.5f) ? 1.0f : 0.1f; }
