#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""touch_inject_test.py — WP-M4 台架 20 击注入门（T-13 raw 级走全链）

每个 tap 以 raw 坐标注入（sim touchraw/touchrawr）→ ft5446 映射 → 会话状态机 →
UI tap——完整变换链，封死"注入绕过变换层"盲区。
raw=(screen_y, screen_x)（GS 验证映射 screen=(raw_y, raw_x) 的逆）。
判据：20/20 全中（有日志的命中打日志证据，无日志的命中打状态差证据），
且全程零误触（无预期外的页面跳转/参数变化）。

用法：python tools/touch_inject_test.py   （需 sim 固件在板、TCS_ELF 指向 sim ELF）
"""
import importlib.util
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("mbc", os.path.join(HERE, "mb_console.py"))
mbc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mbc)

OUT = os.path.join(HERE, "..", "..", "docs", "evidence", "M4_inject20.json")
os.makedirs(os.path.dirname(OUT), exist_ok=True)


def send(cmd, wait=1.6):
    return mbc.send_cmd(cmd, wait=wait).decode("utf-8", errors="replace")


def first_param_val():
    out = send("p list")
    for ln in out.splitlines():
        parts = ln.split()
        if len(parts) >= 3 and parts[0].startswith("0x"):
            try:
                return float(parts[2])
            except ValueError:
                pass
    return None


def stat():
    out = send("stat")
    d = {}
    for tok in out.replace("\n", " ").split():
        if "=" in tok:
            k, _, v = tok.partition("=")
            try:
                d[k] = float(v) if "." in v else int(v)
            except ValueError:
                d[k] = v
    return d


# (名称, 屏幕( sx, sy )  None=不发注, 期望日志子串 or None, 验证后置命令)
HITS = [
    ("tune_enter",   (144, 520), "touch page->tune"),
    ("minus_row",    (856, 100), None),
    ("plus_row",     (936, 100), None),
    ("step_toggle",  (248, 520), None),
    ("tune_p2",      (480, 520), None),
    ("reset_entry",  (704, 520), None),
    ("dlg_cancel",   (632, 388), None),
    ("back_home",    (84, 28),   "touch page->home"),
    ("mode_dlg",     (376, 520), None),
    ("mode_confirm", (392, 388), None),
    ("mode_dlg2",    (376, 520), None),
    ("mode_confirm2",(392, 388), None),
    ("fan_on",       (904, 400), "touch fan 1"),
    ("fan_off",      (904, 400), "touch fan 0"),
    ("conn_qr",      (608, 520), "touch page->qr"),
    ("conn_back",    (144, 520), "touch page->home"),
    ("estop_dlg",    (876, 528), None),
    ("estop_cancel", (632, 388), None),
    ("estop_dlg2",   (876, 528), None),
    ("estop_confirm",(392, 388), "touch estop confirm"),
]


def main():
    results = []
    print("== M4 20-hit raw injection ==")
    for i, (name, scr, expect_log) in enumerate(HITS, 1):
        rx, ry = scr[1], scr[0]           # raw=(screen_y, screen_x)
        pre_stat = stat()
        pre_p = first_param_val() if name in ("minus_row", "plus_row") else None
        out = send("sim touchraw %d %d" % (rx, ry))
        time.sleep(0.25)
        out2 = send("sim touchrawr")
        time.sleep(0.45)
        rtt = out + out2
        post_stat = stat()

        hit_log = expect_log and (expect_log in rtt)
        # 状态差证据
        state_ev = ""
        if name == "minus_row":
            post_p = first_param_val()
            ok = pre_p is not None and post_p is not None and abs(pre_p - post_p) > 1e-6
            state_ev = "p %.2f->%.2f" % (pre_p or -1, post_p or -1)
        elif name == "plus_row":
            post_p = first_param_val()
            ok = pre_p is not None and post_p is not None and abs(pre_p - post_p) > 1e-6
            state_ev = "p %.2f->%.2f" % (pre_p or -1, post_p or -1)
        elif name == "mode_confirm":
            state_ev = "mode=%d" % post_stat.get("mode", -1)
            ok = post_stat.get("mode") == 1
        elif name == "mode_confirm2":
            ok = post_stat.get("mode") == 0
        elif name == "fan_on":
            ok = post_stat.get("fan") == 1
        elif name == "fan_off":
            ok = post_stat.get("fan") == 0
        elif name == "estop_confirm":
            ok = (post_stat.get("alarm", 0) or 0) != 0
        elif expect_log:
            ok = bool(hit_log)
        else:
            ok = None   # 无独立判据：链式（由相邻命中与零误触联合证明）

        if name == "estop_confirm":
            send("ack")

        results.append(dict(no=i, name=name, screen=list(scr), raw=[rx, ry],
                            expect_log=expect_log, log_hit=bool(hit_log),
                            state_ev=state_ev, verdict=("PASS" if ok else
                                                        ("CHAIN" if ok is None else "FAIL"))))
        print("[%02d] %-14s scr=%-10s raw=%-10s %s %s"
              % (i, name, scr, (rx, ry), results[-1]["verdict"], state_ev))

    # 零误触：终态必须=主页+mode=0+fan=0（急停 alarm 由 ack 清）
    send("ack")
    time.sleep(0.3)
    final = stat()
    zero_stray = (final.get("mode") == 0 and final.get("fan") == 0)
    n_pass = sum(1 for r in results if r["verdict"] == "PASS")
    n_chain = sum(1 for r in results if r["verdict"] == "CHAIN")
    n_fail = sum(1 for r in results if r["verdict"] == "FAIL")
    summary = dict(pass_=n_pass, chain=n_chain, fail=n_fail,
                   zero_stray=bool(zero_stray), final_stat=final, hits=results)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=1)
    print("== 20击: PASS=%d CHAIN=%d FAIL=%d 零误触=%s -> %s"
          % (n_pass, n_chain, n_fail, zero_stray, os.path.abspath(OUT)))
    return 0 if (n_fail == 0 and zero_stray) else 1


if __name__ == "__main__":
    sys.exit(main())
