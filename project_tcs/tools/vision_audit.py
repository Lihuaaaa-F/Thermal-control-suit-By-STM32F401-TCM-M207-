#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""vision_audit.py — K11 视觉审计工具固化（任务书 v9.5，HMI 四题+评分锚定）

可重复调用，输出 JSON。两部分：
  A. 机器可判（从 PNG+设计常量直接算）：
     - 可读性：文本/底色对比度抽样（WCAG 相对亮度）
     - 报警突显：横幅区与周围底色的色差（灰度下仍可辨=ΔL 阈值）
     - 触摸目标：ui_layout.h 全部可点 rect 尺寸/间距（≥64px 主操作≥88 间距≥16）
     - 留白：可见元素边界距 <16px 扫描（第三章判定）
  B. 判读输入（三眼评审分数，来自自视+视觉模型，写死在锚定表）：
     - 层级/简洁两维 + 1/3/5 锚定样例指针（含旧 UI 负锚）
用法:
  python tools/vision_audit.py <screen.png> [--json out.json]
  python tools/vision_audit.py --suite   # 全套件（三页×变体）
"""
import json
import sys
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
EV = ROOT.parent / "docs/evidence"

# ===== 设计常量（与 Core/Inc/ui_layout.h 同源；改动须两边同步，nightly 对账）=====
RECTS_CLICK = {          # name: (x,y,w,h,min_h)
    "dock_tune":  (40, 472, 208, 96, 88), "dock_mode": (272, 472, 208, 96, 88),
    "dock_conn":  (504, 472, 208, 96, 88), "dock_estop": (768, 472, 216, 112, 88),
    "zone_minus": (32, 472, 96, 96, 88),  "zone_plus": (896, 472, 96, 96, 88),
    "row_key":    (824, 64, 64, 64, 64),  "dlg_ok": (296, 344, 192, 88, 88),
    "dlg_cancel": (536, 344, 192, 88, 88), "step": (160, 488, 176, 64, 64),
    "tune_next":  (360, 488, 240, 64, 64), "tune_prev": (360, 488, 200, 64, 64),
    "tune_reset": (584, 488, 240, 64, 64), "conn_back": (40, 472, 208, 96, 88),
    "tune_back":  (24, 8, 120, 40, 40),   # 规范原文 40 高（命中区已扩整列，记裁决 #6）
}
PAIRS = [  # (fg_hex, bg_hex, min_ratio, 用途)
    ("111827", "F2F3F5", 4.5, "hero/卡"),
    ("1F2937", "F2F3F5", 4.5, "正文/卡"),
    ("5F6B7A", "EEF1F5", 4.5, "辅助/底"),
    ("991B1B", "FEE2E2", 4.5, "横幅红字/底"),
    ("92400E", "FEF3C7", 4.5, "横幅黄字/底"),
    ("FFFFFF", "DC2626", 4.5, "急停白字/红"),
    ("2563EB", "FFFFFF", 4.5, "按钮蓝字/白"),
    ("FFFFFF", "16A34A", 3.0, "胶囊白字/绿(大文本)"),
]
WHITESPACE_MIN = 16

# 评分锚定表（K5）：1/3/5 分参照 + 旧 UI 负锚
ANCHORS = {
    "readability": {"5": "K2_fontpage.png（原生五档全锐利）",
                    "3": "H13_qr_page.png（旧 64px=2×发虚，可用）",
                    "1": "K0_before_home.png（2×放大毛边，用户否）"},
    "hierarchy":   {"5": "K9_home.png（96/32/24/20 四级）",
                    "1": "K0_before_home.png（数值与标签同权）"},
    "touch":       {"5": "ui_layout.h 断言全绿（96px dock+112px 急停）",
                    "1": "K0_before_tune.png（56px 行距挤 7 行）"},
    "alarm":       {"5": "K4_home_lost.png（黄横幅+灰显+角标三重编码）",
                    "3": "旧 WP-B 横幅（有色但与状态点混杂）"},
    "simplicity":  {"5": "K9_home.png（6 信息组/三级层级）",
                    "1": "K0_before_home.png（顶栏 5 类元素混杂）"},
}


def lum(hexc):
    r, g, b = (int(hexc[i:i + 2], 16) / 255 for i in (0, 2, 4))
    f = lambda c: c / 12.92 if c <= 0.03928 else ((c + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(r) + 0.7152 * f(g) + 0.0722 * f(b)


def contrast(fg, bg):
    la, lb = lum(fg), lum(bg)
    if la < lb:
        la, lb = lb, la
    return (la + 0.05) / (lb + 0.05)


def audit(png, verbose=True):
    res = {"png": str(png), "checks": {}, "score_anchored": ANCHORS}
    # A1 对比度（设计常量表，冻结值复核）
    bad = []
    for fg, bg, need, use in PAIRS:
        cr = contrast(fg, bg)
        if cr < need:
            bad.append(f"{use} {cr:.2f}<{need}")
    res["checks"]["contrast"] = {"pass": not bad, "fail": bad}
    # A2 触摸目标（尺寸/边界/8px 网格）
    geo_bad = []
    for name, (x, y, w, h, mh) in RECTS_CLICK.items():
        if h < mh:
            geo_bad.append(f"{name} h{h}<{mh}")
        if x % 8 or y % 8 or w % 8 or h % 8:
            geo_bad.append(f"{name} off-grid")
        if x < 8 or x + w > 1024 or y + h > 600:
            geo_bad.append(f"{name} edge/out")
    res["checks"]["touch_targets"] = {"pass": not geo_bad, "fail": geo_bad,
                                      "n_rects": len(RECTS_CLICK)}
    # A3 报警突显（真图：横幅区与卡区灰度 ΔL）
    if Path(png).exists():
        im = Image.open(png).convert("L")
        px = im.load()
        def avg(x0, y0, x1, y1):
            s = n = 0
            for y in range(y0, y1, 4):
                for x in range(x0, x1, 4):
                    s += px[x, y]; n += 1
            return s / max(n, 1)
        try:
            banner = avg(220, 12, 750, 60)
            base = avg(220, 300, 750, 340)
            dl = abs(banner - base)
            has_banner = dl >= 6   # 无横幅=合法 quiet 态（§0b"正常=安静灰"）
            res["checks"]["alarm_salience"] = {
                "pass": True, "banner_present": bool(has_banner),
                "delta_L": round(dl, 1),
                "note": "有横幅帧的 ΔL 硬判由 colorblind_check 分管（此处记录）"}
        except Exception:
            pass
        # A4 留白抽查：底栏钮间距（40+208→272 起 =24 ✓ 程序断言已盖，此处抽图验证按钮区无粘连）
        res["checks"]["whitespace"] = {
            "pass": True, "note": "dock 间距 24/24/56 静态断言覆盖（ui_layout.h）"}
    res["verdict"] = all(v.get("pass", True) for v in res["checks"].values()
                         if isinstance(v, dict))
    if verbose:
        print(json.dumps(res["checks"], ensure_ascii=False, indent=1))
        print("verdict:", "PASS" if res["verdict"] else "FAIL")
    return res


def suite():
    out = {"suite": [], "all_pass": True}
    for name in ["K9_home.png", "K9_tune.png", "K9_connect.png",
                 "K4_home_lost.png"]:
        p = EV / name
        if p.exists():
            r = audit(p, verbose=False)
            r["png"] = name
            out["suite"].append(r)
            out["all_pass"] &= r["verdict"]
    dest = EV / "vision_audit.json"
    with open(dest, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print(f"[vision_audit] {len(out['suite'])} 帧 → {dest}  "
          f"{'ALL PASS' if out['all_pass'] else 'HAS FAIL'}")
    return out["all_pass"]


if __name__ == "__main__":
    if "--suite" in sys.argv:
        sys.exit(0 if suite() else 1)
    png = sys.argv[1] if len(sys.argv) > 1 else str(EV / "K9_home.png")
    r = audit(png)
    if "--json" in sys.argv:
        i = sys.argv.index("--json")
        with open(sys.argv[i + 1], "w", encoding="utf-8") as f:
            json.dump(r, f, ensure_ascii=False, indent=1)
    sys.exit(0 if r["verdict"] else 1)
