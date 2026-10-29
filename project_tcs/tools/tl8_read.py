#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tl8_read.py — TL-8（lv_timer_handler 单次耗时）正式采样读取与分位计算

v2 采样块（tcs_lvgl.h）：
  bins[40]    [i*25,(i+1)*25)µs，i=0..39
  tail_ms[64] [i,i+1)ms，i=1..63
  over_64000  ≥64ms
  over_4000   ≥4ms（门限计数）
判定（10000 样本）：P99.9 ≤4ms ⇔ 超过 4ms 的样本数 ≤10。

用法：python tools/tl8_read.py [--wait 秒] [--elf build-g4lv/tcs_main.elf]
"""
import argparse
import os
import subprocess
import sys
import time

PROBE = ["--probe", "0d28:0204", "--chip", "STM32H743IITx"]
MAGIC = 0x544C3821


def nm(elf, name):
    out = subprocess.check_output(["arm-none-eabi-nm", elf], text=True, errors="replace")
    for ln in out.splitlines():
        p = ln.split()
        if len(p) == 3 and p[2] == name:
            return int(p[0], 16)
    raise SystemExit(f"symbol {name} not found in {elf}")


def read_words(addr, n):
    out = subprocess.check_output(
        ["probe-rs", "read", *PROBE, "b32", hex(addr), str(n)], text=True, errors="replace")
    vals = []
    for line in out.splitlines():
        if ":" not in line:
            continue
        body = line.split(":", 1)[1]
        for tok in body.split():
            try:
                vals.append(int(tok, 16))
            except ValueError:
                pass
    return vals[:n]


def read_tl8(addr):
    w = read_words(addr, 1 + 1 + 40 + 64 + 6)   # magic..done
    d = {}
    i = 0
    d["magic"] = w[i]; i += 1
    d["samples"] = w[i]; i += 1
    d["bins"] = w[i:i + 40]; i += 40
    d["tail_ms"] = w[i:i + 64]; i += 64
    d["over_64000"] = w[i]; i += 1
    d["over_4000"] = w[i]; i += 1
    d["max_us"] = w[i]; i += 1
    d["done"] = w[i]
    return d


def percentile(d, pct):
    """返回该分位的上界（µs）：χ 分位落在桶内时，取桶上界（保守）。"""
    n = d["samples"]
    if not n:
        return None
    need = n * pct / 100.0
    cum = 0
    for i, c in enumerate(d["bins"]):
        cum += c
        if cum >= need:
            return (i + 1) * 25
    for i in range(1, 64):
        cum += d["tail_ms"][i]
        if cum >= need:
            return (i + 1) * 1000
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--wait", type=float, default=130.0, help="最多等待采样完成秒数")
    ap.add_argument("--elf", default="build-g4lv/tcs_main.elf")
    a = ap.parse_args()
    addr = nm(a.elf, "g_tl8")
    t0 = time.time()
    while True:
        d = read_tl8(addr)
        if d["magic"] != MAGIC:
            print(f"BAD MAGIC {d['magic']:#x} @ {hex(addr)}")
            return 2
        if d["done"] or time.time() - t0 > a.wait:
            break
        time.sleep(5)

    n = d["samples"]
    o4 = d["over_4000"]
    print(f"TL-8 v2 @ {hex(addr)}  samples={n} done={d['done']}")
    print(f"  P50={percentile(d,50)}µs  P90={percentile(d,90)}µs  "
          f"P99={percentile(d,99)}µs  P99.9={percentile(d,99.9)}µs  max={d['max_us']}µs")
    print(f"  over_4000={o4} ({100.0*o4/max(n,1):.2f}%)  over_64000={d['over_64000']}")
    # 10000 样本时 P99.9 的秩 = 第 10 个最慢样本
    allow = max(1, int(round(n * 0.001)))
    verdict = "PASS" if o4 <= allow else "FAIL"
    print(f"  判定：慢于 4ms 样本 {o4} 个，允许上限 {allow} 个 → {verdict}")
    # 直方图明细（非零桶）
    nz = [(i * 25, (i + 1) * 25, c) for i, c in enumerate(d["bins"]) if c]
    print("  尾部桶（≥1ms）:", [(i, d["tail_ms"][i]) for i in range(1, 64) if d["tail_ms"][i]][:20])
    print("  0-1ms 非零桶:", nz[:12], "..." if len(nz) > 12 else "")
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
