/* ui_common.c — 界面绘图原语（MCU 版：原生 LVGL 样式，零大 sprite）
 * 与模拟器复刻版的差异（有意）：圆角用原生 radius（AA 平滑、边框沿弧全包络），
 * 不再逐像素镜像固件硬切角——G1 像素级复刻证据已冻结归档，生产 UI 以
 * UI 规范 v1.2 设计意图为准（"边框包全"即本版收益）。
 *
 * 内存：lv_malloc 池 96KB。禁大 sprite（一张 460×240 = 220KB → NULL → 未判空 → STKERR）；
 * 小图标必须判空；大位图用瓦片或调用方的静态缓冲。 */
#include "ui_common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint16_t rgb565(lv_color_t c) { return (uint16_t)lv_color_to_u16(c); }

typedef struct { lv_image_dsc_t dsc; uint8_t *data; } spr_alloc_t;

static void spr_delete_cb(lv_event_t *e)
{
    spr_alloc_t *a = lv_event_get_user_data(e);
    lv_free(a->data);
    lv_free(a);
}

static lv_obj_t * mk_img(lv_obj_t *p, int x, int y, int w, int h,
                         uint8_t *buf, lv_color_format_t cf)
{
    spr_alloc_t *a = lv_malloc(sizeof(*a));
    if (a == NULL) { lv_free(buf); return NULL; }
    lv_memset(a, 0, sizeof(*a));
    a->data = buf;
    a->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    a->dsc.header.cf = cf;
    a->dsc.header.w = (uint16_t)w;
    a->dsc.header.h = (uint16_t)h;
    a->dsc.header.stride = (uint16_t)(cf == LV_COLOR_FORMAT_RGB565 ? w * 2 : w * 4);
    a->dsc.data_size = (uint32_t)a->dsc.header.stride * (uint32_t)h;
    a->dsc.data = buf;
    lv_obj_t * img = lv_image_create(p);
    if (img == NULL) { lv_free(buf); lv_free(a); return NULL; }
    lv_image_set_src(img, &a->dsc);
    lv_obj_set_pos(img, x, y);
    lv_obj_set_clickable(img, false);   /* 基类对象默认可点击：不关掉的话图标是一块点击死区 */
    lv_obj_add_event_cb(img, spr_delete_cb, LV_EVENT_DELETE, a);
    return img;
}

lv_obj_t *ui_image_take_rgb565(lv_obj_t *p, int x, int y, int w, int h, uint8_t *buf)
{
    return mk_img(p, x, y, w, h, buf, LV_COLOR_FORMAT_RGB565);
}

void ui_screen_init(lv_obj_t *scr)
{
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, TKS_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_set_scrollable(scr, false);
}

/* ================= 基本图元 ================= */

lv_obj_t *ui_box(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c, int r)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_set_scrollable(o, false);
    lv_obj_set_clickable(o, false);
    return o;
}

void ui_hline(lv_obj_t *p, int x0, int x1, int y, lv_color_t c)
{
    ui_box(p, x0, y, x1 - x0 + 1, 1, c, 0);
}

void ui_vline(lv_obj_t *p, int x, int y0, int y1, lv_color_t c)
{
    ui_box(p, x, y0, 1, y1 - y0 + 1, c, 0);
}

void ui_outline(lv_obj_t *p, int x0, int y0, int x1, int y1, lv_color_t c)
{
    ui_hline(p, x0, x1, y0, c);
    ui_hline(p, x0, x1, y1, c);
    ui_vline(p, x0, y0, y1, c);
    ui_vline(p, x1, y0, y1, c);
}

lv_obj_t *ui_card(lv_obj_t *p, int x, int y, int w, int h, lv_color_t base, lv_color_t bd)
{
    lv_obj_t *o = ui_box(p, x, y, w, h, base, 8);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, bd, 0);
    ui_box(p, x + 1, y + h - 3, w - 2, 2, TKS_CLINE, 0);
    return o;
}

/* ================= 文字 ================= */

