#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""test_m1_gates.py — m1"模式"页批次的源码门禁（只用标准库；python -I 运行）

守的是落在主机单测够不着的地方的接线（这些文件依赖 RTOS / IDF，主机编译不了），
逐字核对——与 S-G e2 的 test_m207_gates.py 同一做法。退出码 0 = 全部通过。
"""
import os
import sys

# 用 python -I 跑（隔离模式不认 PYTHONUTF8）：控制台可能是 cp1252 / gbk，统一按 UTF-8 输出
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.dirname(HERE)                       # esp32/
CORE = os.path.join(ESP, "components", "tcs_core")
LINK = os.path.join(ESP, "components", "tcs_link")


def read(*parts):
    p = os.path.join(*parts)
    if not os.path.isfile(p):
        raise SystemExit("[m1gates] FAIL: 缺少 %s" % p)
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r\n", "\n")


def body_of(text, signature):
    """取以 signature 开头的函数的函数体（到第一个行首的 '}' 为止）。"""
    a = text.find(signature)
    if a < 0:
        return None
    b = text.find("\n}\n", a)
    return text[a:b + 3] if b > 0 else None


def main():
    fails = []

    # 1) 屏链路断开 3 秒 = 急停：control_service.c 的每个控制节拍必须问一次判定，
    #    判定要触发时必须走 tcs_emergency_stop_src（和急停命令同一条路径）。
    cs = read(CORE, "control_service.c")
    for needle in ('#include "link_loss_guard.h"', '#include "link_task.h"'):
        if needle not in cs:
            fails.append("control_service.c 缺 %s" % needle)
    task = body_of(cs, "static void control_task(void *arg)")
    if task is None or "control_poll(now_ms);" not in task:
        fails.append("control_service.c 找不到 control_task / control_poll(now_ms)")
    elif task.count("link_loss_check(now_ms);") != 1:
        fails.append("control_service.c 的控制节拍必须恰好调一次 link_loss_check(now_ms)")
    elif task.index("link_loss_check(now_ms);") < task.index("control_poll(now_ms);"):
        fails.append("link_loss_check 必须排在 control_poll 之后（先按最新状态机状态判'是否已在安全态'）")
    # 取定义（带紧随其后的 '{'），不会误取上方的前置声明
    chk = body_of(cs, "static void link_loss_check(uint32_t now_ms)\n{")
    if chk is None:
        fails.append("control_service.c 找不到 link_loss_check")
    else:
        if "link_peer_age_ms(&age)" not in chk:
            fails.append("link_loss_check 必须用 link_peer_age_ms 取'距最近一帧对端帧的毫秒数'")
        if "link_loss_guard_step(&s_link_loss, seen, age, control_state() == CTRL_SAFE)" not in chk:
            fails.append("link_loss_check 必须把 是否连上过 / 对端帧龄 / 是否已在安全态 交给 link_loss_guard_step")
        if "!= TCS_LINK_LOSS_ESTOP) return;" not in chk:
            fails.append("link_loss_check 只在判定为 TCS_LINK_LOSS_ESTOP 时继续")
        if chk.count("tcs_emergency_stop_src(PARAM_SRC_KERNEL);") != 1:
            fails.append("link_loss_check 触发时必须调一次 tcs_emergency_stop_src(PARAM_SRC_KERNEL)")
        if "SCREEN LINK LOST" not in chk:
            fails.append("link_loss_check 的日志必须写明原因（SCREEN LINK LOST）")

    # 2) "对端帧"的计时起点在方向闸门之后：comm.c 只在 comm_note_peer 里记，
    #    comm_rx_feed_byte 里闸门调用在前、comm_note_peer 在后。
    cc = read(LINK, "comm.c")
    if cc.count("L->peer_ms = L->now_ms_fn ? L->now_ms_fn() : 0u;") != 1:
        fails.append("comm.c 记对端帧时刻的语句必须恰好 1 处")
    gate_call = "    if (comm_rx_gate(L, payload, plen)) return 0;\n"
    note_call = "    comm_note_peer(L);\n"
    if cc.count(gate_call) != 1 or cc.count(note_call) != 1:
        fails.append("comm.c 的闸门调用 / comm_note_peer 调用应当各恰好 1 处")
    elif cc.index(gate_call) > cc.index(note_call):
        fails.append("comm.c 必须先过方向闸门再记对端帧时刻")
    note = body_of(cc, "static void comm_note_peer(comm_link_t *L)")
    if note is None or "L->peer_ms =" not in note or "L->peer_seen = 1;" not in note:
        fails.append("comm.c 对端帧时刻必须记在 comm_note_peer 里（闸门之后唯一的入口）")

    # 3) link_task.c 把它如实转出（时基 = link_now_ms，与 control 任务同为 esp_timer）。
    lt = read(LINK, "link_task.c")
    fn = body_of(lt, "int link_peer_age_ms(uint32_t *age_ms)")
    if fn is None:
        fails.append("link_task.c 找不到 link_peer_age_ms")
    else:
        if "comm_peer_last_ms(&s_link, &last)" not in fn:
            fails.append("link_peer_age_ms 必须读 comm_peer_last_ms(&s_link, …)")
        if "link_now_ms() - last" not in fn:
            fails.append("link_peer_age_ms 的帧龄必须用 link_now_ms() 算")
        if "return 0;" not in fn:
            fails.append("link_peer_age_ms：从未收到对端帧必须返回 0")

    # 4) 新文件进了固件构建（不只是主机单测）。
    cm = read(CORE, "CMakeLists.txt")
    for src in ("control_manual.c", "link_loss_guard.c"):
        if '"%s"' % src not in cm:
            fails.append("tcs_core/CMakeLists.txt 没有编入 %s" % src)

    if fails:
        print("[m1gates] FAIL")
        for f in fails:
            print("  - " + f)
        return 1
    print("[m1gates] PASS (屏链路断开 3 秒 = 急停的接线：控制节拍 → 纯判定 → 急停路径；对端帧时刻在方向闸门之后)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
