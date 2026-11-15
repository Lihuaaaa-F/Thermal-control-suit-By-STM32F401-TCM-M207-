/**
 * ui_common.h — G1 复刻页公共图元（镜像已删除的旧 tcs_ui.c 绘制原语；坐标语义=固件）
 *
 * 复刻判定口径（G1 正典）：坐标/尺寸/颜色逐项照抄 project_tcs/App/tcs/tcs_ui.c 对应
 * 函数（draw_tc_card/draw_body_card/draw_fan_card_hl/topbar_content/draw_prow/...），
 * 颜色走 token 出口（tcs_tokens_lvgl.h，单源 ui_layout.h，D-L8）。
 */
#ifndef UI_COMMON_H
#define UI_COMMON_H

#include "lvgl.h"
#include "tcs_tokens_lvgl.h"

LV_FONT_DECLARE(tcs_font_lv24);
LV_FONT_DECLARE(tcs_font_lv_ext);
LV_FONT_DECLARE(tcs_font_lv32);
LV_FONT_DECLARE(tcs_font_lv96);
LV_FONT_DECLARE(tcs_font_lv48);
LV_FONT_DECLARE(tcs_font_lv20);

void ui_screen_init(lv_obj_t * scr);

lv_obj_t * ui_box(lv_obj_t * p, int x, int y, int w, int h, lv_color_t c, int r);
void ui_hline(lv_obj_t * p, int x0, int x1, int y, lv_color_t c);
void ui_vline(lv_obj_t * p, int x, int y0, int y1, lv_color_t c);
void ui_outline(lv_obj_t * p, int x0, int y0, int x1, int y1, lv_color_t c);
lv_obj_t * ui_rrect(lv_obj_t * p, int x, int y, int w, int h, int r,
                    lv_color_t fill, lv_color_t bg);
void ui_card(lv_obj_t * p, int x, int y, int w, int h, lv_color_t base, lv_color_t bd);
void ui_btn(lv_obj_t * p, int x, int y, int w, int h, const char * label,
            int style, int fs);

void ui_lab(lv_obj_t * p, int x, int y, const lv_font_t * f, lv_color_t c,
            const char * text);
/* 文本相同则不触发 realloc/重绘（TL-8 每 500ms 尾部尖峰根因修复） */
void ui_lab_set(lv_obj_t * o, const char * s);
void ui_text24(lv_obj_t * p, int x, int y, const char * s, lv_color_t c);
void ui_text32(lv_obj_t * p, int x, int y, const char * s, lv_color_t c);
void ui_text48(lv_obj_t * p, int x, int y, const char * s, lv_color_t c);
void ui_text96(lv_obj_t * p, int x, int y, const char * s, lv_color_t c);
void ui_aux20(lv_obj_t * p, int x, int y, const char * s, lv_color_t c);

int ui_text24_w(const char * s);
int ui_text32_w(const char * s);
int ui_aux20_w(const char * s);

void ui_dot8(lv_obj_t * p, int cx, int cy, lv_color_t c);
void ui_icon_tri20(lv_obj_t * p, int x, int y, lv_color_t fg, lv_color_t bg);
void ui_icon_sq20(lv_obj_t * p, int x, int y, lv_color_t fg);
void ui_icon_estop24(lv_obj_t * p, int x, int y, lv_color_t fg, bool alert);
void ui_mask_dots(lv_obj_t * p);
lv_obj_t * ui_image_take_rgb565(lv_obj_t * p, int x, int y, int w, int h, uint8_t * buf);

#endif
