#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mask_lattice.py — 弹窗点阵遮罩的像素级核对

模式一（单张）：在几块"底色均匀、无文字"的区域里检验 3px 点阵假设：
    存在相位 (rx, ry)，使所有 (x%3==rx 且 y%3==ry) 的像素都是点色，其余都不是。
模式二（两张）：在同样的纯背景区域里逐像素比对两张截图。
    python project_tcs/tools/site/mask_lattice.py a.png [b.png ...]
点色 = 弹窗遮罩的点 #D8DEE6；底色 = 页面背景 #EEF1F5。
"""
import sys
from pathlib import Path

from PIL import Image

DOT = (0xD8, 0xDE, 0xE6)
TOL = 10
# 名称, x0, y0, x1, y1（不含 x1, y1）——都是弹窗卡之外、只有背景的点阵区域
REGIONS = [
    ("顶部背景带", 0, 0, 1024, 14),
    ("中部背景带-左", 0, 332, 256, 348),
    ("中部背景带-右", 768, 332, 1024, 348),
    ("水箱卡间隙", 496, 92, 528, 132),
]


def is_dot(p):
    return all(abs(p[i] - DOT[i]) <= TOL for i in range(3))


def lattice(path):
    px = Image.open(path).convert("RGB").load()
    out = []
    for name, x0, y0, x1, y1 in REGIONS:
        best = None
        for rx in range(3):
            for ry in range(3):
                on = on_dot = off = off_dot = 0
                for y in range(y0, y1):
                    for x in range(x0, x1):
                        d = is_dot(px[x, y])
                        if x % 3 == rx and y % 3 == ry:
                            on += 1
                            on_dot += d
                        else:
                            off += 1
                            off_dot += d
                cand = (on_dot, -off_dot, rx, ry, on, off)
                if best is None or cand > best:
                    best = cand
        on_dot, neg_off, rx, ry, on, off = best
        out.append("%-14s 相位 x%%3==%d y%%3==%d  格点是点色 %d/%d (%.1f%%)  格点外点色 %d/%d"
                   % (name, rx, ry, on_dot, on, 100.0 * on_dot / on if on else 0.0, -neg_off, off))
    return out


def diff(a, b):
    pa = Image.open(a).convert("RGB").load()
    pb = Image.open(b).convert("RGB").load()
    out = []
    for name, x0, y0, x1, y1 in REGIONS:
        tot = md = nd = 0
        for y in range(y0, y1):
            for x in range(x0, x1):
                tot += 1
                d = max(abs(pa[x, y][i] - pb[x, y][i]) for i in range(3))
                md = max(md, d)
                nd += (d != 0)
        out.append("%-14s 像素 %d  不同 %d  最大通道差 %d" % (name, tot, nd, md))
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    for p in sys.argv[1:]:
        print(Path(p).name)
        for line in lattice(p):
            print("  " + line)
    if len(sys.argv) == 3:
        print("逐像素比对：%s 对 %s" % (Path(sys.argv[1]).name, Path(sys.argv[2]).name))
        for line in diff(sys.argv[1], sys.argv[2]):
            print("  " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
