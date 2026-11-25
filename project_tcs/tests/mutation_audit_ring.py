#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""mutation_audit_ring.py — 审计留痕环（M07-F-08）的变异验证。

把 App/tcs/audit_ring.c 逐个改坏（每个变异一处），编译 tests/test_audit_ring.c，确认每次都变红：
  什么命令都不留痕 / p list 也留痕 / touch log 也留痕 / 环满后计数溢出 / total 不累加 /
  回放从新到旧 / 回放不管容量（写半条、越界）。
有变异体活下来 = 用例有盲区；编译不过 = 变异锚点过期。两种情况退出码都是 1。
脚手架在 tests/mutkit.py。本脚本只读 work tree。

用法（在 project_tcs 下）：python tests/mutation_audit_ring.py [--verbose]
"""
import io
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutkit  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "App", "tcs", "audit_ring.c")
SRC = io.open(TARGET, encoding="utf-8").read()
WORK = os.path.join(ROOT, "build", "mut_audit_ring")
INCS = [os.path.join(ROOT, "App", "tcs"), os.path.join(ROOT, "App", "proto"), os.path.join(ROOT, "App", "comm")]
SOURCES = [os.path.join(ROOT, "tests", "test_audit_ring.c"), TARGET]

ENTRY = "    if (!cmd || !*cmd) return 0;\n"
P_LIST = "        return sub && *sub && strcmp(sub, \"list\") != 0;\n"
TOUCH = "        return sub && strcmp(sub, \"cal\") == 0;\n"
COUNT = "    if (r->count < AUDIT_SLOT_COUNT) r->count++;\n"
TOTAL = "    r->total++;\n"
ORDER = "        const audit_slot_t *s = &r->slot[(first + n) % AUDIT_SLOT_COUNT];\n"
CAP = "        if (dn + 1u + clen + 4u + rn + 1u > cap - 1u - o) break;   /* 放不下整条就停，不写半条 */\n"

# (编号, 说明, 原文, 改成)
MUTANTS = [
    ("A1", "什么命令都不留痕", ENTRY, ENTRY + "    if (1) return 0;\n"),
    ("A2", "p list 也留痕", P_LIST, "        return sub && *sub;\n"),
    ("A3", "touch log 也留痕", TOUCH, "        return 1;\n"),
    ("A4", "环满后计数继续加（溢出）", COUNT, "    r->count++;\n"),
    ("A5", "total 不累加", TOTAL, ""),
    ("A6", "回放从新到旧", ORDER,
     "        const audit_slot_t *s = &r->slot[(first + r->count - 1u - n) % AUDIT_SLOT_COUNT];\n"),
    ("A7", "回放不管容量（写半条、越界）", CAP, ""),
]


FLAGS = "-DTCS_UNIT_TEST -DTCS_PROFILE=1"       # 与 run_all.sh 同：按台架档编译


def main():
    verbose = "--verbose" in sys.argv
    base, info = mutkit.run_variant(WORK, "base", {}, SOURCES, INCS, flags=FLAGS)
    if base != "PASS":
        print("[mut-audit] FAIL：未变异的实现自己就没过（%s）" % base)
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
            print("  %-3s 被杀  %s%s" % (mid, desc, ("  ← " + "；".join(mutkit.first_fail(out))) if mutkit.first_fail(out) else ""))
        elif res == "COMPILE_FAIL":
            print("  %-3s 编译失败（见 %s）  %s" % (mid, os.path.join(WORK, mid), desc))
            bad += 1
        else:
            print("  %-3s 活下来  %s" % (mid, desc))
            bad += 1
        if verbose and res == "FAIL":
            print(out.rstrip())
    print("mutation_audit_ring: %d 个变异体，%d 个异常（活下来 / 编译失败 / 锚点过期）" % (len(MUTANTS), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
