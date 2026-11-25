#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_m207_gates.py — M207 / TMP117 / 风扇服务的源码门禁（S-G e2）

有些修复落在与硬件绑定的代码里（上电回环自检、UART 驱动安装、控制台 perf 行），主机单测够不着；
这类改动用"读源码、逐字核对"来把关，和单测一起进 run_m207_tests.sh。只依赖标准库。
每条门禁对应一个具体条目；未修的源文件跑它是红的（见 docs/results/S_G_e2_结果.md）。
用法：python -I esp32/tests/test_m207_gates.py     （在仓库任意位置执行；路径相对本文件）
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.normpath(os.path.join(HERE, ".."))
FAIL = 0


def read(rel):
    with io.open(os.path.join(ESP, rel), encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r\n", "\n")


def function_body(src, signature):
    """取从 signature 起到配平的右花括号为止的函数体；找不到返回空串。"""
    i = src.find(signature)
    if i < 0:
        return ""
    j = src.find("{", i)
    depth = 0
    for k in range(j, len(src)):
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                return src[i:k + 1]
    return src[i:]


def check(cond, name):
    global FAIL
    print(("  ok   " if cond else "  FAIL ") + name)
    if not cond:
        FAIL += 1


svc = read(os.path.join("components", "tcs_m207", "m207_service.c"))
core = read(os.path.join("components", "tcs_m207", "m207_core.c"))

# ---------------------------------------------------------------------------
# M12-F-13 上电回环自检
# ---------------------------------------------------------------------------
print("[gate] M12-F-13 boot loopback test: no write frames on the wire; no uninitialized buffer in logs")
boottest = function_body(svc, "static void lb_run_suite(const char *tag)")
probe = function_body(svc, "void m207_lb_boottest(void)")
check(boottest != "" and probe != "", "found lb_run_suite and m207_lb_boottest")
check("TC1:TCACTTEMP=25.5" not in svc,
      "the write-to-read-only-param test frame (TC1:TCACTTEMP=25.5) is gone from m207_service.c")
check(not re.search(r"TCACTTEMP\s*=\s*[-0-9]", boottest + probe),
      "no TCACTTEMP=<number> write frame in the boot-test section")
check('const char *probe = "TC1:TCACTTEMP?\\r";' in probe,
      "the jumper probe frame is a query (TC1:TCACTTEMP?)")
check("line[0] = '\\0';" in boottest,
      "lb_run_suite sets line[0]=NUL before reading the echo (got==0 never prints stack garbage)")
check("m207_lb_echo_ok(" in boottest and "m207_lb_wire_frame(" in boottest,
      "wire frames come from the core table and are judged by m207_lb_echo_ok")
check(not re.search(r"strtol|atoi", function_body(core, "int m207_lb_echo_ok(")),
      "the echo check does not depend on numeric parsing")

print("===================")
if FAIL == 0:
    print("test_m207_gates: PASS")
    sys.exit(0)
print("test_m207_gates: FAIL (%d)" % FAIL)
sys.exit(1)
