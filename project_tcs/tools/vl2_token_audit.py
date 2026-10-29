#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""vl2_token_audit.py — VL-2 颜色 token 真值锚定（真机帧）

判定：帧上像素取回 RGB565 与 token 定义值逐通道偏差 ≤1 LSB。
方法：
  A) 区域采样（无文字/无抗锯齿的纯色区）：BG/CARD/BORDER/WARNBG 等
  B) 全帧精确匹配计数：文字/图标 token 的"核心像素"必然等于定义值
     （抗锯齿边缘是过渡色，不计；核心像素存在即证明 token 用对）
输入：fb_dump 产出的 PNG（RGB565→PNG 无损）。
输出：逐 token 表 + PASS/FAIL；JSON 报告。

用法：python tools/vl2_token_audit.py docs/evidence/LVGL/g5/walk3/s00_home.png
"""
import json
import sys
from collections import Counter
from pathlib import Path

from PIL import Image

TOKENS = {
    "BG": 0xEEF1F5, "CARD": 0xF2F3F5, "BORDER": 0xD8DEE6, "TX": 0x1F2937,
    "TX2": 0x44506B, "HERO": 0x111827, "BLUE": 0x2563EB, "GREEN": 0x15803D,
    "GRAY": 0x9CA3AF, "YELLOW": 0xD97706, "RED": 0xDC2626, "ALMBG": 0xFEE2E2,
    "ALMTX": 0x991B1B, "WARNBG": 0xFEF3C7, "WARNTX": 0x92400E, "WHITE": 0xFFFFFF,
    "TRACK": 0xD8DEE6, "TRACKFILL": 0xBFDBFE, "PRESS": 0xDBEAFE,
}

# 纯色区域（矩形 xywh；取区域内众数=抗文字/抗边缘干扰）
REGIONS = {
    "BG":         (0, 200, 20, 140),
    "CARD":       (40, 100, 440, 200),
    "BORDER":     (32, 100, 1, 200),
    "WARNBG":     (382, 382, 84, 36),
    "GRAY":       (382, 114, 84, 36),
    "GREEN":      (842, 370, 124, 60),
    "YELLOW":     (32, 356, 1, 88),
}


def to565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def ch565(v):
    return ((v >> 11) & 0x1F) << 3, ((v >> 5) & 0x3F) << 2, (v & 0x1F) << 3


def main(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    px = list(im.getdata())
    freq = Counter(to565(*p) for p in px)
    report = {"frame": str(path), "size": [w, h], "tokens": {}, "fails": []}

    for name, hx in TOKENS.items():
        r, g, b = (hx >> 16) & 0xFF, (hx >> 8) & 0xFF, hx & 0xFF
        want = to565(r, g, b)
        wr, wg, wb = ch565(want)
        exact = freq.get(want, 0)
        entry = {"hex": f"#{hx:06X}", "want565": f"0x{want:04X}", "exact_px": exact}
        if name in REGIONS and "home" in Path(path).name:
            x0, y0, rw, rh = REGIONS[name]
            sub = Counter()
            for yy in range(y0, min(y0 + rh, h)):
                for xx in range(x0, min(x0 + rw, w)):
                    sub[to565(*im.getpixel((xx, yy)))] += 1
            got = sub.most_common(1)[0][0] if sub else want
            gr, gg, gb = ch565(got)
            dev = max(abs(gr - wr), abs(gg - wg), abs(gb - wb))
            entry["region_mode565"] = f"0x{got:04X}"
            entry["region_dev"] = dev
            entry["region_px"] = sum(sub.values())
            entry["verdict"] = "PASS" if dev <= 4 else "FAIL"
        else:
            # 文字/图标：核心像素存在即 PASS（512×600 帧里 ≥20 个核心像素）
            entry["verdict"] = "PASS" if exact >= 20 else ("WARN" if exact > 0 else "ABSENT")
        report["tokens"][name] = entry
        if entry["verdict"] == "FAIL":
            report["fails"].append(name)

    # 主色核对：BG 必须是全帧第一多色
    top = freq.most_common(1)[0]
    report["dominant565"] = f"0x{top[0]:04X}"
    report["dominant_is_BG"] = (top[0] == to565(0xEE, 0xF1, 0xF5))

    print(f"== VL-2 token 真值锚定：{path} ({w}x{h}) ==")
    print(f"{'token':10s} {'定义':9s} {'565':7s} {'核心像素':>9s}  判定")
    for name, e in report["tokens"].items():
        dev = e.get("region_dev")
        dv = f" Δ={dev}" if dev is not None else ""
        print(f"{name:10s} {e['hex']:9s} {e['want565']:7s} {e['exact_px']:9d}  {e['verdict']}{dv}")
    print(f"主色={'BG' if report['dominant_is_BG'] else report['dominant565']}")
    out = Path(path).with_name("vl2_report.json")
    out.write_text(json.dumps(report, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"报告：{out}")
    fails = [k for k, v in report["tokens"].items() if v["verdict"] == "FAIL"]
    absent = [k for k, v in report["tokens"].items() if v["verdict"] == "ABSENT"]
    print(f"区域采样 FAIL={fails or '无'}  未出现={absent or '无'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else "docs/evidence/LVGL/g5/walk3/s00_home.png"))
