#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""estop_matrix.py — 急停屏入口的真机矩阵（S 线 B01/B02；规则 v2，2026-11-24 晚）

用探针注入触摸，在四个页面各做"点按"和"按住 2 秒"，再补兜底与弹窗场景。判定全部读固件里的变量：
  g_estop_calls            急停函数被调用次数
  g_estop_dbg[8]           最近一次触发：[3]=按压到触发 ms，[4]=原因，[7]=触发时页码
                           原因 = estop_gate_fire_t：1=首页卡死  2=首页长按  3=其它页面·界面没在处理这次按压
  g_indev_pressed_reads    LVGL 触摸读回调报告"按下"的累计次数（按住期间不变 = 触摸没送进 LVGL）
  s_mask                   弹窗对象指针（非 0 = 有弹窗）
  s_page                   当前页（0 首页 1 调参 2 曲线 3 连接）
规则（estop_gate.h）与期望：
  A 首页 + LVGL 卡死            → 立即触发：原因 1，按压到触发 ≤40ms
  B 首页 + 活着 + 按住 600ms    → 触发：原因 2，600±60ms；"确认急停"弹窗开着时让位（不触发）；
                                  "切自动"这类弹窗挡不住（触发，并把弹窗关掉）
  C 其它页面 + 界面在处理        → 点按、按住 2s 都不触发（调参页这块是"+"键）
  C 其它页面 + 界面没在处理      + 按住 600ms → 触发：原因 3，600±80ms；点一下（300ms）不触发
     "没在处理"的两种：LVGL 卡死（g_lvgl_freeze）/ 触摸没送进 LVGL（g_tap_drop，读次数不变）
  急停后"急停"键像素为灰色，确认恢复后回红。
