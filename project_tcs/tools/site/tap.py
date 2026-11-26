#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tap.py — 用探针写 g_tap_inject 合成一次触摸（走与真手指相同的 LVGL 路径）

用法：python project_tcs/tools/site/tap.py X Y [mode]
    mode 1 = 150ms 按压（默认）
    mode 2 = 持续按住，直到再发一次 mode 0（tap.py 0 0 0）
注意（2026-11-24 踩过）：固件按"序号变化"取坐标，所以**必须最后写序号**。先写序号会让固件
读到上一次的坐标——当时点"切自动"却切换了风扇，查了半天是工具的竞态，不是固件问题。
写入序列与回读校对已在 site_common.tap_guarded 里（含模式页执行器禁区，S-G x3）。
前提：产物里有 g_tap_inject（诊断钩子；PROD 交付档不含）。
"""
import sys

import site_common as sc


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    x, y = int(sys.argv[1]), int(sys.argv[2])
    mode = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    mb = sc.load_mb()
    # 禁区在 tap_guarded 里：模式页（页码 4）上落在制冷/制热/风扇/切自动的矩形里会抛 TapRefused。
    seq = sc.tap_guarded(mb, x, y, mode)
    print("tap seq=%d mode=%d (%d,%d)" % (seq, mode, x, y))
    return 0


if __name__ == "__main__":
    sys.exit(main())
