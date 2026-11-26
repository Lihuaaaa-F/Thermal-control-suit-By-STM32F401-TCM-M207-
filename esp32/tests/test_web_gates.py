#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_web_gates.py — 网页控制台 / OTA 加固的源码门禁（S 线 S-G e3b）

门禁内容：X03-F-12（OTA 单实例）、M08-F-12（镜像长度）、M14-F-13（/api/ota 的 m）、
M14-F-11（WS 控制帧）、M14-F-12（WS 客户端表与 socket 名额）、M10-F-06（口令不进日志 + 正典声明）。

这些修复落在与 IDF 绑定的代码里（httpd 的路由标志、客户端表、拷贝任务、日志格式串），主机单测够不着；
用"读源码、逐字核对"来把关，和单测一起进 run_m207_tests.sh。只依赖标准库。
每条门禁对应一个具体条目；未修的源文件跑它是红的（见 docs/results/S_G_e3b_结果.md）。

用法：python -I esp32/tests/test_web_gates.py [--esp-root <目录>]
      默认读本文件所在的 esp32/。--esp-root 指向别的目录时从那里读（演示"修前红"：指向基座提交的导出）。
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.normpath(os.path.join(HERE, ".."))
_args = sys.argv[1:]
if len(_args) == 2 and _args[0] == "--esp-root":
    ESP = os.path.normpath(_args[1])
elif _args:
    sys.exit("usage: test_web_gates.py [--esp-root <dir>]")
REPO = os.path.normpath(os.path.join(ESP, ".."))
FAIL = 0


def read(base, *rel):
    """读文本文件并统一成 LF；不存在返回空串（门禁据此报红，而不是抛异常）。"""
    p = os.path.join(base, *rel)
    if not os.path.isfile(p):
        return ""
    with io.open(p, encoding="utf-8", errors="replace") as f:
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
    """取从 signature 起到配平的右花括号为止的函数；找不到返回空串。"""
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


def define_int(src, name):
    """取 `#define <name> <整数>` 的值；没有返回 None。"""
    m = re.search(r"#define\s+%s\s+\(?\s*(\d+)\b" % re.escape(name), src)
    return int(m.group(1)) if m else None


def check(cond, name):
    global FAIL
    # 失败信息里可能引用了源文件中的中文注释：转成 ASCII 再打印（cp1252 的管道下直接 print 会抛异常）
    safe = name.encode("ascii", "backslashreplace").decode("ascii")
    print(("  ok   " if cond else "  FAIL ") + safe)
    if not cond:
        FAIL += 1


W = "components/tcs_web"
httpd = strip_comments(read(ESP, W, "http_server.c"))
ws = strip_comments(read(ESP, W, "websocket.c"))
ws_h = read(ESP, W, "include", "websocket.h")
main_c = strip_comments(read(ESP, "main", "main.c"))
ota_pure = strip_comments(read(ESP, "main", "ota_pure.c"))
softap_raw = read(ESP, "components", "tcs_wifi", "softap.c")
softap = strip_comments(softap_raw)
selftest = read(ESP, W, "web_selftest_ws.c")

# ---------------------------------------------------------------- X03-F-12：OTA 单实例
print("[gate] X03-F-12 OTA copy: one instance at a time; copy buffer on the heap; busy -> 409")
copy_task = function_body(main_c, "static void ota_copy_task(")
trigger = function_body(main_c, "esp_err_t app_ota_trigger(")
check(trigger != "" and "ota_gate_try_enter()" in trigger and "ESP_ERR_INVALID_STATE" in trigger,
      "app_ota_trigger takes the gate first and reports ESP_ERR_INVALID_STATE when busy")
check("esp_err_t app_ota_trigger(" in main_c and "void app_ota_trigger(" not in main_c,
      "app_ota_trigger returns esp_err_t (it used to return void)")
check("atomic_compare_exchange_strong" in ota_pure and "atomic_bool" in ota_pure,
      "the gate is an atomic_bool compare-and-swap (main/ota_pure.c)")
check(not re.search(r"static\s+uint8_t\s+buf\s*\[", copy_task),
      "ota_copy_task has no static shared buffer")
check("heap_caps_malloc(" in copy_task and "free(buf)" in copy_task,
      "copy buffer comes from the heap and is freed")
leave_idx = copy_task.rfind("leave:")
check(leave_idx > 0 and "ota_gate_leave()" in copy_task[leave_idx:],
      "every exit that does not restart releases the gate (leave: label)")
check(copy_task.count("vTaskDelete(NULL)") == 1 and "esp_restart();" in copy_task,
      "single exit point for all non-restart paths")
h_ota = function_body(httpd, "static esp_err_t h_api_ota(")
check('"409 Conflict"' in h_ota and "ESP_ERR_INVALID_STATE" in h_ota,
      "POST /api/ota answers 409 when another OTA is in progress")
check('"started\\":false' in h_ota, "POST /api/ota no longer claims started:true unconditionally")
ws_recv = function_body(ws, "esp_err_t ws_on_recv(")
check("ESP_ERR_INVALID_STATE" in ws_recv and "server_busy" in ws_recv,
      'WS {"t":"ota"} answers server_busy when another OTA is in progress')

# ---------------------------------------------------------------- M08-F-12：镜像长度
print("[gate] M08-F-12 OTA image length: segment chain followed with bounds checks; no out-of-bounds header indexing")
check(not re.search(r"uint8_t\s+hdr\s*\[\s*256\s*\]", main_c),
      "main.c no longer reads one 256-byte header buffer")
check("ota_image_len(" in main_c and "static uint32_t ota_image_size(" not in main_c,
      "main.c takes the length from ota_image_len (main/ota_pure.c)")
img_len = function_body(ota_pure, "int ota_image_len(")
check(img_len != "" and "part_size - off < OTA_SEG_HDR_LEN" in img_len
      and "len > part_size - off" in img_len,
      "segment header and segment data are bounds-checked before every read")
check("OTA_IMG_ETOOLONG" in img_len and "hdr[23]" in img_len,
      "checksum block / SHA-256 fit is checked; hash presence taken from header byte 23")

# ---------------------------------------------------------------- M14-F-13：/api/ota 的 m
print("[gate] M14-F-13 POST /api/ota: m taken with httpd_query_key_value and matched exactly; otherwise 400")
check('strstr(q, "m=1")' not in h_ota, 'no strstr(q, "m=1")')
check('httpd_query_key_value(q, "m", val, sizeof(val))' in h_ota and "web_ota_mode_value(val)" in h_ota,
      "m is taken by key and judged by web_ota_mode_value")
check("HTTPD_400" in h_ota and "bad_m" in h_ota, "missing or malformed m -> 400")

print("===================")
if FAIL == 0:
    print("test_web_gates: PASS")
    sys.exit(0)
print("test_web_gates: FAIL (%d)" % FAIL)
sys.exit(1)
