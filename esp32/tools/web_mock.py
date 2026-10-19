#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""web_mock.py — WP-F.2 前端全功能验证台（localhost:8080，协议与 ESP32 demo 同构）

本机 aTrust 拦截 PC→ESP32 AP 的 TCP（ICMP 通、TCP RST，企业零信任策略），
PC 浏览器无法直开 192.168.4.1。本 mock 在 localhost:8080 用**同一份前端文件**
+ 同协议 WS（st/pv/reg、set/cmd）复现 ESP32 demo 行为，验证前端全功能。
真机服务端功能由串口日志 + 手机实操（用户）覆盖。

用法：python tools/web_mock.py   （Ctrl-C 停止）
"""
import base64
import hashlib
import json
import math
import os
import struct
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                     "..", "components", "tcs_web", "web_files"))
PORT = 8123

def _extract_registry():
    """WP-P3 golden：直接解析固件 param_registry.c 的 s_params 表——
    mock 与固件注册表逐条一致（CDP 断言"字典行数=H10 表行数"的锚）。
    纯字符串解析（无正则转义风险）：{ 0xID, "key", "name", "unit", TYPE,
    FLAGS..., min, max, step, value, 0, 0 },"""
    path = os.path.normpath(os.path.join(ROOT, "..", "..", "..", "..", "project_tcs",
                                          "App", "tcs", "param_registry.c"))
    txt = open(path, encoding="utf-8").read()
    txt = txt[txt.index("s_params[PARAM_CAPACITY]"):txt.index("#define PARAM_N_REGISTERED")]
    PF = {"PF_RW": 1, "PF_SCREEN": 2, "PF_WEB": 4, "PF_PERSIST": 8,
          "PF_M207": 16, "PF_DANGER": 32}
    TY = {"PARAM_TYPE_F32": 0, "PARAM_TYPE_U8": 1, "PARAM_TYPE_U16": 2}
    out = []
    pos = 0
    while True:
        i = txt.find("{ 0x", pos)
        if i < 0:
            break
        j = txt.find("},", i)
        if j < 0:
            break
        block = txt[i:j]
        # 四个引号字段：key/name/unit + 类型
        q1 = block.find(chr(34))
        q2 = block.find(chr(34), q1 + 1)
        key = block[q1 + 1:q2]
        q3 = block.find(chr(34), q2 + 1)
        q4 = block.find(chr(34), q3 + 1)
        name = block[q3 + 1:q4]
        q5 = block.find(chr(34), q4 + 1)
        q6 = block.find(chr(34), q5 + 1)
        unit = block[q5 + 1:q6] if q5 >= 0 else ""
        # 类型
        ty = 0
        for k, v in TY.items():
            if k in block:
                ty = v
                break
        # flags：块内 PF_ 名求和
        flags = 0
        for k, v in PF.items():
            if k in block:
                flags |= v
        # 数值段：最后一个逗号段里的浮点（形如 '0.0f, 100.0f, 0.1f, 0.4f,  0, 0'）
        tail = block[block.rfind(",", 0, block.rfind(",")) - 0:]
        nums = []
        for tok in block.split(","):
            tok = tok.strip()
            if tok.endswith("f"):
                try:
                    nums.append(float(tok[:-1]))
                except ValueError:
                    pass
        mn, mx, st, dv = nums[0], nums[1], nums[2], nums[3]
        out.append({"id": int(block[2:block.find(chr(44))].strip(), 16), "type": ty, "flags": flags,
                    "min": mn, "max": mx, "step": st, "def": dv,
                    "key": key, "name": name, "unit": unit})
        pos = j + 2
    return out


REG = _extract_registry()

STATE = {"tc1": 25.0, "tc2": 25.0, "body": 36.8, "tec1": 0, "tec2": 0, "fan": 0,
         "mode": 0, "alarm": 0, "seq": 0, "tc1_set": 25.0, "tc2_set": 25.0}
LOCK = threading.Lock()
CLIENTS = []


def demo_step():
    with LOCK:
        STATE["seq"] += 1
        t = time.time()
        if not (STATE["alarm"] & 4) and STATE["mode"]:
            STATE["tc1"] += (STATE["tc1_set"] - STATE["tc1"]) * 0.08
            STATE["tc2"] += (STATE["tc2_set"] - STATE["tc2"]) * 0.06
            STATE["tc1"] += 0.03 * math.sin(t / 7)
            STATE["tc2"] += 0.02 * math.sin(t / 11 + 1)
        else:
            STATE["tc1"] += (25 - STATE["tc1"]) * 0.01
            STATE["tc2"] += (25 - STATE["tc2"]) * 0.01
        STATE["body"] = 36.8 + 0.35 * math.sin(t / 23)
        return {"t": "st", "valid": True, "ver": 1, "seq": STATE["seq"],
                "tc1_act": round(STATE["tc1"], 2), "tc2_act": round(STATE["tc2"], 2),
                "body": round(STATE["body"], 2), "body_sensor_ok": True,
                "tec1": STATE["tec1"], "tec2": STATE["tec2"], "fan": STATE["fan"],
                "mode": STATE["mode"], "alarm": STATE["alarm"], "online": 3,
                "boot_s": int(t) % 100000}


def demo_set(pid, v):
    with LOCK:
        if pid == 1:
            STATE["tc1_set"] = v
        elif pid == 2:
            STATE["tc2_set"] = v
        elif pid == 6:
            STATE["mode"] = int(v)
        elif pid == 8:
            STATE["commissioned"] = int(v)
        else:
            STATE["p_%d" % pid] = v   # H10 高级参数：mock 层存值即回显（R5 接真传输）


def demo_cmd(c):
    with LOCK:
        if c == 1:
            STATE["tec1"] = 1
        elif c == 2:
            STATE["tec1"] = 0
        elif c == 5:
            STATE["fan"] = 0 if STATE["fan"] else 1
        elif c == 6:
            STATE["mode"] = 1
        elif c == 7:
            STATE["mode"] = 0
        elif c == 8:
            STATE["alarm"] |= 4
            STATE["mode"] = 0
            STATE["tec1"] = STATE["tec2"] = 0
            STATE["fan"] = 1
        elif c == 9:
            STATE["alarm"] &= ~4
        elif c == 10:
            STATE["m207_save_requested"] = True   # WP-P3 保存按钮帧序断言锚


def push_all(obj):
    data = json.dumps(obj).encode()
    dead = []
    for c in list(CLIENTS):
        try:
            ws_send_frame(c, data)
        except OSError:
            dead.append(c)
    for c in dead:
        if c in CLIENTS:
            CLIENTS.remove(c)


def ws_send_frame(s, data):
    n = len(data)
    hdr = bytes([0x81])
    if n < 126:
        hdr += bytes([n])
    elif n < 65536:
        hdr += bytes([126]) + struct.pack(">H", n)
    else:
        hdr += bytes([127]) + struct.pack(">Q", n)
    s.sendall(hdr + data)


def ws_recv_msg(s):
    hdr = s.recv(2)
    if len(hdr) < 2:
        return None
    ln = hdr[1] & 0x7F
    if ln == 126:
        ln = struct.unpack(">H", s.recv(2))[0]
    elif ln == 127:
        ln = struct.unpack(">Q", s.recv(8))[0]
    mask = s.recv(4)
    data = b""
    while len(data) < ln:
        data += s.recv(ln - len(data))
    return bytes(b ^ mask[i % 4] for i, b in enumerate(data))


def ws_serve(conn):
    """握手已在 do_GET 完成；这里收发循环"""
    CLIENTS.append(conn)
    try:
        push_all({"t": "reg", "list": REG})
        while True:
            raw = ws_recv_msg(conn)
            if raw is None:
                break
            try:
                msg = json.loads(raw.decode())
            except json.JSONDecodeError:
                continue
            if msg.get("t") == "set":
                demo_set(int(msg["id"]), float(msg["v"]))
                push_all({"t": "pv", "id": int(msg["id"]), "v": float(msg["v"]),
                          "src": "web_ack"})
            elif msg.get("t") == "cmd":
                demo_cmd(int(msg["c"]))
    except OSError:
        pass
    finally:
        if conn in CLIENTS:
            CLIENTS.remove(conn)


class Handler(BaseHTTPRequestHandler):
    MIME = {".html": "text/html", ".js": "application/javascript",
            ".css": "text/css"}

    def do_GET(self):
        if "websocket" in (self.headers.get("Upgrade") or "").lower():
            return self.handle_ws()
        if self.path == "/__audit":          # K8：注入审计脚本的页面（同源保 WS）
            body = open(os.path.join(ROOT, "index.html"), "rb").read().decode("utf-8")
            inj = open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                    "web_audit_inject.html"), encoding="utf-8").read()
            body = body.replace("</body>", "<script>\n" + inj + "\n</script>\n</body>")
            body = body.encode("utf-8")      # Content-Length 必须按字节数（中文字符数<字节数）
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        name = "index.html" if self.path in ("/", "/index.html") else self.path.lstrip("/")
        path = os.path.join(ROOT, name)
        if not os.path.isfile(path):
            self.send_error(404)
            return
        ext = os.path.splitext(name)[1]
        body = open(path, "rb").read()
        self.send_response(200)
        self.send_header("Content-Type", self.MIME.get(ext, "application/octet-stream"))
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def handle_ws(self):
        key = ""
        for line in str(self.headers).splitlines():
            if line.lower().startswith("sec-websocket-key:"):
                key = line.split(":", 1)[1].strip()
        accept = base64.b64encode(
            hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode())
            .digest()).decode()
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", accept)
        self.end_headers()
        self.close_connection = True
        ws_serve(self.connection)

    def log_message(self, *a):
        pass


if __name__ == "__main__":
    def ticker():
        while True:
            time.sleep(0.5)
            push_all(demo_step())
    threading.Thread(target=ticker, daemon=True).start()
    print(f"web_mock: http://127.0.0.1:{PORT}/  (Ctrl-C 停止)", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
