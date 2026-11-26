#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""regen_font_cells.py — 24/32px 点阵字库重生成（加高格 + 4bpp 抗锯齿）

背景（2026-11-27 用户现场 U4）：
  - 旧 24px 表把 msyh 24px 字形用 `anchor="mm"` 塞进 24×24 全盒：msyh 墨迹在基线上 21px / 下 6px，
    **148/265 个字形（129 个汉字）底部被切 2–4px**（横幅、顶栏状态胶囊最明显）。
  - 旧 32px 表同一口径（32×32 盒，墨迹上 28 / 下 8）→ 页标题底部也会切 ~4px。
  - 两张表都是 1bpp，边缘锯齿明显。

本脚本按同一字源（msyh.ttc）重生成四张表：
  - 24px：格 26×28、基线第 21 行、line_height=28；主表 265 字 + 扩展表 171 字；
  - 32px：格 34×38、基线第 29 行、line_height=38；主表 138 字 + 扩展表（ext32 码表）；
  - 4bpp 抗锯齿：4× 超采样（96/128px）后 LANCZOS 缩回，量化到 0..15；
  - dsc：box = 格尺寸、ofs_x=-1、ofs_y=0、adv 初值 = 字号<<4（ASCII 由
    tools/font_metrics_patch.py 按墨迹收窄）；PLAIN、行首字节对齐（stride=1）。

字符集来源（不改码表文件）：
  - 24px 主表：Core/Inc/tcs_font_cn.inc 的 tcs_font24_cp；
  - 32px 主表：同文件 tcs_font32_cp；
  - 扩展表：fonts/tcs_font_lv_ext_cp.txt / tcs_font_lv_ext32_cp.txt。

用法（在 project_tcs/ 下）：
  python tools/regen_font_cells.py            # 重生成四张表
  python tools/regen_font_cells.py --check    # 只校验现有 .c 与本脚本口径一致
