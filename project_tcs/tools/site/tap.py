#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tap.py — 用探针写 g_tap_inject 合成一次触摸（走与真手指相同的 LVGL 路径）

用法：python project_tcs/tools/site/tap.py X Y [mode]
    mode 1 = 150ms 按压（默认）
    mode 2 = 持续按住，直到再发一次 mode 0（tap.py 0 0 0）
注意（2026-11-24 踩过）：固件按"序号变化"取坐标，所以**必须最后写序号**。先写序号会让固件
读到上一次的坐标——当时点"切自动"却切换了风扇，查了半天是工具的竞态，不是固件问题。
本脚本先写 mode/x/y，回读确认无误后再写序号。
前提：产物里有 g_tap_inject（诊断钩子；PROD 交付档不含）。
"""
import struct
import sys

import site_common as sc


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    x, y = int(sys.argv[1]), int(sys.argv[2])
    mode = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    mb = sc.load_mb()
    a = mb.nm_addr("g_tap_inject")
    mb.pr_write(a + 4, struct.pack("<III", mode, x & 0xFFFFFFFF, y & 0xFFFFFFFF))
    back = mb.pr_read("b32", a + 4, 3)
    if back != [mode, x & 0xFFFFFFFF, y & 0xFFFFFFFF]:
        print("tap: write verify failed, read back %r" % (back,))
        return 1
    seq = (mb.pr_read("b32", a, 1)[0] + 1) & 0xFFFFFFFF
    mb.pr_write(a, struct.pack("<I", seq))
    print("tap seq=%d mode=%d (%d,%d)" % (seq, mode, x, y))
    return 0


if __name__ == "__main__":
    sys.exit(main())
