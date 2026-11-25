#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tune_refresh_check.py — 调参页"外部改参后是否整页重建"的真机检查（S 线 B04）

场景：停在调参页，用 ESP32 控制台改一个参数（相当于网页改参）。
修复前：只有数值文字变，滑条拇指和单位位置不动。修复后：整页重建，拇指跟着动。
做法：改参前后各截一帧（读帧缓冲存 PNG），在截图上量：
  - 第 3 行（body_hot）拇指中心的 x（拇指是直径 80 的圆，在圆心那一行是最宽的同色段）
  - 第 1 行（tc1_set）数值+单位区域的最右墨迹 x（逐列垂直投影）
判定：
  - body_hot 37.5→38.0：拇指中心右移 (0.5/4.0)×(432-80) = 44px，容差 4px；还原后回到原位 ±4px
  - tc1_set 5.0→40.1：数值从 3 位变 4 位，最右墨迹右移 32px（一个字宽），容差 ±4px
参数在脚本结束时全部还原。手动模式下改这些设定值不会驱动 TEC。
用法：python project_tcs/tools/site/tune_refresh_check.py [--shots 目录]
"""
import argparse
import os
import struct
import subprocess
import sys
import time
from collections import Counter

from PIL import Image

import site_common as sc

ROW0_Y = 56                          # 第 1 行左上角
ROW2_Y = 56 + 2 * (88 + 16)          # 第 3 行左上角 = 264
TRACK_X = 32 + 320                   # = 352
TRACK_W = 432


def thumb_center(im, row_y):
    """圆心那一行（row_y+44）上宽度 70–90 的同色连续段的中心 x。"""
    px = im.load()
    y = row_y + 44
    best, i, n = None, TRACK_X - 45, TRACK_X + TRACK_W + 45
    while i < n:
        j = i
        while j < n and px[j, y] == px[i, y]:
            j += 1
        if 70 <= j - i <= 90:
            best = (i + j) // 2
        i = j
    return best


def value_right_edge(im, row_y):
    """数值 + 单位区域的垂直投影，返回最右侧墨迹列（单位的"C"右缘）。
    数值从 3 位变 4 位时单位整体右移一个字宽，所以这个值应当右移约 32px。"""
    px = im.load()
    x0, x1, y0, y1 = 56, 300, row_y + 44, row_y + 86      # 止于滑条之前（轨道从 x=352 起）
    bg = Counter(px[x, y] for x in range(x0, x1) for y in range(y0, y1)).most_common(1)[0][0]
    ink_cols = [x for x in range(x0, x1) if any(px[x, y] != bg for y in range(y0, y1))]
    return ink_cols[-1] if ink_cols else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", default=os.path.join(str(sc.SITE_DIR), "shots"))
    a = ap.parse_args()
    os.makedirs(a.shots, exist_ok=True)
    mb = sc.load_mb()
    A_TAP = mb.nm_addr("g_tap_inject")
    A_PAGE = mb.nm_addr("s_page")

    def tap(x, y):
        mb.pr_write(A_TAP + 4, struct.pack("<III", 1, x, y))
        mb.pr_write(A_TAP, struct.pack("<I", (mb.pr_read("b32", A_TAP, 1)[0] + 1) & 0xFFFFFFFF))

    def esp(cmd):
        r = subprocess.run([sys.executable, os.path.join(str(sc.HERE), "esp_cmd.py"), cmd, "6"],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        return (r.stdout or "").strip().splitlines()

    def shot(name):
        p = os.path.join(a.shots, name)
        subprocess.run([sys.executable, "tools/fb_dump.py", p], capture_output=True)
        return Image.open(p).convert("RGB"), p

    tap(144, 520)                    # 进调参页
    time.sleep(1.2)
    assert mb.pr_read("b8", A_PAGE, 1)[0] == 1, "没进调参页"
    rows = []
    try:
        im0, _ = shot("tune_b0_before.png")
        t0, ux0 = thumb_center(im0, ROW2_Y), value_right_edge(im0, ROW0_Y)

        esp("p set 0x03 38.0")
        time.sleep(2.0)
        im1, _ = shot("tune_b1_after_38.png")
        t1 = thumb_center(im1, ROW2_Y)
        d = None if None in (t0, t1) else t1 - t0
        rows.append(("body_hot 37.5→38.0", d is not None and abs(d - 44) <= 4,
                     "拇指中心 %s → %s（右移 %s px，期望 44）" % (t0, t1, d)))

        esp("p set 0x01 40.1")
        time.sleep(2.0)
        im2, _ = shot("tune_b3_after_401.png")
        ux1 = value_right_edge(im2, ROW0_Y)
        du = None if None in (ux0, ux1) else ux1 - ux0
        rows.append(("tc1_set 5.0→40.1", du is not None and 28 <= du <= 36,
                     "数值+单位最右墨迹 x %s → %s（右移 %s px，期望 32±4）" % (ux0, ux1, du)))
    finally:
        esp("p set 0x03 37.5")
        esp("p set 0x01 5.0")
        time.sleep(2.0)

    im3, _ = shot("tune_b4_restored.png")
    t2 = thumb_center(im3, ROW2_Y)
    d2 = None if None in (t0, t2) else t2 - t0
    rows.append(("还原 37.5", d2 is not None and abs(d2) <= 4, "拇指中心 %s（起始 %s，偏差 %s px）" % (t2, t0, d2)))
    val = [l for l in esp("p list") if l.startswith("0x0001") or l.startswith("0x0003")]
    tap(84, 28)                      # 回首页
    ok = True
    for name, r, detail in rows:
        ok &= r
        print("%-22s %-4s %s" % (name, "PASS" if r else "FAIL", detail))
    for l in val:
        print("  " + l[:80])
    print("tune_refresh_check: %d/%d PASS" % (sum(1 for r in rows if r[1]), len(rows)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
