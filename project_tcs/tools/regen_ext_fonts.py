#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""regen_ext_fonts.py — 把固件 UI 新增文案补进 ext24（LVGL 24px 扩展字库）

背景：`tcs_lvgl/tools/gen_font_ext.py` 只扫模拟器 `app/` 源码，固件 `project_tcs/App/lvgl_ui/*.c`
新增的中文文案（如本轮的 tui_diag 诊断页）不会进字库 → 上屏豆腐块。
本脚本用同一套渲染口径（msyh.ttc / anchor=mm / 24×24 / 1bpp 行优先 MSB 左）重生成
`App/lvgl_ui/fonts/tcs_font_lv_ext.c` 与 `tcs_font_lv_ext_cp.txt`。

用法：python tools/regen_ext_fonts.py [--dry]
"""
import re
import subprocess
import sys
from pathlib import Path

FIRMWARE = Path(__file__).resolve().parent.parent
SIM = FIRMWARE.parent.parent / "tcs_lvgl"   # D:/Espressif/ws/tcs_lvgl
sys.path.insert(0, str(SIM / "tools"))

from PIL import Image, ImageDraw, ImageFont          # noqa: E402
from tcs_font_to_lv import emit_lv_font              # noqa: E402

UI = FIRMWARE / "App/lvgl_ui"
FONTS = UI / "fonts"
FONT_PATH = Path("C:/Windows/Fonts/msyh.ttc")
SIZE = 24


def strip_comments(t: str) -> str:
    t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
    t = re.sub(r"//[^\n]*", "", t)
    return t


LIT = re.compile(r'"((?:[^"\\]|\\.)*)"')


def load_cp(p: Path):
    return {int(l.strip(), 16) for l in p.read_text().splitlines() if l.strip()}


def scan_need():
    need = set()
    for base in (UI,):
        for f in sorted(base.glob("*.c")):
            txt = strip_comments(f.read_text(encoding="utf-8", errors="ignore"))
            for m in LIT.finditer(txt):
                need |= set(m.group(1))
    return need


def render(cp: int, font) -> bytes:
    img = Image.new("L", (SIZE, SIZE), 0)
    d = ImageDraw.Draw(img)
    d.text((SIZE // 2, SIZE // 2), chr(cp), font=font, fill=255, anchor="mm")
    out = bytearray()
    for y in range(SIZE):
        row = 0
        for x in range(SIZE):
            if img.getpixel((x, y)) >= 128:
                row |= 1 << (15 - x) if False else 0
        # 逐像素写：每行 24bit = 3 字节，MSB 在左
        b0 = b1 = b2 = 0
        for x in range(24):
            if img.getpixel((x, y)) >= 128:
                if x < 8:
                    b0 |= 1 << (7 - x)
                elif x < 16:
                    b1 |= 1 << (15 - x)
                else:
                    b2 |= 1 << (23 - x)
        out += bytes((b0, b1, b2))
    return bytes(out)


def main():
    dry = "--dry" in sys.argv
    have = set()
    for f in ("tcs_font_lv24_cp.txt", "tcs_font_lv_ext_cp.txt",
              "tcs_font_lv32_cp.txt", "tcs_font_lv_ext32_cp.txt"):
        have |= load_cp(FONTS / f)
    need = {ord(c) for c in scan_need()}
    missing = sorted(c for c in need
                     if c > 0x20 and c not in have and not (0x7F <= c <= 0x9F))
    print(f"UI 需要 {len(need)} 字，链上已有 {len(have)}，缺 {len(missing)}")
    if missing:
        print("缺字：", "".join(chr(c) for c in missing))
    if dry or not missing:
        return 0

    cps = sorted(load_cp(FONTS / "tcs_font_lv_ext_cp.txt") | set(missing))
    f = ImageFont.truetype(str(FONT_PATH), SIZE)
    glyphs = [render(cp, f) for cp in cps]
    blank = [hex(c) for c, g in zip(cps, glyphs) if not any(g)]
    if blank:
        print("[WARN] 空字形：", blank)
    emit_lv_font(FONTS, "tcs_font_lv_ext", cps, glyphs, SIZE, SIZE, 0, "tcs_font_lv32",
                 "扩展字库 24px（固件 UI 新增字符；msyh 全盒，口径同 gen_font_cn.py）",
                 None, ", ".join("0x%04X" % (c - cps[0]) for c in cps), cps[0],
                 len(cps) * len(glyphs[0]))
    (FONTS / "tcs_font_lv_ext_cp.txt").write_text(
        "\n".join("%04X" % c for c in cps) + "\n", encoding="utf-8")
    print(f"[regen] ext24 重生成：{len(cps)} 字（新增 {len(missing)}）")
    # U 线 S1：新生成的是全角等宽表，接着跑度量补丁，否则其中的 ASCII/符号会被撑开
    subprocess.run([sys.executable, str(FIRMWARE / "tools" / "font_metrics_patch.py"),
                    "tcs_font_lv_ext.c"], check=False)
    sync_charset(cps)
    return 0


def sync_charset(cps):
    """诊断页 TL-18 的 CP24EXT 清单必须等于 ext24 字库的码点清单，否则"逐字查字形"漏查新字。
    （U 线 2026-11-24：此前这份清单靠手工同步，ext24 从 99 字加到 138 字后没人更新。）"""
    hdr = UI / "tui_charset_data.h"
    t = hdr.read_text(encoding="utf-8")
    m = re.search(r"static const uint16_t TUI_CP24EXT\[\] = \{.*?\n\};\n#define TUI_CP24EXT_N\s+\d+",
                  t, flags=re.S)
    if not m:
        print("[WARN] tui_charset_data.h 里找不到 TUI_CP24EXT，未同步")
        return
    rows = [", ".join("0x%04X" % c for c in cps[i:i + 12]) for i in range(0, len(cps), 12)]
    block = "static const uint16_t TUI_CP24EXT[] = {\n  " + ",\n  ".join(rows) + "\n};\n"
    block += "#define TUI_CP24EXT_N  %d" % len(cps)
    hdr.write_text(t[:m.start()] + block + t[m.end():], encoding="utf-8", newline="\n")
    print(f"[regen] 诊断字符集 CP24EXT 同步：{len(cps)} 字")


if __name__ == "__main__":
    sys.exit(main())
