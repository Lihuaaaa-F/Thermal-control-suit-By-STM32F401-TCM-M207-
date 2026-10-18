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

REG = [
    {"id": 1, "type": 0, "flags": 31, "min": 5, "max": 45, "step": 0.1, "def": 25.0,
     "key": "tc1_set", "name": "贴身水箱设定温度", "unit": "°C"},
    {"id": 2, "type": 0, "flags": 31, "min": 5, "max": 45, "step": 0.1, "def": 25.0,
     "key": "tc2_set", "name": "环境水箱设定温度", "unit": "°C"},
    {"id": 3, "type": 0, "flags": 15, "min": 36, "max": 40, "step": 0.1, "def": 37.5,
     "key": "body_hot", "name": "体温过热阈值", "unit": "°C"},
    {"id": 4, "type": 0, "flags": 15, "min": 35, "max": 39, "step": 0.1, "def": 36.5,
     "key": "body_cold", "name": "体温过冷阈值", "unit": "°C"},
    {"id": 5, "type": 0, "flags": 15, "min": 0.1, "max": 1, "step": 0.1, "def": 0.2,
     "key": "hyst", "name": "滞回回差", "unit": "°C"},
    {"id": 6, "type": 1, "flags": 15, "min": 0, "max": 1, "step": 1, "def": 0,
     "key": "mode", "name": "控制模式", "unit": ""},
    {"id": 7, "type": 0, "flags": 13, "min": 38, "max": 45, "step": 0.5, "def": 40,
     "key": "tc_set_confirm", "name": "高温二次确认阈值", "unit": "°C"},
    {"id": 8, "type": 1, "flags": 15, "min": 0, "max": 1, "step": 1, "def": 0,
     "key": "commissioned", "name": "布防", "unit": ""},
    {"id": 256, "type": 0, "flags": 4, "min": 0, "max": 10, "step": 0.1, "def": 1.0,
     "key": "pid_kp", "name": "M207 PID 比例", "unit": ""},
    {"id": 257, "type": 0, "flags": 4, "min": 0, "max": 1, "step": 0.01, "def": 0.1,
     "key": "pid_ki", "name": "M207 PID 积分", "unit": ""},
]

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
