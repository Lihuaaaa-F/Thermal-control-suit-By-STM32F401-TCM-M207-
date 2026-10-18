/**
 * tcs_ui.c — 触摸 UI 实现（任务书 v5.1 WP-B 浅色定稿 + P0 主页 / P1 调参页 / 弹窗）
 *
 * 复用 ltdc.c gfx_* 图元 + memcpy flip（back 读取前作废 D-Cache 行，WP-A.2d）。
 * FT5446 20Hz 轮询；触摸去抖 30ms；抬手才触发（1→0 沿）；长按 400ms 后每 150ms
 * 连发（调参 +/−）；同一时刻只响应第一个触点。
 *
 * WP-B 定稿：浅色冷白系（调色板 RGB888 见下），温度 64px（32px 字形 2×）/标题
 * 24px/正文 16px；顶栏 48px 胶囊+浅红浅黄横幅+状态点；卡片白底描边+右上状态胶囊；
 * 底栏主按钮白底蓝边、急停实红加宽；调参页隔行 #F6F8FA、选中行 4px 蓝竖条+浅蓝底。
 * **触摸 rect 命中区保持现值**（急停视觉加宽至 748 起，命中区在原 rect 上只增不减）。
 *
 * 顶栏重叠说明：急停常驻胶囊 rect(120,8,80,32) 压在横幅区左端——触摸按区域分流。
 */
#include "tcs_ui.h"
#include "ltdc.h"
#include "ft5446.h"
#include "param_registry.h"
#include "control.h"
#include "segger_rtt.h"
#include <string.h>
#include <stdio.h>

/* ====== 调色板（WP-B 总设计师定稿，RGB888→RGB565，不得发挥）====== */
#define C_BG       RGB565(0xEE,0xF1,0xF5)   /* 页面底 #EEF1F5 */
#define C_CARD     RGB565(0xFF,0xFF,0xFF)   /* 卡片 #FFFFFF */
#define C_BORDER   RGB565(0xD8,0xDE,0xE6)   /* 卡片描边 #D8DEE6 */
#define C_CLINE    RGB565(0xDD,0xE3,0xEA)   /* 底饰线 #DDE3EA */
#define C_TEXT     RGB565(0x1F,0x24,0x29)   /* 主文字 #1F2429 */
#define C_VALUE    RGB565(0x11,0x18,0x27)   /* 大数值 #111827 */
#define C_SUB      RGB565(0x6B,0x72,0x80)   /* 次级 #6B7280 */
#define C_ACCENT   RGB565(0x25,0x63,0xEB)   /* 强调蓝 #2563EB（实底时字白）*/
#define C_OK       RGB565(0x16,0xA3,0x4A)   /* 成功绿 #16A34A */
#define C_ALARM    RGB565(0xDC,0x26,0x26)   /* 报警红 #DC2626 */
#define C_ALM_BG   RGB565(0xFE,0xE2,0xE2)   /* 横幅红底 #FEE2E2 */
#define C_ALM_TX   RGB565(0x99,0x1B,0x1B)   /* 横幅红字 #991B1B */
#define C_WARN_BG  RGB565(0xFE,0xF3,0xC7)   /* 警告黄底 #FEF3C7 */
#define C_WARN_TX  RGB565(0x92,0x40,0x0E)   /* 警告黄字 #92400E */
#define C_DIS      RGB565(0x9C,0xA3,0xAF)   /* 禁用 #9CA3AF */
#define C_PRESS    RGB565(0xE5,0xE7,0xEB)   /* 按下反馈 #E5E7EB */
#define C_ROWA     RGB565(0xF6,0xF8,0xFA)   /* 调参隔行 #F6F8FA */
#define C_SELBG    RGB565(0xDB,0xEA,0xFE)   /* 选中行浅蓝 #DBEAFE */
#define C_WHITE    RGB565(0xFF,0xFF,0xFF)

#include "console.h"   /* tcs_log */

/* ====== 24×24 中文/符号 + 32×32 数值字形（tools/gen_font_cn.py 生成）====== */
#include "tcs_font_cn.inc"
#define F24_W  24
#define F24_H  24
#define F24_BPR (F24_W / 8)          /* 3B/行 */
#define F24_SZ  (F24_BPR * F24_H)    /* 72B/字 */
#define F32_W  32
#define F32_H  32
#define F32_BPR (F32_W / 8)          /* 4B/行 */
#define F32_SZ  (F32_BPR * F32_H)    /* 128B/字 */

#define F24_N (sizeof(tcs_font24_cp) / sizeof(tcs_font24_cp[0]))
#define F32_N (sizeof(tcs_font32_cp) / sizeof(tcs_font32_cp[0]))
static const unsigned int   *s_cp24  = tcs_font24_cp;
static const unsigned char (*s_gl24)[F24_SZ] = tcs_font24_glyphs;
static const unsigned int   *s_cp32  = tcs_font32_cp;
static const unsigned char (*s_gl32)[F32_SZ] = tcs_font32_glyphs;

/* 8×16 ASCII（复用 GS 已验证字库，16px 档仅 ASCII）*/
static const uint8_t s_f8x16[][16] = {
#include "font8x16.inc"
};

