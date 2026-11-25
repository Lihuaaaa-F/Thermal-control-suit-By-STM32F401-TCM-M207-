#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""mutation_perf_hist.py — 主循环单拍耗用的窗口直方图（M01-F-09）的变异验证。

把 App/tcs/perf_hist.c 逐个改坏（每个变异一处），编译 tests/test_perf_hist.c，确认每次都变红：
  分桶差一 / 百分位用 32 位乘法（大 n 溢出）/ 百分位不向上取整 / 超预算不含等于 / 清窗口连预算一起清。
有变异体活下来 = 用例有盲区；编译不过 = 变异锚点过期。两种情况退出码都是 1。
脚手架在 tests/mutkit.py。本脚本只读 work tree。

用法（在 project_tcs 下）：python tests/mutation_perf_hist.py [--verbose]
"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutkit  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "App", "tcs", "perf_hist.c")
SRC = io.open(TARGET, encoding="utf-8").read()
WORK = os.path.join(ROOT, "build", "mut_perf_hist")
INCS = [os.path.join(ROOT, "App", "tcs"), os.path.join(ROOT, "App", "proto"), os.path.join(ROOT, "App", "comm")]
SOURCES = [os.path.join(ROOT, "tests", "test_perf_hist.c"), TARGET]
FLAGS = "-DTCS_UNIT_TEST -DTCS_PROFILE=1"

BUCKET = "    while ((us >> b) > 1u && b < PERF_HIST_BUCKETS - 1u) b++;\n"
TARGET64 = "    uint64_t target = ((uint64_t)pct * (uint64_t)h->n + 99u) / 100u;\n"
OVER = "    if (h->over_thresh && us >= h->over_thresh && h->over != 0xFFFFFFFFu) h->over++;\n"
RESET = "    h->over = 0;\n}\n"

# (编号, 说明, 原文, 改成)
MUTANTS = [
    ("H1", "分桶差一（2 落进桶 0）", BUCKET,
     "    while ((us >> b) > 2u && b < PERF_HIST_BUCKETS - 1u) b++;\n"),
    ("H2", "百分位用 32 位乘法（大 n 时溢出）", TARGET64,
     "    uint64_t target = ((uint64_t)((pct * h->n + 99u) / 100u));\n"),
    ("H3", "百分位不向上取整（pct × n 除不尽时差一拍）", TARGET64,
     "    uint64_t target = ((uint64_t)pct * (uint64_t)h->n) / 100u;\n"),
    ("H4", "超预算不含等于预算的那一拍", OVER,
     "    if (h->over_thresh && us > h->over_thresh && h->over != 0xFFFFFFFFu) h->over++;\n"),
    ("H5", "清窗口连预算一起清", RESET,
     "    h->over = 0;\n    h->over_thresh = 0;\n}\n"),
]


def main():
    verbose = "--verbose" in sys.argv
    base, info = mutkit.run_variant(WORK, "base", {}, SOURCES, INCS, flags=FLAGS)
    if base != "PASS":
        print("[mut-perf] FAIL：未变异的实现自己就没过（%s）" % base)
        if base == "FAIL":
            print(info)
        return 1
    print("未变异：%s（%s）" % (base, info.strip().splitlines()[-1] if info.strip() else ""))
    bad = 0
    for mid, desc, old, new in MUTANTS:
        if SRC.count(old) != 1:
            print("  %-3s 锚点匹配 %d 次（应为 1）——实现改了，清单要跟着改：%r" % (mid, SRC.count(old), old[:50]))
            bad += 1
            continue
        res, out = mutkit.run_variant(WORK, mid, {TARGET: SRC.replace(old, new)}, SOURCES, INCS, flags=FLAGS)
        if res == "FAIL":
            ff = mutkit.first_fail(out)
            print("  %-3s 被杀  %s%s" % (mid, desc, ("  ← " + "；".join(ff)) if ff else ""))
        elif res == "COMPILE_FAIL":
            print("  %-3s 编译失败（见 %s）  %s" % (mid, os.path.join(WORK, mid), desc))
            bad += 1
        else:
            print("  %-3s 活下来  %s" % (mid, desc))
            bad += 1
        if verbose and res == "FAIL":
            print(out.rstrip())
    print("mutation_perf_hist: %d 个变异体，%d 个异常（活下来 / 编译失败 / 锚点过期）" % (len(MUTANTS), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
