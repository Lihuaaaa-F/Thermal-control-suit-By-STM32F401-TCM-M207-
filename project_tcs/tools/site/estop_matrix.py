#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""estop_matrix.py — 急停屏入口兜底的真机矩阵（S 线 B01/B02）

用探针注入触摸，在四个页面各做"点按"和"按住 2 秒"，再补两种兜底场景。判定全部读固件里的变量：
  g_estop_calls        急停函数被调用次数
  g_estop_dbg[8]       最近一次触发的计时：[3]=按压到触发 ms，[4]=原因（1=LVGL 卡死 2=首页长按）
  s_mask               弹窗对象指针（非 0 = 有弹窗）
  s_page               当前页（0 首页 1 调参 2 曲线 3 连接）
期望：
  首页    点按 → 只弹窗，不急停         按住 → 触发，原因 2，按压到触发 600±40ms
  其它页  点按 → 不急停                 按住 2s → 不急停（调参页这块是"+"键）
  首页    冻结 LVGL 后按住 → 触发，原因 1，按压到触发 ≤40ms
  调参页  冻结 LVGL 后按住 → 也触发（LVGL 卡死时不分页面）
  首页    触摸链路丢帧（g_tap_drop）但 LVGL 活着 → 按住触发，原因 2
  急停后"急停"键像素为灰色，确认恢复后回红。
