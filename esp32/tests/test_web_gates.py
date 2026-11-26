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

# ---------------------------------------------------------------- 条目 10（S 线 i3）：WS 的 ota m 同一口径
print("[gate] i3 WS {\"t\":\"ota\",\"m\":…}: m judged by web_ota_mode_json (exact 0 / 1); otherwise err bad_m")
_i = ws.find('strcmp(ts, "ota")')
ws_ota_txt = ws[_i:_i + 1800] if _i >= 0 else ""     # 取这一段到下一个分支为止（宽一点没关系）
check("web_ota_mode_json(m)" in ws_ota_txt, "ws ota takes m through web_ota_mode_json")
check("valueint" not in ws_ota_txt,
      "no cJSON valueint on the ws ota path (it truncated 1.5 to 1 = rollback)")
check("bad_m" in ws_ota_txt, "malformed m -> err code bad_m (neither mode triggers)")
check("web_ota_mode_value(" not in ws_ota_txt.replace("web_ota_mode_json(", ""),
      "the ws path uses the json variant, not the query-string one")
# ---------------------------------------------------------------- r7f R7-07：OTA 触发前要求输出已停
print("[gate] R7-07 OTA / rollback: both TEC commands must be off before the trigger")
check("web_ota_outputs_stopped((int)control_tec_cmd(1), (int)control_tec_cmd(2))" in h_ota
      and h_ota.index("web_ota_outputs_stopped(") < h_ota.index("app_ota_trigger(m)"),
      "/api/ota checks web_ota_outputs_stopped(control_tec_cmd(1), control_tec_cmd(2)) before app_ota_trigger")
check('"outputs_on"' in h_ota.replace('\\', '') and "409" in h_ota,
      "/api/ota answers 409 with reason=outputs_on (not started, no restart)")
_ws_ota = ws[ws.index('strcmp(ts, "ota") == 0'):]
check("web_ota_outputs_stopped((int)control_tec_cmd(1), (int)control_tec_cmd(2))" in _ws_ota
      and _ws_ota.index("web_ota_outputs_stopped(") < _ws_ota.index("app_ota_trigger(rb)"),
      "the WS ota frame checks web_ota_outputs_stopped(...) before app_ota_trigger")
check('"outputs_on"' in _ws_ota.replace('\\', ''), "the WS ota frame answers code=outputs_on")

# ---------------------------------------------------------------- M14-F-11：WS 控制帧
print("[gate] M14-F-11 WS control frames: httpd answers PING/CLOSE itself (route flag), app handler never sees them")
_ws_uri = re.search(r"\{[^{}]*?\.uri\s*=\s*\"/ws\"[^{}]*?\}", httpd, re.S)
ws_uri = _ws_uri.group(0) if _ws_uri else ""
check(ws_uri != "" and ".is_websocket = true" in ws_uri, "the /ws route is a websocket route")
check(".handle_ws_control_frames = false" in ws_uri,
      "/ws leaves control frames to httpd (PING -> PONG, CLOSE -> CLOSE); "
      "if set back to true nothing answers PING (the app does not implement it)")
check(".ws_post_handshake_cb = ws_on_open" in ws_uri,
      "/ws registers clients in ws_post_handshake_cb (without it: 101 but no data, 2026-11-24)")
check(not re.search(r"\.type\s*=\s*HTTPD_WS_TYPE_(PONG|CLOSE)", ws),
      "the app handler does not build its own PONG/CLOSE frames (would duplicate httpd's answer)")
check('ws_send_masked(fd, OP_PING, "hb")' in selftest and "op == OP_PONG" in selftest,
      "on-device selftest checks PING -> PONG with the same payload (webtest)")

# ---------------------------------------------------------------- M14-F-12：客户端表与 socket 名额
print("[gate] M14-F-12 WS client table: table full -> server_busy + close; capacities agree with httpd and lwip")
ws_register = function_body(ws, "static int ws_register(")
check(ws_register != "" and "server_busy" in ws_register and "ws_drop(fd)" in ws_register,
      "ws_register answers server_busy and closes the session when the table is full")
