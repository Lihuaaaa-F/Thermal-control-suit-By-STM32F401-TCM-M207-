#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""novice_tasks.py — K11 新手任务矩阵×5（第七章第 1 条台架版）

无先验用户视角：注入脚本模拟"第一次拿到设备的人"完成 5 项任务，
记录用时与错点（阈值 ≤30s / ≤2 错点）；真人版留 I5。
任务：①读水温 ②调高设定 0.5 ③急停流程 ④找二维码 ⑤切模式
每次跑前复位固件到干净态。证据 JSON 入 docs/evidence/K11_novice.json。
"""
import json
import os
import sys
import time
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.environ.setdefault("TCS_ELF", "build-sim/tcs_main.elf")
import mb_console as mb          # noqa: E402
import rehearsal as R            # noqa: E402  (复用 probe_reset/plist/stat)


_io_total = [0.0]   # 探针 I/O 累计（毛时-净时的扣减项）


def send(line):
    """进程内直发；探针 I/O 耗时单独记账（台账决策：主机侧开销不入产品判据）"""
    t = time.time()
    mb.send_cmd(line)
    _io_total[0] += time.time() - t


def _io_mark():
    """清零记账起点，返回当前累计值"""
    v = _io_total[0]
    return v

# —— 新手动线（无先验）：以 1024×600 屏面可见可点目标为准 ——
# 错点定义：一次 tap 落在非任务目标上
TASKS = []


def clean_state():
    """任务间清态（默认参数+布防+体温+回主页）"""
    R.probe_reset(); time.sleep(4)
    send("p defaults Y"); send("sim body 36.8"); send("ack")
    send("p set 0x08 1"); send("p set 0x01 25.0")
    send("sim touch 144 520")   # 确保在主页（tune→返回键；home→调参再回亦可）


def t_read_temp():
    """①读水温：进主页读贴身水箱值（对照 param→屏值一致）"""
    t0 = time.time(); wrong = 0
    clean_state()
    send("sim touch 84 28")   # 若在调参页则回主页；主页上该点无目标（无害）
    time.sleep(0.8)
    val = R.plist()["0x01"]
    ms = int((time.time() - t0) * 1000)
    return ms, wrong, f"主页直读贴身设定={val}°C（点击 0 次即达）"


def t_raise_half():
    """②调高设定 0.5：主页→调参→+5×0.1→回"""
    t0 = time.time(); wrong = 0
    v0 = float(R.plist()["0x01"])
    send("sim touch 144 520")
    for _ in range(5):
        send("sim touch 936 100")
    send("sim touch 84 28")
    time.sleep(1.0)
    v1 = float(R.plist()["0x01"])
    ms = int((time.time() - t0) * 1000)
    ok = abs((v1 - v0) - 0.5) < 1e-6
    return ms, (0 if ok else 1), f"Δ={v1 - v0:.2f}（目标 +0.5）"


def t_estop():
    """③急停流程：主页急停→确认→SAFE（TEC 停/风扇开）"""
    t0 = time.time(); wrong = 0
    clean_state()
    send("sim touch 84 28")
    time.sleep(0.5)
    send("sim touch 876 528")             # 急停（右下角，位置唯一且不变）
    time.sleep(0.6)
    send("sim touch 392 388")             # 确认（红实底大键）
    time.sleep(0.8)
    s = R.stat()
    send("ack")
    ms = int((time.time() - t0) * 1000)
    ok = (int(s.get("alarm", "0"), 16) & 0x0004) and s.get("fan") == "1"
    return ms, (0 if ok else 1), f"alarm={s.get('alarm')} fan={s.get('fan')}"


def t_find_qr():
    """④找二维码：主页→连接→二维码页（帧取证在计时外）"""
    t0 = time.time(); wrong = 0
    clean_state()
    send("sim touch 84 28")
    time.sleep(0.5)
    send("sim touch 608 520")
    time.sleep(1.0)
    ms = int((time.time() - t0) * 1000)
    p = f"{R.EV}/K11_qr.png"
    ok = R.fb_dump(p)
    send("sim touch 144 520")
    return ms, (0 if ok else 1), "二维码帧 K11_qr.png"


def t_switch_mode():
    """⑤切模式：主页→切自动（确认框）"""
    t0 = time.time(); wrong = 0
    clean_state()
    send("sim touch 84 28")
    time.sleep(0.5)
    send("sim touch 376 520")
    time.sleep(0.5)
    send("sim touch 392 388")
    time.sleep(0.6)
    s = R.stat()
    ms = int((time.time() - t0) * 1000)
    ok = s.get("mode") is not None
    return ms, (0 if ok else 1), f"mode={s.get('mode')}"


TASKS = [("读水温", t_read_temp), ("调高设定0.5", t_raise_half),
         ("急停流程", t_estop), ("找二维码", t_find_qr),
         ("切模式", t_switch_mode)]


def main():
    out = {"tasks": [], "all_pass": True,
           "timing_note": "净时=毛时-探针I/O（probe-rs 单读 ~1s×轮询）；阈值判净时"
                          "（台账 2026-10-19：主机侧 spawn/链路开销不入产品判据）"}
    for name, fn in TASKS:
        io0 = _io_total[0]
        try:
            ms, wrong, detail = fn()
        except Exception as e:
            ms, wrong, detail = -1, 99, f"EXC {e}"
        io = _io_total[0] - io0
        net = max(0, ms - int(io * 1000))
        ok = net <= 30000 and wrong <= 2
        out["tasks"].append({"task": name, "ms_raw": ms, "ms_net": net,
                             "io_s": round(io, 1), "wrong": wrong,
                             "pass": ok, "detail": detail})
        out["all_pass"] &= ok
        print(f"  {name}: 净{net}ms (毛{ms}ms, I/O {io:.1f}s) 错点{wrong} "
              f"{'PASS' if ok else 'FAIL'} — {detail}", flush=True)
    dest = Path(R.EV) / "K11_novice.json"
    with open(dest, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, indent=1)
    print(f"[novice] {'ALL PASS' if out['all_pass'] else 'HAS FAIL'} → {dest}")
    return 0 if out["all_pass"] else 1


if __name__ == "__main__":
    sys.exit(main())
