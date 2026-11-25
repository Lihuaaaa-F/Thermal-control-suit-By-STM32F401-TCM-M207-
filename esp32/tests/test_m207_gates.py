#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_m207_gates.py — M207 / TMP117 / 风扇服务的源码门禁（S-G e2）

门禁内容：M12-F-13（上电回环自检）、M12-F-14（unexpected 计数出口）、M12-F-16（UART 安装失败不 abort）、
M12-F-09 下沉的风扇引脚注释核对（M13-F-09）等。

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



def strip_comments(src):
    """剥掉 C 风格注释（跳过字符串字面量里的内容）。"""
    out = []
    i = 0
    in_str = False
    n = len(src)
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
    # 失败信息里可能引用了源文件中的中文注释：转成 ASCII 再打印（cp1252 的管道下直接 print 会抛异常，
    # 把真正的失败原因吞掉）
    safe = name.encode("ascii", "backslashreplace").decode("ascii")
    print(("  ok   " if cond else "  FAIL ") + safe)
    if not cond:
        FAIL += 1



def c_call_text(src, start):
    """取从 src[start] 处的函数调用括号内的文本（跳过字符串字面量，括号配平）。"""
    j = src.find("(", start)
    depth = 0
    in_str = False
    k = j
    while k < len(src):
        c = src[k]
        if in_str:
            if c == "\\":
                k += 2
                continue
            if c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
            elif c in "([{":
                depth += 1
            elif c in ")]}":
                depth -= 1
                if depth == 0:
                    return src[j + 1:k]
        k += 1
    return src[j + 1:]


def split_args(text):
    """按顶层逗号切分实参（跳过字符串与括号，剥掉注释）。"""
    args, cur, depth, in_str, k = [], [], 0, False, 0
    while k < len(text):
        c = text[k]
        if in_str:
            cur.append(c)
            if c == "\\":
                cur.append(text[k + 1])
                k += 2
                continue
            if c == '"':
                in_str = False
        else:
            if c == '"':
                in_str = True
                cur.append(c)
            elif c in "([{":
                depth += 1
                cur.append(c)
            elif c in ")]}":
                depth -= 1
                cur.append(c)
            elif c == "," and depth == 0:
                args.append("".join(cur))
                cur = []
            else:
                if text[k:k + 2] == "/*":
                    e = text.find("*/", k + 2)
                    k = (e + 2) if e >= 0 else len(text)
                    continue
                cur.append(c)
        k += 1
    args.append("".join(cur))
    return [a for a in args if a.strip()]


def fmt_specs(fmt):
    return re.findall(r"%(?!%)[0-9]*(?:ll|l)?[a-zA-Z]", fmt)


svc = read(os.path.join("components", "tcs_m207", "m207_service.c"))
core = read(os.path.join("components", "tcs_m207", "m207_core.c"))
core_proto_src = read(os.path.join("components", "tcs_m207", "m207_proto.c"))
core_proto_hdr = read(os.path.join("components", "tcs_m207", "include", "m207_proto.h"))

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

# ---------------------------------------------------------------------------
# M12-F-14 unexpected 计数
# ---------------------------------------------------------------------------
console = read(os.path.join("main", "tcs_console.c"))
hdr = read(os.path.join("components", "tcs_m207", "include", "m207_service.h"))
print("[gate] M12-F-14 unexpected counter: accessor exists; console perf line prints the number")
check("uint16_t m207_stats_unexpected(void)" in core and "return s_stat.unexpected;" in core,
      "core provides m207_stats_unexpected()")
check("uint16_t m207_stats_unexpected(void);" in hdr, "declared in m207_service.h")
perf_cmd = function_body(console, "static int cmd_perf(")
check("m207_stats_unexpected()" in perf_cmd, "console perf line calls m207_stats_unexpected()")
i = console.find('printf("[perf] m207')
if i < 0:
    check(False, "console perf printf found")
else:
    a = split_args(c_call_text(console, i))
    specs = fmt_specs(a[0])
    check("unexpected=%u" in a[0], "console perf line format has unexpected=%u")
    check("rx?unexpected" not in a[0],
          "console perf line format no longer carries the literal rx?unexpected")
    check(len(specs) == len(a) - 1,
          "console perf printf: %d conversion specs vs %d args (must match)" % (len(specs), len(a) - 1))
j = svc.find('"[M207P] tick_max=')
if j < 0:
    check(False, "[M207P] periodic line found")
else:
    k = svc.rfind("ESP_LOGI(", 0, j)
    a = split_args(c_call_text(svc, k))
    specs = fmt_specs(a[1])
    check("unexpected=%u" in a[1], "[M207P] periodic line has unexpected=%u")
    check(len(specs) == len(a) - 2,
          "[M207P] ESP_LOGI: %d conversion specs vs %d args (must match)" % (len(specs), len(a) - 2))

