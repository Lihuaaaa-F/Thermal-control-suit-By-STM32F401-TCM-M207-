#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_font_ui.py — 新 UI 字库生成（任务书 v9.5 K2，原生尺寸渲染禁放大）

新字号阶梯（第四章固定）：96 hero / 48 半 hero / 32 标题 / 24 正文 / 20 辅助(仅ASCII)。
本脚本产出三张新表（24/32 中文表仍由 gen_font_cn.py 自动收集生成）：
  Core/Inc/tcs_font96.inc : "0123456789.-" ×12，msyhbd.ttc 96px 原生（≈14KB const Flash）
  Core/Inc/tcs_font48.inc : 同字符集 48px 原生（≈3.5KB）——体温卡 hero
  Core/Inc/tcs_font20.inc : ASCII95 20px 原生（≈3.8KB）——时间戳/URL/SSID/角标（20px 无中文，
                            任务书 1.2"中文 24/32px"字面执行）
格式：比例宽度字形（advance 逐字存储，'.'/- 窄格省宽），1bpp 行优先 MSB。
  struct: { cp(u32), w(u8), adv(u8), rows(u8), bitmap[rows*stride] } 表前加 count(u32)。
用法：python tools/gen_font_ui.py
"""
import struct
from pathlib import Path
from PIL import ImageFont, ImageDraw, Image

ROOT = Path(__file__).resolve().parent.parent
BOLD = r"C:\Windows\Fonts\msyhbd.ttc"
REG = r"C:\Windows\Fonts\msyh.ttc"

CHARSET_NUM = "0123456789.-"
CHARSET_20 = "".join(chr(c) for c in range(0x20, 0x7F)) + "°"


def render_glyph(font: ImageFont.FreeTypeFont, ch: str, size: int,
                 ascent: int, descent: int):
    """原生尺寸渲染 → 1bpp，先按基线全格渲染再裁掉空白行，返回
    (w, h, adv, yoff, bits)。yoff=墨迹顶相对绘制 y 的偏移——调用方
    draw(y+yoff) 即得基线正确（'.'/'-' 落低位），且只画墨迹不擦底。
    （v1 等高字格整格擦底会压掉下方设定行——真屏实测已修）"""
    cell_h = ascent + descent
    adv = max(4, int(round(font.getlength(ch))))
    bbox = font.getbbox(ch)
    x0 = bbox[0]
    w = bbox[2] - bbox[0]
    if w <= 0:
        w = 1
    pad = 1
    img = Image.new("L", (w + pad * 2, cell_h), 0)
    d = ImageDraw.Draw(img)
    d.text((pad - x0, 0), ch, fill=255, font=font)
    px = img.load()
    stride = (img.width + 7) // 8
    grid = [[1 if px[x, y] >= 128 else 0 for x in range(img.width)]
            for y in range(img.height)]
    row_has_ink = [any(r) for r in grid]
    try:
        top = row_has_ink.index(True)
        bot = len(row_has_ink) - 1 - row_has_ink[::-1].index(True)
    except ValueError:
        top, bot = 0, 0
    yoff = top
    rows = bytearray()
    for y in range(top, bot + 1):
        row = bytearray(stride)
        for x in range(img.width):
            if grid[y][x]:
                row[x // 8] |= 0x80 >> (x % 8)
        rows += row
    return img.width, max(1, bot - top + 1), adv, yoff, bytes(rows)


def emit(path: Path, charset: str, fontfile: str, size: int, idx: int = 0):
    font = ImageFont.truetype(fontfile, size, index=idx)
    ascent, descent = font.getmetrics()
    entries = []
    for ch in charset:
        w, h, adv, yoff, bits = render_glyph(font, ch, size, ascent, descent)
        entries.append((ord(ch), w, adv, h, yoff, bits))
    out = [f"/* 由 tools/gen_font_ui.py 生成 — {size}px 原生字形（禁放大），勿手改 */",
           f"/* 字符集: {charset if len(charset) < 40 else charset[:36] + '...'} */",
           f"#ifndef TCS_FONT{size}_GEN",
           f"#define TCS_FONT{size}_GEN",
           ""]
    for i, (cp, w, adv, h, yoff, bits) in enumerate(entries):
        out.append(f"static const uint8_t f{size}_g{i}[{len(bits)}] = {{")
        for off in range(0, len(bits), 16):
            chunk = ",".join(f"0x{b:02X}" for b in bits[off:off + 16])
            out.append(f"    {chunk},")
        out.append("};")
    out.append("")
    out.append(f"static const ui_glyph_t tcs_font{size}[] = {{")
    for i, (cp, w, adv, h, yoff, bits) in enumerate(entries):
        out.append(f"    {{ {cp}, {w}, {adv}, {h}, {yoff}, f{size}_g{i} }},")
    out.append("};")
    out.append("")
    out.append(f"#define TCS_FONT{size}_N (sizeof(tcs_font{size}) / sizeof(tcs_font{size}[0]))")
    out.append(f"#endif /* TCS_FONT{size}_GEN */")
    total = sum(len(b) for *_, b in entries) + len(entries) * 8
    path.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"OK {path.name}: {len(entries)} glyphs, ~{total // 1024}KB")


def main():
    emit(ROOT / "Core/Inc/tcs_font96.inc", CHARSET_NUM, BOLD, 96)
    emit(ROOT / "Core/Inc/tcs_font48.inc", CHARSET_NUM, BOLD, 48)
    emit(ROOT / "Core/Inc/tcs_font20.inc", CHARSET_20, REG, 20)


if __name__ == "__main__":
    main()
