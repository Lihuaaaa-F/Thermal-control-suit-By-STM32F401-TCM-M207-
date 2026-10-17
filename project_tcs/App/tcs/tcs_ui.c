/**
 * tcs_ui.c — 触摸 UI 实现（任务书 §3.7 像素规格 + P0 主页 / P1 调参页 / 弹窗）
 *
 * 复用 ltdc.c gfx_* 图元 + back→front flip（DCache 一致性由 ltdc.c write-through
 * 机制处理，不重造）。FT5446 20Hz 轮询；触摸去抖 30ms；抬手才触发（1→0 沿）；
 * 长按 400ms 后每 150ms 连发（调参 +/−）；同一时刻只响应第一个触点。
 *
 * 顶栏重叠说明（§3.7+C8 口径）：急停常驻图标 rect(120,0,80,40) 压在报警横幅
 * rect(128,0,640,40) 左端之上——横幅文字从 x=208 起画，触摸按区域分流。
 */
#include "tcs_ui.h"
#include "ltdc.h"
#include "ft5446.h"
#include "param_registry.h"
#include "control.h"
#include "segger_rtt.h"
#include <string.h>
#include <stdio.h>

/* ====== 调色板（§3.7）====== */
#define C_BG       RGB565(0x10,0x14,0x18)
#define C_CARD     RGB565(0x1B,0x21,0x29)
#define C_TEXT     RGB565(0xE8,0xEA,0xED)
#define C_VALUE    RGB565(0xFF,0xFF,0xFF)
#define C_ACCENT   RGB565(0x4F,0x8C,0xFF)
#define C_ALARM    RGB565(0xE5,0x48,0x4D)
#define C_OK       RGB565(0x46,0xA7,0x58)
#define C_DISABLED RGB565(0x5A,0x64,0x72)

#include "console.h"   /* tcs_log */

/* ====== 24×24 中文/符号字形（tools/gen_font_cn.py 生成）====== */
#include "tcs_font_cn.inc"
#define F24_W  24
#define F24_H  24
#define F24_BPR (F24_W / 8)          /* 3B/行 */
#define F24_SZ  (F24_BPR * F24_H)    /* 72B/字 */

static const unsigned int   *s_cp  = tcs_font24_cp;
static const unsigned char (*s_gl)[F24_SZ] = tcs_font24_glyphs;
#define F24_N (sizeof(tcs_font24_cp) / sizeof(tcs_font24_cp[0]))

/* 8×16 ASCII（复用 GS 已验证字库）*/
static const uint8_t s_f8x16[][16] = {
#include "font8x16.inc"
};