需要台架档固件（tools/build_bench.sh）。前提：ESP32 在线、当前无报警、手动模式（TEC 不会动，急停只做关断方向）。
用法：TCS_ELF=build-bench/tcs_main.elf python project_tcs/tools/site/estop_matrix.py [--shots 输出目录]
"""
import argparse
import os
import struct
import sys
import time

import site_common as sc

ZONE = (940, 520)            # 兜底区内、调参页"+"键也在这个位置（最坏情况：右下角那块是别的控件）
BACK = (84, 28)              # 二级页左上角"‹ 返回"（24,8,120,40）的中心；调参 / 曲线 / 连接三页相同
BTN_PROBE = (790, 482)       # "急停"键内、文字之外的像素
FB = 0xC0000000
PAGE_NAME = {0: "首页", 1: "调参", 2: "曲线", 3: "连接"}
UI_CMD = {"dlg_estop": 1, "dlg_mode": 2, "dlg_close": 15}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shots", default=os.path.join(str(sc.SITE_DIR), "shots"))
    a = ap.parse_args()
    os.makedirs(a.shots, exist_ok=True)
    mb = sc.load_mb()
    A = {n: mb.nm_addr(n) for n in ("g_estop_calls", "g_estop_dbg", "g_tap_inject", "g_lvgl_freeze",
                                    "g_tap_drop", "g_ui_cmd", "g_indev_pressed_reads", "s_mask", "s_page")}
    r32 = lambda n, k=1: mb.pr_read("b32", A[n], k)
    rows = []

    def tap(x, y, mode=1):
        mb.pr_write(A["g_tap_inject"] + 4, struct.pack("<III", mode, x, y))
        seq = (r32("g_tap_inject")[0] + 1) & 0xFFFFFFFF
        mb.pr_write(A["g_tap_inject"], struct.pack("<I", seq))

    def ui_cmd(n):
        mb.pr_write(A["g_ui_cmd"], struct.pack("<I", n))

    def pixel(x, y):
        return mb.pr_read("b16", FB + (y * 1024 + x) * 2, 1)[0]

    def page():
        return mb.pr_read("b8", A["s_page"], 1)[0]

    def calls():
        return r32("g_estop_calls")[0]

    def reads():
        return r32("g_indev_pressed_reads")[0]

    def mask():
        return r32("s_mask")[0]

    def shot(name):
        os.system('python tools/fb_dump.py "%s" >nul 2>&1' % os.path.join(a.shots, name))
        return os.path.join(a.shots, name)

    def goto(p):
        if page() == p:
            return True
        if page() != 0:
            tap(*BACK)
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
            tap(*BACK)
            time.sleep(1.0)
        time.sleep(0.8)
        tap(400, 36)          # 横幅
        time.sleep(2.5)

    def setvar(name, v):
        mb.pr_write(A[name], struct.pack("<I", v))

    for p in (0, 1, 2, 3):
        assert goto(p), "导航失败：到不了 %s" % PAGE_NAME[p]
        c0 = calls()
        # --- 点按 ---
        tap(*ZONE)
        time.sleep(1.2)
        c1 = calls()
        dlg = mask() != 0
        if dlg:
            tap(632, 388)     # 取消
            time.sleep(0.8)
        if p == 1:            # 调参页：刚才点到的是选中行的"+"，用"−"抵消
            tap(80, 520)
            time.sleep(0.8)
        rows.append((PAGE_NAME[p], "点按", c1 == c0, "急停调用 +%d，弹窗=%s" % (c1 - c0, dlg)))
        # --- 按住 2 秒 ---
        c0, r0 = calls(), reads()
        hold(2000)
        time.sleep(1.0)
        c1, r1 = calls(), reads()
        dbg = r32("g_estop_dbg", 8)
        fired = c1 != c0
        ok = fired == (p == 0)
        detail = "急停调用 +%d，LVGL 读到按下 +%d 次" % (c1 - c0, r1 - r0)
        if fired:
            detail += "，原因=%d 按压到触发=%dms" % (dbg[4], dbg[3])
            if p == 0:
                ok = ok and dbg[4] == 2 and 540 <= dbg[3] <= 680
        else:
            ok = ok and (r1 - r0) > 0      # 没触发的那些页：必须是因为界面确实在处理这次按压
        rows.append((PAGE_NAME[p], "按住2s", ok, detail))
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

    try:
        # ---------- 兜底：界面没在处理这次按压 ----------
        def fallback(pg, how, hold_ms, freeze, drop):
            """在页 pg 上制造"界面没在处理"：hold_ms>0 按住 hold_ms；hold_ms=0 点一下。返回 (读数, 是否触发, 调试量)。"""
            assert goto(pg), "导航失败：到不了 %s" % PAGE_NAME[pg]
            if freeze:
                setvar("g_lvgl_freeze", 1)
            if drop:
                setvar("g_tap_drop", 1)
            time.sleep(0.5)
            c0, r0 = calls(), reads()
            if hold_ms:
                hold(hold_ms)
            else:
                # 点一下：mode 1，固件侧 150ms 自动松开。不能用 hold(300)——主机每做一次探针写要约 0.35 秒，
                # "按下"与"松开"两次写之间隔着 sleep(0.3)+两次写，固件看到的按压接近 1 秒，早就过了 600ms 阈值
                # （2026-11-25 真机据此误报过一条 FAIL）。
                tap(*ZONE)
                time.sleep(0.5)
            time.sleep(0.4)
            c1, r1 = calls(), reads()
            dbg = r32("g_estop_dbg", 8)
            if freeze:
                setvar("g_lvgl_freeze", 0)
            if drop:
                setvar("g_tap_drop", 0)
            fired = c1 != c0
            detail = "急停调用 +%d，LVGL 读到按下 +%d 次" % (c1 - c0, r1 - r0)
            if fired:
                detail += "，原因=%d 按压到触发=%dms（页码=%d）" % (dbg[4], dbg[3], dbg[7])
            return detail, fired, dbg

        # A 首页 + LVGL 卡死 → 立即触发
        detail, fired, dbg = fallback(0, "冻结LVGL", 600, freeze=True, drop=False)
        rows.append(("首页", "冻结LVGL按住", fired and dbg[4] == 1 and dbg[3] <= 40 and dbg[7] == 0, detail))
        time.sleep(1.0)
        if fired:
            ack_and_home()

        # C 调参页 + LVGL 卡死：点一下（300ms）不触发
        detail, fired, _ = fallback(1, "冻结LVGL", 0, freeze=True, drop=False)
        rows.append(("调参", "冻结LVGL点一下", not fired, detail + "（150ms 点按，误碰不停机）"))

        # C 调参页 + LVGL 卡死：按住 600ms 才触发
        detail, fired, dbg = fallback(1, "冻结LVGL", 800, freeze=True, drop=False)
        rows.append(("调参", "冻结LVGL按住", fired and dbg[4] == 3 and 540 <= dbg[3] <= 760 and dbg[7] == 1, detail))
        time.sleep(1.0)
        if fired:
            ack_and_home()

        # B 首页 + 触摸没送进 LVGL（LVGL 活着）→ 首页长按压住 600ms 触发
        detail, fired, dbg = fallback(0, "触摸丢帧", 800, freeze=False, drop=True)
        rows.append(("首页", "触摸丢帧按住", fired and dbg[4] == 2 and 540 <= dbg[3] <= 700 and dbg[7] == 0, detail))
        time.sleep(1.0)
        if fired:
            ack_and_home()

        # C 调参页 + 触摸没送进 LVGL（LVGL 活着）→ 按住 600ms 触发（界面回不了首页，急停不能因此够不着）
        detail, fired, dbg = fallback(1, "触摸丢帧", 800, freeze=False, drop=True)
        rows.append(("调参", "触摸丢帧按住", fired and dbg[4] == 3 and 540 <= dbg[3] <= 700 and dbg[7] == 1, detail))
        time.sleep(1.0)
        if fired:
            ack_and_home()

        # ---------- 弹窗 ----------
        # B 首页 + "切自动"弹窗开着：按住照样触发，并且弹窗被关掉
        assert goto(0), "导航失败：回不了首页"
        ui_cmd(UI_CMD["dlg_mode"])
        time.sleep(1.0)
        opened = mask() != 0
        c0 = calls()
        hold(800)
        time.sleep(0.5)
        c1 = calls()
        dbg = r32("g_estop_dbg", 8)
        fired = c1 != c0
        rows.append(("首页", "别的弹窗挡不住", opened and fired and dbg[4] == 2 and mask() == 0,
                     "弹窗先开着=%s；急停调用 +%d，原因=%d 按压到触发=%dms，触发后弹窗=%s"
                     % (opened, c1 - c0, dbg[4], dbg[3], "已关" if mask() == 0 else "还开着")))
        if mask():
            ui_cmd(UI_CMD["dlg_close"])
        if fired:
            ack_and_home()

        # B 首页 + "确认急停"弹窗开着：按住 2s 让位，不触发
        assert goto(0), "导航失败：回不了首页"
        ui_cmd(UI_CMD["dlg_estop"])
        time.sleep(1.0)
        opened = mask() != 0
        c0 = calls()
        hold(2000)
        time.sleep(0.5)
        c1 = calls()
        rows.append(("首页", "确认急停弹窗让位", opened and c1 == c0 and mask() != 0,
                     "弹窗先开着=%s；急停调用 +%d；按完弹窗=%s" % (opened, c1 - c0, "还开着" if mask() else "没了")))
        if mask():
            ui_cmd(UI_CMD["dlg_close"])
            time.sleep(0.6)

    except Exception as e:                                  # noqa: BLE001
        # 2026-11-25 教训：兜底段中途抛异常时，前两轮已经量到的行一个都没打印出来。
        # 现在照常出表；冻结 / 丢帧钩子可能还开着，先复位，免得界面一直卡着。
        for hook in ("g_lvgl_freeze", "g_tap_drop"):
            try:
                mb.pr_write(A[hook], struct.pack("<I", 0))
            except Exception:                                  # noqa: BLE001
                pass
        rows.append(("—", "脚本中断", False, "%s: %s" % (type(e).__name__, e)))
        raise
    ok_all = True
    print("%-6s %-16s %-4s %s" % ("页面", "动作", "结果", "读数"))
    for pg, act, ok, detail in rows:
        ok_all &= ok
        print("%-6s %-16s %-4s %s" % (pg, act, "PASS" if ok else "FAIL", detail))
    print("estop_matrix: %d/%d PASS" % (sum(1 for r in rows if r[2]), len(rows)))
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
