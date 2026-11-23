/* ui_common.c — G5 生产 UI 图元（MCU 版：原生 LVGL 样式，零大 sprite）
 * 与模拟器复刻版的差异（有意）：圆角用原生 radius（AA 平滑、边框沿弧全包络），
 * 不再逐像素镜像固件硬切角——G1 像素级复刻证据已冻结归档，生产 UI 以
 * UI 规范 v1.2 设计意图为准（“边框包全”即本版收益）。 */
#include "ui_common.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint16_t rgb565(lv_color_t c) { return (uint16_t)lv_color_to_u16(c); }

typedef struct { lv_image_dsc_t dsc; uint8_t * data; } spr_alloc_t;

static void spr_delete_cb(lv_event_t * e)
{
    spr_alloc_t * a = lv_event_get_user_data(e);
    lv_free(a->data);
    lv_free(a);
}

static lv_obj_t * mk_img(lv_obj_t * p, int x, int y, int w, int h,
                         uint8_t * buf, lv_color_format_t cf)
{
    spr_alloc_t * a = lv_malloc(sizeof(*a));
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
    lv_obj_add_event_cb(img, spr_delete_cb, LV_EVENT_DELETE, a);
    return img;
}

lv_obj_t * ui_image_take_rgb565(lv_obj_t * p, int x, int y, int w, int h, uint8_t * buf)
{
    return mk_img(p, x, y, w, h, buf, LV_COLOR_FORMAT_RGB565);
}

void ui_screen_init(lv_obj_t * scr)
{
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, TKS_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t * ui_box(lv_obj_t * p, int x, int y, int w, int h, lv_color_t c, int r)
{
    lv_obj_t * o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

/* 圆角矩形：原生 radius（bg 参数保留兼容，语义不再需要切角） */
lv_obj_t * ui_rrect(lv_obj_t * p, int x, int y, int w, int h, int r,
                    lv_color_t fill, lv_color_t bg)
{
    LV_UNUSED(bg);
    return ui_box(p, x, y, w, h, fill, r);
}

void ui_hline(lv_obj_t * p, int x0, int x1, int y, lv_color_t c)
{
    ui_box(p, x0, y, x1 - x0 + 1, 1, c, 0);
}

void ui_vline(lv_obj_t * p, int x, int y0, int y1, lv_color_t c)
{
    ui_box(p, x, y0, 1, y1 - y0 + 1, c, 0);
}

void ui_outline(lv_obj_t * p, int x0, int y0, int x1, int y1, lv_color_t c)
{
    ui_hline(p, x0, x1, y0, c);
    ui_hline(p, x0, x1, y1, c);
    ui_vline(p, x0, y0, y1, c);
    ui_vline(p, x1, y0, y1, c);
}

/* 卡片：圆角 8 原生底 + 1px 全包络描边 + 2px 底饰线 */
void ui_card(lv_obj_t * p, int x, int y, int w, int h, lv_color_t base, lv_color_t bd)
{
    lv_obj_t * o = ui_box(p, x, y, w, h, base, 8);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, bd, 0);
    ui_box(p, x + 1, y + h - 3, w - 2, 2, TKS_CLINE, 0);
}

/* 按钮：0=主(白/蓝) 1=禁用 2=危险 3=暗红 4=按住 5=幽灵 */
void ui_btn(lv_obj_t * p, int x, int y, int w, int h, const char * label,
            int style, int fs)
{
    lv_color_t bg, bd, tx;
    switch (style) {
        case 1: bg = TKS_CARD;  bd = TKS_BORDER; tx = TKS_GRAY;  break;
        case 2: bg = TKS_RED;   bd = TKS_RED;    tx = TKS_WHITE; break;
        case 3: bg = TKS_REDDK; bd = TKS_REDDK;  tx = TKS_WHITE; break;
        case 4: bg = TKS_PRESS; bd = TKS_BLUE;   tx = TKS_BLUE;  break;
        case 5: bg = TKS_CARD;  bd = TKS_BORDER; tx = TKS_TX2;   break;
        default: bg = TKS_CARD; bd = TKS_BLUE;   tx = TKS_BLUE;  break;
    }
    lv_obj_t * o = ui_box(p, x, y, w, h, bg, 8);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, bd, 0);
    int lw = (fs == 32) ? ui_text32_w(label) : ui_text24_w(label);
    int lx = x + (w - lw) / 2;
    int ly = y + (h - fs) / 2;
    if (fs == 32) ui_text32(p, lx, ly, label, tx);
    else          ui_text24(p, lx, ly, label, tx);
}

/* 2026-11-23：返回 label 指针——单位「°C」需要按数值宽度重定位（tui_p0.place_unit）*/
lv_obj_t * ui_lab(lv_obj_t * p, int x, int y, const lv_font_t * f, lv_color_t c,
                  const char * text)
{
    if (!p || !text) return NULL;
    lv_obj_t * l = lv_label_create(p);
    lv_obj_remove_style_all(l);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    return l;
}

void ui_lab_set(lv_obj_t * o, const char * s)
{
    if (!o || !s) return;
    const char * cur = lv_label_get_text(o);
    if (cur && strcmp(cur, s) == 0) return;   /* 同文不重排：消除每 500ms 的 realloc+重绘尖峰 */
    lv_label_set_text(o, s);
}