/* ====== 文本渲染 ====== */
static int f24_find(unsigned int cp)
{
    int lo = 0, hi = (int)F24_N - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (s_cp[mid] == cp) return mid;
        if (s_cp[mid] < cp) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

static void f24_draw(int x, int y, int gi, uint16_t fg, uint16_t bg)
{
    const unsigned char *g = s_gl[gi];
    for (int row = 0; row < F24_H; row++) {
        for (int col = 0; col < F24_W; col++) {
            uint8_t bits = g[row * F24_BPR + col / 8];
            gfx_draw_pixel(x + col, y + row,
                           (bits & (0x80 >> (col % 8))) ? fg : bg);
        }
    }
}

/* 24px 字符 advance（等宽栅格：CJK/ASCII 均 24px，HMI 风格）*/
static int ui_char24(int x, int y, unsigned int cp, uint16_t fg, uint16_t bg)
{
    int gi = f24_find(cp);
    if (gi < 0) gi = f24_find('?');
    if (gi >= 0) f24_draw(x, y, gi, fg, bg);
    return F24_W;
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

/* 48px = 24px 字形 2× 最近邻放大（数值大字/开关大字）*/
static void ui_char48(int x, int y, unsigned int cp, uint16_t fg, uint16_t bg)
{
    int gi = f24_find(cp);
    if (gi < 0) gi = f24_find('?');
    if (gi < 0) return;
    const unsigned char *g = s_gl[gi];
    for (int row = 0; row < F24_H; row++) {
        for (int col = 0; col < F24_W; col++) {
            uint8_t bits = g[row * F24_BPR + col / 8];
            if (bits & (0x80 >> (col % 8))) {
                gfx_draw_hline(x + col * 2, x + col * 2 + 1, y + row * 2, fg);
                gfx_draw_hline(x + col * 2, x + col * 2 + 1, y + row * 2 + 1, fg);
            } else if (bg != 0xFFFF) {
                gfx_draw_hline(x + col * 2, x + col * 2 + 1, y + row * 2, bg);
                gfx_draw_hline(x + col * 2, x + col * 2 + 1, y + row * 2 + 1, bg);
            }
        }
    }
}

static void ui_text48(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    while (*s) {
        unsigned int cp;
        int n = utf8_next(s, &cp);
        if (n == 0) break;
        ui_char48(x, y, cp, fg, bg);
        x += F24_W * 2;
        s += n;
    }
}

/* ====== 基础控件 ====== */
static void ui_btn(int x, int y, int w, int h, uint16_t bgc,
                   const char *label, uint16_t fgc, int label24)
{
    gfx_fill_rect(x, y, x + w - 1, y + h - 1, bgc);
    int lw = label24 ? ui_text24_w(label) : (int)strlen(label) * 8;
    int lx = x + (w - lw) / 2;
    int ly = y + (h - (label24 ? 24 : 16)) / 2;
    if (label24) ui_text24(lx, ly, label, fgc, bgc);
    else         ui_text16(lx, ly, label, fgc, bgc);
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

/* ====== 主页绘制（P0 像素规格）====== */
static void draw_topbar(void)
{
    gfx_fill_rect(0, 0, LCD_WIDTH - 1, 39, C_CARD);
    /* 模式徽标 (0,0,120,40) */
    int auto_m = (g_tcs.mode != 0);
    gfx_fill_rect(0, 0, 119, 39, auto_m ? C_ACCENT : C_DISABLED);
    ui_text24((120 - 48) / 2, 8, auto_m ? "自动" : "手动", C_VALUE, auto_m ? C_ACCENT : C_DISABLED);

    /* 急停常驻图标 (120,0,80,40)（压横幅左端，触摸分流见 touch 处理）*/
    gfx_fill_rect(120, 0, 199, 39, C_ALARM);
    ui_text24(120 + (80 - 48) / 2, 8, "急停", C_VALUE, C_ALARM);

    /* 报警横幅 (128,0,640,40)：有报警红底白字最紧急一条；无报警并入底色 */
    if (g_tcs.alarm_bits) {
        const char *msg = "ALARM";
        if (g_tcs.alarm_bits & ALM_ESTOP)        msg = "急停已触发 确认恢复请点此横幅";
        else if (g_tcs.alarm_bits & ALM_OVERTEMP) msg = "水温过高(>48°C)";
        else if (g_tcs.alarm_bits & ALM_BODY_SENSOR) msg = "体温传感器失联 确认恢复请点此横幅";
        else if (g_tcs.alarm_bits & ALM_M207)    msg = "M207 失联 确认恢复请点此横幅";
        else if (g_tcs.alarm_bits & ALM_EEPROM)  msg = "EEPROM 配置无效 已恢复默认";
        gfx_fill_rect(200, 0, 767, 39, C_ALARM);
        /* 滚动：4px/200ms 循环 */
        extern uint32_t millis(void);
        int w = ui_text24_w(msg);
        int span = w + 560;
        int off = (int)((millis() / 200u) % (unsigned)(span ? span : 1));
        int x0 = 768 - off;
        if (x0 > 208) x0 = 208;
        ui_text24(x0, 8, msg, C_VALUE, C_ALARM);
    }

    /* 链路 (776,0,120,40) 16px */
    gfx_fill_rect(768, 0, LCD_WIDTH - 1, 39, C_CARD);
    char ln[24];
    snprintf(ln, sizeof(ln), "M207%c ESP%c",
             g_tcs.m207_online ? '+' : '-', g_tcs.esp_link_ok ? '+' : '-');
    ui_text16(776, 12, ln, (g_tcs.m207_online && g_tcs.esp_link_ok) ? C_OK : C_ALARM, C_CARD);

    /* 时间 (904,0,120,40)：开机时长 HH:MM:SS */
    extern uint32_t millis(void);
    uint32_t t = millis() / 1000u;
    char ts[12];
    snprintf(ts, sizeof(ts), "%02lu:%02lu:%02lu",
             (unsigned long)(t / 3600u), (unsigned long)((t / 60u) % 60u),
             (unsigned long)(t % 60u));
    ui_text16(904, 12, ts, C_TEXT, C_CARD);
}

static void draw_tc_card(int cx, const char *title, int ch,
                         uint16_t dirty_bit)
{
    (void)dirty_bit;
    gfx_fill_rect(cx, 56, cx + 480 - 1, 56 + 224 - 1, C_CARD);
    gfx_draw_rect(cx, 56, cx + 480 - 1, 56 + 224 - 1, C_DISABLED);
    ui_text24(cx + 16, 72, title, C_TEXT, C_CARD);

    /* TEC 徽标 (400,72,80,40) 相对卡内 x+384 */
    uint8_t tec_on = ch ? g_tcs.tec2_on : g_tcs.tec1_on;
    uint16_t bc = tec_on ? C_OK : C_DISABLED;
    gfx_fill_rect(cx + 384, 72, cx + 464 - 1, 112 - 1, bc);
    ui_text24(cx + 384 + (80 - 48) / 2, 80, tec_on ? "运行" : "停止", C_VALUE, bc);

    /* 实际温 48px (32,120,240,72) 相对 → (cx+16, 120) */
    float act = ch ? g_tcs.tc2_act : g_tcs.tc1_act;
    char big[16];
    snprintf(big, sizeof(big), "%.1f", (double)act);
    ui_text48(cx + 16, 120, big, C_VALUE, C_CARD);
    ui_text24(cx + 264, 168, "°C", C_TEXT, C_CARD);

    /* 设定行 (32,216) */
    float setv = param_get(ch ? PARAM_ID_TC2_SET : PARAM_ID_TC1_SET);
    char setl[48];
    snprintf(setl, sizeof(setl), "设定 %.1f°C", (double)setv);
    ui_text24(cx + 16, 216, setl, C_TEXT, C_CARD);
}

static void draw_body_card(void)
{
    gfx_fill_rect(16, 296, 16 + 480 - 1, 296 + 152 - 1, C_CARD);
    uint16_t border = g_tcs.body_sensor_ok ? C_DISABLED : C_ALARM;
    gfx_draw_rect(16, 296, 16 + 480 - 1, 296 + 152 - 1, border);
    ui_text24(32, 300, "体温", C_TEXT, C_CARD);
    char big[16];
    if (g_tcs.body_sensor_ok) snprintf(big, sizeof(big), "%.1f", (double)g_tcs.body_temp);
    else                      snprintf(big, sizeof(big), "--.-");
    ui_text48(32, 328, big, g_tcs.body_sensor_ok ? C_VALUE : C_ALARM, C_CARD);
    ui_text24(32 + 4 * 48 + 8, 376, "°C", C_TEXT, C_CARD);
}

static void draw_fan_card(void)
{
    gfx_fill_rect(528, 296, 528 + 480 - 1, 296 + 152 - 1, C_CARD);
    gfx_draw_rect(528, 296, 528 + 480 - 1, 296 + 152 - 1, C_DISABLED);
    ui_text24(544, 300, "风扇", C_TEXT, C_CARD);
    ui_text48(544, 328, g_tcs.fan_on ? "开" : "关",
              g_tcs.fan_on ? C_OK : C_DISABLED, C_CARD);
}

static void draw_home_buttons(void)
{
    /* 底部按钮 y=544,h=56 */
    ui_btn(16, 544, 220, 56, C_CARD, "调参", C_TEXT, 1);
    int auto_m = (g_tcs.mode != 0);
    ui_btn(252, 544, 220, 56, C_CARD, auto_m ? "切手动" : "切自动", C_TEXT, 1);
    /* TEC1 启停：自动模式禁用灰 */
    uint16_t tecbg = auto_m ? C_DISABLED : (g_tcs.tec1_on ? C_OK : C_CARD);
    ui_btn(488, 544, 220, 56, tecbg, "TEC1 启停", auto_m ? C_VALUE : C_TEXT, 1);
    ui_btn(788, 544, 220, 56, C_ALARM, "急停", C_VALUE, 1);
}

static void draw_home(void)
{
    gfx_clear(C_BG);
    draw_topbar();
    draw_tc_card(16, "贴身水箱", 0, D_TC1);
    draw_tc_card(528, "环境水箱", 1, D_TC2);
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

    /* 页顶：标题 + 常驻灰字说明（调参即实时生效，保存=EEPROM 固化）*/
    ui_text24(32, 8, "调参", C_TEXT, C_BG);
    ui_text16(300, 12, "实时生效中  保存=断电保持(EEPROM)", C_DISABLED, C_BG);

    /* 列表 8 行 (16,56+i*56,640,48) */
    for (int i = 0; i < 8 && i < n; i++) {
        const param_desc_t *p = param_find((uint16_t)ids[i]);
        if (!p) continue;
        int y = 56 + i * 56;
        gfx_fill_rect(16, y, 16 + 640 - 1, y + 48 - 1, C_CARD);
        if (i == s_tune_sel)
            gfx_draw_rect(16, y, 16 + 640 - 1, y + 48 - 1, C_ACCENT);
        ui_text24(32, y + 12, p->name, C_TEXT, C_CARD);
        char vs[32];
        if (p->id == PARAM_ID_MODE)
            snprintf(vs, sizeof(vs), "%s", p->value != 0.0f ? "自动" : "手动");
        else
            snprintf(vs, sizeof(vs), "%.1f%s", (double)p->value, p->unit);
        int vw = ui_text24_w(vs);
        ui_text24(16 + 480 - vw, y + 12, vs, C_VALUE, C_CARD);
    }

    /* −/+ 大按钮 (700,168,140,140)/(884,168,140,140) */
    ui_btn(700, 168, 140, 140, C_CARD, "-", C_VALUE, 1);
    ui_btn(884, 168, 140, 140, C_CARD, "+", C_VALUE, 1);

    /* 步进切换 (700,336,324,56) */
    char step_l[32];
    snprintf(step_l, sizeof(step_l), "步进 %.1f", (double)s_tune_step);
    ui_btn(700, 336, 324, 56, C_CARD, step_l, C_ACCENT, 1);

    /* 范围提示 (700,408,324,40) */
    const param_desc_t *sp = (s_tune_sel < n) ? param_find((uint16_t)ids[s_tune_sel]) : 0;
    if (sp) {
        char rg[64];
        snprintf(rg, sizeof(rg), "范围 %.1f ~ %.1f %s",
                 (double)sp->min, (double)sp->max, sp->unit);
        ui_text24(700 + 12, 414, rg, C_DISABLED, C_BG);
    }

    ui_btn(16, 544, 200, 56, C_CARD, "返回", C_TEXT, 1);
    ui_btn(700, 544, 324, 56, C_ACCENT, "保存并返回", C_VALUE, 1);
    gfx_flip();
    s_dirty = 0;
}

/* ====== 弹窗 ====== */
static void draw_dialog(void)
{
    /* 全屏半透明遮罩（RGB565 无 alpha → 深色遮罩色近似）*/
    for (int y = 40; y < LCD_HEIGHT; y += 2)
        for (int x = 0; x < LCD_WIDTH; x += 2)
            gfx_draw_pixel(x, y, RGB565(0x08, 0x0A, 0x0C));

    if (s_dlg == DLG_ESTOP) {
        ui_text24(312, 200, "确认急停？", C_ALARM, C_BG);
        ui_text24(232, 240, "TEC 将全部停止，风扇全开", C_TEXT, C_BG);
        ui_btn(412, 300, 200, 64, C_ALARM, "确认", C_VALUE, 1);
        ui_btn(612, 300, 200, 64, C_CARD, "取消", C_TEXT, 1);
    } else if (s_dlg == DLG_MODE_AUTO) {
        ui_text24(392, 240, "切换到自动模式？", C_TEXT, C_BG);
        ui_btn(412, 300, 200, 64, C_ACCENT, "确认", C_VALUE, 1);
        ui_btn(612, 300, 200, 64, C_CARD, "取消", C_TEXT, 1);
    } else if (s_dlg == DLG_SETCONFIRM) {
        char msg[48];
        snprintf(msg, sizeof(msg), "设定 %.1f°C 超过确认阈值",
                 (double)s_pending_set);
        ui_text24(232, 240, msg, C_ALARM, C_BG);
        ui_btn(412, 300, 200, 64, C_ALARM, "确认", C_VALUE, 1);
        ui_btn(612, 300, 200, 64, C_CARD, "取消", C_TEXT, 1);
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
    draw_tune();   /* 值变更行重绘（页小，整页重绘 <6ms）*/
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
static void touch_tap(int x, int y)
{
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
        if (ui_in(x, y, 120, 0, 80, 40)) {             /* 常驻急停图标 */
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
        if (ui_in(x, y, 788, 544, 220, 56)) {          /* 急停（大按钮→弹确认）*/
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
    if (s_dirty & D_TOPBAR) draw_topbar();
    if (s_dirty & D_TC1) draw_tc_card(16, "贴身水箱", 0, D_TC1);
    if (s_dirty & D_TC2) draw_tc_card(528, "环境水箱", 1, D_TC2);
    if (s_dirty & D_BODY) draw_body_card();
    if (s_dirty & D_FAN) draw_fan_card();
    if (s_dirty & D_BTNS) draw_home_buttons();
    gfx_flip();
    s_dirty = 0;
}