/* ====== 字形查找 ====== */
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

static void f24_draw(int x, int y, int gi, uint16_t fg, uint16_t bg)
{
    const unsigned char *g = s_gl24[gi];
    for (int row = 0; row < F24_H; row++) {
        for (int col = 0; col < F24_W; col++) {
            uint8_t bits = g[row * F24_BPR + col / 8];
            gfx_draw_pixel(x + col, y + row,
                           (bits & (0x80 >> (col % 8))) ? fg : bg);
        }
    }
}

/* UTF-8 → codepoint。返回消耗字节数；*cp 出参。 */
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

/* ====== 24px 文本（CJK/符号/ASCII 通吃）====== */
static int ui_char24(int x, int y, unsigned int cp, uint16_t fg, uint16_t bg)
{
    int gi = f24_find(cp);
    if (gi < 0) gi = f24_find('?');
    if (gi >= 0) f24_draw(x, y, gi, fg, bg);
    return F24_W;
}

static void ui_text24(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    while (*s) {
        unsigned int cp;
        int n = utf8_next(s, &cp);
        if (n == 0) break;
        x += ui_char24(x, y, cp, fg, bg);
        s += n;
    }
}

static int ui_text24_w(const char *s)
{
    int w = 0;
    while (*s) {
        unsigned int cp;
        int n = utf8_next(s, &cp);
        if (n == 0) break;
        w += F24_W;
        s += n;
    }
    return w;
}

/* ====== 16px 文本（仅 ASCII；CJK 会成 '?'，勿用——WP-A.3 实测教训）====== */
static void ui_text16(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    while (*s) {
        char c = *s;
        if (c < 0x20 || c > 0x7E) c = '?';
        const uint8_t *g = s_f8x16[(int)c - 0x20];
        for (int row = 0; row < 16; row++) {
            uint8_t bits = g[row];
            for (int col = 0; col < 8; col++)
                gfx_draw_pixel(x + col, y + row,
                               (bits & (0x80 >> col)) ? fg : bg);
        }
        x += 8;
        s++;
    }
}

/* ====== 64px 大数值（32px 字形 2× 最近邻；行内游程合并降 DMA2D 次数）====== */
static void ui_char64(int x, int y, unsigned int cp, uint16_t fg, uint16_t bg)
{
    int gi = f32_find(cp);
    if (gi < 0) gi = f32_find('?');
    if (gi < 0) return;
    const unsigned char *g = s_gl32[gi];
    for (int row = 0; row < F32_H; row++) {
        int col = 0;
        while (col < F32_W) {
            uint8_t b = g[row * F32_BPR + col / 8];
            int bit = (b >> (7 - (col & 7))) & 1;
            int run = 1;
            while (col + run < F32_W) {
                uint8_t b2 = g[row * F32_BPR + (col + run) / 8];
                if (((b2 >> (7 - ((col + run) & 7))) & 1) != bit) break;
                run++;
            }
            if (bit || bg != 0xFFFF) {
                int x0 = x + col * 2, x1 = x + (col + run) * 2 - 1;
                gfx_draw_hline(x0, x1, y + row * 2,     bit ? fg : bg);
                gfx_draw_hline(x0, x1, y + row * 2 + 1, bit ? fg : bg);
            }
            col += run;
        }
    }
}

static void ui_text64(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    while (*s) {
        unsigned int cp;
        int n = utf8_next(s, &cp);
        if (n == 0) break;
        ui_char64(x, y, cp, fg, bg);
        x += F32_W * 2;
        s += n;
    }
}

static int ui_text64_w(const char *s)
{
    int w = 0;
    while (*s) {
        unsigned int cp;
        int n = utf8_next(s, &cp);
        if (n == 0) break;
        w += F32_W * 2;
        s += n;
    }
    return w;
}

/* ====== 圆角/圆形图元（RGB565 无 alpha，角落以 bg 补）====== */
static void ui_fill_round(int x, int y, int w, int h, int r,
                          uint16_t c, uint16_t bg)
{
    if (r * 2 > h) r = h / 2;
    gfx_fill_rect(x, y, x + w - 1, y + h - 1, c);
    for (int dy = 0; dy < r; dy++) {
        for (int dx = 0; dx < r; dx++) {
            int cx = r - 1 - dx, cy = r - 1 - dy;
            if (cx * cx + cy * cy >= r * r) {
                gfx_draw_pixel(x + dx,         y + dy,         bg);
                gfx_draw_pixel(x + w - 1 - dx, y + dy,         bg);
                gfx_draw_pixel(x + dx,         y + h - 1 - dy, bg);
                gfx_draw_pixel(x + w - 1 - dx, y + h - 1 - dy, bg);
            }
        }
    }
}

static void ui_fill_circle(int cx, int cy, int r, uint16_t c)
{
    for (int dy = -r; dy <= r; dy++) {
        int dx = 0;
        while (dx * dx + dy * dy <= r * r) dx++;   /* 整数开方（r≤40，免 libm）*/
        dx--;
        gfx_draw_hline(cx - dx, cx + dx, cy + dy, c);
    }
}

