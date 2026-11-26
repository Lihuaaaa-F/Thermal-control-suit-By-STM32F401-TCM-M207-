#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""check_keypad_pd5.py — PD5 只能属于 USART2_TX（M02-F-07，S-G z4 收尾）。

审查时的现象：`App/keypad.c` 是板载矩阵键盘驱动，公开的 `keypad_init()` 会把 KP_COL2=PD5
从 USART2_TX(AF7) 改配成 GPIO 输入。全工程零调用，冲突只靠"没人调"——任何一次接线/
排障时顺手加一行初始化，屏链的发送脚就静默失效（H743→ESP32 方向彻底断，且没有报错）。

2026-11-25 已把 `keypad.c` 从工程里删掉（CMakeLists 的 APP_SRC 里没有它）。本门禁把删除
的结果钉住：文件不再出现、全仓没有 keypad 初始化调用、PD5 在 USART2 的 GPIO 初始化里
显式配置为 AF7。都是纯源码判据，不依赖真机。

用法（在 project_tcs 下）：python tests/check_keypad_pd5.py      退出码 0 通过 / 1 不符
run_all.sh 每次都跑。只读。
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FAIL = 0


def check(cond, name):
    global FAIL
    print(("  ok   " if cond else "  FAIL ") + name)
    if not cond:
        FAIL += 1


def read(rel):
    p = os.path.join(ROOT, rel)
    if not os.path.isfile(p):
        return None
    with io.open(p, encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r\n", "\n")


def strip_comments(src):
    out = []
    i, n = 0, len(src)
    in_str = False
    while i < n:
        c = src[i]
        if in_str:
            out.append(c)
            if c == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if c == '"':
                in_str = False
            i += 1
            continue
        if c == '"':
            in_str = True
            out.append(c)
            i += 1
            continue
        if src.startswith("/*", i):
            e = src.find("*/", i + 2)
            i = n if e < 0 else e + 2
            out.append(" ")
            continue
        if src.startswith("//", i):
            e = src.find("\n", i)
            i = n if e < 0 else e
            continue
        out.append(c)
        i += 1
    return "".join(out)


print("[gate] M02-F-07 PD5 belongs to USART2_TX only: keypad driver is gone and nothing re-claims PD5")

# 1) 驱动文件不再存在（也不许换个名字复活成 App/*keypad*）
_app = os.path.join(ROOT, "App")
_keypads = [f for f in os.listdir(_app) if "keypad" in f.lower()] if os.path.isdir(_app) else []
check(_keypads == [], "no keypad driver file under App/ (%s)" % (", ".join(_keypads) or "none"))

# 2) 源码里没有 keypad 初始化调用（注释不算：CMakeLists / main.h 的说明文字里会提到它）
calls = []
for base, _dirs, files in os.walk(ROOT):
    if any(x in base.replace("\\", "/") for x in ("/third_party", "/build", "/.git")):
        continue
    for fn in files:
        if not fn.endswith((".c", ".h", ".txt", ".cmake")):
            continue
        p = os.path.join(base, fn)
        txt = strip_comments(io.open(p, encoding="utf-8", errors="replace").read())
        for m in re.finditer(r"\bkeypad_(?:init|scan|poll)\s*\(", txt):
            calls.append("%s:%s" % (os.path.relpath(p, ROOT).replace("\\", "/"),
                                    txt[:m.start()].count("\n") + 1))
check(calls == [], "no keypad_init/scan/poll call anywhere in the tree (%s)" % (", ".join(calls) or "none"))

# 3) USART2 的 GPIO 初始化里 PD5 显式配置为 AF7（不是"没人碰"，而是"明确归 USART2"）
main_c = read(os.path.join("Core", "Src", "main.c"))
check(main_c is not None, "Core/Src/main.c readable")
m = re.search(r"uart_gpio_af\s*\(\s*GPIOD\s*,\s*GPIO_PIN_5\s*,\s*GPIO_AF7_USART2\s*\)",
              main_c or "")
check(m is not None, "USART2 init configures PD5 explicitly as GPIO_AF7_USART2")

# 4) 没有别处把 PD5(GPIOD) 配成输入/输出（keypad 复活的最短路径就是一段 HAL_GPIO_Init）
bad = []
for base, _dirs, files in os.walk(os.path.join(ROOT, "App")):
    for fn in files:
        if not fn.endswith(".c"):
            continue
        p = os.path.join(base, fn)
        lines = strip_comments(io.open(p, encoding="utf-8", errors="replace").read()).split("\n")
        for i, ln in enumerate(lines):
            if "GPIOD" in ln and "GPIO_PIN_5" in ln:
                win = "\n".join(lines[i:i + 8])
                if re.search(r"GPIO_MODE_(INPUT|OUTPUT|ANALOG)", win):
                    bad.append("%s:%d" % (os.path.relpath(p, ROOT).replace("\\", "/"), i + 1))
check(bad == [], "no App/ code reconfigures GPIOD pin 5 as a plain GPIO (%s)" % (", ".join(bad) or "none"))

print("===================")
if FAIL == 0:
    print("check_keypad_pd5: PASS")
    sys.exit(0)
print("check_keypad_pd5: FAIL (%d)" % FAIL)
sys.exit(1)
