/**
 * tcs_ui.c — 触摸 UI（任务书 v9.5 UI 重构专册 K3/K4/K4b 全量落地）
 *
 * 设计唯一来源 = Core/Inc/ui_layout.h（token=C 仪表定稿，三眼评审 1/2 眼过）。
 * 视觉规范（ISA-101 对齐）：灰阶底、颜色只留给报警；蓝仅交互（按钮描边/选中/拖动）；
 * 状态双编码（●○▲■+颜色+文字）；"正常=安静灰，异常=一点红"。
 *
 * 渲染策略（K3 闪屏根治）：
 *   - 小更新（数值/胶囊/横幅/按住态/滑条/时钟/角标）= ui_patch_begin 直绘显示缓冲
 *     + ui_patch_commit(rect) 矩形同步 → 全程零 flip，根治 500ms 周期×乒乓的条带闪屏；
 *   - 仅切页/弹窗/开机 ui_full_begin + ui_full_commit(flip+sync)；
 *   - 不变量：两缓冲任意时刻除最近 patch 区外逐像素一致（patch 必伴随等区同步）。
 *
 * 交互（K4b 滑条细则）：轨道 432×16 + 80px 拇指球；拖动跟手回显 ≤20Hz；
 * 拖出轨道 ±32px=取消回弹；松手 1s 防抖后生效下发；±/步进/翻页与拖动并发时
 * 后到操作取消未生效拖动值；触摸校准归属 I5 真人（sim 注入不依赖校准）。
 *
 * 字体（K2）：96/48 原生数字字模（msyhbd，比例宽）+ 32/24 中文（gen_font_cn 自动收集）
 * + 20px ASCII（辅助档）；禁任何整数倍放大；字库 const 驻 Flash。
 */
#include "tcs_ui.h"
#include "ui_layout.h"
#include "ltdc.h"
#include "ft5446.h"
#include "param_registry.h"
#include "control.h"
#include "post.h"
#include "segger_rtt.h"
#include <string.h>
#include <stdio.h>

#include "console.h"   /* tcs_log + g_stat_* */

extern uint32_t millis(void);

/* ====== K2 字库 ====== */
typedef struct { uint32_t cp; uint8_t w, adv, h, yoff; const uint8_t *bits; } ui_glyph_t;
#include "tcs_font_cn.inc"      /* 24px 全量 + 32px（ASCII/°℃/32px 中文集）*/
#include "tcs_font96.inc"       /* 96px 数字（原生，比例宽）*/
#include "tcs_font48.inc"       /* 48px 数字（原生，比例宽）*/
#include "tcs_font20.inc"       /* 20px ASCII（辅助档，比例宽）*/

#define F24_W 24
#define F24_H 24
#define F24_BPR (F24_W / 8)
#define F24_SZ  (F24_BPR * F24_H)
#define F32_W 32
#define F32_H 32
#define F32_BPR (F32_W / 8)
#define F32_SZ  (F32_BPR * F32_H)

#define F24_N (sizeof(tcs_font24_cp) / sizeof(tcs_font24_cp[0]))
#define F32_N (sizeof(tcs_font32_cp) / sizeof(tcs_font32_cp[0]))
static const unsigned int   *s_cp24  = tcs_font24_cp;
static const unsigned char (*s_gl24)[F24_SZ] = tcs_font24_glyphs;
static const unsigned int   *s_cp32  = tcs_font32_cp;
static const unsigned char (*s_gl32)[F32_SZ] = tcs_font32_glyphs;

/* ====== UTF-8 → codepoint ====== */
static int utf8_next(const char *s, unsigned int *cp)
{
    const unsigned char *p = (const unsigned char *)s;
    if (p[0] < 0x80) { *cp = p[0]; return p[0] ? 1 : 0; }
    if ((p[0] & 0xE0) == 0xC0) { *cp = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); return 2; }
    if ((p[0] & 0xF0) == 0xE0) {
        *cp = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    if ((p[0] & 0xF8) == 0xF0) {
        *cp = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12)
            | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    *cp = '?';
    return 1;
}