ws_max = define_int(ws_h, "WS_MAX_CLIENTS")
httpd_max = define_int(ws_h, "WS_HTTPD_MAX_SOCKETS")
lwip = None
for _rel in (("sdkconfig.defaults",), ("sdkconfig.site",)):      # 现场档的 sdkconfig.site 不入库；都没有就用 IDF 默认值
    _m = re.search(r"^CONFIG_LWIP_MAX_SOCKETS=(\d+)", read(ESP, *_rel), re.M)
    if _m:
        lwip = int(_m.group(1))
        break
if lwip is None:
    lwip = 10                                                   # components/lwip/Kconfig: LWIP_MAX_SOCKETS default 10
check(ws_max is not None and httpd_max is not None, "WS_MAX_CLIENTS and WS_HTTPD_MAX_SOCKETS are declared in websocket.h")
check(ws_max is not None and httpd_max is not None and ws_max + 2 <= httpd_max,
      "WS_MAX_CLIENTS (%s) + 2 slots for plain HTTP <= WS_HTTPD_MAX_SOCKETS (%s)" % (ws_max, httpd_max))
check(httpd_max is not None and httpd_max + 3 <= lwip,
      "WS_HTTPD_MAX_SOCKETS (%s) + 3 httpd-internal sockets <= CONFIG_LWIP_MAX_SOCKETS (%s)" % (httpd_max, lwip))
check("_Static_assert(WS_MAX_CLIENTS + 2 <= WS_HTTPD_MAX_SOCKETS" in ws
      and "_Static_assert(WS_HTTPD_MAX_SOCKETS + 3 <= CONFIG_LWIP_MAX_SOCKETS" in ws,
      "both capacity rules are compile-time assertions in websocket.c")
check("cfg.max_open_sockets = WS_HTTPD_MAX_SOCKETS;" in httpd,
      "web_start sets max_open_sockets explicitly to the asserted value")

# ---------------------------------------------------------------- M10-F-06：口令不进日志 + 正典声明
print("[gate] M10-F-06 AP password never reaches the serial log; the canon says it is not a secret and NVS is unencrypted")
_log_stmts = [m.group(0) for m in re.finditer(r"ESP_LOG[IWED]\s*\([^;]*;", softap)]


def _args_only(stmt):
    """去掉格式串（含相邻字面量拼接，如 "a" IPSTR "b"），只留实参部分。"""
    return re.sub(r'"(?:\\.|[^"\\])*"', '""', stmt)


check(len(_log_stmts) > 0, "softap.c logs the AP-up line")
check("pass=%s" not in softap, "softap.c has no pass=%s log")
check("pass=***" in softap, "the AP-up log line prints pass=***")
check(not any(re.search(r"\bpass\b", _args_only(s)) for s in _log_stmts),
      "no log call in softap.c passes the password variable as an argument")
canon = None
_docs = os.path.join(REPO, "docs")
if os.path.isdir(_docs):
    _cands = [f for f in os.listdir(_docs) if f.startswith(u"现行规格说明书_v") and f.endswith(".md")]
    if _cands:
        _cands.sort(key=lambda fn: [int(x) for x in re.findall(r"\d+", fn.rsplit("_v", 1)[-1])])
        canon = read(REPO, "docs", _cands[-1])          # 版本号最高的一份就是现行正典
check(canon is not None, "canon spec found (docs: xianxing guigeshuomingshu, highest version)")
check(canon is not None and u"口令非机密" in canon and u"NVS 不加密" in canon,
      "canon: the AP password is not a secret; NVS is not encrypted")
check(canon is not None and u"产线" in canon and u"后续项" in canon,
      "canon: production encryption is listed as a follow-up item that needs the production line")

print("===================")
if FAIL == 0:
    print("test_web_gates: PASS")
    sys.exit(0)
print("test_web_gates: FAIL (%d)" % FAIL)
sys.exit(1)