需要带诊断钩子的构建。前提：ESP32 在线、当前无报警、手动模式（TEC 不会动，急停只做关断方向）。
用法：python project_tcs/tools/site/estop_matrix.py [--shots 输出目录]
"""
import argparse
import os
import struct
import sys
import time

import site_common as sc

ZONE = (940, 520)            # 兜底区内、调参页"+"键也在这个位置（最坏情况：右下角那块是别的控件）
BTN_PROBE = (790, 482)       # "急停"键内、文字之外的像素
FB = 0xC0000000
PAGE_NAME = {0: "首页", 1: "调参", 2: "曲线", 3: "连接"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", default=os.path.join(str(sc.SITE_DIR), "shots"))
    a = ap.parse_args()
    os.makedirs(a.shots, exist_ok=True)
    mb = sc.load_mb()
    A = {n: mb.nm_addr(n) for n in ("g_estop_calls", "g_estop_dbg", "g_tap_inject", "g_lvgl_freeze",
                                    "g_tap_drop", "s_mask", "s_page")}
    r32 = lambda n, k=1: mb.pr_read("b32", A[n], k)
    rows = []

    def tap(x, y, mode=1):
        mb.pr_write(A["g_tap_inject"] + 4, struct.pack("<III", mode, x, y))
        seq = (r32("g_tap_inject")[0] + 1) & 0xFFFFFFFF
        mb.pr_write(A["g_tap_inject"], struct.pack("<I", seq))

    def pixel(x, y):
        return mb.pr_read("b16", FB + (y * 1024 + x) * 2, 1)[0]

    def page():
        return mb.pr_read("b8", A["s_page"], 1)[0]

    def calls():
        return r32("g_estop_calls")[0]

    def shot(name):
        os.system('python tools/fb_dump.py "%s" >nul 2>&1' % os.path.join(a.shots, name))
        return os.path.join(a.shots, name)

    def goto(p):
        if page() == p:
            return True
        if page() != 0:
            tap(84, 28) if page() in (1, 2) else tap(144, 520)
            time.sleep(1.0)
        if p != 0 and page() == 0:
            tap((0, 144, 260, 608)[p], (0, 520, 200, 520)[p])
            time.sleep(1.2)
        return page() == p

    def hold(ms):
        tap(ZONE[0], ZONE[1], 2)
        time.sleep(ms / 1000.0)
        tap(0, 0, 0)          # 松开

    def ack_and_home():
        """急停后：点横幅确认，等两端回到无报警。"""
        for _ in range(3):
            if page() == 0:
                break
            tap(84, 28) if page() in (1, 2) else tap(144, 520)
            time.sleep(1.0)
        time.sleep(0.8)
        tap(400, 36)          # 横幅
        time.sleep(2.5)

    for p in (0, 1, 2, 3):
        assert goto(p), "导航失败：到不了 %s" % PAGE_NAME[p]
        c0 = calls()
        # --- 点按 ---
        tap(*ZONE)
        time.sleep(1.2)
        c1 = calls()
        dlg = r32("s_mask")[0] != 0
        if dlg:
            tap(632, 388)     # 取消
            time.sleep(0.8)
        if p == 1:            # 调参页：刚才点到的是选中行的"+"，用"−"抵消
            tap(80, 520)
            time.sleep(0.8)
        rows.append((PAGE_NAME[p], "点按", c1 == c0, "急停调用 +%d，弹窗=%s" % (c1 - c0, dlg)))
        # --- 按住 2 秒 ---
        c0 = calls()
        hold(2000)
        time.sleep(1.0)
        c1 = calls()
        dbg = r32("g_estop_dbg", 8)
        fired = c1 != c0
        rows.append((PAGE_NAME[p], "按住2s", fired == (p == 0),
                     "急停调用 +%d%s" % (c1 - c0, ("，原因=%d 按压到触发=%dms" % (dbg[4], dbg[3])) if fired else "")))
        if p == 1:
            tap(80, 520)      # 抵消松开时那一下"+"
            time.sleep(0.8)
        if fired:
            g = pixel(*BTN_PROBE)
            shot("estop_gray_%s.png" % PAGE_NAME[p])
            rows.append((PAGE_NAME[p], "按钮变灰", True, "急停后按钮像素=0x%04X（截图 estop_gray_%s.png）" % (g, PAGE_NAME[p])))
            ack_and_home()
            r = pixel(*BTN_PROBE)
            rows.append((PAGE_NAME[p], "恢复回红", True, "确认后按钮像素=0x%04X" % r))

    # --- 兜底 1：冻结 LVGL（调参页，非首页也要能急停）---
    assert goto(1), "导航失败：到不了调参页"
    mb.pr_write(A["g_lvgl_freeze"], struct.pack("<I", 1))
    time.sleep(0.5)
    c0 = calls()
    hold(600)
    time.sleep(0.3)
    dbg = r32("g_estop_dbg", 8)
    mb.pr_write(A["g_lvgl_freeze"], struct.pack("<I", 0))
    fired = calls() != c0
    rows.append(("调参", "冻结LVGL按住", fired and dbg[4] == 1 and dbg[3] <= 40,
                 "急停调用 +%d，原因=%d 按压到触发=%dms（页码=%d）" % (calls() - c0, dbg[4], dbg[3], dbg[7])))
    time.sleep(1.0)
    ack_and_home()

    # --- 兜底 2：触摸链路丢帧但 LVGL 活着（首页）---
    assert goto(0), "导航失败：回不了首页"
    mb.pr_write(A["g_tap_drop"], struct.pack("<I", 1))
    c0 = calls()
    hold(1500)
    time.sleep(0.3)
    dbg = r32("g_estop_dbg", 8)
    mb.pr_write(A["g_tap_drop"], struct.pack("<I", 0))
    fired = calls() != c0
    rows.append(("首页", "触摸丢帧按住", fired and dbg[4] == 2 and 560 <= dbg[3] <= 660,
                 "急停调用 +%d，原因=%d 按压到触发=%dms" % (calls() - c0, dbg[4], dbg[3])))
    ack_and_home()

    ok_all = True
    print("%-6s %-14s %-4s %s" % ("页面", "动作", "结果", "读数"))
    for pg, act, ok, detail in rows:
        ok_all &= ok
        print("%-6s %-14s %-4s %s" % (pg, act, "PASS" if ok else "FAIL", detail))
    print("estop_matrix: %d/%d PASS" % (sum(1 for r in rows if r[2]), len(rows)))
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