/* ====== 固定格 24/32 文本 ====== */
static int f24_find(unsigned int cp)
{
    int lo = 0, hi = (int)F24_N - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (s_cp24[mid] == cp) return mid;
        if (s_cp24[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}
static int f32_find(unsigned int cp)
{
    int lo = 0, hi = (int)F32_N - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (s_cp32[mid] == cp) return mid;
        if (s_cp32[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

/* 行游程合并绘制一格（bg=0xFFFF 只画前景）*/
static void f_cell_draw(int x, int y, int w, int h, int bpr,
                        const unsigned char *g, uint16_t fg, uint16_t bg)
{
    for (int row = 0; row < h; row++) {
        int col = 0;
        while (col < w) {
            uint8_t bits = g[row * bpr + col / 8];
            int bit = (bits & (0x80 >> (col & 7))) != 0;
            int run = 1;
            while (col + run < w) {
                uint8_t b2 = g[row * bpr + (col + run) / 8];
                if (((b2 >> (7 - ((col + run) & 7))) & 1) != bit) break;
                run++;
            }
            if (bit || bg != 0xFFFF)
                gfx_fill_rect(x + col, y + row, x + col + run - 1, y + row, bit ? fg : bg);
            col += run;
        }
    }
}

static void ui_text24(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    while (*s) {
        unsigned int cp; int n = utf8_next(s, &cp);
        if (n == 0) break;
        int gi = f24_find(cp);
        if (gi < 0) gi = f24_find('?');
        if (gi >= 0) f_cell_draw(x, y, F24_W, F24_H, F24_BPR, s_gl24[gi], fg, bg);
        x += F24_W; s += n;
    }
}
static int ui_text24_w(const char *s)
{
    int w = 0;
    while (*s) { unsigned int cp; int n = utf8_next(s, &cp); if (!n) break; w += F24_W; s += n; }
    return w;
}

static void ui_text32(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    while (*s) {
        unsigned int cp; int n = utf8_next(s, &cp);
        if (n == 0) break;
        int gi = f32_find(cp);
        if (gi < 0) gi = f32_find('?');
        if (gi >= 0) f_cell_draw(x, y, F32_W, F32_H, F32_BPR, s_gl32[gi], fg, bg);
        x += F32_W; s += n;
    }
}
static int ui_text32_w(const char *s)
{
    int w = 0;
    while (*s) { unsigned int cp; int n = utf8_next(s, &cp); if (!n) break; w += F32_W; s += n; }
    return w;
}

/* ====== 比例宽文本（96/48/20；原生字形禁放大）====== */
static const ui_glyph_t *prop_find(const ui_glyph_t *tab, int n, unsigned int cp)
{
    for (int i = 0; i < n; i++)
        if (tab[i].cp == cp) return &tab[i];
    for (int i = 0; i < n; i++)   /* 兜底：数字表→'-'，ASCII 表→'?' */
        if (tab[i].cp == (n > 20 ? (uint32_t)'?' : (uint32_t)'-')) return &tab[i];
    return 0;
}

static int prop_text(const ui_glyph_t *tab, int n, int x, int y,
                     const char *s, uint16_t fg, uint16_t bg)
{
    int x0 = x;
    while (*s) {
        unsigned int cp; int nb = utf8_next(s, &cp);
        if (nb == 0) break;
        const ui_glyph_t *g = prop_find(tab, n, cp);
        if (g) {
            int gy = y + g->yoff;   /* 墨迹基线定位：'.'/'-' 落低位 */
            int stride = (g->w + 7) / 8;
            for (int row = 0; row < g->h; row++) {
                int col = 0;
                while (col < g->w) {
                    uint8_t bits = g->bits[row * stride + col / 8];
                    int bit = (bits & (0x80 >> (col & 7))) != 0;
                    int run = 1;
                    while (col + run < g->w) {
                        uint8_t b2 = g->bits[row * stride + (col + run) / 8];
                        if (((b2 >> (7 - ((col + run) & 7))) & 1) != bit) break;
                        run++;
                    }
                    if (bit)   /* 只画前景：擦底由调用方显式矩形负责 */
                        gfx_fill_rect(x + col, gy + row, x + col + run - 1, gy + row, fg);
                    col += run;
                }
            }
            x += g->adv;
        }
        s += nb;
    }
    return x - x0;
}

static int prop_w(const ui_glyph_t *tab, int n, const char *s)
{
    int w = 0;
    while (*s) {
        unsigned int cp; int nb = utf8_next(s, &cp);
        if (!nb) break;
        const ui_glyph_t *g = prop_find(tab, n, cp);
        w += g ? g->adv : 8;
        s += nb;
    }
    return w;
}

#define ui_text96(x, y, s, fg, bg) prop_text(tcs_font96, (int)TCS_FONT96_N, x, y, s, fg, bg)
#define ui_text96_w(s) prop_w(tcs_font96, (int)TCS_FONT96_N, s)
#define ui_text48(x, y, s, fg, bg) prop_text(tcs_font48, (int)TCS_FONT48_N, x, y, s, fg, bg)
#define ui_text48_w(s) prop_w(tcs_font48, (int)TCS_FONT48_N, s)
#define ui_text20(x, y, s, fg, bg) prop_text(tcs_font20, (int)TCS_FONT20_N, x, y, s, fg, bg)

/* 20px 辅助档输入护栏：非 ASCII → '?'（16px 档教训：中文进 ASCII 表变乱码）*/
static void ui_aux20(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    char buf[48];
    int i = 0;
    while (*s && i < (int)sizeof(buf) - 1) {
        unsigned int cp;
        int n = utf8_next(s, &cp);
        if (cp < 0x80) {
            buf[i++] = (char)cp;
        } else if (cp == 0xB0 && i < (int)sizeof(buf) - 2) {
            buf[i++] = (char)0xC2;   /* ° 按 UTF-8 双字节回填（单字节会被
                                        utf8_next 判成续字节→'?'）*/
            buf[i++] = (char)0xB0;
        } else {
            buf[i++] = '?';
        }
        s += n;
    }
    buf[i] = 0;
    ui_text20(x, y, buf, fg, bg);
}

/* ====== K3 渲染路径 ======
 * 整页期间组件内的 patch 调用必须降级为无操作——否则绘制目标中途切到
 * 陈旧显示缓冲，flip 后旧页内容现形（真机实测：主页边缘残留标定页）。*/
static int s_render_full;
static void ui_full_begin(void)  { s_render_full = 1; gfx_use_hidden_buffer(); }
volatile uint8_t g_stat_flip_src;   /* 翻转来源诊断：1=home 2=tune 3=qr 4=dlg 5=clear 6=text */
/* 不变量：flip 前目标缓冲必被整页重画（gfx_clear+内容），旧内容永不被读到——
 * 故切页后无需 sync_buffers 整帧拷贝（实测该 memcpy ~40ms，是全页预算超标的
 * 全部来源）。隐藏缓冲的陈旧内容只在 gfx_sync_rect 的"其他缓冲"一侧短暂存在，
 * 且必在下次 flip 前被整页重画覆盖。*/
static void ui_full_commit(void) { gfx_flip(); s_render_full = 0; }
/* WP-C nightly perf 门禁沿用 ui_ms 口径：整页耗时（含 sync memcpy）×10 */
static void ui_page_stat(uint32_t cyc_diff)
{
    g_stat_full_us_last = cyc_diff / (SystemCoreClock / 1000000u);
    g_stat_ui_ms_x10 = (uint16_t)(g_stat_full_us_last / 100u);
}
#define UI_FULL_COMMIT(src) do { g_stat_flip_src = (src); ui_full_commit(); } while (0)
static void ui_patch_begin(void) { if (!s_render_full) gfx_use_shown_buffer(); }
static void ui_patch_commit(int x0, int y0, int x1, int y1)
{
    if (!s_render_full) gfx_sync_rect(x0, y0, x1, y1);
}

/* ====== 性能统计（K3 预算表实测；probe-rs 读符号取证）====== */
volatile uint32_t g_stat_hero_us_max;    /* hero 数值区重绘耗时（预算 ≤3000µs）*/
volatile uint32_t g_stat_full_us_last;   /* 整页重绘耗时（预算 ≤25000µs）*/
volatile uint32_t g_stat_patch_cnt;      /* 小更新直绘次数 */
volatile uint32_t g_stat_touch_hz;       /* K12：真实触摸 IC 报点率（点/s，1s 窗口）*/
volatile uint32_t g_stat_flip_cnt;       /* flip 次数（闪屏指标：应仅切页/弹窗增长）*/

/* ====== 图元 ====== */
static void ui_round_rect(int x, int y, int w, int h, int r, uint16_t c, uint16_t bg)
{
    if (r * 2 > h) r = h / 2;
    gfx_fill_rect(x, y, x + w - 1, y + h - 1, c);
    for (int dy = 0; dy < r; dy++)
        for (int dx = 0; dx < r; dx++) {
            int cx = r - 1 - dx, cy = r - 1 - dy;
            if (cx * cx + cy * cy >= r * r) {
                gfx_draw_pixel(x + dx, y + dy, bg);
                gfx_draw_pixel(x + w - 1 - dx, y + dy, bg);
                gfx_draw_pixel(x + dx, y + h - 1 - dy, bg);
                gfx_draw_pixel(x + w - 1 - dx, y + h - 1 - dy, bg);
            }
        }
}

/* 卡片：底色 + 1px 描边 + 2px 底饰线，圆角 8 */
static void ui_card(int x, int y, int w, int h, uint16_t base, uint16_t border)
{
    ui_round_rect(x, y, w, h, 8, base, C_BG);
    gfx_draw_hline(x + 6, x + w - 7, y, border);
    gfx_draw_hline(x + 6, x + w - 7, y + h - 1, border);
    gfx_draw_vline(x, y + 6, y + h - 7, border);
    gfx_draw_vline(x + w - 1, y + 6, y + h - 7, border);
    gfx_fill_rect(x + 1, y + h - 3, x + w - 2, y + h - 2, C_CLINE);
}

static int ui_in(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* 按钮 style：0=主(白底蓝边) 1=禁用 2=危险(实红) 3=危险按住(暗红)
 *            4=按住(浅蓝底蓝边) 5=幽灵(白底灰边灰字)。fs=32/24 */
static void ui_btn(int x, int y, int w, int h, const char *label, int style, int fs)
{
    uint16_t bg, bd, tx;
    switch (style) {
    case 1: bg = C_CARD;  bd = C_BORDER; tx = C_GRAY;   break;
    case 2: bg = C_RED;   bd = C_RED;    tx = C_WHITE;  break;
    case 3: bg = C_REDDK; bd = C_REDDK;  tx = C_WHITE;  break;
    case 4: bg = C_PRESS; bd = C_BLUE;   tx = C_BLUE;   break;
    case 5: bg = C_CARD;  bd = C_BORDER; tx = C_TX2;    break;
    default: bg = C_CARD; bd = C_BLUE;   tx = C_BLUE;   break;
    }
    ui_round_rect(x, y, w, h, 8, bg, C_BG);
    gfx_draw_hline(x + 6, x + w - 7, y, bd);
    gfx_draw_hline(x + 6, x + w - 7, y + h - 1, bd);
    gfx_draw_vline(x, y + 6, y + h - 7, bd);
    gfx_draw_vline(x + w - 1, y + 6, y + h - 7, bd);
    int lw = (fs == 32) ? ui_text32_w(label) : ui_text24_w(label);
    int lx = x + (w - lw) / 2;
    if (fs == 32) ui_text32(lx, y + (h - 32) / 2, label, tx, bg);
    else          ui_text24(lx, y + (h - 24) / 2, label, tx, bg);
}

/* 圆点 8px（呼吸点/链路点/模式点共用）*/
static void ui_dot8(int cx, int cy, uint16_t c)
{
    for (int dy = -4; dy < 4; dy++)
        for (int dx = -4; dx < 4; dx++)
            if (dx * dx + dy * dy <= 12)
                gfx_draw_pixel(cx + dx, cy + dy, c);
}

/* ====== 页面/会话状态 ====== */
typedef enum { UI_PAGE_HOME = 0, UI_PAGE_TUNE, UI_PAGE_QR } ui_page_t;
typedef enum { DLG_NONE = 0, DLG_ESTOP, DLG_MODE_AUTO, DLG_SETCONFIRM, DLG_RESET } ui_dlg_t;

static ui_page_t s_page = UI_PAGE_HOME;
static ui_dlg_t  s_dlg = DLG_NONE;
static int  s_tune_page;               /* 0=核心4参 1=其余+恢复默认 */
static int  s_tune_sel;                /* 全局序号 0..6 */
static float s_tune_step_f = 0.1f;
static float s_pending_set;
static uint16_t s_pending_id;
static uint8_t  s_step_granted;

static int   s_touch_down;
static int   s_last_x, s_last_y;
static uint32_t s_press_t0;
static uint8_t  s_press_lp_fired;
static uint32_t s_last_lp_ms;
static uint32_t s_touch_release_ok;
static int s_prev_touched;
static uint8_t s_sl_cancelled;   /* 拖出取消后抑制本次抬手的 tap（取消≠点选）*/

/* 新鲜度：UI 自记最近变更时刻（param/control 零改动红线）*/
static uint32_t s_fresh_tc1, s_fresh_tc2, s_fresh_body;

/* 已保存角标（2s flush 防抖的 UI 镜像提示，亮 1s）*/
static uint32_t s_saved_apply_t;
static uint8_t  s_saved_toast_on;
static uint32_t s_saved_toast_t0;

/* ====== 滑条状态机（K4b）====== */
typedef enum { SL_IDLE = 0, SL_DRAG, SL_PENDING } sl_state_t;
static struct {
    sl_state_t st;
    float    val;            /* 拖动回显值 / 待生效值 */
    float    orig;           /* 取消回弹基准 */
    uint32_t t_release;
    uint32_t t_last_draw;
    uint16_t id;
} s_sl;

/* ====== 调参清单 ====== */
static int tune_items(int *ids, int max)
{
    int n = 0;
    for (int i = 0; i < param_count() && n < max; i++) {
        const param_desc_t *p = param_at(i);
        if (p->flags & PF_SCREEN) ids[n++] = p->id;
    }
    return n;
}

static const param_desc_t *tune_sel_desc(void)
{
    int ids[16]; int n = tune_items(ids, 16);
    if (s_tune_sel >= n) return 0;
    return param_find((uint16_t)ids[s_tune_sel]);
}

static void fmt_val(const param_desc_t *p, char *out, int cap)
{
    if (p->id == PARAM_ID_MODE)
        snprintf(out, cap, "%s", p->value != 0.0f ? "自动" : "手动");
    else if (p->id == PARAM_ID_COMMISSIONED)
        snprintf(out, cap, "%s", p->value == 1.0f ? "强制布防" :
                 p->value == 2.0f ? "强制不布防" : "自动");
    else
        snprintf(out, cap, "%.1f", (double)p->value);
}

/* ====== K10 报警动作建议文案表（每条报警一句"怎么办"）====== */
static int alarm_pick(uint16_t bits, const char **msg, const char **act, int *red)
{
    struct { uint16_t bit; const char *msg; const char *act; int red; } tab[] = {
        { ALM_ESTOP,      "急停已触发",     "已断电制动 · 点击横幅确认恢复",     1 },
        { ALM_OVERTEMP,   "水温过高",       "已停止加热 · 点击横幅确认恢复",     1 },
        { ALM_BODY_SENSOR,"体温传感器失联", "检查传感器接头 · 点击横幅确认恢复", 0 },
        { ALM_M207,       "M207 通信失联",  "检查通信线缆 · 点击横幅确认恢复",   0 },
        { ALM_EEPROM,     "配置校验失败",   "已恢复默认参数 · 请重新设定",       0 },
    };
    int extra = 0;
    *msg = 0; *act = 0; *red = 0;
    for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        if (bits & tab[i].bit) {
            if (!*msg) { *msg = tab[i].msg; *act = tab[i].act; *red = tab[i].red; }
            else extra++;
        }
    }
    return extra;
}

/* 横幅图标：■危险(红)/▲警告(黄)，程序化绘制（双编码 2/2）*/
static void draw_banner_icon(int x, int y, int red, uint16_t tx, uint16_t bg)
{
    if (red) {
        gfx_fill_rect(x, y, x + 19, y + 19, tx);
    } else {
        for (int r = 0; r < 20; r++) {
            int hw = (r + 2) * 11 / 20;   /* 半宽随行增长 */
            gfx_fill_rect(x + 10 - hw, y + r, x + 10 + hw, y + r, tx);
        }
    }
    (void)bg;
}

static void draw_banner_content(void)
{
    int bx = 208, by = 8, bw = 552, bh = 56;
    const char *msg, *act; int red;
    int extra = alarm_pick(g_tcs.alarm_bits, &msg, &act, &red);
    if (!msg) return;
    uint16_t bg = red ? C_ALMBG : C_WARNBG;
    uint16_t tx = red ? C_ALMTX : C_WARNTX;
    ui_round_rect(bx, by, bw, bh, 12, bg, C_BG);
    draw_banner_icon(bx + 18, by + 18, red, tx, bg);
    ui_text24(bx + 50, by + 4, msg, tx, bg);
    ui_text24(bx + 50, by + 30, act, tx, bg);
    if (extra > 0) {
        char cnt[24];
        snprintf(cnt, sizeof(cnt), "还有%d条", extra);
        int cw = ui_text24_w(cnt) + 16;
        int cx0 = bx + bw - cw - 12;
        gfx_draw_rect(cx0, by + 12, cx0 + cw - 1, by + 43, tx);
        ui_text24(cx0 + 8, by + 16, cnt, tx, bg);
    }
}

/* ✓/✗ 手编码 8x16（H6 沿用：16px 档无此字形，直接位图不经字库双源）*/
static const uint8_t s_glyph_ok[16] = {
    0,0,0,0, 0x06,0x06,0x0C,0x48,0x68,0x30,0x10,0x30, 0,0,0,0
};
static const uint8_t s_glyph_ng[16] = {
    0,0,0, 0x42,0x24,0x18,0x18,0x18,0x18,0x24,0x42, 0,0,0,0
};

/* ====== 顶栏内容（无 patch 包装；主页整页与 patch 两路复用）====== */
static void draw_post_badge(void);

static void topbar_content(void)
{
    gfx_fill_rect(0, 0, UI_W - 1, 87, C_BG);   /* 含横幅全高与卡前间隙 */

    /* 模式胶囊 (24,16,160,40)：白底描边 + 点(绿=自动/灰=手动) + 文字 */
    ui_round_rect(24, 16, 160, 40, 20, C_CARD, C_BG);
    gfx_draw_hline(30, 178, 16, C_BORDER);
    gfx_draw_hline(30, 178, 55, C_BORDER);
    gfx_draw_vline(24, 22, 50, C_BORDER);
    gfx_draw_vline(183, 22, 50, C_BORDER);
    int auto_m = (g_tcs.mode != 0);
    ui_dot8(52, 36, auto_m ? C_GREEN : C_GRAY);
    ui_text24(68, 24, auto_m ? "自动" : "手动", C_TX, C_CARD);

    if (g_tcs.alarm_bits) {
        draw_banner_content();   /* 报警时链路点并入横幅（隐藏）*/
    } else {
        ui_dot8(784, 26, g_tcs.m207_online ? C_GREEN : C_GRAY);
        ui_aux20(796, 18, "M207", C_TX2, C_BG);
        ui_dot8(856, 26, g_tcs.esp_link_ok ? C_GREEN : C_GRAY);
        ui_aux20(868, 18, "ESP", C_TX2, C_BG);
    }

    /* 急停小图标常驻 (940,20,24,24)：灰描边/急停红实心（非触摸）*/
    if (g_tcs.alarm_bits & ALM_ESTOP) {
        gfx_fill_rect(942, 22, 963, 43, C_RED);
        gfx_fill_rect(948, 28, 957, 37, C_WHITE);
    } else {
        gfx_draw_rect(942, 22, 963, 43, C_GRAY);
        gfx_fill_rect(948, 28, 957, 37, C_GRAY);
    }

    if (post_badge_state() == 1) draw_post_badge();
}

static void draw_post_badge(void)
{
    ui_round_rect(128, 0, 640, 40, 8, C_CARD, C_BG);
    gfx_draw_rect(128, 0, 767, 39, C_BORDER);
    static const char *labels[POST_ITEM_N] = {
        "TMP117", "M207", "EEPR", "TABLE", "SCR", "VER"
    };
    int x = 152;
    for (int i = 0; i < POST_ITEM_N; i++) {
        uint8_t ok = post_item_ok((uint8_t)i);
        const uint8_t *g = ok ? s_glyph_ok : s_glyph_ng;
        for (int row = 0; row < 16; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; col++)
                gfx_draw_pixel(x + col, 12 + row,
                               (bits & (0x80 >> col)) ? (ok ? C_GREEN : C_WARNTX) : C_CARD);
        }
        ui_aux20(x + 12, 12, labels[i], C_TX, C_CARD);
        x += 104;
    }
}

static void draw_topbar(void)
{
    ui_patch_begin();
    topbar_content();
    /* 横幅 8..64 越过顶栏条，同步区下沿取 87（卡片起始前一行）*/
    ui_patch_commit(0, 0, UI_W - 1, 87);
    g_stat_patch_cnt++;
}

/* ====== hero 卡 ====== */
static void draw_tc_value(int cx, int ch)
{
    uint32_t t0 = DWT->CYCCNT;
    char big[16];
    int lost = !g_tcs.m207_online;
    float act = ch ? g_tcs.tc2_act : g_tcs.tc1_act;
    if (lost) snprintf(big, sizeof(big), "--.-");
    else      snprintf(big, sizeof(big), "%.1f", (double)act);
    int nx = cx + 24, ny = 88 + 72;
    /* 擦除/同步区按实际字宽收窄（K3 预算：≤3ms；全卡宽版实测 4.07ms 超标）*/
    int tw = ui_text96_w(big);
    gfx_fill_rect(nx - 4, ny, nx + tw + 64, ny + 106, C_CARD);
    ui_text96(nx, ny, big, lost ? C_GRAY : C_HERO, C_CARD);
    ui_text32(nx + tw + 8, ny + 64, "°C", C_TX2, C_CARD);
    int lit = (millis() - (ch ? s_fresh_tc2 : s_fresh_tc1)) <= 1000u;
    /* 呼吸点跟随字尾（1.4"数值旁"），与数值同一同步区——避免跨区二次 sync */
    ui_dot8(nx + tw + 44, ny + 8, (lit && !lost) ? C_GREEN : C_GRAY);
    ui_patch_commit(nx - 4, ny, nx + tw + 64, ny + 106);
    uint32_t us = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000000u);
    if (us > g_stat_hero_us_max) g_stat_hero_us_max = us;
    g_stat_patch_cnt++;
}

static void draw_tc_setline(int cx, int ch)
{
    float setv = param_get(ch ? PARAM_ID_TC2_SET : PARAM_ID_TC1_SET);
    int sx = cx + 24, sy = 88 + 184;
    gfx_fill_rect(sx - 4, sy - 8, cx + 444, sy + 30, C_CARD);
    ui_text24(sx, sy, "设定", C_TX2, C_CARD);
    char vs[16];
    snprintf(vs, sizeof(vs), "%.1f", (double)setv);
    ui_text32(sx + 56, sy - 4, vs, C_TX, C_CARD);
    ui_text32(sx + 56 + ui_text32_w(vs) + 4, sy - 4, "°C", C_TX2, C_CARD);
    ui_patch_commit(sx - 4, sy - 8, cx + 444, sy + 30);
    g_stat_patch_cnt++;
}

static void draw_tc_card(int cx, const char *title, int ch)
{
    ui_patch_begin();
    ui_card(cx, 88, 460, 240, C_CARD, C_BORDER);
    ui_text32(cx + 24, 88 + 20, title, C_TX, C_CARD);
    {
        int on = ch ? g_tcs.tec2_on : g_tcs.tec1_on;
        uint16_t bg = on ? C_GREEN : C_GRAY;
        ui_round_rect(cx + 348, 88 + 24, 88, 40, 20, bg, C_CARD);
        ui_text24(cx + 356, 88 + 32, on ? "●运行" : "○停止", C_WHITE, bg);
    }
    ui_patch_commit(cx, 88, cx + 460, 88 + 240);   /* 卡壳/标题/胶囊同步 */
    draw_tc_value(cx, ch);
    draw_tc_setline(cx, ch);
    g_stat_patch_cnt++;
}

/* ====== 体温卡 ====== */
static void draw_body_card(void)
{
    ui_patch_begin();
    ui_card(32, 352, 460, 96, C_CARD, g_tcs.body_sensor_ok ? C_BORDER : C_YELLOW);
    ui_text24(56, 364, "体温", C_TX, C_CARD);
    char big[16];
    if (g_tcs.body_sensor_ok) snprintf(big, sizeof(big), "%.1f", (double)g_tcs.body_temp);
    else                      snprintf(big, sizeof(big), "--.-");
    gfx_fill_rect(140, 368, 380, 432, C_CARD);
    ui_text48(152, 372, big, g_tcs.body_sensor_ok ? C_HERO : C_GRAY, C_CARD);
    ui_aux20(152 + ui_text48_w(big) + 6, 400, "°C", C_TX2, C_CARD);
    int lit = (millis() - s_fresh_body) <= 1000u;
    ui_dot8(328, 392, (lit && g_tcs.body_sensor_ok) ? C_GREEN : C_GRAY);
    if (!g_tcs.body_sensor_ok) {
        ui_round_rect(380, 380, 88, 40, 20, C_WARNBG, C_CARD);
        ui_text24(392, 388, "▲失联", C_WARNTX, C_WARNBG);
    }
    ui_patch_commit(32, 352, 492, 448);
    g_stat_patch_cnt++;
}

/* ====== 风扇卡（128×64 轨道 + 64px 球）====== */
static void draw_fan_card_hl(int hl)
{
    uint16_t base = hl ? C_PRESS : C_CARD;
    ui_patch_begin();
    ui_card(532, 352, 460, 96, base, C_BORDER);
    ui_text24(556, 364, "风扇", C_TX, base);
    ui_text24(556, 402, g_tcs.fan_on ? "运行中" : "已停止", C_TX2, base);
    int tx0 = 840, ty0 = 368;
    ui_round_rect(tx0, ty0, 128, 64, 32, g_tcs.fan_on ? C_GREEN : C_GRAY, base);
    int bx = tx0 + (g_tcs.fan_on ? 64 : 0);
    ui_round_rect(bx, ty0, 64, 64, 32, C_WHITE, g_tcs.fan_on ? C_GREEN : C_GRAY);
    ui_text24(bx + 20, ty0 + 20, g_tcs.fan_on ? "开" : "关", C_TX, C_WHITE);
    ui_patch_commit(532, 352, 992, 448);
    g_stat_patch_cnt++;
}

/* ====== 底栏 ====== */
static void draw_dock_content(void)
{
    int auto_m = (g_tcs.mode != 0);
    ui_btn(40, 472, 208, 96, "调参", 0, 32);
    ui_btn(272, 472, 208, 96, auto_m ? "切手动" : "切自动", 0, 32);
    ui_btn(504, 472, 208, 96, "连接", 0, 32);
    ui_btn(768, 472, 216, 112, "急停", 2, 32);
}
static void draw_dock(void)
{
    ui_patch_begin();
    draw_dock_content();
    ui_patch_commit(40, 472, 984, 584);
    g_stat_patch_cnt++;
}

/* ====== 主页整页 ====== */
static void draw_home(void)
{
    uint32_t t0 = DWT->CYCCNT;
    s_page = UI_PAGE_HOME;
    ui_full_begin();
    gfx_clear(C_BG);
    topbar_content();
    draw_tc_card(32, "贴身水箱", 0);
    draw_tc_card(532, "环境水箱", 1);
    draw_body_card();
    draw_fan_card_hl(0);
    draw_dock_content();
    ui_full_commit();
    g_stat_full_us_last = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000000u);
    g_stat_flip_cnt++;
}

/* ====== 滑条（K4b）====== */
static int tune_row_y(int page_idx)
{
    return UI_TUNE_ROW_Y0 + page_idx * (UI_TUNE_ROW_H + UI_TUNE_ROW_GAP);
}

static float slider_px2val(const param_desc_t *p, int px)
{
    int tx0 = UI_TUNE_ROW_X + 320, tw = UI_ROW_TRACK_W;
    float frac = (float)(px - (tx0 + 40)) / (float)(tw - 80);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    float v = p->min + frac * (p->max - p->min);
    float step = (p->step > 0.0f) ? p->step : 0.1f;
    v = p->min + (float)((int)((v - p->min) / step + 0.5f)) * step;
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    return v;
}

/* 滑条区重绘；drag=1 拇指内嵌实时值（跟手回显，20Hz 外部限幅）*/
static void draw_slider(int row_y, const param_desc_t *p, float v, int drag,
                        int selected, uint16_t row_bg)
{
    ui_patch_begin();
    int tx0 = UI_TUNE_ROW_X + 320, ty0 = row_y + 36, tw = UI_ROW_TRACK_W, th = 16;
    /* 拖动时回显=行值区实时刷新：80px 拇指容不下 24px×4 字（比例字体才塞得下），
     * 改刷行值区（与 K4b"拖动跟手回显"等效且更大更清晰）*/
    if (drag) {
        char vs[24];
        if (p->id == PARAM_ID_MODE)
            snprintf(vs, sizeof(vs), "%s", v != 0.0f ? "自动" : "手动");
        else if (p->id == PARAM_ID_COMMISSIONED)
            snprintf(vs, sizeof(vs), "%s", v == 2.0f ? "强制不布防" :
                     v == 1.0f ? "强制布防" : "自动");
        else
            snprintf(vs, sizeof(vs), "%.1f", (double)v);
        gfx_fill_rect(UI_TUNE_ROW_X + 20, row_y + 42, UI_TUNE_ROW_X + 200,
                      row_y + 80, row_bg);
        ui_text32(UI_TUNE_ROW_X + 24, row_y + 44, vs, C_HERO, row_bg);
    }
    gfx_fill_rect(tx0 - 4, row_y + 2, tx0 + tw + 4, row_y + UI_TUNE_ROW_H - 2, row_bg);
    float frac = (p->max > p->min) ? (v - p->min) / (p->max - p->min) : 0.0f;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    gfx_fill_rect(tx0, ty0, tx0 + tw - 1, ty0 + th - 1, C_TRACK);
    if (frac > 0.01f)
        gfx_fill_rect(tx0, ty0, tx0 + (int)(frac * (float)(tw - 1)), ty0 + th - 1,
                      C_TRACKFILL);
    int bx = tx0 + 40 + (int)(frac * (float)(tw - 80)) - 40;
    int by = ty0 + th / 2 - 40;
    uint16_t ball = selected ? C_BLUE : C_GRAY;
    ui_round_rect(bx, by, 80, 80, 40, ball, row_bg);
    ui_patch_commit(UI_TUNE_ROW_X + 16, row_y + 2, tx0 + tw + 4,
                    row_y + UI_TUNE_ROW_H - 2);
    g_stat_patch_cnt++;
}

/* ====== 调参页 ====== */
static void draw_prow(int page_idx, int glob_idx, uint16_t id)
{
    int y = tune_row_y(page_idx);
    int sel = (glob_idx == s_tune_sel);
    uint16_t bg = sel ? C_PRESS : C_CARD;
    uint16_t bd = sel ? C_BLUE : C_BORDER;
    ui_card(UI_TUNE_ROW_X, y, UI_TUNE_ROW_W, UI_TUNE_ROW_H, bg, bd);
    if (sel) gfx_fill_rect(UI_TUNE_ROW_X + 1, y + 1, UI_TUNE_ROW_X + 4,
                           y + UI_TUNE_ROW_H - 4, C_BLUE);
    const param_desc_t *p = param_find(id);
    if (!p) return;
    ui_text24(UI_TUNE_ROW_X + 24, y + 12, p->name, C_TX, bg);
    char vs[24];
    fmt_val(p, vs, sizeof(vs));
    ui_text32(UI_TUNE_ROW_X + 24, y + 44, vs, C_HERO, bg);
    if (p->unit[0] && p->id != PARAM_ID_MODE && p->id != PARAM_ID_COMMISSIONED)
        ui_aux20(UI_TUNE_ROW_X + 24 + ui_text32_w(vs) + 4, y + 52, "°C", C_TX2, bg);
    draw_slider(y, p, p->value, 0, sel, bg);
    ui_btn(UI_TUNE_ROW_X + 792, y + 12, 64, 64, "-", sel ? 0 : 1, 32);
    ui_btn(UI_TUNE_ROW_X + 872, y + 12, 64, 64, "+", sel ? 0 : 1, 32);
}

static void draw_tune_topbar(void)
{
    ui_btn(24, 8, 120, 40, "‹返回", 5, 24);
    const param_desc_t *p = tune_sel_desc();
    char ctx[80];
    if (p) {
        if (p->id == PARAM_ID_MODE)
            snprintf(ctx, sizeof(ctx), "选中：%s · 手动/自动", p->name);
        else if (p->id == PARAM_ID_COMMISSIONED)
            snprintf(ctx, sizeof(ctx), "选中：%s · 自动/强制布防/强制不布防", p->name);
        else
            snprintf(ctx, sizeof(ctx), "选中：%s · 范围 %.1f~%.1f°C",
                     p->name, (double)p->min, (double)p->max);
    } else {
        snprintf(ctx, sizeof(ctx), "调参");
    }
    ui_text24(176, 22, ctx, C_TX2, C_BG);
    uint32_t t = millis() / 1000u;
    char ts[12];
    snprintf(ts, sizeof(ts), "%02lu:%02lu:%02lu",
             (unsigned long)(t / 3600u), (unsigned long)((t / 60u) % 60u),
             (unsigned long)(t % 60u));
    ui_aux20(896, 24, ts, C_TX2, C_BG);
    if (s_saved_toast_on) {
        ui_round_rect(716, 8, 168, 40, 8, C_PRESS, C_BG);
        gfx_draw_rect(716, 8, 883, 47, C_BLUE);
        ui_text24(764, 16, "已保存", C_BLUE, C_PRESS);
    }
}

static void tune_zone_content(void)
{
    ui_btn(32, 472, 96, 96, "-", 0, 32);
    ui_btn(896, 472, 96, 96, "+", 0, 32);
    char step_l[24];
    snprintf(step_l, sizeof(step_l), "步进%.1f", (double)s_tune_step_f);
    ui_btn(160, 488, 180, 64, step_l, 0, 24);
    if (s_tune_page == 0) {
        ui_btn(360, 488, 240, 64, "下一页›", 0, 24);
    } else {
        ui_btn(360, 488, 200, 64, "‹上一页", 0, 24);
        ui_btn(580, 488, 240, 64, "恢复默认", 5, 24);
        ui_text24(56, 404, "拖动滑条或点 ± 调整选中参数 · 实时生效 · 断电自动保持",
                  C_TX2, C_BG);
    }
}

static void draw_tune(void)
{
    uint32_t t0 = DWT->CYCCNT;
    s_page = UI_PAGE_TUNE;
    int ids[16];
    int n = tune_items(ids, 16);
    ui_full_begin();
    gfx_clear(C_BG);
    draw_tune_topbar();
    int base = s_tune_page * UI_TUNE_ROW_N;
    for (int i = 0; i < UI_TUNE_ROW_N && base + i < n; i++)
        draw_prow(i, base + i, (uint16_t)ids[base + i]);
    tune_zone_content();
    UI_FULL_COMMIT(2);
    ui_page_stat(DWT->CYCCNT - t0);
    g_stat_flip_cnt++;
}

/* 选中行迁移/值变更后的局部刷新（零 flip）*/
static void tune_refresh_rows(void)
{
    int ids[16]; int n = tune_items(ids, 16);
    int base = s_tune_page * UI_TUNE_ROW_N;
    for (int i = 0; i < UI_TUNE_ROW_N && base + i < n; i++)
        draw_prow(i, base + i, (uint16_t)ids[base + i]);
    ui_patch_begin();
    gfx_fill_rect(168, 8, 884, 51, C_BG);   /* 时钟(896)前全清，防长短句残尾 */
    draw_tune_topbar();
    ui_patch_commit(168, 8, 1015, 51);
    g_stat_patch_cnt++;
}

/* ====== 连接页 ====== */
#include "qr/qrcodegen.h"
static void draw_qr(void)
{
    uint32_t t0 = DWT->CYCCNT;
    s_page = UI_PAGE_QR;
    ui_full_begin();
    gfx_clear(C_BG);
    ui_text32(32, 16, "连接", C_TX, C_BG);

    ui_round_rect(392, 88, 240, 240, 8, C_WHITE, C_BG);
    uint8_t qrc[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    bool ok = qrcodegen_encodeText("http://192.168.4.1", tmp, qrc,
                                   qrcodegen_Ecc_MEDIUM, 2, 6, -1, true);
    if (ok) {
        int nmod = qrcodegen_getSize(qrc);
        int mod = 200 / nmod;
        if (mod < 1) mod = 1;
        int x0 = 392 + (240 - nmod * mod) / 2;
        int y0 = 88 + (240 - nmod * mod) / 2;
        for (int y = 0; y < nmod; y++)
            for (int x = 0; x < nmod; x++)
                if (qrcodegen_getModule(qrc, x, y))
                    gfx_fill_rect(x0 + x * mod, y0 + y * mod,
                                  x0 + x * mod + mod - 1, y0 + y * mod + mod - 1, C_TX);
    } else {
        ui_text24(416, 200, "二维码生成失败", C_RED, C_WHITE);
    }

    ui_text24(292, 352, "热点名称", C_TX2, C_BG);
    ui_aux20(412, 356, "TCS-XXXX", C_TX, C_BG);
    ui_text24(292, 392, "热点密码", C_TX2, C_BG);
    ui_aux20(412, 396, "12345678", C_TX, C_BG);
    ui_text24(292, 432, "控制页", C_TX2, C_BG);
    ui_aux20(412, 436, "http://192.168.4.1", C_TX, C_BG);

    char fw[24];
    snprintf(fw, sizeof(fw), "fw v%lu.%lu.%lu",
             (unsigned long)(g_tcs.fw_ver >> 16) & 0xFFu,
             (unsigned long)(g_tcs.fw_ver >> 8) & 0xFFu,
             (unsigned long)(g_tcs.fw_ver) & 0xFFu);
    ui_aux20(896, 520, fw, C_TX2, C_BG);

    ui_btn(40, 472, 208, 96, "返回", 0, 32);
    UI_FULL_COMMIT(3);
    ui_page_stat(DWT->CYCCNT - t0);
    g_stat_flip_cnt++;
}

/* ====== 弹窗 ====== */
static void dlg_labels(const char **title, const char **body, const char **okl, int *ok_red)
{
    static char tbuf[48];
    *ok_red = 1;
    switch (s_dlg) {
    case DLG_ESTOP:
        *title = "确认急停？"; *body = "TEC 将全部停止，风扇全开。"; *okl = "确认急停"; break;
    case DLG_MODE_AUTO:
        *title = "切换到自动模式？"; *body = "自动模式按体温闭环控制制冷。";
        *okl = "确认切换"; *ok_red = 0; break;
    case DLG_SETCONFIRM:
        snprintf(tbuf, sizeof(tbuf), "设定 %.1f°C 超过确认阈值", (double)s_pending_set);
        *title = tbuf; *body = "高温设定需二次确认，确认后本会话放行。";
        *okl = "确认设定"; break;
    case DLG_RESET:
        *title = "恢复默认参数？"; *body = "全部参数将回到出厂值，立即生效。";
        *okl = "恢复默认"; break;
    default: *title = ""; *body = ""; *okl = "确认"; break;
    }
}

/* 弹窗底页内容（整页重画到当前缓冲，不 commit）*/
static void draw_underlay(void)
{
    if (s_page == UI_PAGE_TUNE) {
        int ids[16];
        int n = tune_items(ids, 16);
        gfx_clear(C_BG);
        draw_tune_topbar();
        int base = s_tune_page * UI_TUNE_ROW_N;
        for (int i = 0; i < UI_TUNE_ROW_N && base + i < n; i++)
            draw_prow(i, base + i, (uint16_t)ids[base + i]);
        tune_zone_content();
    } else {
        gfx_clear(C_BG);
        topbar_content();
        draw_tc_card(32, "贴身水箱", 0);
        draw_tc_card(532, "环境水箱", 1);
        draw_body_card();
        draw_fan_card_hl(0);
        draw_dock_content();
    }
}

static void draw_dialog(void)
{
    ui_full_begin();          /* 底页整画到隐藏缓冲，杜绝陈旧内容 */
    draw_underlay();

    for (int y = 0; y < UI_H; y += 3)               /* 3px 点阵遮罩 */
        for (int x = 0; x < UI_W; x += 3)
            gfx_draw_pixel(x, y, C_BORDER);

    ui_round_rect(256, 136, 512, 320, 12, C_CARD, C_BG);
    gfx_draw_hline(262, 762, 136, C_BORDER);
    gfx_draw_hline(262, 762, 455, C_BORDER);
    gfx_draw_vline(256, 142, 449, C_BORDER);
    gfx_draw_vline(767, 142, 449, C_BORDER);
    gfx_fill_rect(257, 453, 766, 454, C_CLINE);

    const char *title, *body, *okl; int ok_red;
    dlg_labels(&title, &body, &okl, &ok_red);
    ui_text32(296, 168, title, ok_red ? C_RED : C_TX, C_CARD);
    ui_text24(296, 232, body, C_TX, C_CARD);

    int oy = 344;
    if (ok_red) {
        ui_round_rect(296, oy, 192, 88, 8, C_RED, C_CARD);
        ui_text24(296 + (192 - ui_text24_w(okl)) / 2, oy + 32, okl, C_WHITE, C_RED);
    } else {
        ui_round_rect(296, oy, 192, 88, 8, C_BLUE, C_CARD);
        ui_text24(296 + (192 - ui_text24_w(okl)) / 2, oy + 32, okl, C_WHITE, C_BLUE);
    }
    ui_round_rect(536, oy, 192, 88, 8, C_CARD, C_CARD);
    gfx_draw_hline(542, 722, oy, C_BLUE);
    gfx_draw_hline(542, 722, oy + 87, C_BLUE);
    gfx_draw_vline(536, oy + 6, oy + 81, C_BLUE);
    gfx_draw_vline(727, oy + 6, oy + 81, C_BLUE);
    ui_text24(536 + (192 - 48) / 2, oy + 32, "取消", C_BLUE, C_CARD);

    UI_FULL_COMMIT(4);
    g_stat_flip_cnt++;
}

/* ====== 动作路径 ====== */
static void slider_cancel_pending(void)
{
    s_sl.st = SL_IDLE;
    s_saved_apply_t = 0;
}

static void apply_param(uint16_t id, float v)
{
    const param_desc_t *p = param_find(id);
    if (!p) return;
    if ((id == PARAM_ID_TC1_SET || id == PARAM_ID_TC2_SET)
        && v > param_get(PARAM_ID_SET_CONFIRM) && !s_step_granted) {
        s_pending_id = id;
        s_pending_set = v;
        s_dlg = DLG_SETCONFIRM;
        draw_dialog();
        return;
    }
    (void)param_set(id, v, PARAM_SRC_TOUCH);
    if ((id == PARAM_ID_TC1_SET || id == PARAM_ID_TC2_SET)
        && v <= param_get(PARAM_ID_SET_CONFIRM)) s_step_granted = 0;
    s_saved_apply_t = millis();
}

static void tune_apply_selected(float v)
{
    const param_desc_t *p = tune_sel_desc();
    if (!p) return;
    apply_param(p->id, v);
    if (s_dlg == DLG_NONE) {
        int ids[16]; tune_items(ids, 16);
        draw_prow(s_tune_sel - s_tune_page * UI_TUNE_ROW_N, s_tune_sel,
                  (uint16_t)ids[s_tune_sel]);
    }
}

static void tune_step(float dir)
{
    slider_cancel_pending();
    const param_desc_t *p = tune_sel_desc();
    if (!p) return;
    float v = p->value + dir * s_tune_step_f;
    float step = (p->step > 0.0f) ? p->step : 0.1f;
    v = p->min + (float)((int)((v - p->min) / step + 0.5f)) * step;
    if (v < p->min) v = p->min;
    if (v > p->max) v = p->max;
    tune_apply_selected(v);
}

/* ====== 按住态（patch 直绘，精确矩形区同步）====== */
typedef enum {
    HL_NONE = 0,
    HL_DOCK_TUNE, HL_DOCK_MODE, HL_DOCK_CONN, HL_DOCK_ESTOP,
    HL_FAN,
    HL_BACK, HL_ROW_KEYM, HL_ROW_KEYP, HL_STEP, HL_NEXT, HL_PREV, HL_RESET,
    HL_ZM, HL_ZP,
    HL_CONN_BACK, HL_DLG_OK, HL_DLG_CANCEL,
} hl_t;
static hl_t s_press_hl;

static hl_t press_hit_test(int x, int y)
{
    if (s_dlg != DLG_NONE) {
        if (ui_in(x, y, 296, 344, 192, 88)) return HL_DLG_OK;
        if (ui_in(x, y, 536, 344, 192, 88)) return HL_DLG_CANCEL;
        return HL_NONE;
    }
    if (s_page == UI_PAGE_HOME) {
        if (ui_in(x, y, R_DOCK_TUNE))  return HL_DOCK_TUNE;
        if (ui_in(x, y, R_DOCK_MODE))  return HL_DOCK_MODE;
        if (ui_in(x, y, R_DOCK_CONN))  return HL_DOCK_CONN;
        if (ui_in(x, y, R_DOCK_ESTOP)) return HL_DOCK_ESTOP;
        if (ui_in(x, y, 532, 352, 460, 96)) return HL_FAN;
    } else if (s_page == UI_PAGE_QR) {
        if (ui_in(x, y, R_CONN_BACK)) return HL_CONN_BACK;
    } else {
        int base = s_tune_page * UI_TUNE_ROW_N;
        int ry = tune_row_y(s_tune_sel - base);
        if (ui_in(x, y, UI_TUNE_ROW_X + 792, ry + 8, 64, 64)) return HL_ROW_KEYM;
        if (ui_in(x, y, UI_TUNE_ROW_X + 872, ry + 8, 64, 64)) return HL_ROW_KEYP;
        if (ui_in(x, y, R_TUNE_STEP)) return HL_STEP;
        if (s_tune_page == 0 && ui_in(x, y, R_TUNE_NEXT)) return HL_NEXT;
        if (s_tune_page == 1 && ui_in(x, y, R_TUNE_PREV))  return HL_PREV;
        if (s_tune_page == 1 && ui_in(x, y, R_TUNE_RESET)) return HL_RESET;
        if (ui_in(x, y, R_TUNE_ZONE_MINUS)) return HL_ZM;
        if (ui_in(x, y, R_TUNE_ZONE_PLUS))  return HL_ZP;
        if (ui_in(x, y, UI_BACK_HIT)) return HL_BACK;
    }
    return HL_NONE;
}

static void hl_rect(hl_t h, int *x0, int *y0, int *x1, int *y1)
{
    switch (h) {
    case HL_DOCK_TUNE:  *x0 = 40;  *y0 = 472; *x1 = 247; *y1 = 567; break;
    case HL_DOCK_MODE:  *x0 = 272; *y0 = 472; *x1 = 479; *y1 = 567; break;
    case HL_DOCK_CONN:  *x0 = 504; *y0 = 472; *x1 = 711; *y1 = 567; break;
    case HL_DOCK_ESTOP: *x0 = 768; *y0 = 472; *x1 = 983; *y1 = 583; break;
    case HL_FAN:        *x0 = 532; *y0 = 352; *x1 = 991; *y1 = 447; break;
    case HL_CONN_BACK:  *x0 = 40;  *y0 = 472; *x1 = 247; *y1 = 567; break;
    case HL_BACK:       *x0 = 24;  *y0 = 8;   *x1 = 143; *y1 = 47;  break;
    case HL_STEP:       *x0 = 160; *y0 = 488; *x1 = 339; *y1 = 551; break;
    case HL_NEXT:       *x0 = 356; *y0 = 488; *x1 = 595; *y1 = 551; break;
    case HL_PREV:       *x0 = 356; *y0 = 488; *x1 = 555; *y1 = 551; break;
    case HL_RESET:      *x0 = 580; *y0 = 488; *x1 = 819; *y1 = 551; break;
    case HL_ZM:         *x0 = 32;  *y0 = 472; *x1 = 127; *y1 = 567; break;
    case HL_ZP:         *x0 = 896; *y0 = 472; *x1 = 991; *y1 = 567; break;
    case HL_DLG_OK:     *x0 = 296; *y0 = 344; *x1 = 487; *y1 = 431; break;
    case HL_DLG_CANCEL: *x0 = 536; *y0 = 344; *x1 = 727; *y1 = 431; break;
    default: {
        int base = s_tune_page * UI_TUNE_ROW_N;
        int ry = tune_row_y(s_tune_sel - base);
        if (h == HL_ROW_KEYM) { *x0 = UI_TUNE_ROW_X + 792; *y0 = ry + 8; *x1 = *x0 + 63; *y1 = ry + 75; }
        else                  { *x0 = UI_TUNE_ROW_X + 872; *y0 = ry + 8; *x1 = *x0 + 63; *y1 = ry + 75; }
        break;
    }
    }
}

static void press_hl_draw(int down)
{
    int auto_m = (g_tcs.mode != 0);
    int x0, y0, x1, y1;
    hl_rect(s_press_hl, &x0, &y0, &x1, &y1);
    ui_patch_begin();
    switch (s_press_hl) {
    case HL_DOCK_TUNE:  ui_btn(40, 472, 208, 96, "调参", down ? 4 : 0, 32); break;
    case HL_DOCK_MODE:  ui_btn(272, 472, 208, 96, auto_m ? "切手动" : "切自动",
                               down ? 4 : 0, 32); break;
    case HL_DOCK_CONN:  ui_btn(504, 472, 208, 96, "连接", down ? 4 : 0, 32); break;
    case HL_DOCK_ESTOP: ui_btn(768, 472, 216, 112, "急停", down ? 3 : 2, 32); break;
    case HL_FAN:        draw_fan_card_hl(down); break;
    case HL_CONN_BACK:  ui_btn(40, 472, 208, 96, "返回", down ? 4 : 0, 32); break;
    case HL_BACK:       ui_btn(24, 8, 120, 40, "‹返回", down ? 4 : 5, 24); break;
    case HL_STEP: {
        char step_l[24];
        snprintf(step_l, sizeof(step_l), "步进%.1f", (double)s_tune_step_f);
        ui_btn(160, 488, 180, 64, step_l, down ? 4 : 0, 24);
        break;
    }
    case HL_NEXT:       ui_btn(360, 488, 240, 64, "下一页›", down ? 4 : 0, 24); break;
    case HL_PREV:       ui_btn(360, 488, 200, 64, "‹上一页", down ? 4 : 0, 24); break;
    case HL_RESET:      ui_btn(580, 488, 240, 64, "恢复默认", down ? 4 : 5, 24); break;
    case HL_ZM:         ui_btn(32, 472, 96, 96, "-", down ? 4 : 0, 32); break;
    case HL_ZP:         ui_btn(896, 472, 96, 96, "+", down ? 4 : 0, 32); break;
    case HL_ROW_KEYM:
    case HL_ROW_KEYP: {
        int base = s_tune_page * UI_TUNE_ROW_N;
        int ry = tune_row_y(s_tune_sel - base);
        int kx = (s_press_hl == HL_ROW_KEYM) ? UI_TUNE_ROW_X + 792 : UI_TUNE_ROW_X + 872;
        ui_btn(kx, ry + 8, 64, 64, (s_press_hl == HL_ROW_KEYM) ? "-" : "+",
               down ? 4 : 0, 32);
        break;
    }
    case HL_DLG_OK: {
        const char *title, *body, *okl; int ok_red;
        dlg_labels(&title, &body, &okl, &ok_red);
        if (down) {
            ui_round_rect(296, 344, 192, 88, 8, C_PRESS, C_CARD);
            ui_text24(296 + (192 - ui_text24_w(okl)) / 2, 376, okl, C_BLUE, C_PRESS);
        } else {
            ui_round_rect(296, 344, 192, 88, 8, ok_red ? C_RED : C_BLUE, C_CARD);
            ui_text24(296 + (192 - ui_text24_w(okl)) / 2, 376, okl, C_WHITE,
                      ok_red ? C_RED : C_BLUE);
        }
        break;
    }
    case HL_DLG_CANCEL:
        if (down) {
            ui_round_rect(536, 344, 192, 88, 8, C_PRESS, C_CARD);
            ui_text24(584, 376, "取消", C_BLUE, C_PRESS);
        } else {
            ui_round_rect(536, 344, 192, 88, 8, C_CARD, C_CARD);
            gfx_draw_hline(542, 722, 344, C_BLUE);
            gfx_draw_hline(542, 722, 431, C_BLUE);
            gfx_draw_vline(536, 350, 425, C_BLUE);
            gfx_draw_vline(727, 350, 425, C_BLUE);
            ui_text24(584, 376, "取消", C_BLUE, C_CARD);
        }
        break;
    default: break;
    }
    ui_patch_commit(x0 - 2, y0 - 2, x1 + 2, y1 + 2);
    g_stat_patch_cnt++;
}

static void press_hl_update(int down_now, int x, int y)
{
    static int hl_active;
    if (down_now) {
        hl_t h = press_hit_test(x, y);
        if (h != HL_NONE && h != s_press_hl) {
            s_press_hl = h;
            hl_active = 1;
            press_hl_draw(1);
        }
    } else if (hl_active) {
        hl_active = 0;
        hl_t h = s_press_hl;
        s_press_hl = HL_NONE;
        press_hl_draw(0);
        (void)h;
    }
}

/* ====== 触摸分发 ====== */
static volatile uint32_t s_tap_cnt;

static void tune_select(int page_idx)
{
    int ids[16]; int n = tune_items(ids, 16);
    int gi = s_tune_page * UI_TUNE_ROW_N + page_idx;
    if (gi >= n) return;
    if (s_tune_sel != gi) {
        s_tune_sel = gi;
        s_step_granted = 0;
        slider_cancel_pending();
        tcs_log("[APP]", "touch sel %s", param_find((uint16_t)ids[gi])->key);
        tune_refresh_rows();
    }
}

static void tune_goto_page(int pg)
{
    slider_cancel_pending();
    s_tune_page = pg;
    s_tune_sel = pg * UI_TUNE_ROW_N;
    s_step_granted = 0;
    tcs_log("[APP]", "tune page %d", pg);
    draw_tune();
}

static void touch_tap(int x, int y)
{
    s_tap_cnt++;

    if (s_dlg != DLG_NONE) {
        if (ui_in(x, y, 296, 344, 192, 88)) {
            ui_dlg_t d = s_dlg;
            s_dlg = DLG_NONE;
            if (d == DLG_ESTOP) {
                tcs_emergency_stop();
                tcs_log("[APP]", "touch estop confirm");
                draw_home();
            } else if (d == DLG_MODE_AUTO) {
                (void)param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_TOUCH);
                draw_home();
            } else if (d == DLG_SETCONFIRM) {
                s_step_granted = 1;
                (void)param_set(s_pending_id, s_pending_set, PARAM_SRC_TOUCH);
                s_saved_apply_t = millis();
                draw_tune();
            } else if (d == DLG_RESET) {
                param_load_defaults();
                (void)param_persist_now();
                s_tune_page = 0; s_tune_sel = 0; s_step_granted = 0;
                tcs_log("[APP]", "factory defaults restored");
                draw_tune();
            }
        } else if (ui_in(x, y, 536, 344, 192, 88)) {
            s_dlg = DLG_NONE;
            if (s_page == UI_PAGE_HOME) draw_home(); else draw_tune();
        }
        return;
    }

    if (s_page == UI_PAGE_HOME) {
        if (g_tcs.alarm_bits && ui_in(x, y, 208, 8, 552, 56)) {
            tcs_alarm_ack();
            tcs_log("[APP]", "touch alarm ack");
            draw_topbar();
            return;
        }
        if (ui_in(x, y, R_DOCK_TUNE)) {
            s_tune_page = 0; s_tune_sel = 0; s_step_granted = 0;
            slider_cancel_pending();
            tcs_log("[APP]", "touch page->tune");
            draw_tune(); return;
        }
        if (ui_in(x, y, R_DOCK_MODE)) {
            if (g_tcs.mode == 0) { s_dlg = DLG_MODE_AUTO; draw_dialog(); }
            else {
                slider_cancel_pending();
                (void)param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_TOUCH);
                draw_topbar(); draw_dock();
            }
            return;
        }
        if (ui_in(x, y, R_DOCK_CONN)) {
            tcs_log("[APP]", "touch page->qr");
            draw_qr(); return;
        }
        if (ui_in(x, y, R_DOCK_ESTOP)) {
            s_dlg = DLG_ESTOP; draw_dialog(); return;
        }
        if (ui_in(x, y, 532, 352, 460, 96)) {
            control_manual_fan(g_tcs.fan_on ? 0u : 1u);
            tcs_log("[APP]", "touch fan %u", g_tcs.fan_on);
            draw_fan_card_hl(0);
            return;
        }
    } else if (s_page == UI_PAGE_QR) {
        if (ui_in(x, y, R_CONN_BACK)) {
            tcs_log("[APP]", "touch page->home");
            draw_home(); return;
        }
    } else { /* 调参页 */
        int ids[16];
        int n = tune_items(ids, 16);
        int base = s_tune_page * UI_TUNE_ROW_N;
        for (int i = 0; i < UI_TUNE_ROW_N && base + i < n; i++) {
            int ry = tune_row_y(i);
            if (ui_in(x, y, UI_TUNE_ROW_X, ry, UI_TUNE_ROW_W, UI_TUNE_ROW_H)) {
                /* 选中行的 ± 键在行矩形内：放行给下方键处理（附录 A-05 教训）*/
                if ((base + i) == s_tune_sel
                    && (ui_in(x, y, UI_TUNE_ROW_X + 792, ry + 8, 64, 64)
                        || ui_in(x, y, UI_TUNE_ROW_X + 872, ry + 8, 64, 64)))
                    break;
                if (!ui_in(x, y, UI_TUNE_ROW_X + 312, ry + 4, 448, 80)
                    || (base + i) != s_tune_sel)
                    tune_select(i);
                return;
            }
        }
        int ry = tune_row_y(s_tune_sel - base);
        if (ui_in(x, y, UI_TUNE_ROW_X + 792, ry + 8, 64, 64)) { tune_step(-1.0f); return; }
        if (ui_in(x, y, UI_TUNE_ROW_X + 872, ry + 8, 64, 64)) { tune_step(1.0f); return; }
        if (ui_in(x, y, R_TUNE_ZONE_MINUS)) { tune_step(-1.0f); return; }
        if (ui_in(x, y, R_TUNE_ZONE_PLUS))  { tune_step(1.0f); return; }
        if (ui_in(x, y, R_TUNE_STEP)) {
            s_tune_step_f = (s_tune_step_f > 0.05f && s_tune_step_f < 0.5f) ? 1.0f : 0.1f;
            slider_cancel_pending();
            char step_l[24];
            snprintf(step_l, sizeof(step_l), "步进%.1f", (double)s_tune_step_f);
            ui_patch_begin();
            ui_btn(160, 488, 180, 64, step_l, 0, 24);
            ui_patch_commit(160, 488, 340, 552);
            g_stat_patch_cnt++;
            return;
        }
        if (s_tune_page == 0 && ui_in(x, y, R_TUNE_NEXT)) { tune_goto_page(1); return; }
        if (s_tune_page == 1 && ui_in(x, y, R_TUNE_PREV))  { tune_goto_page(0); return; }
        if (s_tune_page == 1 && ui_in(x, y, R_TUNE_RESET)) {
            s_dlg = DLG_RESET; draw_dialog(); return;
        }
        if (ui_in(x, y, UI_BACK_HIT)) {
            slider_cancel_pending();
            tcs_log("[APP]", "touch page->home");
            draw_home(); return;
        }
    }
}

#ifdef TCS_SIM
static int s_inj_hold, s_inj_release, s_inj_x, s_inj_y;
void tcs_ui_inject_touch(int down, int x, int y)
{
    if (down) { s_inj_x = x; s_inj_y = y; s_inj_hold = 1; s_inj_release = 0; }
    else      { s_inj_hold = 0; s_inj_release = 1; }
}
static int touch_inject_get(touch_event_t *ev)
{
    if (s_inj_hold)    { ev->touched = 1; ev->x = s_inj_x; ev->y = s_inj_y; return 1; }
    if (s_inj_release) { s_inj_release = 0; ev->touched = 0; return 1; }
    return 0;
}
#endif

static void touch_poll(uint32_t now)
{
    touch_event_t ev;
#ifdef TCS_SIM
    int injected = touch_inject_get(&ev);
#else
    int injected = 0;
#endif
    if (!injected) {
        if (!touch_ready()) return;
        int rv = ft5446_read(&ev);
        if (rv != 0) return;   /* -1 未就绪；2=幽灵帧丢弃 */
        /* K12 报点率：真实 IC 触点样本计数（1s 窗口锁存）*/
        static uint32_t cnt = 0, t0 = 0;
        cnt++;
        if (!t0) t0 = now;
        if (now - t0 >= 1000u) {
            g_stat_touch_hz = cnt;
            cnt = 0; t0 = now;
        }
    }

    if (ev.touched) {
        if (!s_touch_down) {
            s_touch_down = 1;
            s_press_t0 = now;
            s_press_lp_fired = 0;
            s_last_lp_ms = now;
            s_touch_release_ok = now;
            s_last_x = ev.x;
            s_last_y = ev.y;

            /* K4b：选中行滑条热区按下 → 进入拖动 */
            if (s_dlg == DLG_NONE && s_page == UI_PAGE_TUNE) {
                int base = s_tune_page * UI_TUNE_ROW_N;
                int ry = tune_row_y(s_tune_sel - base);
                int tx0 = UI_TUNE_ROW_X + 320, tw = UI_ROW_TRACK_W;
                const param_desc_t *p = tune_sel_desc();
                if (p && ui_in(ev.x, ev.y, tx0 - 8, ry + 4, tw + 16, UI_TUNE_ROW_H - 8)) {
                    s_sl.st = SL_DRAG;
                    s_sl.id = p->id;
                    s_sl.orig = p->value;
                    s_sl.val = slider_px2val(p, ev.x);
                    s_sl.t_last_draw = 0;
                    draw_slider(ry, p, s_sl.val, 1, 1, C_PRESS);
                }
            }
            press_hl_update(1, ev.x, ev.y);
        } else {
            s_last_x = ev.x;
            s_last_y = ev.y;
            /* 拖动跟手（≤20Hz 回显）*/
            if (s_sl.st == SL_DRAG && s_dlg == DLG_NONE && s_page == UI_PAGE_TUNE) {
                int base = s_tune_page * UI_TUNE_ROW_N;
                int ry = tune_row_y(s_tune_sel - base);
                int ty0 = ry + 36, th = 16;
                if (ev.y < ty0 - UI_SLIDER_CANCEL_DY
                    || ev.y > ty0 + th + UI_SLIDER_CANCEL_DY) {
                    const param_desc_t *p = param_find(s_sl.id);
                    s_sl.st = SL_IDLE;
                    s_sl_cancelled = 1;
                    tcs_log("[APP]", "slider cancel");
                    if (p) draw_slider(ry, p, s_sl.orig, 0, 1, C_PRESS);
                } else if (now - s_sl.t_last_draw >= UI_SLIDER_MIN_PERIOD_MS) {
                    const param_desc_t *p = param_find(s_sl.id);
                    if (p) {
                        s_sl.val = slider_px2val(p, ev.x);
                        s_sl.t_last_draw = now;
                        draw_slider(ry, p, s_sl.val, 1, 1, C_PRESS);
                    }
                }
            }
        }
        /* 长按连发：±（行内键 + 底部 ±区）*/
        if (s_page == UI_PAGE_TUNE && s_dlg == DLG_NONE && s_sl.st == SL_IDLE) {
            uint32_t held = now - s_press_t0;
            int base = s_tune_page * UI_TUNE_ROW_N;
            int ry = tune_row_y(s_tune_sel - base);
            int in_m = ui_in(s_last_x, s_last_y, R_TUNE_ZONE_MINUS)
                    || ui_in(s_last_x, s_last_y, UI_TUNE_ROW_X + 792, ry + 8, 64, 64);
            int in_p = ui_in(s_last_x, s_last_y, R_TUNE_ZONE_PLUS)
                    || ui_in(s_last_x, s_last_y, UI_TUNE_ROW_X + 872, ry + 8, 64, 64);
            if ((in_m || in_p) && held >= UI_REPEAT_FIRST_MS
                && now - s_last_lp_ms >= UI_REPEAT_MS) {
                s_last_lp_ms = now;
                s_press_lp_fired++;
                tune_step(in_m ? -1.0f : 1.0f);
            }
        }
        s_prev_touched = 1;
    } else if (s_prev_touched) {
        s_prev_touched = 0;
        press_hl_update(0, 0, 0);
        if (s_touch_down && now - s_touch_release_ok >= 30u) {
            if (s_sl_cancelled) {
                s_sl_cancelled = 0;   /* 取消的拖动不放行 tap（回弹即终结）*/
            } else if (s_press_lp_fired == 0) {
                if (s_sl.st == SL_DRAG) {
                    s_sl.st = SL_PENDING;
                    s_sl.t_release = now;
                    tcs_log("[APP]", "slider pending %.2f", (double)s_sl.val);
                } else {
                    touch_tap(s_last_x, s_last_y);
                }
            }
        }
        s_touch_down = 0;
    }
}

/* ====== 500ms 状态扫描 → 小更新直绘（零 flip）====== */
static void ui_sync_state(void)
{
    static float last_tc1 = -999, last_tc2 = -999, last_body = -999;
    static uint8_t last_tec1 = 0xFF, last_tec2 = 0xFF, last_fan = 0xFF;
    static uint8_t last_mode = 0xFF;
    static uint16_t last_alarm = 0xFFFF;
    static uint8_t last_m207 = 0xFF, last_esp = 0xFF;
    static int last_lit1 = -1, last_lit2 = -1, last_litb = -1;

    if (s_page != UI_PAGE_HOME || s_dlg != DLG_NONE) return;

    int tc1_chg = (g_tcs.tc1_act != last_tc1);
    int tc2_chg = (g_tcs.tc2_act != last_tc2);
    int body_chg = (g_tcs.body_temp != last_body);
    if (tc1_chg) { last_tc1 = g_tcs.tc1_act; s_fresh_tc1 = millis(); }
    if (tc2_chg) { last_tc2 = g_tcs.tc2_act; s_fresh_tc2 = millis(); }
    if (body_chg) { last_body = g_tcs.body_temp; s_fresh_body = millis(); }

    int lit1 = ((millis() - s_fresh_tc1) <= 1000u) && g_tcs.m207_online;
    int lit2 = ((millis() - s_fresh_tc2) <= 1000u) && g_tcs.m207_online;
    int litb = ((millis() - s_fresh_body) <= 1000u) && g_tcs.body_sensor_ok;

    /* 数值/失联/呼吸点状态驱动重绘（首次必画：last_lit 初值 -1）*/
    if (tc1_chg || lit1 != last_lit1) draw_tc_value(32, 0);
    if (tc2_chg || lit2 != last_lit2) draw_tc_value(532, 1);
    if (body_chg || litb != last_litb) draw_body_card();

    last_lit1 = lit1; last_lit2 = lit2; last_litb = litb;

    if (g_tcs.tec1_on != last_tec1) { last_tec1 = g_tcs.tec1_on; draw_tc_card(32, "贴身水箱", 0); }
    if (g_tcs.tec2_on != last_tec2) { last_tec2 = g_tcs.tec2_on; draw_tc_card(532, "环境水箱", 1); }
    if (g_tcs.fan_on != last_fan) { last_fan = g_tcs.fan_on; draw_fan_card_hl(0); }
    if (g_tcs.mode != last_mode) { last_mode = g_tcs.mode; draw_topbar(); draw_dock(); }
    if (g_tcs.alarm_bits != last_alarm) { last_alarm = g_tcs.alarm_bits; draw_topbar(); }
    if (g_tcs.m207_online != last_m207 || g_tcs.esp_link_ok != last_esp) {
        last_m207 = g_tcs.m207_online; last_esp = g_tcs.esp_link_ok;
        draw_topbar();
        draw_tc_value(32, 0);
        draw_tc_value(532, 1);
        draw_body_card();
    }
}

void tcs_ui_init(void)
{
    s_page = UI_PAGE_HOME;
    s_dlg = DLG_NONE;
    s_tune_page = 0;
    s_tune_sel = 0;
    s_tune_step_f = 0.1f;
    s_step_granted = 0;
    s_sl.st = SL_IDLE;
    if (lcd_ready()) {
        draw_home();
    }
    tcs_log("[APP]", "ui init page=home lcd=%d touch=%d", lcd_ready(), touch_ready());
}

#ifdef TCS_DEBUG_UI
static void draw_debug_footer(uint32_t now)
{
    (void)now;
    char ln[104];
    snprintf(ln, sizeof(ln),
             "L=%lu Wmax=%luus Wavg=%luus UI=%lu.%lums T=%luus OVF=%lu",
             (unsigned long)g_stat_loop_passes,
             (unsigned long)g_stat_work_us_max, (unsigned long)g_stat_work_us_avg,
             (unsigned long)(g_stat_ui_ms_x10 / 10u),
             (unsigned long)(g_stat_ui_ms_x10 % 10u),
             (unsigned long)g_stat_touch_us_max,
             (unsigned long)g_stat_tick_ovf);
    gfx_fill_rect(0, 583, 1023, 599, 0x0000);
    gfx_draw_string(8, 583, ln, 0xFFFF, 0x0000);
}
#else
static void draw_debug_footer(uint32_t now) { (void)now; }
#endif

uint32_t tcs_ui_tap_count(void) { return s_tap_cnt; }

void tcs_ui_screen_clear(uint16_t rgb565)
{
    if (!lcd_ready()) return;
    ui_full_begin();
    gfx_clear(rgb565);
    UI_FULL_COMMIT(5);
}

/* 字体标定页（新五档原生：20/24/32/48/96）*/
void tcs_ui_screen_text(void)
{
    if (!lcd_ready()) return;
    ui_full_begin();
    gfx_clear(C_BG);
    ui_aux20(16, 12, "ASCII20 0123:/- TCS-3C71", C_TX, C_BG);
    ui_text24(16, 48, "中文24 布防调参保存·横幅", C_TX, C_BG);
    ui_text32(16, 96, "32px 温度标题", C_TX, C_BG);
    ui_text48(16, 160, "-25.0", C_HERO, C_BG);
    ui_text96(16, 240, "25.0", C_HERO, C_BG);
    ui_aux20(16, 360, "fw v1.1.0", C_TX2, C_BG);
    UI_FULL_COMMIT(6);
}

#ifdef TCS_SIM
void tcs_ui_inject_tap(int x, int y)
{
    tcs_log("[SIM]", "tap %d,%d", x, y);
    touch_tap(x, y);
}
#endif

void tcs_ui_force_home(void)
{
    if (!lcd_ready()) return;
    s_page = UI_PAGE_HOME;
    s_dlg = DLG_NONE;
    draw_home();
}

void tcs_ui_force_tune(void)
{
    if (!lcd_ready()) return;
    s_page = UI_PAGE_TUNE;
    s_tune_page = 0;
    s_tune_sel = 0;
    s_dlg = DLG_NONE;
    draw_tune();
}

/* 压测算子（语义沿用：0纯flip 1横幅填充 2横幅文本 3胶囊填充 4 hero 大字）*/
void tcs_ui_screen_stress(int mode, int n)
{
    if (!lcd_ready()) return;
    for (int i = 0; i < n; i++) {
        switch (mode) {
        case 0: break;
        case 1: gfx_fill_rect(208, 8, 759, 63, C_RED); break;
        case 2: ui_text24(224, 16, "EEPROM", C_WHITE, C_RED); break;
        case 3: gfx_fill_rect(380, 112, 467, 151, C_GRAY); break;
        case 4: ui_text96(56, 160, "-25.0", C_HERO, C_CARD); break;
        default: return;
        }
        ui_full_begin();
        ui_full_commit();
    }
}

void tcs_ui_poll(uint32_t now)
{
    uint32_t t0 = DWT->CYCCNT;
    touch_poll(now);
    uint32_t us = (DWT->CYCCNT - t0) / (SystemCoreClock / 1000000u);
    if (us > g_stat_touch_us_max) g_stat_touch_us_max = us;

    /* K4b：PENDING 1s 防抖 → 生效下发 + clamp 后实际值回显 */
    if (s_sl.st == SL_PENDING && now - s_sl.t_release >= UI_SLIDER_DEBOUNCE_MS) {
        s_sl.st = SL_IDLE;
        apply_param(s_sl.id, s_sl.val);
        tcs_log("[APP]", "slider apply %.2f", (double)s_sl.val);
        if (s_dlg == DLG_NONE && s_page == UI_PAGE_TUNE) {
            int ids[16]; tune_items(ids, 16);
            draw_prow(s_tune_sel - s_tune_page * UI_TUNE_ROW_N, s_tune_sel,
                      (uint16_t)ids[s_tune_sel]);
        }
    }

    /* 已保存角标：值生效 +2s（flush 防抖镜像）亮 1s */
    if (s_saved_apply_t && now - s_saved_apply_t >= 2000u) {
        s_saved_apply_t = 0;
        if (s_page == UI_PAGE_TUNE && s_dlg == DLG_NONE) {
            s_saved_toast_on = 1;
            s_saved_toast_t0 = now;
            ui_patch_begin();
            draw_tune_topbar();
            ui_patch_commit(700, 8, 1015, 51);
            g_stat_patch_cnt++;
        }
    }
    if (s_saved_toast_on && now - s_saved_toast_t0 >= 1000u) {
        s_saved_toast_on = 0;
        if (s_page == UI_PAGE_TUNE && s_dlg == DLG_NONE) {
            ui_patch_begin();
            gfx_fill_rect(716, 8, 884, 48, C_BG);
            draw_tune_topbar();
            ui_patch_commit(700, 8, 1015, 51);
            g_stat_patch_cnt++;
        }
    }

    /* 500ms 扫描 */
    static uint32_t last_sync = 0;
    if (now - last_sync >= 500u) {
        last_sync = now;
        ui_sync_state();
        if (s_page == UI_PAGE_TUNE && s_dlg == DLG_NONE) {
            static uint32_t last_clk = 0;
            uint32_t t = now / 1000u;
            if (t != last_clk) {
                last_clk = t;
                ui_patch_begin();
                char ts[12];
                snprintf(ts, sizeof(ts), "%02lu:%02lu:%02lu",
                         (unsigned long)(t / 3600u), (unsigned long)((t / 60u) % 60u),
                         (unsigned long)(t % 60u));
                gfx_fill_rect(890, 20, 1015, 48, C_BG);
                ui_aux20(896, 24, ts, C_TX2, C_BG);
                ui_patch_commit(890, 20, 1015, 48);
                g_stat_patch_cnt++;
            }
        }
    }

    /* H6 角标状态沿 → 顶栏 */
    {
        static uint8_t last_badge = 0xFF;
        uint8_t b = post_badge_state();
        if (b != last_badge) {
            last_badge = b;
            if (s_page == UI_PAGE_HOME && s_dlg == DLG_NONE) draw_topbar();
        }
    }

    /* 弹窗/非主页期间冻结周期重绘（H1 教训：模态期间不动屏）*/
    if (s_dlg != DLG_NONE) return;
    draw_debug_footer(now);
}