/* ====== 基础控件 ====== */
/* 白底蓝边按钮（底栏主按钮）；solid=1 时实底、字白 */
static void ui_btn(int x, int y, int w, int h, const char *label,
                   uint16_t labelc, int solid)
{
    if (solid) {
        ui_fill_round(x, y, w, h, 8, labelc, C_BG);
    } else {
        ui_fill_round(x, y, w, h, 8, C_CARD, C_BG);
        gfx_draw_hline(x + 6, x + w - 7, y, labelc);
        gfx_draw_hline(x + 6, x + w - 7, y + h - 1, labelc);
        gfx_draw_vline(x, y + 6, y + h - 7, labelc);
        gfx_draw_vline(x + w - 1, y + 6, y + h - 7, labelc);
    }
    int lw = ui_text24_w(label);
    ui_text24(x + (w - lw) / 2, y + (h - 24) / 2, label,
              solid ? C_WHITE : labelc, solid ? labelc : C_CARD);
}

/* 状态胶囊（右上小胶囊）：on=绿"运行/开"，off=灰"停止/关" */
static void ui_pill(int x, int y, int w, int h, const char *label, uint8_t on)
{
    uint16_t bg = on ? C_OK : C_DIS;
    ui_fill_round(x, y, w, h, h / 2, bg, C_CARD);
    int lw = ui_text24_w(label);
    ui_text24(x + (w - lw) / 2, y + (h - 24) / 2, label, C_WHITE, bg);
}

