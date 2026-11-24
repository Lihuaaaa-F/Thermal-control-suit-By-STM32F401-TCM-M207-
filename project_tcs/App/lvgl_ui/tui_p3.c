/* tui_p3.c — 连接页：手机扫码连上设备热点，再用浏览器打开控制台
 * 热点名 / 密码 / 网址 / 主控固件版本来自主控（0x22 INFO），没收到之前显示"—"。
 * 二维码编的是 WIFI 凭据（手机相机一扫即连热点）；旧主控固件不发 INFO 时退回编网址。
 * 主控重发 INFO（热点被改名等）后整页重建一次，之后保持。 */
#include "tui.h"
#include "ui_common.h"
#include "link_info.h"
#include "qr/qrcodegen.h"
#include <stdio.h>

#define QR_BOX_X  96
#define QR_BOX_Y  100
#define QR_BOX    288
#define QR_MAX    232                 /* 二维码边长上限（模块取整后 ≤ 它）*/
#define COL_LX    440                 /* 左列：标签 */
#define COL_VX    620                 /* 右列：值 */

static uint32_t s_gen_at_build;
static uint8_t  s_built;

static void clk_back(lv_event_t *e) { LV_UNUSED(e); tui_request(TUI_HOME); }

static void info_row(lv_obj_t *scr, int y, const char *label, const char *value)
{
    ui_text24(scr, COL_LX, y + 6, label, TKS_TX2);
    ui_text32(scr, COL_VX, y, value, TKS_TX);
}

/* 主控没发 INFO / 这一项为空 → 显示 "--"（纯 ASCII，不赌字库里有没有某个符号形），
 * 绝不显示写死的占位串：2026-11-24 真机上 32px 的破折号字形就是个方框 */
static const char *or_none(const char *s) { return (s && s[0]) ? s : "--"; }

static void draw_qr(lv_obj_t *scr)
{
    static uint8_t qrc[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(10)];
    /* 放 AXIRAM 段（NOLOAD）：232×232×2 ≈ 105KB，不能进 .bss 的 DTCM，也不能开在栈上 */
    static uint16_t pixels[QR_MAX * QR_MAX] __attribute__((section(".axi_pool"), aligned(32)));
    static lv_image_dsc_t dsc;

    ui_box(scr, QR_BOX_X, QR_BOX_Y, QR_BOX, QR_BOX, TKS_WHITE, 8);   /* 白底 = 二维码静区 */
    char text[160];
    size_t n = link_info_wifi_qr(text, sizeof text);
    if (!n || !qrcodegen_encodeText(text, tmp, qrc, qrcodegen_Ecc_MEDIUM, 1, 10, -1, true)) {
        ui_text24(scr, QR_BOX_X + 64, QR_BOX_Y + 128, "二维码生成失败", TKS_RED);
        return;
    }
    int nmod = qrcodegen_getSize(qrc);
    int mod = QR_MAX / nmod;
    if (mod < 2) {                      /* 版本 10 最多 57 模块 → mod 最小 4；这里只是兜底 */
        ui_text24(scr, QR_BOX_X + 64, QR_BOX_Y + 128, "二维码生成失败", TKS_RED);
        return;
    }
    int qw = nmod * mod;
    int x0 = QR_BOX_X + (QR_BOX - qw) / 2;
    int y0 = QR_BOX_Y + (QR_BOX - qw) / 2;
    uint16_t cwh = (uint16_t)lv_color_to_u16(TKS_WHITE);
    uint16_t ctx = (uint16_t)lv_color_to_u16(TKS_TX);
    for (int i = 0; i < qw * qw; i++) pixels[i] = cwh;
    for (int y = 0; y < nmod; y++)
        for (int x = 0; x < nmod; x++)
            if (qrcodegen_getModule(qrc, x, y))
                for (int yy = 0; yy < mod; yy++)
                    for (int xx = 0; xx < mod; xx++)
                        pixels[(y * mod + yy) * qw + x * mod + xx] = ctx;
    dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc.header.w = (uint16_t)qw;
    dsc.header.h = (uint16_t)qw;
    dsc.header.stride = (uint16_t)(qw * 2);
    dsc.data_size = (uint32_t)qw * (uint32_t)qw * 2u;
    dsc.data = (const uint8_t *)pixels;
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &dsc);
    lv_obj_set_pos(img, x0, y0);
    lv_obj_set_clickable(img, false);
}

void tui_p3_build(lv_obj_t *scr)
{
    const tcs_link_info_t *li = link_info();
    char fw_screen[24];
    uint32_t v = g_tcs.fw_ver;
    snprintf(fw_screen, sizeof fw_screen, "%lu.%lu.%lu",
             (unsigned long)(v >> 16), (unsigned long)((v >> 8) & 0xFFu), (unsigned long)(v & 0xFFu));

    s_built = 0;
    ui_button(scr, 24, 8, 120, 40, "‹ 返回", UI_BTN_GHOST, &tcs_font_lv24, clk_back, NULL);
    ui_text32(scr, 176, 16, "连接", TKS_TX);      /* 32px 字库只有既有的字：新文案一律走 24px */

    draw_qr(scr);
    {
        /* 说明居中写在二维码正下方 */
        const char *cap = "手机扫码连接热点";
        ui_text24(scr, QR_BOX_X + (QR_BOX - ui_text24_w(cap)) / 2, QR_BOX_Y + QR_BOX + 20, cap, TKS_TX2);
    }

    info_row(scr, 112, "热点名称", or_none(li->valid ? li->ssid : NULL));
    info_row(scr, 176, "热点密码", or_none(li->valid ? li->pass : NULL));
    info_row(scr, 240, "控制页",   or_none(li->valid ? li->url  : NULL));
    info_row(scr, 304, "屏幕固件", fw_screen);
    info_row(scr, 368, "主控固件", or_none(li->valid ? li->fw   : NULL));
    ui_text24(scr, COL_LX, 452, "连上热点后，用手机浏览器打开控制页", TKS_TX2);

    s_gen_at_build = link_info_gen();
    s_built = 1;
}

/* 主控重发了内容不同的 INFO（最常见：这一页刚打开时还没收到过）→ 重建一次 */
int tui_p3_update(void)
{
    if (!s_built) return 1;
    return (link_info_gen() != s_gen_at_build) ? 1 : 0;
}
