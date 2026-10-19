#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mock_diff.py — K9 设计-实现一致性机器验证（任务书 v9.5）

定稿 mock 截图 ↔ fb_dump 真屏帧 像素 diff（容差表），走样自动 FAIL 进 nightly。
容差表（规范→实现允许偏差，全部有因）：
  - 字体渲染：浏览器 FreeType hinting vs 固件 1bpp 门槛 → 文本区按盒容差
    （膨胀半径 dilate_px 内允许差异）
  - 灰阶卡底/描边等大面积色块：RGB565 量化 → 色距 ≤ 24/通道
  - 动态区掩码：数值/时钟/呼吸点/二维码图案/滑条拇指（实时值随时间变）
  - 圆角抗锯齿角点：≤4px 角域豁免
用法:
  python tools/mock_diff.py <mock.png> <dump.png> [--mask x0,y0,x1,y1 ...] [--dilate 3]
  python tools/mock_diff.py --suite   # 标准三页套件（nightly 入口）
退出码 0=PASS 1=FAIL（nightly 直接可 gate）。
"""
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
MOCK = ROOT / "mock/final"
EV = ROOT.parent / "docs/evidence"

# 容差表
COLOR_TOL = 24        # 大面积色块逐通道容差（RGB565 量化步长 8 的 3 倍）
DILATE_DEFAULT = 3    # 文本盒膨胀半径（字体 hinting 差异）

# 动态区掩码（x0,y0,x1,y1）：实时数据不参与一致性判定
MASKS_DYNAMIC = [
    (44, 136, 474, 294),    # hero1 数值+单位+呼吸点
    (544, 136, 974, 294),   # hero2 数值区
    (136, 356, 404, 448),   # 体温数值
    (832, 364, 996, 448),   # 风扇开关
    (876, 4, 1024, 56),     # 时钟+链路点+急停图标（右上动态带）
    (20, 8, 188, 64),       # 模式胶囊（自动/手动实时）
    (344, 100, 452, 156),   # TEC1 胶囊（运行/停止实时）
    (844, 100, 952, 156),   # TEC2 胶囊
    (204, 4, 764, 68),      # 报警横幅（有无=实时）
    (376, 76, 648, 340),    # 二维码图案（内容=真实 URL 编码）
]


def near(a, b, tol):
    return all(abs(a[i] - b[i]) <= tol for i in range(3))


def dilate_ok(failmap, x, y, r, w, h):
    """失败点 r 邻域内是否有非失败点（有=文本边缘容差内，豁免）——
    反向判据：孤立差异点=字形渲染差；成片差异=布局走样"""
    x0, x1 = max(0, x - r), min(w, x + r + 1)
    y0, y1 = max(0, y - r), min(h, y + r + 1)
    for yy in range(y0, y1):
        for xx in range(x0, x1):
            if not failmap[yy * w + xx]:
                return True
    return False


def diff(mock_png, dump_png, extra_masks=(), dilate=DILATE_DEFAULT, verbose=True):
    a = Image.open(mock_png).convert("RGB")
    b = Image.open(dump_png).convert("RGB")
    if a.size != b.size:
        if verbose:
            print(f"[mock_diff] size mismatch {a.size} vs {b.size}")
        return False, 1.0
    w, h = a.size
    pa, pb = a.load(), b.load()
    masked = [[False] * w for _ in range(h)]
    for (x0, y0, x1, y1) in list(MASKS_DYNAMIC) + list(extra_masks):
        for yy in range(max(0, y0), min(h, y1)):
            row = masked[yy]
            for xx in range(max(0, x0), min(w, x1)):
                row[xx] = True
    failmap = [False] * (w * h)
    nfail = 0
    for y in range(h):
        for x in range(w):
            if masked[y][x]:
                continue
            if not near(pa[x, y], pb[x, y], COLOR_TOL):
                failmap[y * w + x] = True
                nfail += 1
    # 成片性过滤：孤立点（文本边缘）豁免，成片（≥6 邻域全败）判走样
    hard = 0
    for y in range(h):
        for x in range(w):
            if failmap[y * w + x] and not dilate_ok(failmap, x, y, dilate, w, h):
                hard += 1
    total = w * h
    ratio = hard / total
    ok = hard < total * 0.001   # 硬失败 <0.1% 像素
    if verbose:
        print(f"[mock_diff] {Path(mock_png).name} vs {Path(dump_png).name}: "
              f"raw_diff={nfail} hard={hard} ({ratio * 100:.3f}%) -> "
              f"{'PASS' if ok else 'FAIL'}")
    return ok, ratio


def suite():
    """标准三页套件：mock final/*.png ↔ docs/evidence/K9_*.png"""
    pairs = [
        ("home.png",   "K9_home.png",   []),
        ("tune.png",   "K9_tune.png",   [(880, 8, 1024, 52)]),   # 时钟
        ("connect.png","K9_connect.png",[]),
    ]
    all_ok = True
    for mock_name, ev_name, masks in pairs:
        mp = MOCK / mock_name
        dp = EV / ev_name
        if not dp.exists():
            print(f"[mock_diff] 缺真屏帧 {dp}（先跑 capture）")
            all_ok = False
            continue
        ok, _ = diff(mp, dp, masks)
        all_ok &= ok
    return all_ok


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if "--suite" in sys.argv:
        sys.exit(0 if suite() else 1)
    if len(args) < 2:
        print(__doc__)
        sys.exit(2)
    masks = []
    for i, a in enumerate(sys.argv[1:]):
        if a == "--mask" and i + 1 < len(sys.argv) - 1:
            masks.append(tuple(int(v) for v in sys.argv[i + 2].split(",")))
    ok, _ = diff(args[0], args[1], masks)
    sys.exit(0 if ok else 1)