static int ui_in(int x, int y, int rx, int ry, int rw, int rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

/* ====== 页面/弹窗状态 ====== */
typedef enum { UI_PAGE_HOME = 0, UI_PAGE_TUNE } ui_page_t;
typedef enum { DLG_NONE = 0, DLG_ESTOP, DLG_MODE_AUTO, DLG_SETCONFIRM } ui_dlg_t;

static ui_page_t s_page = UI_PAGE_HOME;
static ui_dlg_t  s_dlg = DLG_NONE;
static int  s_tune_sel;              /* 调参页选中行（PF_SCREEN 列表 idx）*/
static float s_tune_step = 0.1f;
static float s_pending_set;          /* SETCONFIRM 待确认值 */
static uint16_t s_pending_id;
static uint8_t  s_step_granted;      /* 已确认本次超阈连续调节（值落回阈值下复位）*/

static int   s_touch_down;           /* 当前按住 */
static int   s_last_x, s_last_y;
static uint32_t s_press_t0;          /* 按下时刻 */
static uint8_t  s_press_lp_fired;    /* 长按已触发次数 */
static uint32_t s_last_lp_ms;        /* 上次连发时刻 */
static uint32_t s_touch_release_ok;  /* 去抖：按下时刻 */
static int s_prev_touched;

/* 脏区位 */
#define D_TOPBAR   0x001
#define D_TC1      0x002
#define D_TC2      0x004
#define D_BODY     0x008
#define D_FAN      0x010
#define D_BTNS     0x020
#define D_ALL      0x03F
static uint16_t s_dirty = D_ALL;

/* ====== 顶栏 48px ====== */
static void draw_topbar(void)
{
    gfx_fill_rect(0, 0, LCD_WIDTH - 1, 47, C_CARD);
    gfx_draw_hline(0, LCD_WIDTH - 1, 47, C_BORDER);

    /* 左：模式胶囊 (16,8,120,32) */
    int auto_m = (g_tcs.mode != 0);
    ui_fill_round(16, 8, 120, 32, 16, auto_m ? C_ACCENT : C_DIS, C_CARD);
    ui_text24(16 + (120 - 48) / 2, 12, auto_m ? "自动" : "手动", C_WHITE,
              auto_m ? C_ACCENT : C_DIS);

    /* 急停常驻胶囊 (120,8,80,32)（命中 rect(120,0,80,40) 保持现值）*/
    ui_fill_round(120, 8, 80, 32, 16, C_ALARM, C_CARD);
    ui_text24(120 + (80 - 48) / 2, 12, "急停", C_WHITE, C_ALARM);

    /* 中：横幅浅红/浅黄圆角条 (208,8,552,32)；急停/过热=红，失联/EEPROM=黄 */
    if (g_tcs.alarm_bits) {
        const char *msg = "ALARM";
        uint16_t bg = C_ALM_BG, tx = C_ALM_TX;
        int red = 0;
        if (g_tcs.alarm_bits & ALM_ESTOP)         { msg = "急停已触发 点击恢复"; red = 1; }
        else if (g_tcs.alarm_bits & ALM_OVERTEMP) { msg = "水温过高 >48°C";    red = 1; }
        else if (g_tcs.alarm_bits & ALM_BODY_SENSOR) msg = "体温失联 点击恢复";
        else if (g_tcs.alarm_bits & ALM_M207)        msg = "M207 失联 点击恢复";
        else if (g_tcs.alarm_bits & ALM_EEPROM)      msg = "EEPROM 配置无效 已恢复默认";
        if (red) { bg = C_ALM_BG; tx = C_ALM_TX; }
        else     { bg = C_WARN_BG; tx = C_WARN_TX; }
        ui_fill_round(208, 8, 552, 32, 10, bg, C_CARD);
        ui_text24(224, 12, msg, tx, bg);
    }

    /* 右：M207/ESP 状态点 + 时间（16px ASCII）*/
    gfx_fill_rect(768, 0, LCD_WIDTH - 1, 47, C_CARD);
    ui_text16(776, 8, "M207", g_tcs.m207_online ? C_TEXT : C_DIS, C_CARD);
    ui_fill_circle(816, 16, 4, g_tcs.m207_online ? C_OK : C_DIS);
    ui_text16(832, 8, "ESP", g_tcs.esp_link_ok ? C_TEXT : C_DIS, C_CARD);
    ui_fill_circle(868, 16, 4, g_tcs.esp_link_ok ? C_OK : C_DIS);

    extern uint32_t millis(void);
    uint32_t t = millis() / 1000u;
    char ts[12];
    snprintf(ts, sizeof(ts), "%02lu:%02lu:%02lu",
             (unsigned long)(t / 3600u), (unsigned long)((t / 60u) % 60u),
             (unsigned long)(t % 60u));
    ui_text16(904, 16, ts, C_TEXT, C_CARD);
}

/* ====== 主页卡片 ====== */
static void draw_tc_card(int cx, const char *title, int ch)
{
    gfx_fill_rect(cx, 56, cx + 480 - 1, 56 + 224 - 1, C_CARD);
    gfx_draw_rect(cx, 56, cx + 480 - 1, 56 + 224 - 1, C_BORDER);
    ui_text24(cx + 16, 68, title, C_TEXT, C_CARD);

    /* 右上状态胶囊（绿运行/灰停止）*/
    uint8_t tec_on = ch ? g_tcs.tec2_on : g_tcs.tec1_on;
    ui_pill(cx + 376, 64, 88, 32, tec_on ? "运行" : "停止", tec_on);

    /* 温度大数值 64px + °C 次级 */
    float act = ch ? g_tcs.tc2_act : g_tcs.tc1_act;
    char big[16];
    snprintf(big, sizeof(big), "%.1f", (double)act);
    ui_text64(cx + 16, 112, big, C_VALUE, C_CARD);
    ui_text24(cx + 16 + ui_text64_w(big) + 12, 152, "°C", C_SUB, C_CARD);

    /* 底饰线 + 设定行 */
    float setv = param_get(ch ? PARAM_ID_TC2_SET : PARAM_ID_TC1_SET);
    char setl[48];
    snprintf(setl, sizeof(setl), "设定 %.1f°C", (double)setv);
    ui_text24(cx + 16, 224, setl, C_SUB, C_CARD);
    gfx_draw_hline(cx + 16, cx + 464 - 1, 264, C_CLINE);
    gfx_draw_hline(cx + 16, cx + 464 - 1, 265, C_CLINE);
}

static void draw_body_card(void)
{
    gfx_fill_rect(16, 296, 16 + 480 - 1, 296 + 152 - 1, C_CARD);
    uint16_t border = g_tcs.body_sensor_ok ? C_BORDER : C_ALARM;
    gfx_draw_rect(16, 296, 16 + 480 - 1, 296 + 152 - 1, border);
    ui_text24(32, 304, "体温", C_TEXT, C_CARD);
    char big[16];
    if (g_tcs.body_sensor_ok) snprintf(big, sizeof(big), "%.1f", (double)g_tcs.body_temp);
    else                      snprintf(big, sizeof(big), "--.-");
    ui_text64(32, 336, big, g_tcs.body_sensor_ok ? C_VALUE : C_ALARM, C_CARD);
    ui_text24(32 + ui_text64_w(big) + 12, 376, "°C", C_SUB, C_CARD);
}

static void draw_fan_card(void)
{
    gfx_fill_rect(528, 296, 528 + 480 - 1, 296 + 152 - 1, C_CARD);
    gfx_draw_rect(528, 296, 528 + 480 - 1, 296 + 152 - 1, C_BORDER);
    ui_text24(544, 304, "风扇", C_TEXT, C_CARD);
    ui_pill(528 + 376, 300, 88, 32, g_tcs.fan_on ? "开" : "关", g_tcs.fan_on);
    /* 状态圆盘：绿开/灰关，中心 24px 字 */
    int cx0 = 640, cy0 = 372;
    ui_fill_circle(cx0, cy0, 36, g_tcs.fan_on ? C_OK : C_CLINE);
    ui_text24(cx0 - 12, cy0 - 12, g_tcs.fan_on ? "开" : "关",
              C_WHITE, g_tcs.fan_on ? C_OK : C_CLINE);
}

static void draw_home_buttons(void)
{
    int auto_m = (g_tcs.mode != 0);
    ui_btn(16, 544, 220, 56, "调参", C_ACCENT, 0);
    ui_btn(252, 544, 220, 56, auto_m ? "切手动" : "切自动", C_ACCENT, 0);
    /* TEC1 启停：自动模式禁用灰 */
    ui_btn(488, 544, 220, 56, "TEC1 启停", auto_m ? C_DIS : C_ACCENT, 0);
    /* 急停：实心红加宽（视觉 748 起；命中区=原 rect + 加宽条，只增不减）*/
    ui_btn(748, 544, 260, 56, "急停", C_ALARM, 1);
}

static void draw_home(void)
{
    gfx_clear(C_BG);
    draw_topbar();
    draw_tc_card(16, "贴身水箱", 0);
    draw_tc_card(528, "环境水箱", 1);
    draw_body_card();
    draw_fan_card();
    draw_home_buttons();
    gfx_flip();
    s_dirty = 0;
}

/* ====== 调参页（P1）====== */
static int tune_items(int *ids, int max)
{
    int n = 0;
    for (int i = 0; i < param_count() && n < max; i++) {
        const param_desc_t *p = param_at(i);
        if (p->flags & PF_SCREEN) ids[n++] = p->id;   /* D6：无 SCREEN 永不出现 */
    }
    return n;
}

static void draw_tune(void)
{
    int ids[16];
    int n = tune_items(ids, 16);
    gfx_clear(C_BG);

    /* 页顶：标题 + 常驻灰字说明（中文必须 24px——16px 仅 ASCII，WP-A.3）*/
    ui_text24(32, 8, "调参", C_TEXT, C_BG);
    ui_text24(300, 10, "实时生效 保存=断电保持", C_SUB, C_BG);

    /* 列表 8 行 (16,56+i*56,640,48)：隔行 #F6F8FA；选中行浅蓝底+4px 蓝竖条 */
    for (int i = 0; i < 8 && i < n; i++) {
        const param_desc_t *p = param_find((uint16_t)ids[i]);
        if (!p) continue;
        int y = 56 + i * 56;
        uint16_t bg = (i == s_tune_sel) ? C_SELBG : ((i & 1) ? C_ROWA : C_CARD);
        gfx_fill_rect(16, y, 16 + 640 - 1, y + 48 - 1, bg);
        if (i == s_tune_sel) {
            gfx_fill_rect(16, y, 16 + 4 - 1, y + 48 - 1, C_ACCENT);
        }
        ui_text24(32, y + 12, p->name, C_TEXT, bg);
        char vs[32];
        if (p->id == PARAM_ID_MODE)
            snprintf(vs, sizeof(vs), "%s", p->value != 0.0f ? "自动" : "手动");
        else if (p->id == PARAM_ID_COMMISSIONED)
            snprintf(vs, sizeof(vs), "%s", p->value != 0.0f ? "已布防" : "未布防");
        else
            snprintf(vs, sizeof(vs), "%.1f%s", (double)p->value, p->unit);
        int vw = ui_text24_w(vs);
        ui_text24(16 + 480 - vw, y + 12, vs, C_VALUE, bg);
    }

    /* −/+ 大按钮 (700,168,140,140)/(884,168,140,140)：白底蓝边 */
    ui_btn(700, 168, 140, 140, "-", C_ACCENT, 0);
    ui_btn(884, 168, 140, 140, "+", C_ACCENT, 0);

    /* 步进切换 (700,336,324,56) */
    char step_l[32];
    snprintf(step_l, sizeof(step_l), "步进 %.1f", (double)s_tune_step);
    ui_btn(700, 336, 324, 56, step_l, C_ACCENT, 0);

    /* 范围提示 (700,408,324,40) */
    const param_desc_t *sp = (s_tune_sel < n) ? param_find((uint16_t)ids[s_tune_sel]) : 0;
    if (sp) {
        char rg[64];
        snprintf(rg, sizeof(rg), "范围 %.1f ~ %.1f %s",
                 (double)sp->min, (double)sp->max, sp->unit);
        ui_text24(712, 414, rg, C_SUB, C_BG);
    }

    ui_btn(16, 544, 200, 56, "返回", C_ACCENT, 0);
    ui_btn(700, 544, 324, 56, "保存并返回", C_ACCENT, 1);   /* 主操作=实底蓝字白 */
    gfx_flip();
    s_dirty = 0;
}

/* ====== 弹窗（白卡 + 描边；确认实底红/蓝，取消白底蓝边）====== */
static void draw_dialog(void)
{
    /* 全屏点阵遮罩（RGB565 无 alpha → 浅灰点阵近似）*/
    for (int y = 48; y < LCD_HEIGHT; y += 3)
        for (int x = 0; x < LCD_WIDTH; x += 3)
            gfx_draw_pixel(x, y, C_CLINE);

    /* 白色圆角卡片 (292,180,440,240) */
    ui_fill_round(292, 180, 440, 240, 12, C_CARD, C_BG);
    gfx_draw_rect(292, 180, 292 + 440 - 1, 180 + 240 - 1, C_BORDER);

    if (s_dlg == DLG_ESTOP) {
        ui_text24(312, 208, "确认急停？", C_ALARM, C_CARD);
        ui_text24(312, 248, "TEC 将全部停止，风扇全开", C_TEXT, C_CARD);
        ui_btn(412, 300, 200, 64, "确认", C_ALARM, 1);
        ui_btn(612, 300, 200, 64, "取消", C_ACCENT, 0);
    } else if (s_dlg == DLG_MODE_AUTO) {
        ui_text24(392, 232, "切换到自动模式？", C_TEXT, C_CARD);
        ui_btn(412, 300, 200, 64, "确认", C_ACCENT, 1);
        ui_btn(612, 300, 200, 64, "取消", C_ACCENT, 0);
    } else if (s_dlg == DLG_SETCONFIRM) {
        char msg[48];
        snprintf(msg, sizeof(msg), "设定 %.1f°C 超过确认阈值",
                 (double)s_pending_set);
        ui_text24(312, 208, msg, C_ALARM, C_CARD);
        ui_text24(312, 248, "高温设定需二次确认", C_TEXT, C_CARD);
        ui_btn(412, 300, 200, 64, "确认", C_ALARM, 1);
        ui_btn(612, 300, 200, 64, "取消", C_ACCENT, 0);
    }
    gfx_flip();
}

/* ====== 调参动作（touch 路径，含 F4 二次确认）====== */
static void tune_apply(float v)
{
    int ids[16];
    int n = tune_items(ids, 16);
    if (s_tune_sel >= n) return;
    uint16_t id = (uint16_t)ids[s_tune_sel];
    const param_desc_t *p = param_find(id);
    if (!p) return;

    /* F4：温度设定超 tc_set_confirm → 二次确认弹窗（确认后本次会话放行）*/
    if ((id == PARAM_ID_TC1_SET || id == PARAM_ID_TC2_SET)
        && v > param_get(PARAM_ID_SET_CONFIRM) && !s_step_granted) {
        s_pending_id = id;
        s_pending_set = v;
        s_dlg = DLG_SETCONFIRM;
        draw_dialog();
        return;
    }
    int rv = param_set(id, v, PARAM_SRC_TOUCH);
    (void)rv;
    if (id == PARAM_ID_TC1_SET || id == PARAM_ID_TC2_SET) {
        if (v <= param_get(PARAM_ID_SET_CONFIRM)) s_step_granted = 0;
    }
    draw_tune();   /* 值变更行重绘（页小，整页重绘）*/
}

static void tune_step(float dir)
{
    int ids[16];
    int n = tune_items(ids, 16);
    if (s_tune_sel >= n) return;
    const param_desc_t *p = param_find((uint16_t)ids[s_tune_sel]);
    if (!p) return;
    tune_apply(p->value + dir * s_tune_step);
}

/* ====== 触摸分发 ====== */
static volatile uint32_t s_tap_cnt;   /* 诊断：点击累计（幻触摸侦测）*/

static void touch_tap(int x, int y)
{
    s_tap_cnt++;

    /* 弹窗优先 */
    if (s_dlg != DLG_NONE) {
        if (ui_in(x, y, 412, 300, 200, 64)) {          /* 确认 */
            ui_dlg_t d = s_dlg;
            s_dlg = DLG_NONE;
            if (d == DLG_ESTOP) {
                tcs_emergency_stop();
                tcs_log("[APP]", "touch estop confirm");
                s_page = UI_PAGE_HOME;
                draw_home();
            } else if (d == DLG_MODE_AUTO) {
                (void)param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_TOUCH);
                draw_home();
            } else if (d == DLG_SETCONFIRM) {
                s_step_granted = 1;
                (void)param_set(s_pending_id, s_pending_set, PARAM_SRC_TOUCH);
                draw_tune();
            }
        } else if (ui_in(x, y, 612, 300, 200, 64)) {   /* 取消 */
            s_dlg = DLG_NONE;
            if (s_page == UI_PAGE_HOME) draw_home(); else draw_tune();
        }
        return;
    }

    if (s_page == UI_PAGE_HOME) {
        if (ui_in(x, y, 120, 0, 80, 40)) {             /* 常驻急停胶囊 */
            s_dlg = DLG_ESTOP; draw_dialog(); return;
        }
        if (g_tcs.alarm_bits && ui_in(x, y, 200, 0, 568, 40)) {
            tcs_alarm_ack();                            /* 点横幅=人工确认恢复 */
            tcs_log("[APP]", "touch alarm ack");
            draw_topbar(); gfx_flip(); return;
        }
        if (ui_in(x, y, 16, 544, 220, 56)) {           /* 调参 */
            s_page = UI_PAGE_TUNE; s_tune_sel = 0; s_step_granted = 0;
            tcs_log("[APP]", "touch page->tune");
            draw_tune(); return;
        }
        if (ui_in(x, y, 252, 544, 220, 56)) {          /* 模式切换 */
            if (g_tcs.mode == 0) { s_dlg = DLG_MODE_AUTO; draw_dialog(); }
            else (void)param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_TOUCH);
            draw_home(); return;
        }
        if (ui_in(x, y, 488, 544, 220, 56)) {          /* TEC1 启停 */
            if (g_tcs.mode == 0) {
                control_manual_tec(1, g_tcs.tec1_on ? 0u : 1u);
                tcs_log("[APP]", "touch tec1 %u", g_tcs.tec1_on);
            }
            draw_home(); return;
        }
        if (ui_in(x, y, 788, 544, 220, 56)             /* 急停原 rect */
            || ui_in(x, y, 748, 544, 40, 56)) {        /* + 视觉加宽条（只增不减）*/
            s_dlg = DLG_ESTOP; draw_dialog(); return;
        }
        if (ui_in(x, y, 528, 296, 480, 152)) {         /* 风扇整卡切换（低风险无确认）*/
            control_manual_fan(g_tcs.fan_on ? 0u : 1u);
            tcs_log("[APP]", "touch fan %u", g_tcs.fan_on);
            draw_fan_card(); gfx_flip(); return;
        }
    } else { /* P1 调参页 */
        int ids[16];
        int n = tune_items(ids, 16);
        for (int i = 0; i < 8 && i < n; i++) {
            int ry = 56 + i * 56;
            if (ui_in(x, y, 16, ry, 640, 48)) {
                s_tune_sel = i;
                s_step_granted = 0;
                tcs_log("[APP]", "touch sel %s", param_find((uint16_t)ids[i])->key);
                draw_tune(); return;
            }
        }
        if (ui_in(x, y, 700, 336, 324, 56)) {          /* 步进 0.1↔1.0 */
            s_tune_step = (s_tune_step > 0.05f && s_tune_step < 0.5f) ? 1.0f : 0.1f;
            draw_tune(); return;
        }
        if (ui_in(x, y, 16, 544, 200, 56)) {           /* 返回（不保存也回）*/
            s_page = UI_PAGE_HOME;
            tcs_log("[APP]", "touch page->home");
            draw_home(); return;
        }
        if (ui_in(x, y, 700, 544, 324, 56)) {          /* 保存并返回 */
            (void)param_persist_now();
            tcs_log("[APP]", "touch save+home");
            s_page = UI_PAGE_HOME;
            draw_home(); return;
        }
    }
}