lv_obj_t * ui_text24(lv_obj_t * p, int x, int y, const char * s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv24, c, s); }
lv_obj_t * ui_text32(lv_obj_t * p, int x, int y, const char * s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv32, c, s); }
lv_obj_t * ui_text48(lv_obj_t * p, int x, int y, const char * s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv48, c, s); }
lv_obj_t * ui_text96(lv_obj_t * p, int x, int y, const char * s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv96, c, s); }
lv_obj_t * ui_aux20(lv_obj_t * p, int x, int y, const char * s, lv_color_t c) { return ui_lab(p, x, y, &tcs_font_lv20, c, s); }

static int nchars(const char * s)
{
    int n = 0;
    while (*s) {
        unsigned char c = (unsigned char)*s;
        if ((c & 0xC0) != 0x80) n++;
        s++;
    }
    return n;
}
int ui_text24_w(const char * s) { return nchars(s) * 24; }
int ui_text32_w(const char * s) { return nchars(s) * 32; }
int ui_aux20_w(const char * s)
{
    lv_point_t sz;
    lv_text_get_size(&sz, s, &tcs_font_lv20, 0, 0, 1000, LV_TEXT_FLAG_NONE);
    return (int)sz.x;
}

/* 8px 点（小 sprite 12.8B 级，池无压力） */
void ui_dot8(lv_obj_t * p, int cx, int cy, lv_color_t c)
{
    uint8_t * buf = lv_malloc(8 * 8 * 2);
    if (!buf) return;
    uint16_t v = rgb565(c), bgv = rgb565(TKS_BG);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++) {
            int dx = x - 4, dy = y - 4;
            ((uint16_t *)buf)[y * 8 + x] = (dx * dx + dy * dy <= 12) ? v : bgv;
        }
    mk_img(p, cx - 4, cy - 4, 8, 8, buf, LV_COLOR_FORMAT_RGB565);
}

void ui_icon_tri20(lv_obj_t * p, int x, int y, lv_color_t fg, lv_color_t bg)
{
    uint8_t * buf = lv_malloc(20 * 20 * 2);
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

void ui_icon_sq20(lv_obj_t * p, int x, int y, lv_color_t fg)
{
    uint8_t * buf = lv_malloc(20 * 20 * 2);
    if (!buf) return;
    uint16_t cf = rgb565(fg);
    for (int i = 0; i < 400; i++) ((uint16_t *)buf)[i] = cf;
    mk_img(p, x, y, 20, 20, buf, LV_COLOR_FORMAT_RGB565);
}

void ui_icon_estop24(lv_obj_t * p, int x, int y, lv_color_t fg, bool alert)
{
    uint8_t * buf = lv_malloc(24 * 24 * 2);
    if (!buf) return;
    uint16_t cf = rgb565(fg), cb = rgb565(TKS_BG);
    for (int yy = 0; yy < 24; yy++)
        for (int xx = 0; xx < 24; xx++) {
            float dx = xx - 11.5f, dy = yy - 11.5f;
            float d2 = dx * dx + dy * dy;
            uint16_t c = cb;
            if (d2 >= 72.0f && d2 <= 121.0f) c = cf;
            if (xx >= 8 && xx <= 15 && yy >= 8 && yy <= 15) c = cf;
            ((uint16_t *)buf)[yy * 24 + xx] = c;
        }
    if (alert) {
        cf = rgb565(TKS_RED);
        for (int i = 0; i < 24 * 24; i++) ((uint16_t *)buf)[i] = cb;
        for (int yy = 0; yy < 24; yy++)
            for (int xx = 0; xx < 24; xx++) {
                float dx = xx - 11.5f, dy = yy - 11.5f;
                if (dx * dx + dy * dy <= 121.0f) ((uint16_t *)buf)[yy * 24 + xx] = cf;
            }
        for (int yy = 6; yy <= 17; yy++)
            for (int xx = 6; xx <= 17; xx++)
                ((uint16_t *)buf)[yy * 24 + xx] = rgb565(TKS_WHITE);
    }
    mk_img(p, x, y, 24, 24, buf, LV_COLOR_FORMAT_RGB565);
}

/* 3px 点阵遮罩：3x3 瓦片平铺（36B 级，替代 2.4MB 整屏 sprite） */
void ui_mask_dots(lv_obj_t * p)
{
    static uint8_t tile[3 * 3 * 4];
    static lv_image_dsc_t dsc;
    static int inited;
    if (!inited) {
        inited = 1;
        memset(tile, 0, sizeof(tile));
        tile[0] = 0xE6; tile[1] = 0xDE; tile[2] = 0xD8; tile[3] = 0xFF;
        dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc.header.cf = LV_COLOR_FORMAT_ARGB8888;
        dsc.header.w = 3;
        dsc.header.h = 3;
        dsc.header.stride = 12;
        dsc.data_size = sizeof(tile);
        dsc.data = tile;
    }
    lv_obj_t * m = lv_obj_create(p);
    lv_obj_remove_style_all(m);
    lv_obj_set_pos(m, 0, 0);
    lv_obj_set_size(m, 1024, 600);
    lv_obj_set_style_bg_image_src(m, &dsc, 0);
    lv_obj_set_style_bg_image_tiled(m, true, 0);
    lv_obj_clear_flag(m, LV_OBJ_FLAG_SCROLLABLE);
}
