#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""l1_assert.py — WP-L1 nightly 断言（微观工艺，基于真机帧）

断言（全部可在帧上机械判定）：
  A. D2 镜像对称：风扇开关 ON/OFF 两态轨道轮廓逐行 diff≤1
  B. D4 状态点锚定：六点坐标处有状态色（绿/灰），无漂移
  C. D3b 警示三角：横幅图标三角含叹号（像素数阈值）
需 web/串口取证帧先行（tools/touch_inject_test 等已含采集；本脚本消费 PNG）。

用法：python tools/l1_assert.py <fan_on.png> <fan_off.png> <home.png> [banner.png]
"""
import json
import os
import re
import sys
import numpy as np
from PIL import Image

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                   "..", "docs", "evidence", "L1_assert.json")
CARD = np.array([238, 241, 245])


def hexarr(h):
    return np.array([int(h[i:i + 2], 16) for i in (0, 2, 4)])
DOTS = [("mode", 76, 36), ("m207", 780, 31), ("esp", 863, 31),
        ("tc1", 455, 173), ("tc2", 955, 173), ("body", 333, 397)]


def outline(img, color):
    reg = img[368:432, 840:968]
    m = (np.abs(reg[:, :, 0] - color[0]) < 28) & \
        (np.abs(reg[:, :, 1] - color[1]) < 28) & \
        (np.abs(reg[:, :, 2] - color[2]) < 28)
    right = [int(np.where(m[y])[0].max()) if m[y].any() else -1 for y in range(64)]
    left = [int(np.where(m[y])[0].min()) if m[y].any() else -1 for y in range(64)]
    return right, left


def main():
    fail = 0
    res = {}
    lay = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                            "..", "Core", "Inc", "ui_layout.h"), encoding="utf-8").read()
    fan_on = np.asarray(Image.open(sys.argv[1]).convert("RGB")).astype(int)
    fan_off = np.asarray(Image.open(sys.argv[2]).convert("RGB")).astype(int)
    home = np.asarray(Image.open(sys.argv[3]).convert("RGB")).astype(int)

    grn = fan_on[400, 850]
    on_r, _ = outline(fan_on, grn)
    gry = fan_off[400, 940]
    _, off_l = outline(fan_off, gry)
    mir = [127 - v if v >= 0 else -1 for v in off_l]
    diffs = [abs(a - b) for a, b in zip(on_r, mir) if a >= 0 and b >= 0]
    a_pass = bool(diffs and max(diffs) <= 1)
    res["A_mirror"] = {"max": int(max(diffs)) if diffs else None, "pass": a_pass}
    print("A 镜像对称:", "PASS" if a_pass else "FAIL")
    fail += 0 if a_pass else 1

    gray_c = hexarr(re.search(r"UI_C_GRAY_HEX\s+0x([0-9A-Fa-f]{6})", lay).group(1))
    dot_ok = 0
    for name, cx, cy in DOTS:
        px = home[cy - 4:cy + 4, cx - 4:cx + 4].reshape(-1, 3)
        grey = (np.abs(px - gray_c).sum(axis=1) < 120).sum()
        green = (np.abs(px - grn).sum(axis=1) < 150).sum()
        if grey + green > 10:
            dot_ok += 1
        else:
            print(f"  dot {name} @({cx},{cy}) 缺失")
    res["B_dots"] = {"ok": dot_ok, "pass": dot_ok == 6}
    print("B 状态点锚定:", f"{dot_ok}/6", "PASS" if dot_ok == 6 else "FAIL")
    fail += 0 if dot_ok == 6 else 1

    if len(sys.argv) > 4:
        banner = np.asarray(Image.open(sys.argv[4]).convert("RGB")).astype(int)
        icon = banner[26:46, 226:246]
        warnbg = hexarr(re.search(r"UI_C_WARNBG_HEX\s+0x([0-9A-Fa-f]{6})", lay).group(1))
        warntx = hexarr(re.search(r"UI_C_WARNTX_HEX\s+0x([0-9A-Fa-f]{6})", lay).group(1))
        warn = (np.abs(icon - warntx).sum(axis=2) < 90)
        # 叹号杆窗（x 8..13，y 6..16）：三角内部出现底色=WARNBG 的像素
        shaft = icon[6:16, 8:13]
        bang = int((np.abs(shaft - warnbg).sum(axis=2) < 60).sum())
        c_pass = int(warn.sum()) >= 120 and bang >= 12
        res["C_bang"] = {"warn_px": int(warn.sum()), "bang_px": bang, "pass": c_pass}
        print("C 三角叹号:", "PASS" if c_pass else "FAIL")
        fail += 0 if c_pass else 1

    json.dump(res, open(OUT, "w"), indent=1)
    print("===================")
    print("l1_assert:", "PASS" if fail == 0 else f"FAIL({fail}) ->", OUT)
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