static void touch_poll(uint32_t now)
{
    if (!touch_ready()) return;
    touch_event_t ev;
    if (ft5446_read(&ev) != 0) return;

    /* 30ms 去抖：释放沿距按下 <30ms 忽略 */
    if (ev.touched) {
        if (!s_touch_down) {
            s_touch_down = 1;
            s_press_t0 = now;
            s_press_lp_fired = 0;
            s_last_lp_ms = now;
            s_touch_release_ok = now;
            s_last_x = ev.x;
            s_last_y = ev.y;
        } else {
            s_last_x = ev.x;   /* 跟踪（长按期间滑出按钮则停发）*/
            s_last_y = ev.y;
        }
        /* 长按连发：仅调参页 +/− 按钮，400ms 首发后每 150ms */
        if (s_page == UI_PAGE_TUNE && s_dlg == DLG_NONE) {
            uint32_t held = now - s_press_t0;
            int in_btn = ui_in(s_last_x, s_last_y, 700, 168, 140, 140)
                       || ui_in(s_last_x, s_last_y, 884, 168, 140, 140);
            if (in_btn && held >= 400u
                && now - s_last_lp_ms >= 150u) {
                s_last_lp_ms = now;
                s_press_lp_fired++;
                tune_step(ui_in(s_last_x, s_last_y, 700, 168, 140, 140) ? -1.0f : 1.0f);
            }
        }
        s_prev_touched = 1;
    } else if (s_prev_touched) {
        /* 抬手沿：去抖 + 长按未发过才触发单击 */
        s_prev_touched = 0;
        if (s_touch_down && now - s_touch_release_ok >= 30u) {
            if (s_press_lp_fired == 0)
                touch_tap(s_last_x, s_last_y);
            else {
                /* 长按后抬手：无额外动作（连发已生效）*/
            }
        }
        s_touch_down = 0;
    }
}

