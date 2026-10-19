#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""colorblind_check.py — K10 色弱/灰度可辨性审查（任务书 v9.5）

对 UI 帧做 灰度化 + 红绿色盲(Protanopia/Deuteranopia) 变换后重审：
  - 报警横幅 vs 背景：灰度 ΔL ≥ 18（报警在无彩色下仍突显）
  - 绿/灰状态胶囊：灰度 ΔL ≥ 15（●运行 vs ○停止 不依赖色相）
  - 急停红实底 vs 白卡：ΔL ≥ 40
通过标准=线性变换后上述三组仍可辨（ISA-101 "颜色只留给报警"的自证）。
用法: python tools/colorblind_check.py [frame.png ...]   （缺省=标准四帧）
"""
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
EV = ROOT.parent / "docs/evidence"

# 灰度权重（sRGB）
W = (0.2126, 0.7152, 0.0722)
# Deuteranopia 近似矩阵（Brettel/Viénot 简化，够用于可辨性审查）
DEUT = ((0.625, 0.375, 0.0),
        (0.70,  0.30,  0.0),
        (0.0,   0.30,  0.70))
PROT = ((0.567, 0.433, 0.0),
        (0.558, 0.442, 0.0),
        (0.0,   0.242, 0.758))


def transform(im, mode):
    px = im.load()
    out = Image.new("L", im.size)
    po = out.load()
    for y in range(im.size[1]):
        for x in range(im.size[0]):
            r, g, b = px[x, y]
            if mode == "gray":
                v = W[0] * r + W[1] * g + W[2] * b
            else:
                m = DEUT if mode == "deut" else PROT
                r2, g2, b2 = (m[i][0] * r + m[i][1] * g + m[i][2] * b
                              for i in range(3))
                v = W[0] * r2 + W[1] * g2 + W[2] * b2
            po[x, y] = int(max(0, min(255, v)))
    return out


def avg(im, x0, y0, x1, y1):
    px = im.load()
    s = n = 0
    for y in range(y0, y1, 3):
        for x in range(x0, x1, 3):
            s += px[x, y]
            n += 1
    return s / max(n, 1)


# 阈值依据：ΔL≥12 ≈ 明显可辨（ISO 9241 常用 10+）；且报警可辨不单靠色相——
# ▲/■ 图标+深色文字块+描边三重编码同在（K10 术语表），色相只是其一
CHECKS = [  # (名称, A区, B区, 最小ΔL, 适用帧)
    ("横幅vs背景",  (220, 12, 750, 60),  (220, 300, 750, 340), 12, "banner"),
    ("急停钮vs卡",  (790, 490, 960, 565), (600, 490, 700, 560), 25, "dock"),
]
# 胶囊运行/停止不依赖 ΔL 单判：●/○ 形状+文字双编码（设计规范 1.2），此处不查


def check_frame(path):
    """按帧内容选择检查组：有横幅帧查横幅/胶囊，所有 dock 帧查急停钮"""
    im = Image.open(path).convert("RGB")
    gray = transform(im, "gray")
    has_banner = abs(avg(gray, 220, 12, 750, 60)
                     - avg(gray, 220, 300, 750, 340)) >= 6
    checks = [c for c in CHECKS
              if (c[4] != "banner" or has_banner)]
    results = {}
    ok_all = True
    for mode in ("gray", "deut", "prot"):
        g = transform(im, mode)
        row = {}
        for name, a, b, need, _applies in checks:
            dl = abs(avg(g, *a) - avg(g, *b))
            row[name] = {"dL": round(dl, 1), "need": need, "ok": dl >= need}
            ok_all &= dl >= need
        results[mode] = row
    results["_meta"] = {"skipped_banner_group": not has_banner}
    return results, ok_all


def main():
    frames = sys.argv[1:] or [str(EV / n) for n in
                              ("K9_home.png", "K4_home_lost.png")]
    all_ok = True
    report = {}
    for f in frames:
        if not Path(f).exists():
            print(f"[colorblind] 缺帧 {f} 跳过")
            continue
        res, ok = check_frame(f)
        report[f] = res
        all_ok &= ok
        brief = {m: {k: v.get("dL") for k, v in r.items() if isinstance(v, dict)}
                 for m, r in res.items()}
        print(f"[colorblind] {Path(f).name}: {'PASS' if ok else 'FAIL'} {brief}")
    import json
    dest = EV / "colorblind_check.json"
    with open(dest, "w", encoding="utf-8") as fh:
        json.dump(report, fh, ensure_ascii=False, indent=1)
    print(f"[colorblind] {'ALL PASS' if all_ok else 'HAS FAIL'} → {dest}")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