重生成后必须跑：python tools/font_metrics_patch.py（ASCII 比例步进门禁）。
依赖：Pillow；msyh.ttc。
"""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "App" / "lvgl_ui" / "fonts"
INC = ROOT / "Core" / "Inc" / "tcs_font_cn.inc"
FONT_PATH = r"C:\Windows\Fonts\msyh.ttc"
SS = 4                     # 超采样倍率
BPP = 4                    # 4bpp 抗锯齿


def load_inc_cps(table):
    txt = INC.read_text(encoding="utf-8")
    m = re.search(table + r"_cp\[\d+\]\s*=\s*\{(.*?)\};", txt, re.S)
    cps = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+", m.group(1))]
    assert cps == sorted(cps) and len(set(cps)) == len(cps), table + " 码表必须升序无重复"
    return cps


def load_txt_cps(name):
    p = FONTS / (name + "_cp.txt")
    cps = []
    for ln in p.read_text(encoding="utf-8", errors="ignore").splitlines():
        ln = ln.strip()
        if not ln or ln.startswith("#"):
            continue
        try:
            cps.append(int(ln, 16))
        except ValueError:
            pass
    cps = sorted(set(cps))
    assert cps and all(c >= 0x20 for c in cps), name + " 码表异常"
    return cps


class Job:
    def __init__(self, name, cps, size, cw, ch, base_row, fallback, note):
        self.name, self.cps, self.size = name, cps, size
        self.cw, self.ch, self.base = cw, ch, base_row
        self.fallback, self.note = fallback, note
        self.stride = (cw * BPP + 7) // 8


def render_cell(job, font_small, font_big, cp):
    ch = chr(cp)
    blank = bytes(job.ch * job.stride)
    if cp == 0x20:
        return blank
    b = font_small.getbbox(ch, anchor="ls")
    if b is None:
        return blank
    xoff = int(round((job.cw - (b[2] - b[0])) / 2.0)) - b[0]
    big = Image.new("L", (job.cw * SS, job.ch * SS), 0)
    d = ImageDraw.Draw(big)
    d.text((xoff * SS, job.base * SS), ch, font=font_big, fill=255, anchor="ls")
    img = big.resize((job.cw, job.ch), Image.LANCZOS)
    out = bytearray(job.ch * job.stride)
    px = img.load()
    for y in range(job.ch):
        for x in range(job.cw):
            v = (px[x, y] * 15 + 127) // 255
            if v == 0:
                continue
            if x & 1:
                out[y * job.stride + (x >> 1)] |= v          # 低半字节 = 奇像素
            else:
                out[y * job.stride + (x >> 1)] |= v << 4     # 高半字节 = 偶像素
    return bytes(out)


def emit(job, cells):
    n = len(job.cps)
    L = []
    L.append("/*******************************************************************************")
    L.append(" * %s — %dpx 字库（%dx%d 格，4bpp；2026-11-27 U4 重生成）" % (job.name, job.size, job.cw, job.ch))
    L.append(" * 源：msyh.ttc @%dpx；tools/regen_font_cells.py 生成，勿手改" % job.size)
    L.append(" * 口径：4bpp 抗锯齿（高半字节=偶像素，低半字节=奇像素）；box %dx%d ofs(-1,0)；" % (job.cw, job.ch))
    L.append(" *       公共基线格内第 %d 行；line_height=%d base_line=0；adv 初值 %d<<4（ASCII 由 metrics 收窄）"
             % (job.base, job.ch, job.size))
    L.append(" * %s" % job.note)
    L.append(" ******************************************************************************/")
    L.append("")
    L.append("#ifdef __has_include")
    L.append('    #if __has_include("lvgl.h")')
    L.append("        #ifndef LV_LVGL_H_INCLUDE_SIMPLE")
    L.append("            #define LV_LVGL_H_INCLUDE_SIMPLE")
    L.append("        #endif")
    L.append("    #endif")
    L.append("#endif")
    L.append("")
    L.append("#ifdef LV_LVGL_H_INCLUDE_SIMPLE")
    L.append('    #include "lvgl.h"')
    L.append("#else")
    L.append('    #include "../../lvgl.h"')
    L.append("#endif")
    L.append("")
    if job.fallback != "NULL":
        L.append("LV_FONT_DECLARE(%s);" % job.fallback)
        L.append("")
    L.append("#ifndef %s" % job.name.upper())
    L.append("    #define %s 1" % job.name.upper())
    L.append("")
    L.append("/*----------------")
    L.append(" *  BITMAP")
    L.append(" *--------------*/")
    L.append("static const uint8_t glyph_bitmap[] = {")
    for i, cell in enumerate(cells):
        L.append("    /* U+%04X */" % job.cps[i])
        for y in range(job.ch):
            L.append("    " + "".join("0x%02X," % cell[y * job.stride + k] for k in range(job.stride)))
    L.append("};")
    L.append("")
    L.append("static const lv_font_fmt_txt_glyph_dsc_t glyph_dsc[] = {")
    L.append('    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0}, /* id = 0 reserved */')
    for i, cp in enumerate(job.cps):
        L.append("    {.bitmap_index = %d, .adv_w = %d, .box_w = %d, .box_h = %d, "
                 ".ofs_x = -1, .ofs_y = 0}, /* U+%04X */"
                 % (i * job.ch * job.stride, job.size << 4, job.cw, job.ch, cp))
    L.append("};")
    L.append("")
    L.append("static const uint16_t unicode_list_0[] = {")
    for i in range(0, n, 16):
        L.append("    " + ", ".join("0x%04X" % (c - 0x20) for c in job.cps[i:i + 16]) + ",")
    L.append("};")
    L.append("")
    L.append("static const lv_font_fmt_txt_cmap_t cmaps[] = {")
    L.append("    {")
    L.append("        .range_start = 32, .range_length = 65535 - 32, .glyph_id_start = 1,")
    L.append("        .unicode_list = unicode_list_0, .glyph_id_ofs_list = NULL, .list_length = %d," % n)
    L.append("        .type = LV_FONT_FMT_TXT_CMAP_SPARSE_TINY")
    L.append("    }")
    L.append("};")
    L.append("")
    L.append("static const lv_font_fmt_txt_dsc_t font_dsc = {")
    L.append("    .glyph_bitmap = glyph_bitmap,")
    L.append("    .glyph_dsc = glyph_dsc,")
    L.append("    .cmaps = cmaps,")
    L.append("    .kern_dsc = NULL,")
    L.append("    .kern_scale = 0,")
    L.append("    .cmap_num = 1,")
    L.append("    .bpp = %d," % BPP)
    L.append("    .kern_classes = 0,")
    L.append("    .stride = 1,   /* 行首字节对齐（fmt_txt stride=0 是行间比特连续）*/")
    L.append("    .bitmap_format = 0,   /* PLAIN，4bpp；行 stride=ceil(%d*4/8)=%d */" % (job.cw, job.stride))
    L.append("};")
    L.append("")
    L.append("/*-----------------")
    L.append(" *  PUBLIC FONT")
    L.append(" *----------------*/")
    L.append("const lv_font_t %s = {" % job.name)
    L.append("    .get_glyph_dsc = lv_font_get_glyph_dsc_fmt_txt,")
    L.append("    .get_glyph_bitmap = lv_font_get_bitmap_fmt_txt,")
    L.append("    .line_height = %d," % job.ch)
    L.append("    .base_line = 0,")
    L.append("#if LV_VERSION_CHECK(9, 6, 0) || LVGL_VERSION_MAJOR >= 10")
    L.append("    .cap_height = %d, .x_height = %d," % (job.ch, job.ch))
    L.append("#endif")
    L.append("    .subpx = LV_FONT_SUBPX_NONE,")
    L.append("    .underline_position = 0,")
    L.append("    .underline_thickness = 1,")
    L.append("    .static_bitmap = 1,")
    L.append("    .dsc = &font_dsc,")
    L.append("    .fallback = %s," % ("NULL" if job.fallback == "NULL" else "&" + job.fallback))
    L.append("    .user_data = NULL,")
    L.append("};")
    L.append("")
    L.append("#endif")
    return "\n".join(L) + "\n"


def main():
    check = "--check" in sys.argv
    jobs = [
        Job("tcs_font_lv24", load_inc_cps("tcs_font24"), 24, 26, 28, 21,
            "tcs_font_lv_ext", "主表（Core/Inc/tcs_font_cn.inc 的 24px 码表）；链条 24→ext→32"),
        Job("tcs_font_lv_ext", load_txt_cps("tcs_font_lv_ext"), 24, 26, 28, 21,
            "tcs_font_lv32", "扩展表（tcs_font_lv_ext_cp.txt）；覆盖固件 UI 新增字符"),
        Job("tcs_font_lv32", load_inc_cps("tcs_font32"), 32, 34, 38, 29,
            "tcs_font_lv_ext32", "32px 主表（tcs_font_cn.inc 的 32px 码表）；数值/页标题"),
        Job("tcs_font_lv_ext32", load_txt_cps("tcs_font_lv_ext32"), 32, 34, 38, 29,
            "NULL", "32px 扩展表（tcs_font_lv_ext32_cp.txt）；链尾（fallback=NULL，同旧口径）"),
    ]
    fonts = {}
    ok = True
    for job in jobs:
        key = job.size
        if key not in fonts:
            fonts[key] = (ImageFont.truetype(FONT_PATH, job.size),
                          ImageFont.truetype(FONT_PATH, job.size * SS))
        small, big = fonts[key]
        # 墨迹越界检查
        for cp in job.cps:
            if cp == 0x20:
                continue
            b = big.getbbox(chr(cp), anchor="ls")
            if b is None:
                continue
            b = tuple(v / SS for v in b)
            if -b[1] > job.base or b[3] > job.ch - job.base:
                raise SystemExit("%s: U+%04X 墨迹越界 bbox=%s" % (job.name, cp, b))
        cells = [render_cell(job, small, big, cp) for cp in job.cps]
        txt = emit(job, cells)
        p = FONTS / (job.name + ".c")
        if check:
            same = p.exists() and p.read_text(encoding="utf-8") == txt
            print("%-20s %s" % (job.name, "口径一致" if same else "与脚本口径不一致"))
            ok = ok and same
        else:
            p.write_text(txt, encoding="utf-8", newline="\n")
            print("%-20s %3d 字 -> %s" % (job.name, len(job.cps), p.name))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