# ---------------------------------------------------------------------------
# M12-F-16 UART 驱动安装失败不得 abort
# ---------------------------------------------------------------------------
print("[gate] M12-F-16 m207_service_start: no ESP_ERROR_CHECK; every step checked; cleanup on failure")
start = strip_comments(function_body(svc, "int m207_service_start(void)"))  # 只看代码，注释里提到的不算
check(start != "", "found m207_service_start")
check("ESP_ERROR_CHECK" not in start, "m207_service_start has no ESP_ERROR_CHECK (a peripheral fault must not abort)")
for call in ("uart_driver_install(", "uart_param_config(", "uart_set_pin("):
    checked = re.search(r"err = " + re.escape(call), start) is not None and \
        re.search(r"if \(err != ESP_OK\)", start) is not None
    check(checked, "%s return value is checked" % call.rstrip("("))
check("uart_driver_delete(M207_UART);" in start,
      "a failure after the driver is installed uninstalls it (UART2 is not left occupied)")
check(start.find("s_started = 1;") > start.find("xTaskCreatePinnedToCore("),
      "s_started is set only after the service task exists")
check("return -1;" in start, "start failure returns -1 (the caller can see it)")

# ---------------------------------------------------------------------------
# M13-F-09 fan pin comments
# ---------------------------------------------------------------------------
fan_h = read(os.path.join("components", "tcs_fan", "include", "fan_service.h"))
fan_c = read(os.path.join("components", "tcs_fan", "fan_service.c"))
print("[gate] M13-F-09 fan pin comments agree with the code: fan = GPIO42, 48/38 are the RGB clear pins only")
check(re.search(r"#define\s+FAN_GPIO\s+42\b", fan_h) is not None, "FAN_GPIO is 42")
check("**GPIO42**" in fan_h and "GPIO42 \u2192 \u5149\u8026" in fan_h.replace("**", ""),
      "fan_service.h header comment names GPIO42 as the fan pin")
for name, src in (("fan_service.h", fan_h), ("fan_service.c", fan_c)):
    stale = [l.strip() for l in src.splitlines()
             if re.search(r"(GPIO48\s*\u2192\s*\u5149\u8026|\u98ce\u6247\u811a\uff0848\uff09|48\s+\u82e5\u63a5\s+RGB|\u98ce\u6247\uff08GPIO48)", l)]
    check(not stale, "%s has no comment calling GPIO48 the fan pin%s" % (name, "" if not stale else ": " + stale[0]))
check(re.search(r"fan_ws2812_black_frame\(48\);", fan_c) is not None and
      re.search(r"fan_ws2812_black_frame\(38\);", fan_c) is not None,
      "48 / 38 are still cleared at boot (on-board WS2812 on v1.0 / v1.1 boards)")
check("FAN_GPIO" in fan_c and "gpio_num = FAN_GPIO" in fan_c, "LEDC channel is bound to FAN_GPIO, not a literal pin number")

# ---------------------------------------------------------------------------
# M12-F-12 两份协议副本逐字节一致（Q5 单向同步：H743 源侧 → ESP32 复制件）
# ---------------------------------------------------------------------------
H7 = os.path.normpath(os.path.join(ESP, "..", "project_tcs", "App", "tcs"))
h7c = read(os.path.join(H7, "m207.c")).replace("\r\n", "\n")
h7h = read(os.path.join(H7, "m207.h")).replace("\r\n", "\n")


def region(text, start, end):
    i = text.find(start)
    if i < 0:
        return None
    j = text.find(end, i)
    return None if j < 0 else text[i:j]


print("[gate] M12-F-12 protocol copies are byte-identical: project_tcs m207.[ch] pure region == esp32 m207_proto.[ch]")
c_h7 = region(h7c, "void m207_lineasm_reset(", "/* ===================================================================== */\n/*                          目标机驱动")
c_es = region(core_proto_src, "void m207_lineasm_reset(", "/* ====== 轮询等待期 ERRCODE")
check(c_h7 is not None and c_es is not None, "pure-logic regions found on both sides (.c)")
check(c_h7 == c_es, ".c pure region is byte-identical (H743 %d chars vs ESP32 %d chars)"
      % (len(c_h7 or ""), len(c_es or "")))
h_h7 = region(h7h, "/* ====== 解析结果 ====== */", "/* ====== 目标机驱动")
h_es = region(core_proto_hdr, "/* ====== 解析结果 ====== */", "/* ====== 纯逻辑 3")
check(h_h7 is not None and h_es is not None, "declaration regions found on both sides (.h)")
check(h_h7 == h_es, ".h declaration region is byte-identical (H743 %d chars vs ESP32 %d chars)"
      % (len(h_h7 or ""), len(h_es or "")))
for fn in ("m207_lineasm_reset", "m207_lineasm_feed", "m207_parse_line", "m207_mod_name", "m207_map_format"):
    check(fn in (c_h7 or "") and fn in (c_es or ""), "shared function %s is inside the compared region" % fn)

print("===================")
if FAIL == 0:
    print("test_m207_gates: PASS")
    sys.exit(0)
print("test_m207_gates: FAIL (%d)" % FAIL)
sys.exit(1)
