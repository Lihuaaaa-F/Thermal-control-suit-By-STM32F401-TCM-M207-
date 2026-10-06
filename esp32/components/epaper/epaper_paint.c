#include "epaper_paint.h"

/* 七段布局: 段名 A(顶) B(右上) C(右下) D(底) E(左下) F(左上) G(中)
 * 段长 = h/2 - t, 厚度 t = h/5 */
static void seg_h(uint8_t *p, int x, int y, int len, int t) { paint_fill_rect(p, x, y, len, t, true); }
static void seg_v(uint8_t *p, int x, int y, int len, int t) { paint_fill_rect(p, x, y, t, len, true); }

static const uint8_t SEG_MAP[10] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,   /* 标准 7 段码,bit0=A..bit6=G */
};

void paint_fill_rect(uint8_t *plane, int x, int y, int w, int h, bool ink)
{
    for (int yy = y; yy < y + h; yy++) {
        if (yy < 0 || yy >= PAINT_H) continue;
        for (int xx = x; xx < x + w; xx++) {
            if (xx < 0 || xx >= PAINT_W) continue;
            uint8_t mask = 0x80 >> (xx & 7);
            if (ink) plane[yy * PAINT_STRIDE + (xx >> 3)] &= ~mask;   /* 0=墨 */
            else     plane[yy * PAINT_STRIDE + (xx >> 3)] |= mask;    /* 1=白 */
        }
    }
}

void paint_clear(uint8_t *plane, bool white)
{
    for (int i = 0; i < PAINT_H * PAINT_STRIDE; i++) {
        plane[i] = white ? 0xFF : 0x00;
    }
}

void paint_draw_7seg(uint8_t *plane, int x, int y, int h, char ch)
{
    if (ch == ' ') return;
    int t = h / 5;                 /* 厚度 */
    int seg = h / 2 - t;           /* 段长(半高减厚度) */
    int mid = y + h / 2 - t / 2;

    if (ch == '.') { paint_fill_rect(plane, x, y + h - t, t, t, true); return; }
    if (ch == '-') { seg_h(plane, x + t, mid, seg, t); return; }

    uint8_t m;
    if (ch == 'C') m = 0x39;                          /* A F E D */
    else if (ch >= '0' && ch <= '9') m = SEG_MAP[ch - '0'];
    else return;

    seg_h(plane, x + t, y, seg, t);                          /* A */
    if (m & 0x40) seg_h(plane, x + t, mid, seg, t);          /* G */
    seg_h(plane, x + t, y + h - t, seg, t);                  /* D */
    seg_v(plane, x, y + t / 2, seg + t / 2, t);              /* F(左上) */
    seg_v(plane, x, mid + t / 2, seg + t / 2, t);            /* E(左下) */
    seg_v(plane, x + t + seg, y + t / 2, seg + t / 2, t);    /* B(右上) */
    seg_v(plane, x + t + seg, mid + t / 2, seg + t / 2, t);  /* C(右下) */
}

void paint_draw_dot(uint8_t *plane, int x, int y, int h)
{
    paint_draw_7seg(plane, x, y, h, '.');
}

int paint_render_temp(uint8_t *plane, int x, int y, int h, int tenths)
{
    char buf[8];
    bool neg = tenths < 0;
    int v = neg ? -tenths : tenths;
    int n = 0;
    buf[n++] = neg ? '-' : ' ';
    buf[n++] = (char)('0' + (v / 100) % 10);
    buf[n++] = (char)('0' + (v / 10) % 10);
    buf[n++] = '.';
    buf[n++] = (char)('0' + v % 10);
    buf[n++] = 'C';
    buf[n] = 0;

    int t = h / 5;
    int cx = x;
    for (int i = 0; i < n; i++) {
        if (buf[i] == '.') {
            paint_draw_dot(plane, cx + t / 2, y, h);
            cx += t * 2;                       /* 点占窄位 */
        } else {
            if (buf[i] != ' ') {
                paint_draw_7seg(plane, cx, y, h, buf[i]);
            }
            cx += h / 2 + t;                   /* 数字位宽 */
        }
    }
    return cx - x;
}