/* ====== 状态→脏区（500ms 扫描）====== */
static void ui_sync_state(void)
{
    static float last_tc1 = -999, last_tc2 = -999, last_body = -999;
    static uint8_t last_tec1 = 0xFF, last_tec2 = 0xFF, last_fan = 0xFF;
    static uint8_t last_mode = 0xFF;
    static uint16_t last_alarm = 0xFFFF;
    static uint8_t last_m207 = 0xFF, last_esp = 0xFF;

    if (s_page != UI_PAGE_HOME) { /* 调参页由操作自绘 */ }
    if (g_tcs.tc1_act != last_tc1) { last_tc1 = g_tcs.tc1_act; s_dirty |= D_TC1; }
    if (g_tcs.tc2_act != last_tc2) { last_tc2 = g_tcs.tc2_act; s_dirty |= D_TC2; }
    if (g_tcs.body_temp != last_body) { last_body = g_tcs.body_temp; s_dirty |= D_BODY; }
    if (g_tcs.tec1_on != last_tec1) { last_tec1 = g_tcs.tec1_on; s_dirty |= D_TC1 | D_BTNS; }
    if (g_tcs.tec2_on != last_tec2) { last_tec2 = g_tcs.tec2_on; s_dirty |= D_TC2; }
    if (g_tcs.fan_on != last_fan) { last_fan = g_tcs.fan_on; s_dirty |= D_FAN; }
    if (g_tcs.mode != last_mode) { last_mode = g_tcs.mode; s_dirty |= D_TOPBAR | D_BTNS; }
    if (g_tcs.alarm_bits != last_alarm) { last_alarm = g_tcs.alarm_bits; s_dirty |= D_TOPBAR; }
    if (g_tcs.m207_online != last_m207 || g_tcs.esp_link_ok != last_esp) {
        last_m207 = g_tcs.m207_online; last_esp = g_tcs.esp_link_ok;
        s_dirty |= D_TOPBAR;
    }
}

