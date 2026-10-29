#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""vl4_aa_check.py — VL-4「AA 边缘无彩边（子像素顺序）」机器判定（真机帧，内容无关）

原理：LVGL v9 默认渲染器不做 LCD 子像素滤波 → 抗锯齿只能是"两端颜色"的线性混合。
做法：对每个边缘像素（5×5 邻域亮度差 >24），取邻域暗端/亮端两种颜色，
      检查该像素是否落在两色的连线附近（垂距）。
      只看"两端色相差小"的边缘（|Δ(R−B)|<30），排除不同色块的真实边界。
判定：中性边缘上，垂距 >8 的像素数为 0 → 无彩边。

用法：python tools/vl4_aa_check.py docs/evidence/LVGL/g5/walk3/s00_home.png [更多帧...]
"""
import json
import sys
from pathlib import Path

from PIL import Image

R = 2          # 邻域半径
LUMA_EDGE = 24 # 边缘判定
HUE_MAX = 30   # 只测"两端近同色相"的边缘
RESID_MAX = 8  # 允许垂距
MARGIN = 8     # 画面边缘 8px 是圆角/边框交接，跳过


def lum(c):
    return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]


def main(paths):
    report, bad = {}, 0
    for path in paths:
        im = Image.open(path).convert("RGB")
        w, h = im.size
        px = im.load()
        tested = viol = 0
        worst = 0.0
        for y in range(MARGIN, h - MARGIN):
            for x in range(MARGIN, w - MARGIN):
                c = px[x, y]
                lo = hi = None
                same = 0
                for yy in range(y - R, y + R + 1):
                    for xx in range(x - R, x + R + 1):
                        q = px[xx, yy]
                        if q == c:
                            same += 1   # 同色出现≥2 次 = 区域实色，不是混合像素
                        lq = lum(q)
                        if lo is None or lq < lum(lo):
                            lo = q
                        if hi is None or lq > lum(hi):
                            hi = q
                if same >= 2:
                    continue
                if lum(hi) - lum(lo) < LUMA_EDGE:
                    continue
                # 只测近同色相边缘（否则是不同色块的真实交界）
                if abs((lo[0] - lo[2]) - (hi[0] - hi[2])) > HUE_MAX:
                    continue
                # 端点在"端色亮度"处取样，避免选到 AA 中间像素本身
                l0, l1 = lum(lo), lum(hi)
                f = (lum(c) - l0) / (l1 - l0 + 1e-6)
                f = max(0.0, min(1.0, f))
                exp = tuple(lo[i] + (hi[i] - lo[i]) * f for i in range(3))
                d = max(abs(c[i] - exp[i]) for i in range(3))
                tested += 1
                if d > RESID_MAX:
                    viol += 1
                    worst = max(worst, d)
        verdict = "PASS" if viol == 0 else ("WARN" if viol <= 5 else "FAIL")
        bad += 0 if verdict != "FAIL" else 1
        name = Path(path).name
        report[name] = {"edge_px": tested, "viol": viol, "worst": round(worst, 1), "verdict": verdict}
        print(f"{name:24s} 中性边缘={tested:6d} 越界={viol:4d} 最远={worst:5.1f}  {verdict}")
    out = Path(paths[0]).with_name("vl4_report.json")
    out.write_text(json.dumps(report, indent=1, ensure_ascii=False), encoding="utf-8")
    print(f"报告：{out}")
    return 1 if bad else 0


if __name__ == "__main__":
    args = sys.argv[1:] or ["docs/evidence/LVGL/g5/walk3/s00_home.png"]
    sys.exit(main(args))
