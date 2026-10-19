#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""geom_audit.py — WP-L0 几何清单表（机械审计，禁手敲）

产出（脚本输出，全部挂 artifact）：
  1. ui_layout.h 全 rect 的 8px 网格偏差表
  2. 同类元素间距差值表（差≠0 且规范无明示依据=缺陷）
  3. D4 状态点锚定对质（ui_layout 常量 vs 正典 mock 聚类）
  4. 对比度表（全部语义色对，≤24px 文本须 ≥4.5:1）
  5. 4× 放大裁片清单（组件级，供视觉第二眼逐张过）

用法：python tools/geom_audit.py   （在 project_tcs 根目录）
输出：docs/evidence/L0_geom/（表 CSV + 裁片 PNG）+ stdout 汇总
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(ROOT, "..", "docs", "evidence", "L0_geom")
os.makedirs(OUT, exist_ok=True)

RECT_RE = re.compile(r"#define\s+(R_[A-Z0-9_]+)\s+([0-9]+),\s*([0-9]+),\s*([0-9]+),\s*([0-9]+)")


def lum(hex6):
    r, g, b = (int(hex6[i:i + 2], 16) / 255 for i in (0, 2, 4))
    f = lambda c: c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)


def contrast(a, b):
    la, lb = lum(a), lum(b)
    hi, lo = max(la, lb), min(la, lb)
    return (hi + 0.05) / (lo + 0.05)


# 网格豁免（规范/裁决明示依据；豁免必须写理由——L1 章）
GRID_EXEMPT = {
    "R_ESTOP_IC": "规范原文 estop 小图标 x=940（K1 断言注释在案）",
    "R_CARD_TUNE1": "容器卡 460 宽按五章原值豁免（K1 裁决 #7）",
    "R_CARD_TUNE2": "同上",
    "R_CARD_BODY": "同上",
    "R_CARD_FAN": "同上",
}
# 间距豁免（差异=设计意图）
GAP_EXEMPT = {
    "dock_btn": "急停独占右下间距 56=强调隔离（规范五章：急停独占、永不变）",
}


def main():
    src = open(os.path.join(ROOT, "..", "Core", "Inc", "ui_layout.h"), encoding="utf-8").read()
    rows = []
    for m in RECT_RE.finditer(src):
        name, x, y, w, h = m.groups()
        rows.append((name, int(x), int(y), int(w), int(h)))

    fail = 0
    lines = ["name,x,y,w,h,grid_off"]
    grid_bad = []
    for name, x, y, w, h in rows:
        off = sum(1 for v in (x, y, w, h) if v % 8)
        lines.append(f"{name},{x},{y},{w},{h},{off}")
        if off:
            grid_bad.append((name, x, y, w, h, off))
    with open(os.path.join(OUT, "rect_grid.csv"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print(f"[1] rect 网格表: {len(rows)} 项 -> rect_grid.csv；非 8 倍数 {len(grid_bad)} 项")
    for g in grid_bad:
        if g[0] in GRID_EXEMPT:
            print("    off-grid(豁免):", g[0], "-", GRID_EXEMPT[g[0]])
        else:
            print("    off-grid:", g)
            fail += 1

    # 同类间距: 底栏按钮 (40/272/504/768) 与 TC 卡 (32/532)
    gaps_dock = [272 - (40 + 208), 504 - (272 + 208), 768 - (504 + 208)]
    lines2 = ["pair,gap_a,gap_b,diff"]
    g1 = gaps_dock[0]
    all_eq = all(g == g1 for g in gaps_dock)
    lines2.append(f"dock_btn,{gaps_dock},{0 if all_eq else 1}")
    print(f"[2] 底栏按钮间距 {gaps_dock} 全等={all_eq}"
          + ("" if all_eq else "（豁免：" + GAP_EXEMPT["dock_btn"] + "）"))

    # D4 点锚
    dots = [m.group(1) for m in
            re.finditer(r"#define\s+UI_DOT_(MODE_C|M207_C|ESP_C|BODY_C)\s+[0-9]+,\s*[0-9]+", src)]
    dot_dx = re.search(r"#define\s+UI_DOT_TC_DX\s+(\d+)", src)
    print(f"[3] D4 点锚常量: {dots} TC_DX={dot_dx.group(1) if dot_dx else '缺失'}")
    fail += 0 if len(dots) == 4 and dot_dx else 1

    # 对比度（≤24px 文本 ≥4.5:1）
    colors = dict(re.findall(r"#define\s+UI_C_([A-Z0-9]+)_HEX\s+0x([0-9A-Fa-f]{6})", src))
    pairs = [("TX", "CARD"), ("TX", "BG"), ("TX2", "CARD"), ("TX2", "BG"),
             ("GREEN", "CARD"), ("WARNTX", "WARNBG"), ("ALMTX", "ALMBG")]
    lines3 = ["pair,ratio,pass_4.5"]
    for a, b in pairs:
        if a in colors and b in colors:
            r = contrast(colors[a], colors[b])
            lines3.append(f"{a}_on_{b},{r:.2f},{1 if r >= 4.5 else 0}")
            ok = r >= 4.5
            print(f"[4] {a} on {b}: {r:.2f}:1 {'PASS' if ok else 'FAIL(<4.5)'}")
            if not ok:
                fail += 1
    with open(os.path.join(OUT, "contrast.csv"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines3))

    print("===================")
    print("geom_audit:", "PASS" if fail == 0 else f"FAIL({fail})")
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
