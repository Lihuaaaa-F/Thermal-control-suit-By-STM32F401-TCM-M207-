#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""fb_dump.py — 通过 probe-rs 读 SDRAM 前缓冲 → PNG（WP-A/B 运行时取证）

屏 = 1024x600 RGB565 @0xC0000000（LTDC 实时扫描），gfx_flip 后前缓冲即屏上像素。
本工具读回前缓冲并转 PNG，等效"屏幕拍照"（像素级，比相机照片更适合作代码层取证）。

用法：python tools/fb_dump.py 输出.png [起始行 行数]
默认整屏；可只转储部分行加快速度。依赖 Pillow。
"""
import re
import subprocess
import sys
from pathlib import Path

from PIL import Image

CHIP = "STM32H743II"
PROBE = "0d28:0204"
FB_ADDR = 0xC0000000
W, H = 1024, 600
WORDS_PER_LINE = W // 2            # b32: 每 u32 含 2 个 RGB565 像素（小端：低半字=x 偶数）


def read_words(addr: int, nwords: int) -> list:
    out = subprocess.run(
        ["probe-rs", "read", "--chip", CHIP, "--probe", PROBE, "b32",
         hex(addr), str(nwords)],
        capture_output=True, text=True, timeout=300, check=True)
    words = []
    for line in out.stdout.splitlines():
        m = re.match(r'^[0-9a-fA-F]+:\s*(.*)$', line)
        if not m:
            continue
        for tok in m.group(1).split():
            words.append(int(tok, 16))
    if len(words) < nwords:
        raise RuntimeError(f"read short: got {len(words)} of {nwords} words")
    return words[:nwords]


def rgb565_to_png(words, path, y0=0, nrows=H):
    img = Image.new("RGB", (W, nrows))
    px = img.load()
    base = y0 * WORDS_PER_LINE
    for y in range(nrows):
        rowbase = base + y * WORDS_PER_LINE
        for x in range(0, W, 2):
            v = words[rowbase + (x >> 1)]
            p0 = v & 0xFFFF          # 低半字 = 偶数像素
            p1 = (v >> 16) & 0xFFFF  # 高半字 = 奇数像素
            px[x, y] = (((p0 >> 11) & 0x1F) << 3, ((p0 >> 5) & 0x3F) << 2, (p0 & 0x1F) << 3)
            px[x + 1, y] = (((p1 >> 11) & 0x1F) << 3, ((p1 >> 5) & 0x3F) << 2, (p1 & 0x1F) << 3)
    img.save(path)
    print(f"OK {path} ({W}x{nrows}, y0={y0})")


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "fb_dump.png"
    y0 = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    nrows = int(sys.argv[3]) if len(sys.argv) > 3 else H
    if y0 < 0 or nrows <= 0 or y0 + nrows > H:
        raise SystemExit("bad rows")
    nwords = nrows * WORDS_PER_LINE
    words = read_words(FB_ADDR + y0 * W * 2, nwords)
    rgb565_to_png(words, out, y0, nrows)


if __name__ == "__main__":
    main()