lv_obj_t *ui_lab(lv_obj_t *p, int x, int y, const lv_font_t *f, lv_color_t c, const char *text)
{
    if (!p || !text) return NULL;
    lv_obj_t *l = lv_label_create(p);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_clickable(l, false);   /* 让按压落到下面的可点对象上 */
    return l;
}

void ui_lab_set(lv_obj_t *o, const char *s)
{
    if (!o || !s) return;
    const char *cur = lv_label_get_text(o);
    if (cur && strcmp(cur, s) == 0) return;   /* 同文不重排：消除周期刷新的 realloc+重绘尖峰 */
    lv_label_set_text(o, s);
}

lv_obj_t *ui_text24(lv_obj_t *p, int x, int y, const char *s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv24, c, s); }
lv_obj_t *ui_text32(lv_obj_t *p, int x, int y, const char *s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv32, c, s); }
lv_obj_t *ui_text48(lv_obj_t *p, int x, int y, const char *s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv48, c, s); }
lv_obj_t *ui_text96(lv_obj_t *p, int x, int y, const char *s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv96, c, s); }
lv_obj_t *ui_aux20(lv_obj_t *p, int x, int y, const char *s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv20, c, s); }

int ui_text_w(const lv_font_t *f, const char *s)
{
    lv_point_t sz;
    if (!s || !*s) return 0;
    lv_text_get_size(&sz, s, f, 0, 0, 4096, LV_TEXT_FLAG_NONE);
    return (int)sz.x;
}

int ui_line_h(const lv_font_t *f) { return (int)f->line_height; }

int ui_text_dy(const lv_font_t *f)
{
    /* 一行文字占 line_height，基线下方留 base_line 给下伸部；数字与汉字坐在基线上，
     * 墨迹中心比行框中心高 base_line/2 → 往下挪这么多才是光学居中。*/
    return (int)f->base_line / 2;
}

int ui_text24_w(const char *s) { return ui_text_w(&tcs_font_lv24, s); }
int ui_text32_w(const char *s) { return ui_text_w(&tcs_font_lv32, s); }
int ui_aux20_w(const char *s) { return ui_text_w(&tcs_font_lv20, s); }

lv_obj_t *ui_text_center(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *text)
{
    lv_obj_t *l = ui_lab(parent, 0, 0, f, c, text);
    if (!l) return NULL;
    /* lv_obj_align = 设 align 样式 + 偏移：之后文字变宽/变窄，位置都按新尺寸重新对齐 */
    lv_obj_align(l, LV_ALIGN_CENTER, 0, ui_text_dy(f));
    return l;
}

/* ================= 按钮 ================= */

/* 配色 token（TKS_*）是运行期求值的颜色构造，不能当静态初始化器 → 用 switch 取 */
typedef struct { lv_color_t bg, bd, tx, bg_press; } btn_look_t;

static btn_look_t btn_look(int style)
{
    btn_look_t k;
    k.bg = TKS_CARD; k.bd = TKS_BORDER; k.tx = TKS_TX2; k.bg_press = TKS_PRESS;
    switch (style) {
        case UI_BTN_PRIMARY:  k.bd = TKS_BLUE;   k.tx = TKS_BLUE;  break;
        case UI_BTN_DISABLED: k.tx = TKS_GRAY;   k.bg_press = TKS_CARD; break;
        case UI_BTN_DANGER:   k.bg = TKS_RED;    k.bd = TKS_RED;   k.tx = TKS_WHITE; k.bg_press = TKS_REDDK; break;
        case UI_BTN_GHOST:    k.bd = TKS_BORDER; k.tx = TKS_TX2;   break;
        case UI_BTN_LATCHED:  k.bg = TKS_GRAY;   k.bd = TKS_GRAY;  k.tx = TKS_WHITE; k.bg_press = TKS_GRAY; break;
        case UI_BTN_CARD:     k.bd = TKS_BORDER; k.tx = TKS_TX;    break;
        case UI_BTN_CARD_SEL: k.bg = TKS_PRESS;  k.bd = TKS_BLUE;  k.tx = TKS_TX;    break;
        default: break;
    }
    return k;
}

#define UI_BTN_STYLE_MAX  UI_BTN_CARD_SEL

