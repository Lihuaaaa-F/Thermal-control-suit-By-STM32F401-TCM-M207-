/* tui_p3.c — G5 连接页（镜像 tcs_ui draw_qr：同 qrcodegen 同参 → QR 矩阵一致） */
#include "tui.h"
#include "ui_common.h"
#include "qr/qrcodegen.h"

static void clk_back(lv_event_t * e) { LV_UNUSED(e); tui_goto(TUI_HOME); }

void tui_p3_build(lv_obj_t * scr)
{
    ui_text32(scr, 32, 16, "连接", TKS_TX);
    ui_rrect(scr, 392, 88, 240, 240, 8, TKS_WHITE, TKS_BG);

    static uint8_t qrc[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];   /* 静态化：4KB 栈缓冲→.bss */
    static uint8_t tmp[qrcodegen_BUFFER_LEN_FOR_VERSION(6)];
    if (qrcodegen_encodeText("http://192.168.4.1", tmp, qrc,
                             qrcodegen_Ecc_MEDIUM, 2, 6, -1, true)) {
        int nmod = qrcodegen_getSize(qrc);
        int mod = 200 / nmod;
        if (mod < 1) mod = 1;
        int x0 = 392 + (240 - nmod * mod) / 2;
        int y0 = 88 + (240 - nmod * mod) / 2;
        int qw = nmod * mod;
        static uint16_t s_qr[200 * 200] __attribute__((section(".axi_pool"), aligned(32)));
        if (qw > 200) return;
        uint8_t * buf = (uint8_t *)s_qr;
        uint16_t ctx = (uint16_t)lv_color_to_u16(TKS_TX);
        uint16_t cwh = (uint16_t)lv_color_to_u16(TKS_WHITE);
        for (int i = 0; i < qw * qw; i++) ((uint16_t *)buf)[i] = cwh;
        for (int y = 0; y < nmod; y++)
            for (int x = 0; x < nmod; x++)
                if (qrcodegen_getModule(qrc, x, y))
                    for (int yy = 0; yy < mod; yy++)
                        for (int xx = 0; xx < mod; xx++)
                            ((uint16_t *)buf)[(size_t)(y * mod + yy) * qw + x * mod + xx] = ctx;
        static lv_image_dsc_t dsc;
        dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
        dsc.header.cf = LV_COLOR_FORMAT_RGB565;
        dsc.header.w = (uint16_t)qw;
        dsc.header.h = (uint16_t)qw;
        dsc.header.stride = (uint16_t)(qw * 2);
        dsc.data_size = (uint32_t)qw * qw * 2;
        dsc.data = buf;
        lv_obj_t * img = lv_image_create(scr);
        lv_image_set_src(img, &dsc);
        lv_obj_set_pos(img, x0, y0);
    } else {
        ui_text24(scr, 416, 200, "二维码生成失败", TKS_RED);
    }

    ui_text24(scr, 292, 352, "热点名称", TKS_TX2);
    ui_aux20(scr, 412, 356, "TCS-XXXX", TKS_TX);
    ui_text24(scr, 292, 392, "热点密码", TKS_TX2);
    ui_aux20(scr, 412, 396, "12345678", TKS_TX);
    ui_text24(scr, 292, 432, "控制页", TKS_TX2);
    ui_aux20(scr, 412, 436, "http://192.168.4.1", TKS_TX);
    ui_aux20(scr, 896, 520, "fw v1.2.0", TKS_TX2);

    ui_btn(scr, 40, 472, 208, 96, "返回", 0, 32);
    lv_obj_t * z = lv_obj_create(scr);
    lv_obj_remove_style_all(z);
    lv_obj_set_pos(z, 40, 472);
    lv_obj_set_size(z, 208, 96);
    lv_obj_add_flag(z, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(z, clk_back, LV_EVENT_CLICKED, NULL);
}
