#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tune_refresh_check.py — 调参页"外部改参后是否就地刷新"的真机检查（S 线 B04）

场景：停在调参页，用 ESP32 控制台改参数（相当于网页改参；需要 ESP32 诊断档，命令 `p set`）。
检查两件事（并行线 2026-11-25 把调参页改成就地更新、行布局也变了，本脚本按新布局重量）：
  - 滑条圆钮中心 x：每一行都是  KNOB_X(v) = 368 + (v-min)/(max-min)*400
    （轨道 x=352..784；圆钮中心范围在轨道两端各缩 16px）
  - 数值位数变化时，单位"°C"跟着平移（数值左对齐，单位的右缘随位数增减）
判定：
  - 基线：四个圆钮的位置都与开始时 `p list` 读到的值对得上（±4px）
  - body_hot → 38.0（或 37.0）：圆钮移动量 = 两个值算出来的位置差，±4px
  - tc2_set → 9.9（4 位变 3 位；若起始已是 3 位则改 26.0）：单位右缘平移 14–36px，方向与位数变化一致；
    圆钮到算出来的位置 ±4px
  - 还原后：圆钮回到起始位置 ±4px
参数在脚本结束时还原成开始时读到的值。手动模式下改这些设定值不会驱动 TEC。
用法：TCS_ELF=build-bench/tcs_main.elf python project_tcs/tools/site/tune_refresh_check.py [--shots 目录]
"""
import argparse
import os
import re
import subprocess
import sys
import time
from collections import Counter

from PIL import Image

import site_common as sc

# 调参页布局（App/lvgl_ui/tui_p1.c：行高 88、行距 16；轨道 x=352..784；圆钮直径约 34）
ROW0_Y = 128
ROW_H, ROW_GAP = 88, 16
KNOB_DY = 44                       # 圆钮中心相对行顶
TRACK_X0, TRACK_X1 = 352, 784
KNOB_INSET = 16
ROWS = ((0x01, 5.0, 45.0), (0x02, 5.0, 45.0), (0x03, 36.0, 40.0), (0x04, 35.0, 39.0))   # 行序与 min/max


def knob_x(v, lo, hi):
    return KNOB_INSET + TRACK_X0 + (v - lo) / (hi - lo) * (TRACK_X1 - TRACK_X0 - 2 * KNOB_INSET)


def knob_center(im, row):
    """行 row 的圆钮中心 x：圆钮中心那一行上，宽度 24–44、且颜色比轨道深（最小通道 < 200）的连续段的中点。
    不拿"轨道色"做对照：圆钮在最左端时正好盖住轨道采样点（2026-11-25 离线验截图时踩到）。
    选中行里"已填充"的浅蓝轨段比 24px 宽得多，被宽度条件自然排除。"""
    px = im.load()
    y = ROW0_Y + row * (ROW_H + ROW_GAP) + KNOB_DY
    best, i, n = None, TRACK_X0 - 10, TRACK_X1 + 10
    while i < n:
        j = i
        while j < n and px[j, y] == px[i, y]:
            j += 1
        if 24 <= j - i <= 44 and min(px[i, y]) < 200:
            best = (i + j) // 2
        i = j
    return best


def value_right_edge(im, row):
    """数值 + 单位那一行的最右侧墨迹列（单位最后那个字符的右缘）。"""
    px = im.load()
    x0, x1 = 40, 330                                   # 止于滑条之前
    y0 = ROW0_Y + row * (ROW_H + ROW_GAP) + 36
    y1 = y0 + 48
    bg = Counter(px[x, y] for x in range(x0, x1) for y in range(y0, y1)).most_common(1)[0][0]
    ink = [x for x in range(x0, x1)
           if any(sum(abs(a - b) for a, b in zip(px[x, y], bg)) > 90 for y in range(y0, y1))]
    return ink[-1] if ink else None


def parse_list(lines):
    out = {}
    for l in lines:
        m = re.match(r"^(0x[0-9A-Fa-f]{4})\s+(\S+)\s+\S*\s+(-?[\d.]+)", l)
        if m:
            out[int(m.group(1), 16)] = float(m.group(3))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", default=os.path.join(str(sc.SITE_DIR), "shots"))
    a = ap.parse_args()
    os.makedirs(a.shots, exist_ok=True)
    mb = sc.load_mb()
    A_PAGE = mb.nm_addr("s_page")

    def tap(x, y, mode=1):
        # S-G x3（2026-11-26）：注入点击统一走 site_common.tap_guarded——模式页上的执行器禁区
        # 在这里同样生效（原来这份工具自己写 g_tap_inject，绕过了 estop_matrix 里的禁区）。
        return sc.tap_guarded(mb, x, y, mode, page)

    def page():
        return mb.pr_read("b8", A_PAGE, 1)[0]

    def esp(cmd):
        r = subprocess.run([sys.executable, os.path.join(str(sc.HERE), "esp_cmd.py"), cmd, "6"],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        return (r.stdout or "").strip().splitlines()

    def shot(name):
        p = os.path.join(a.shots, name)
        subprocess.run([sys.executable, "tools/fb_dump.py", p], capture_output=True)
        return Image.open(p).convert("RGB")

    start = parse_list(esp("p list"))
    assert start, "读不到参数表（ESP32 是不是诊断档？p list 只在诊断档有）"
    for _ in range(3):                 # 先回首页（二级页左上角的"‹ 返回"）
        if page() == 0:
            break
        tap(84, 28)
        time.sleep(1.0)
    tap(144, 520)                      # 进调参页
    time.sleep(1.2)
    assert page() == 1, "没进调参页"

    def knobs(im):
        return [knob_center(im, r) for r in range(len(ROWS))]

    rows = []
    try:
        im0 = shot("tune_b0_before.png")
        k0, e0 = knobs(im0), value_right_edge(im0, 1)
        exp0 = [knob_x(start[pid], lo, hi) for pid, lo, hi in ROWS]
        rows.append(("基线：圆钮位置与参数值一致",
                     all(k is not None and abs(k - e) <= 4 for k, e in zip(k0, exp0)),
                     "实测 %s，按参数算 %s" % (k0, [round(e) for e in exp0])))

        # ① body_hot：圆钮移动量
        cur = start[0x03]
        v1 = 38.0 if cur < 37.95 else 37.0
        esp("p set 0x03 %.1f" % v1)
        time.sleep(2.0)
        k1 = knob_center(shot("tune_b1_bodyhot.png"), 2)
        d = None if None in (k0[2], k1) else k1 - k0[2]
        want = knob_x(v1, 36.0, 40.0) - knob_x(cur, 36.0, 40.0)
        rows.append(("body_hot %.1f→%.1f" % (cur, v1), d is not None and abs(d - want) <= 4,
                     "圆钮中心 %s → %s（移动 %s px，期望 %.0f）" % (k0[2], k1, d, want)))

        # ② tc2_set：数值位数变化 → 单位平移
        cur2 = start[0x02]
        v2 = 9.9 if cur2 >= 10.0 else 26.0
        esp("p set 0x02 %.1f" % v2)
        time.sleep(2.0)
        im2 = shot("tune_b2_tc2.png")
        e2, k2 = value_right_edge(im2, 1), knob_center(im2, 1)
        du = None if None in (e0, e2) else e2 - e0
        want_k2 = knob_x(v2, 5.0, 45.0)
        sign = -1 if v2 < 10.0 <= cur2 else 1
        rows.append(("tc2_set %.1f→%.1f（位数%s）" % (cur2, v2, "减" if sign < 0 else "增"),
                     du is not None and sign * du >= 14 and sign * du <= 36
                     and k2 is not None and abs(k2 - want_k2) <= 4,
                     "单位右缘 x %s → %s（平移 %s px，期望 %s 14–36）；圆钮 %s（期望 %.0f）"
                     % (e0, e2, du, "左" if sign < 0 else "右", k2, want_k2)))
    finally:
        for pid, v in ((0x03, start[0x03]), (0x02, start[0x02])):
            esp("p set 0x%02X %.3f" % (pid, v))
        time.sleep(2.0)

    k3 = knobs(shot("tune_b3_restored.png"))
    back = all(k is not None and j is not None and abs(k - j) <= 4 for k, j in zip(k3, k0))
    rows.append(("还原后圆钮回原位", back, "实测 %s，起始 %s" % (k3, k0)))
    now = parse_list(esp("p list"))
    rows.append(("参数还原", all(abs(now.get(p, -1e9) - start[p]) < 0.001 for p in (0x02, 0x03)),
                 "tc2_set %.3f→%.3f，body_hot %.3f→%.3f" % (start[0x02], now.get(0x02, float("nan")),
                                                           start[0x03], now.get(0x03, float("nan")))))
    tap(84, 28)                       # 回首页
    ok = True
    for name, r, detail in rows:
        ok &= r
        print("%-30s %-4s %s" % (name, "PASS" if r else "FAIL", detail))
    print("tune_refresh_check: %d/%d PASS" % (sum(1 for r in rows if r[1]), len(rows)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
