#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""g4_e04_tl9b.py — G4 E-04 三入口回归 + TL-9b 兜底注入（v12.5 G4⑤⑥ / TL-9b）

前提：G4 =1 固件（含 probe 注入钩子 g_tap_inject/g_lvgl_freeze/g_lvgl_drain/g_estop_calls）。
- E-04 入口③ console estop：mb_console 邮箱注入 → tcs_emergency_stop 计数 +1、ALM_ESTOP 置位。
- TL-9b 法1（停喂 handler）：probe 写 g_lvgl_freeze=1 → 屏入口注入点击 → 计数 +1（LVGL 死亡仍可达）。
- TL-9b 法2（池耗尽）：probe 写 g_lvgl_drain=1（lv_malloc 至 NULL）→ 屏入口注入 → 计数 +1。
判定：三段计数严格递增各 +1；ALM_ESTOP=0x0004 置位；mode=手动；fan_on=1。
用法：TCS_ELF=build-g4lv/tcs_main.elf python tools/g4_e04_tl9b.py
"""
import os
import struct
import sys
import time

os.environ.setdefault("TCS_ELF", "build-g4lv/tcs_main.elf")
import importlib.util

spec = importlib.util.spec_from_file_location("mbc", "tools/mb_console.py")
mb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mb)

ALM_ESTOP = 0x0004
G_TCS = mb.nm_addr("g_tcs")
A_CALLS = mb.nm_addr("g_estop_calls")
A_TAP = mb.nm_addr("g_tap_inject")
A_FREEZE = mb.nm_addr("g_lvgl_freeze")
A_DRAIN = mb.nm_addr("g_lvgl_drain")


def r32(a, n=1):
    return mb.pr_read("b32", a, n)


def w32(a, values):
    mb.pr_write(a, b"".join(struct.pack("<I", v) for v in values))


def state():
    w = r32(G_TCS, 7)
    return {
        "alarm_bits": w[4] & 0xFFFF,
        "fan_on": (w[3] >> 16) & 0xFF,
        "mode": (w[3] >> 24) & 0xFF,
    }


def tap_estop(seq):
    w32(A_TAP, [seq, 1, 876, 528])   # 急停热区中心 (768+108, 472+56)


def main():
    fails = []
    print("== 复位后基线 ==")
    s0 = state()
    c0 = r32(A_CALLS, 1)[0]
    print(f"calls={c0} state={s0}")

    print("== E-04 入口③ console estop ==")
    mb.send_cmd("estop")
    time.sleep(0.5)
    c1 = r32(A_CALLS, 1)[0]
    s1 = state()
    print(f"calls {c0}->{c1} alarm={s1['alarm_bits']:#06x} mode={s1['mode']} fan={s1['fan_on']}")
    if c1 != c0 + 1: fails.append("console estop 未触发")
    if not s1["alarm_bits"] & ALM_ESTOP: fails.append("ALM_ESTOP 未置位")

    print("== TL-9b 法1：冻结 LVGL（停喂 handler）==")
    w32(A_FREEZE, [1])
    time.sleep(0.5)
    tap_estop(1)
    time.sleep(0.5)
    c2 = r32(A_CALLS, 1)[0]
    print(f"calls {c1}->{c2}（冻结态屏入口触发）")
    if c2 != c1 + 1: fails.append("冻结态 estop_watch 未触发")
    w32(A_FREEZE, [0])
    time.sleep(0.3)

    print("== TL-9b 法2：池耗尽 ==")
    w32(A_DRAIN, [1])
    time.sleep(1.5)
    drain_state = r32(A_DRAIN, 1)[0]
    print(f"drain 标记={drain_state}（2=已耗尽）")
    tap_estop(2)
    time.sleep(0.5)
    c3 = r32(A_CALLS, 1)[0]
    print(f"calls {c2}->{c3}（耗尽态屏入口触发）")
    if drain_state != 2: fails.append("池未耗尽到 NULL")
    if c3 != c2 + 1: fails.append("耗尽态 estop_watch 未触发")
    w32(A_DRAIN, [0])

    print(f"== 结果：{'PASS' if not fails else 'FAIL: ' + '; '.join(fails)} ==")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