void tcs_ui_init(void)
{
    s_page = UI_PAGE_HOME;
    s_dlg = DLG_NONE;
    s_tune_sel = 0;
    s_tune_step = 0.1f;
    s_step_granted = 0;
    s_dirty = D_ALL;
    if (lcd_ready()) draw_home();
    tcs_log("[APP]", "ui init page=home lcd=%d touch=%d", lcd_ready(), touch_ready());
}

#ifdef TCS_DEBUG_UI
/* T1 调试页脚（WP-A.2b：默认关；WP-C 删 blackbox 后本块改挂 stat 计数器）*/
#include "blackbox.h"
static void draw_debug_footer(uint32_t now)
{
    gfx_fill_rect(0, 583, 1023, 599, 0x0000);
    gfx_draw_string(8, 583, blackbox_footer_text(now), 0xFFFF, 0x0000);
}
#else
static void draw_debug_footer(uint32_t now) { (void)now; }
#endif

/* ====== 屏诊断（console scrtest；WP-A.2d 噪声二分用）====== */
uint32_t tcs_ui_tap_count(void) { return s_tap_cnt; }

void tcs_ui_screen_clear(uint16_t rgb565)
{
    if (!lcd_ready()) return;
    gfx_clear(rgb565);
    gfx_flip();
}

