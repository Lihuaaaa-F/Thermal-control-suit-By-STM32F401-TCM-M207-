/* tui_diag.c — G5 诊断页（VL-12 极端内容 / U-6 错误码人话表 / TL-18 全字符集轮播）
 *
 * 这些页面只在 g_ui_cmd 100..103 触发时构建（探针走查用），不进正常导航路径。
 * 机器判定载体：g_diag_stat[]（probe 直读）把"最长串宽度 / 卡片内宽 / 缺字数"
 * 这类结论变成可复算的数字，避免"看图说话"。
 */
#include "tui.h"
#include "ui_common.h"
#include "control.h"
#include "tui_charset_data.h"
#include <stdio.h>
#include <string.h>

LV_FONT_DECLARE(tcs_font_lv_ext32);

/* [0]=magic 'DIA1' [1]=最长参数名 px [2]=最长单位 px [3]=卡片内宽 px
 * [4]=最长串总宽 px [5]=错误码行数 [6]=空串宽度 px [7]=TL-18 缺字数
 * [8]=已检查字符数 [9]=TL-18 使用的字体档位 */
volatile uint32_t g_diag_stat[12];

/* M207 错误码人话表（U-6：禁止裸代码上屏；数字仅作"内部代码"辅助列） */
typedef struct { uint8_t code; const char * text; } err_row_t;
static const err_row_t s_err[] = {
    { 0, "模块无异常" },
    { 1, "设置成功，模块已确认" },
    { 2, "模块不认识这个参数名" },
    { 3, "该参数只读，不能保存" },
    { 4, "设定值超出模块允许范围" },
    { 5, "模块当前无法执行该操作" },
    { 6, "命令格式有误，未被接受" },
    { 7, "数据校验不通过，请重试" },
    { 8, "参数已保存到模块" },
};
#define ERR_N (sizeof(s_err)/sizeof(s_err[0]))

const char * m207_errcode_text(uint8_t code)
{
    for (unsigned i = 0; i < ERR_N; i++)
        if (s_err[i].code == code) return s_err[i].text;
    return "未知返回码，请联系维护";
}

/* ---------- TL-18：字符集覆盖检查（逐字查字形描述符，不经 fallback） ---------- */
static uint32_t glyph_missing(const lv_font_t * f, const uint16_t * cps, uint32_t n)
{
    uint32_t miss = 0;
    lv_font_glyph_dsc_t d;
    for (uint32_t i = 0; i < n; i++)
        if (!f->get_glyph_dsc(f, &d, cps[i], 0)) miss++;
    return miss;
}

static uint32_t tl18_check(void)
{
    uint32_t miss = 0;
    miss += glyph_missing(&tcs_font_lv24,  TUI_CP24CORE, TUI_CP24CORE_N);
    miss += glyph_missing(&tcs_font_lv_ext, TUI_CP24EXT,  TUI_CP24EXT_N);
    miss += glyph_missing(&tcs_font_lv32,  TUI_CP32CORE, TUI_CP32CORE_N);
    miss += glyph_missing(&tcs_font_lv_ext32, TUI_CP32EXT, TUI_CP32EXT_N);
    miss += glyph_missing(&tcs_font_lv48,  TUI_CP48CORE, TUI_CP48CORE_N);
    miss += glyph_missing(&tcs_font_lv96,  TUI_CP96CORE, TUI_CP96CORE_N);
    miss += glyph_missing(&tcs_font_lv20,  TUI_CP20CORE, TUI_CP20CORE_N);
    g_diag_stat[8] = TUI_CP24CORE_N + TUI_CP24EXT_N + TUI_CP32CORE_N + TUI_CP32EXT_N
                   + TUI_CP48CORE_N + TUI_CP96CORE_N + TUI_CP20CORE_N;
    g_diag_stat[7] = miss;
    return miss;
}