void ui_button_set_style(ui_button_t *b, int style)
{
    if (!b || !b->obj || style < 0 || style > UI_BTN_STYLE_MAX) return;
    btn_look_t k = btn_look(style);
    b->style = (uint8_t)style;
    lv_obj_set_style_bg_color(b->obj, k.bg, 0);
    lv_obj_set_style_border_color(b->obj, k.bd, 0);
    lv_obj_set_style_bg_color(b->obj, k.bg_press, LV_STATE_PRESSED);
    lv_obj_set_style_text_color(b->label, k.tx, 0);
    /* 禁用/已锁存：不可点（命中测试直接跳过，不会出现"点了没反应"的假象）*/
    if (style == UI_BTN_DISABLED || style == UI_BTN_LATCHED)
        lv_obj_set_clickable(b->obj, false);
    else
        lv_obj_set_clickable(b->obj, true);
}

ui_button_t ui_button(lv_obj_t *p, int x, int y, int w, int h, const char *text, int style,
                      const lv_font_t *f, lv_event_cb_t cb, void *user)
{
    ui_button_t b;
    b.obj = lv_obj_create(p);
    lv_obj_remove_style_all(b.obj);
    lv_obj_set_pos(b.obj, x, y);
    lv_obj_set_size(b.obj, w, h);
    lv_obj_set_style_bg_opa(b.obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(b.obj, 1, 0);
    lv_obj_set_style_radius(b.obj, 8, 0);
    lv_obj_set_scrollable(b.obj, false);
    lv_obj_set_clickable(b.obj, true);
    if (cb) lv_obj_add_event_cb(b.obj, cb, LV_EVENT_CLICKED, user);
    b.label = ui_text_center(b.obj, f, TKS_TX, text);
    b.style = 0xFF;
    ui_button_set_style(&b, style);
    return b;
}

void ui_button_set_text(ui_button_t *b, const char *text)
{
    if (!b || !b->label) return;
    ui_lab_set(b->label, text);
}

/* ================= 状态胶囊 ================= */

int ui_pill_width(const lv_font_t *f, const char *text, int dot_r)
{
    return (dot_r > 0 ? 2 * dot_r + UI_PILL_GAP : 0) + ui_text_w(f, text) + 2 * UI_PILL_PAD;
}

ui_pill_t ui_pill(lv_obj_t *p, int x, int y, int h, const lv_font_t *f)
{
    ui_pill_t pp;
    int d = 2 * UI_PILL_DOT_R;
    pp.obj = ui_box(p, x, y, 2 * UI_PILL_PAD, h, TKS_CARD, h / 2);
    pp.dot = ui_box(pp.obj, UI_PILL_PAD, (h - d) / 2, d, d, TKS_GREEN, UI_PILL_DOT_R);
    pp.label = ui_lab(pp.obj, UI_PILL_PAD, (h - ui_line_h(f)) / 2 + ui_text_dy(f), f, TKS_TX, "");
    pp.h = h;          /* 高度记下来：布局是惰性刷新的，建完立刻问对象要高度会得到 0 */
    return pp;
}

void ui_pill_set(ui_pill_t *pp, const char *text, lv_color_t bg, lv_color_t fg,
                 int dot_r, lv_color_t dot_c)
{
    if (!pp || !pp->obj || !pp->dot || !pp->label) return;
    const lv_font_t *f = lv_obj_get_style_text_font(pp->label, 0);
    /* 用创建时记下的高度，不读 lv_obj_get_height：刚建完/刚改完尺寸时布局还没刷新，读到的是旧值
     * （2026-11-24 真机：二级页顶栏胶囊里的文字被摆高 16px、上缘裁掉）。*/
    int h = pp->h;
    lv_obj_set_style_bg_color(pp->obj, bg, 0);
    lv_obj_set_style_text_color(pp->label, fg, 0);
    ui_lab_set(pp->label, text);
    int x = UI_PILL_PAD;
    if (dot_r > 0) {
        int d = 2 * dot_r;
        lv_obj_set_size(pp->dot, d, d);
        lv_obj_set_style_radius(pp->dot, dot_r, 0);
        lv_obj_set_style_bg_color(pp->dot, dot_c, 0);
        lv_obj_set_pos(pp->dot, x, (h - d) / 2);
        lv_obj_set_hidden(pp->dot, false);
        x += d + UI_PILL_GAP;
    } else {
        lv_obj_set_hidden(pp->dot, true);
    }
    /* 胶囊宽度 = 内边距 + 圆点 + 间距 + 实测文字宽 → 内容自然居中，文字变了跟着变宽 */
    lv_obj_set_width(pp->obj, ui_pill_width(f, text, dot_r));
    if (dot_r > 0) lv_obj_set_x(pp->dot, UI_PILL_PAD);
    lv_obj_set_pos(pp->label, x, (h - ui_line_h(f)) / 2 + ui_text_dy(f));
}

void ui_pill_move(ui_pill_t *pp, int x, int y)
{
    if (!pp || !pp->obj) return;
    lv_obj_set_pos(pp->obj, x, y);
}

/* ================= 小图标 ================= */

void ui_icon_tri20(lv_obj_t *p, int x, int y, lv_color_t fg, lv_color_t bg)
{
    uint8_t *buf = lv_malloc(20 * 20 * 2);
    if (!buf) return;
    uint16_t cf = rgb565(fg), cb = rgb565(bg);
    for (int yy = 0; yy < 20; yy++)
        for (int xx = 0; xx < 20; xx++) {
            int hw = (yy + 2) * 11 / 20;
            ((uint16_t *)buf)[yy * 20 + xx] = (xx >= 10 - hw && xx <= 10 + hw) ? cf : cb;
        }
    for (int yy = 8; yy <= 14; yy++) for (int xx = 9; xx <= 11; xx++) ((uint16_t *)buf)[yy * 20 + xx] = cb;
    for (int yy = 16; yy <= 18; yy++) for (int xx = 9; xx <= 11; xx++) ((uint16_t *)buf)[yy * 20 + xx] = cb;
    mk_img(p, x, y, 20, 20, buf, LV_COLOR_FORMAT_RGB565);
}

void ui_icon_sq20(lv_obj_t *p, int x, int y, lv_color_t fg)
{
    uint8_t *buf = lv_malloc(20 * 20 * 2);
    if (!buf) return;
    uint16_t cf = rgb565(fg);
    for (int i = 0; i < 400; i++) ((uint16_t *)buf)[i] = cf;
    mk_img(p, x, y, 20, 20, buf, LV_COLOR_FORMAT_RGB565);
}

/* 3px 点阵遮罩：瓦片平铺（替代 2.4MB 整屏 sprite）。
 * 2026-11-24 现场实测：3x3 瓦片 → 整屏约 6.8 万个瓦片，弹窗约 2s 才画完整；
 * 瓦片放大到 48x48：点距仍为 3px，像素级外观不变，瓦片数降到约 1100；静态 9KB。*/
#define MASK_TILE_PX   48
void ui_mask_dots(lv_obj_t *p)
{
    static uint8_t tile[MASK_TILE_PX * MASK_TILE_PX * 4];
    static lv_image_dsc_t dsc;
    static int inited;
    if (!inited) {
        inited = 1;
        memset(tile, 0, sizeof(tile));
        for (int y = 0; y < MASK_TILE_PX; y += 3)
            for (int x = 0; x < MASK_TILE_PX; x += 3) {
                uint8_t *px = tile + (y * MASK_TILE_PX + x) * 4;
                px[0] = 0xE6; px[1] = 0xDE; px[2] = 0xD8; px[3] = 0xFF;
            }
        dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
        dsc.header.w = MASK_TILE_PX;
        dsc.header.h = MASK_TILE_PX;
        dsc.header.stride = MASK_TILE_PX * 4;
        dsc.data_size = sizeof(tile);
        dsc.data = tile;
    }
    lv_obj_t * m = lv_obj_create(p);
    lv_obj_remove_style_all(m);
    lv_obj_set_pos(m, 0, 0);
    lv_obj_set_size(m, 1024, 600);
    lv_obj_set_style_bg_image_src(m, &dsc, 0);
    lv_obj_set_style_bg_image_tiled(m, true, 0);
    lv_obj_set_scrollable(m, false);
}