void tcs_ui_screen_text(void)
{
    if (!lcd_ready()) return;
    gfx_clear(C_BG);
    ui_text16(16, 16, "ASCII16 0123 :/ -", C_TEXT, C_BG);
    ui_text24(16, 48, "ASCII24 012.3 -/yz", C_TEXT, C_BG);
    ui_text24(16, 88, "中文24 布防调参保存", C_TEXT, C_BG);
    ui_text64(16, 128, "-25.0", C_VALUE, C_BG);
    ui_text24(16, 210, "64px 温度大字标定", C_SUB, C_BG);
    gfx_flip();
}

void tcs_ui_force_home(void)
{
    if (!lcd_ready()) return;
    s_page = UI_PAGE_HOME;
    s_dlg = DLG_NONE;
    s_dirty = D_ALL;
    draw_home();
}

void tcs_ui_force_tune(void)
{
    if (!lcd_ready()) return;
    s_page = UI_PAGE_TUNE;
    s_tune_sel = 0;
    s_dlg = DLG_NONE;
    draw_tune();
}

/* 压测算子（WP-A.2d 分音定位遗留工具）*/
void tcs_ui_screen_stress(int mode, int n)
{
    if (!lcd_ready()) return;
    for (int i = 0; i < n; i++) {
        switch (mode) {
        case 0:                                   /* 纯 flip（back 无新绘制）*/
            break;
        case 1:                                   /* 横幅红底填充 + flip */
            gfx_fill_rect(200, 0, 767, 39, C_ALARM);
            break;
        case 2:                                   /* 横幅文本（红底白字）+ flip */
            ui_text24(208, 8, "EEPROM", C_WHITE, C_ALARM);
            break;
        case 3:                                   /* TEC 徽章灰填充 + flip */
            gfx_fill_rect(400, 72, 463, 111, C_DIS);
            break;
        case 4:                                   /* 64px 大字 + flip */
            ui_text64(32, 120, "-25.0", C_VALUE, C_CARD);
            break;
        default:
            return;
        }
        gfx_flip();
    }
}

void tcs_ui_poll(uint32_t now)
{
    touch_poll(now);

    /* 500ms 状态扫描 → 脏区；顶栏时间每秒刷 */
    static uint32_t last_sync = 0;
    if (now - last_sync >= 500u) {
        last_sync = now;
        ui_sync_state();
        if (s_page == UI_PAGE_HOME) s_dirty |= D_TOPBAR;
    }

    if (s_page != UI_PAGE_HOME || !s_dirty) return;

    /* 脏区局部重绘（back buffer 画完翻转）*/
    if (s_dirty & D_TOPBAR) { draw_topbar(); draw_debug_footer(now); }
    if (s_dirty & D_TC1) draw_tc_card(16, "贴身水箱", 0);
    if (s_dirty & D_TC2) draw_tc_card(528, "环境水箱", 1);
    if (s_dirty & D_BODY) draw_body_card();
    if (s_dirty & D_FAN) draw_fan_card();
    if (s_dirty & D_BTNS) draw_home_buttons();
    gfx_flip();
    s_dirty = 0;
}
