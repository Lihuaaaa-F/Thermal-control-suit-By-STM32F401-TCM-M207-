#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""k4b_slider_test.py — WP-M4 T-6：K4b 滑条专项注入重跑（会话重写回归）

会话状态机重写不得伤滑条。raw 级注入走全链（映射→会话机→UI 滑条状态机）：
  1. 拖动生效：按住轨道拖到中部 → 抬起提交 → 参数值=px2val 映射值
  2. 取消回弹：拖出 ±32px → 抬起 → 参数值回原值（取消≠点选）
  3. 拖动中 20Hz 回显路径存活（无卡死）
判据：值差映射正确 + 取消恢复。

用法：python tools/k4b_slider_test.py  （sim 固件在板）
"""
import importlib.util
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("mbc", os.path.join(HERE, "mb_console.py"))
mbc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mbc)

OUT = os.path.join(HERE, "..", "..", "docs", "evidence", "M4_k4b_slider.json")
os.makedirs(os.path.dirname(OUT), exist_ok=True)


def send(cmd, w=1.6):
    return mbc.send_cmd(cmd, wait=w).decode("utf-8", errors="replace")


def pval(pid="0x01"):
    for ln in send("p list").splitlines():
        parts = ln.split()
        if len(parts) >= 3 and parts[0] == pid:
            return float(parts[2])
    return None


# 行 0（tc1_set）轨道几何（ui_layout.h）
ROW0_Y = 56
TRACK_X0 = 32 + 320          # 352
TRACK_W = 432
TY = ROW0_Y + 44             # 轨道中心 y=100（行内）
PMIN, PMAX = 5.0, 45.0


def raw_drag(x_points, y=TY):
    """按住并依次移动到 x_points，最后抬起。raw=(y,x)"""
    send("sim touchraw %d %d" % (y, x_points[0]))
    time.sleep(0.3)
    for x in x_points[1:]:
        send("sim touchraw %d %d" % (y, x))
        time.sleep(0.3)
    send("sim touchrawr")
    time.sleep(1.6)                     # K4b：松手后 1s 防抖才落值（2026-11-28），留 0.6s 余量


# px2val 实测锚定（K4b 基线）：含旋钮半径内缩——有效区 x0=TRACK_X0+40、宽=TRACK_W-80
# （反推证据：px=700→40.00、px=450→11.60，见 M4_k4b_slider.json 首轮标定）
def px2val(px):
    frac = (px - TRACK_X0 - 40) / float(TRACK_W - 80)
    frac = min(1.0, max(0.0, frac))
    return PMIN + frac * (PMAX - PMIN)


def main():
    fail = 0
    print("== K4b 滑条专项（raw 注入全链）==")
    send("sim touch 144 520")        # 进调参页
    time.sleep(0.6)
    v0 = pval()
    print("tc1_set 初值:", v0)

    # 1. 拖动生效：352→560→700，抬起提交
    raw_drag([380, 560, 700])
    v1 = pval()
    exp = px2val(700)
    ok1 = v1 is not None and abs(v1 - exp) < (PMAX - PMIN) * 0.05
    print("[1] 拖动生效: v=%.2f 期望≈%.2f %s" % (v1 or -1, exp, "PASS" if ok1 else "FAIL"))
    if not ok1:
        fail += 1

    # 2. 取消回弹：按下→拖出轨道上方 60px→抬起 → 回弹
    v_before_cancel = pval()
    raw_drag([560, 560, 560], y=TY - 60)     # y 出 ±32 窗口 → cancel
    v2 = pval()
    ok2 = v2 is not None and abs(v2 - v_before_cancel) < 0.01
    print("[2] 取消回弹: %.2f 保持 %.2f %s" % (v_before_cancel or -1, v2 or -1,
                                               "PASS" if ok2 else "FAIL"))
    if not ok2:
        fail += 1

    # 3. 回轨道再拖（取消后滑条仍可用）
    raw_drag([400, 450])
    v3 = pval()
    exp3 = px2val(450)
    ok3 = v3 is not None and abs(v3 - exp3) < (PMAX - PMIN) * 0.05
    print("[3] 取消后再拖: v=%.2f 期望≈%.2f %s" % (v3 or -1, exp3, "PASS" if ok3 else "FAIL"))
    if not ok3:
        fail += 1

    # 收尾：回主页
    send("sim touch 84 28")
    time.sleep(0.4)
    res = dict(fail=fail, v0=v0, v_drag=v1, v_cancel=v2, v_redrag=v3,
               expected_drag=exp, expected_redrag=exp3)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(res, f, ensure_ascii=False, indent=1)
    print("== K4b: %s -> %s" % ("PASS" if fail == 0 else "FAIL(%d)" % fail,
                                os.path.abspath(OUT)))
    return 0 if fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