/* ---------- UTF-8 编码 + 分页行串 ---------- */
static int cp2utf8(uint32_t cp, char * o)
{
    if (cp < 0x80) { o[0] = (char)cp; return 1; }
    if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 0x3F)); return 2; }
    o[0] = (char)(0xE0 | (cp >> 12));
    o[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    o[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
}

/* 把一段码点按每行 per 个拼成行串（空格分隔） */
static void row_text(const uint16_t * cps, uint32_t n, uint32_t start,
                     uint32_t per, char * out, size_t cap)
{
    size_t k = 0;
    out[0] = '\0';
    for (uint32_t i = start; i < n && i < start + per; i++) {
        char b[4];
        int m = cp2utf8(cps[i], b);
        if (k + (size_t)m + 2 >= cap) break;
        if (k) out[k++] = ' ';
        memcpy(out + k, b, (size_t)m);
        k += (size_t)m;
        out[k] = '\0';
    }
}

static void tl18_page(lv_obj_t * scr, const uint16_t * cps, uint32_t n,
                      const lv_font_t * f, uint32_t per, int row_h, const char * tag)
{
    char title[64], line[160];
    snprintf(title, sizeof title, "TL-18 %s  共 %u 字（20/行）", tag, (unsigned)n);
    ui_text24(scr, 16, 8, title, TKS_TX);
    uint32_t rows = (n + per - 1) / per;
    int y = 40;
    for (uint32_t r = 0; r < rows && y + row_h <= 596; r++) {
        row_text(cps, n, r * per, per, line, sizeof line);
        ui_lab(scr, 16, y, f, TKS_TX, line);
        y += row_h;
    }
}

/* ---------- VL-12：极端内容 ---------- */
static void vl12_page(lv_obj_t * scr)
{
    /* 扫注册表取最长参数名/单位（真实数据，不是编的） */
    const param_desc_t * pn = NULL, * pu = NULL;
    int best_name = -1, best_unit = -1;
    for (int i = 0; i < param_count(); i++) {
        const param_desc_t * p = param_at(i);
        int ln = (int)strlen(p->name ? p->name : "");
        int lu = (int)strlen(p->unit ? p->unit : "");
        if (ln > best_name) { best_name = ln; pn = p; }
        if (lu > best_unit) { best_unit = lu; pu = p; }
    }
    char b[128];

    ui_text24(scr, 16, 8, "VL-12 极端内容（最长名/极值/空串/符号）", TKS_TX);

    int x = 16, w = 460, inner = w - 32;
    ui_card(scr, x,  44, w, 84, TKS_CARD, TKS_BORDER);
    ui_text24(scr, x + 16, 56, "最长参数名（注册表实取）", TKS_TX2);
    snprintf(b, sizeof b, "%s", pn ? pn->name : "?");
    ui_text24(scr, x + 16, 84, b, TKS_TX);
    g_diag_stat[1] = (uint32_t)ui_text24_w(b);

    ui_card(scr, x, 140, w, 84, TKS_CARD, TKS_BORDER);
    ui_text24(scr, x + 16, 152, "最长单位", TKS_TX2);
    snprintf(b, sizeof b, "[%s]  -50.00 / 200.00", pu && pu->unit && pu->unit[0] ? pu->unit : "无单位");
    ui_text24(scr, x + 16, 180, b, TKS_TX);
    g_diag_stat[2] = (uint32_t)ui_text24_w(b);

    ui_card(scr, x, 236, w, 84, TKS_CARD, TKS_BORDER);
    ui_text24(scr, x + 16, 248, "数值极值与负号", TKS_TX2);
    ui_text24(scr, x + 16, 276, "-0.01  -50.00  200.00  999.9", TKS_TX);

    ui_card(scr, x, 332, w, 84, TKS_CARD, TKS_BORDER);
    ui_text24(scr, x + 16, 344, "空字符串（应零宽、不报错）", TKS_TX2);
    ui_text24(scr, x + 16, 372, "", TKS_TX);
    g_diag_stat[6] = (uint32_t)ui_text24_w("");

    ui_card(scr, x, 428, w, 84, TKS_CARD, TKS_BORDER);
    ui_text24(scr, x + 16, 440, "全角/符号混排", TKS_TX2);
    ui_text24(scr, x + 16, 468, "°C  ℃  25.1  ●○ ▲ ▼ — ‹ › · ‰", TKS_TX);

    /* 错误码人话表（U-6） */
    int rx = 500, rw = 508;
    ui_card(scr, rx, 44, rw, 468, TKS_CARD, TKS_BORDER);
    ui_text24(scr, rx + 16, 56, "M207 返回码 → 人话（U-6）", TKS_TX2);
    int y = 88;
    for (unsigned i = 0; i < ERR_N; i++) {
        snprintf(b, sizeof b, "%u", (unsigned)s_err[i].code);
        ui_text24(scr, rx + 16, y, b, TKS_GRAY);
        ui_text24(scr, rx + 48, y, s_err[i].text, TKS_TX);
        y += 44;
    }
    g_diag_stat[5] = ERR_N;
    g_diag_stat[3] = (uint32_t)inner;

    /* 最长串总宽（取几张卡里最宽的实际渲染宽度） */
    uint32_t mx = g_diag_stat[1] > g_diag_stat[2] ? g_diag_stat[1] : g_diag_stat[2];
    for (unsigned i = 0; i < ERR_N; i++) {
        uint32_t ww = (uint32_t)ui_text24_w(s_err[i].text) + 48;
        if (ww > mx) mx = ww;
    }
    g_diag_stat[4] = mx;
}

void tui_diag_build(lv_obj_t * scr, int mode)
{
    g_diag_stat[0] = 0x44494131u;   /* 'DIA1' */
    switch (mode) {
        case 0: vl12_page(scr); break;
        case 1:
            tl18_check();
            g_diag_stat[9] = 24;
            tl18_page(scr, TUI_CP24CORE, TUI_CP24CORE_N, &tcs_font_lv24, 20, 32, "24px 主体");
            tl18_page(scr, TUI_CP24EXT, TUI_CP24EXT_N, &tcs_font_lv_ext, 20, 32, "24px 扩展");
            break;
        case 2:
            tl18_check();
            g_diag_stat[9] = 32;
            tl18_page(scr, TUI_CP32CORE, TUI_CP32CORE_N, &tcs_font_lv32, 20, 40, "32px 主体");
            tl18_page(scr, TUI_CP32EXT, TUI_CP32EXT_N, &tcs_font_lv_ext32, 20, 40, "32px 扩展");
            break;
        case 3:
            tl18_check();
            g_diag_stat[9] = 48;
            tl18_page(scr, TUI_CP48CORE, TUI_CP48CORE_N, &tcs_font_lv48, 12, 60, "48px");
            tl18_page(scr, TUI_CP96CORE, TUI_CP96CORE_N, &tcs_font_lv96, 12, 110, "96px");
            break;
        default: break;
    }
}
