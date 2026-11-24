/**
 * ui_common.h — 界面绘图原语（LVGL 9，MCU 版：原生样式，零大 sprite）
 *
 * 排版口径（U 线 S1/S3）：字体已改为比例步进，**文字宽度必须实测**（ui_text_w），
 * 居中一律用 LVGL 的对齐（ui_text_center / ui_button / ui_pill）——文字变了仍然居中。
 * 不要再按"字数 × 字号"估算，也不要给透明点击区单独贴一层。
 *
 * 颜色走 token（tcs_tokens_lvgl.h，单源 ui_layout.h）。
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

void ui_screen_init(lv_obj_t *scr);

/* ---- 基本图元（都不可点击）---- */
lv_obj_t *ui_box(lv_obj_t *p, int x, int y, int w, int h, lv_color_t c, int r);
void ui_hline(lv_obj_t *p, int x0, int x1, int y, lv_color_t c);
void ui_vline(lv_obj_t *p, int x, int y0, int y1, lv_color_t c);
void ui_outline(lv_obj_t *p, int x0, int y0, int x1, int y1, lv_color_t c);
/* 卡片：圆角 8 + 1px 全包络描边 + 2px 底饰线 */
lv_obj_t *ui_card(lv_obj_t *p, int x, int y, int w, int h, lv_color_t base, lv_color_t bd);

/* ---- 文字 ---- */
lv_obj_t *ui_lab(lv_obj_t *p, int x, int y, const lv_font_t *f, lv_color_t c, const char *text);
void ui_lab_set(lv_obj_t *o, const char *s);       /* 同文不重设（不触发重排/重绘）*/
lv_obj_t *ui_text24(lv_obj_t *p, int x, int y, const char *s, lv_color_t c);
lv_obj_t *ui_text32(lv_obj_t *p, int x, int y, const char *s, lv_color_t c);
lv_obj_t *ui_text48(lv_obj_t *p, int x, int y, const char *s, lv_color_t c);
lv_obj_t *ui_text96(lv_obj_t *p, int x, int y, const char *s, lv_color_t c);
lv_obj_t *ui_aux20(lv_obj_t *p, int x, int y, const char *s, lv_color_t c);

int  ui_text_w(const lv_font_t *f, const char *s);   /* 实测文字宽度 px */
int  ui_line_h(const lv_font_t *f);                  /* 行高 */
int  ui_text_dy(const lv_font_t *f);                 /* 竖向光学居中微调（数字/汉字坐在基线上）*/
int  ui_text24_w(const char *s);
int  ui_text32_w(const char *s);
int  ui_aux20_w(const char *s);

/* 相对 parent 居中的文字（LVGL 对齐，文字变化后仍居中）*/
lv_obj_t *ui_text_center(lv_obj_t *parent, const lv_font_t *f, lv_color_t c, const char *text);

/* ---- 按钮：一个可点对象 = 底 + 居中文字，自带按下态 ---- */
#define UI_BTN_PRIMARY  0     /* 白底蓝边蓝字（常规操作）*/
#define UI_BTN_DISABLED 1     /* 灰边灰字，不可点（主控断了时的模式键 / 风扇卡）*/
#define UI_BTN_DANGER   2     /* 红底白字（急停）*/
#define UI_BTN_GHOST    5     /* 白底灰边次级字（返回 / 取消）*/
#define UI_BTN_LATCHED  6     /* 灰底白字：急停已锁存，解除后回红 */
#define UI_BTN_CARD     7     /* 卡片外观的整块可点区（按下变浅蓝底）*/
#define UI_BTN_CARD_SEL 8     /* 卡片被选中：浅蓝底 + 蓝边 */

typedef struct { lv_obj_t *obj; lv_obj_t *label; uint8_t style; } ui_button_t;

ui_button_t ui_button(lv_obj_t *p, int x, int y, int w, int h, const char *text, int style,
                      const lv_font_t *f, lv_event_cb_t cb, void *user);
void ui_button_set_text(ui_button_t *b, const char *text);
void ui_button_set_style(ui_button_t *b, int style);

/* ---- 状态胶囊：圆点 + 文字，整组居中；宽度随文字 ---- */
typedef struct { lv_obj_t *obj; lv_obj_t *dot; lv_obj_t *label; int h; } ui_pill_t;

#define UI_PILL_DOT_R  5       /* 圆点半径 px */
#define UI_PILL_PAD    12      /* 左右内边距 */
#define UI_PILL_GAP    8       /* 圆点与文字间距 */

ui_pill_t ui_pill(lv_obj_t *p, int x, int y, int h, const lv_font_t *f);
/* 设文案与配色。dot_r=0 不画圆点。胶囊宽度随文字变（左上角不动）。*/
void ui_pill_set(ui_pill_t *pp, const char *text, lv_color_t bg, lv_color_t fg,
                 int dot_r, lv_color_t dot_c);
/* 按给定文案算胶囊宽度（不画，供调用方排布）*/
int  ui_pill_width(const lv_font_t *f, const char *text, int dot_r);
void ui_pill_move(ui_pill_t *pp, int x, int y);

/* ---- 小图标 ---- */
void ui_icon_tri20(lv_obj_t *p, int x, int y, lv_color_t fg, lv_color_t bg);   /* 警告三角 */
void ui_icon_sq20(lv_obj_t *p, int x, int y, lv_color_t fg);                   /* 实心方块（危险）*/
void ui_mask_dots(lv_obj_t *p);                                               /* 弹窗点阵遮罩 */
lv_obj_t *ui_image_take_rgb565(lv_obj_t *p, int x, int y, int w, int h, uint8_t *buf);

#endif
