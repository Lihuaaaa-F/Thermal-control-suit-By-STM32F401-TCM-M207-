#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""web_e2e.py — WP-F.2/F3/F4 服务端 E2E 实测（全裸 socket，绕开本机 aTrust/代理拦截）

对 http://192.168.4.1 实测：
  1) GET /             → 200 + gzip
  2) GET /api/registry → 含 0x100/0x101 假 M207 参数（F3）
  3) WS  /ws           → reg 推送 + demo st 推送 + set 回显(web_ack) + cmd 8/9（F2）
  4) POST /api/ota?m=0 → 自拷贝+切换重启；?m=1 = 指回对槽（F4）。m 必填、只认 "0"/"1"（其余 400）
用法：python tools/web_e2e.py [rollback]
"""
import base64
import hashlib
import json
import os
import socket
import struct
import sys
import time

HOST = "192.168.4.1"
SRC_IP = "192.168.4.2"          # WLAN DHCP 地址（绑源地址，绕本机拦截）
CRLF = "\r\n"


def _sock():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind((SRC_IP, 0))          # 绑定 WLAN 源 IP：流量确定走 WLAN 口
    s.settimeout(8)
    s.connect((HOST, 80))
    return s


def http_raw(method, path, timeout=8):
    s = _sock()
    req = method + " " + path + " HTTP/1.1" + CRLF + "Host: " + HOST + CRLF \
        + "Connection: close" + CRLF + CRLF
    s.sendall(req.encode())
    resp = b""
    s.settimeout(timeout)
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            resp += chunk
    except (socket.timeout, TimeoutError):
        pass
    s.close()
    head, _, body = resp.partition(bytes(CRLF + CRLF, "ascii"))
    status = int(head.split(bytes(CRLF, "ascii"))[0].split()[1])
    enc = "gzip" if b"Content-Encoding: gzip" in head else ""
    return status, enc, body


def http_get(path, timeout=8):
    return http_raw("GET", path, timeout)


def ws_connect(path="/ws"):
    s = _sock()
    key = base64.b64encode(os.urandom(16)).decode()
    req = ("GET " + path + " HTTP/1.1" + CRLF + "Host: " + HOST + CRLF
           + "Upgrade: websocket" + CRLF + "Connection: Upgrade" + CRLF
           + "Sec-WebSocket-Key: " + key + CRLF
           + "Sec-WebSocket-Version: 13" + CRLF + CRLF)
    s.sendall(req.encode())
    resp = b""
    while bytes(CRLF + CRLF, "ascii") not in resp:
        resp += s.recv(1024)
    line0 = resp.split(bytes(CRLF, "ascii"))[0]
    assert b"101" in line0, resp[:200]
    return s


def ws_send(s, obj):
    data = json.dumps(obj).encode()
    mask = os.urandom(4)
    hdr = bytes([0x81])
    n = len(data)
    if n < 126:
        hdr += bytes([0x80 | n])
    elif n < 65536:
        hdr += bytes([0x80 | 126]) + struct.pack(">H", n)
    else:
        hdr += bytes([0x80 | 127]) + struct.pack(">Q", n)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    s.sendall(hdr + mask + masked)


def ws_recv(s, timeout=3):
    s.settimeout(timeout)
    hdr = s.recv(2)
    if len(hdr) < 2:
        return None
    ln = hdr[1] & 0x7F
    if ln == 126:
        ln = struct.unpack(">H", s.recv(2))[0]
    elif ln == 127:
        ln = struct.unpack(">Q", s.recv(8))[0]
    data = b""
    while len(data) < ln:
        chunk = s.recv(ln - len(data))
        if not chunk:
            break
        data += chunk
    return json.loads(data.decode("utf-8", "replace"))


def drain(s, seconds):
    msgs = []
    t0 = time.time()
    while time.time() - t0 < seconds:
        try:
            m = ws_recv(s, timeout=max(0.2, seconds - (time.time() - t0)))
        except (socket.timeout, TimeoutError, OSError):
            break
        if m is None:
            break
        msgs.append(m)
    return msgs


def main():
    rollback = len(sys.argv) > 1 and sys.argv[1] == "rollback"
    ok, fail = 0, 0

    def check(name, cond, detail=""):
        nonlocal ok, fail
        if cond:
            ok += 1
            print("  [PASS] " + name + " " + detail)
        else:
            fail += 1
            print("  [FAIL] " + name + " " + detail)

    if rollback:
        print("== F4 回滚：POST /api/ota?m=1 ==")
        st, _, body = http_raw("POST", "/api/ota?m=1")
        check("rollback trigger", st == 200 and b"started" in body, str(body[:60]))
        return

    print("== 1) 静态页 ==")
    st, enc, body = http_get("/")
    check("GET / = 200", st == 200, str(len(body)) + "B")
    check("gzip 编码", enc == "gzip")
    check("gzip magic 1f8b", body[:2] == b"\x1f\x8b")

    print("== 2) 注册表（F3 假 M207 参数）==")
    st, _, body = http_get("/api/registry")
    reg = json.loads(body)
    ids = {e["id"] for e in reg["list"]}
    check("注册表条目数=10", len(reg["list"]) == 10, "n=" + str(len(reg["list"])))
    check("0x100 pid_kp 在列", 0x100 in ids)
    check("0x101 pid_ki 在列", 0x101 in ids)
    kp = next(e for e in reg["list"] if e["id"] == 0x100)
    check("pid_kp 命名", "PID" in kp["name"], kp["name"])

    print("== 3) WebSocket 全功能 ==")
    s = ws_connect()
    msgs = drain(s, 2)
    reg_push = next((m for m in msgs if m.get("t") == "reg"), None)
    check("ws 连上即推 reg", reg_push is not None and len(reg_push["list"]) == 10)
    st_msgs = [m for m in drain(s, 2.5) if m.get("t") == "st"]
    check("demo st 推送", len(st_msgs) >= 3, "n=" + str(len(st_msgs)))
    check("st.valid=true(演示)", all(m.get("valid") for m in st_msgs))
    seqs = [m["seq"] for m in st_msgs]
    check("seq 单调(数值在动)", seqs == sorted(seqs) and len(set(seqs)) > 1)
    check("双水温字段", all("tc1_act" in m and "tc2_act" in m for m in st_msgs))

    ws_send(s, {"t": "set", "id": 1, "v": 28.0})
    msgs = drain(s, 2)
    pv = next((m for m in msgs if m.get("t") == "pv" and m.get("id") == 1), None)
    check("set → pv 回显", pv is not None and abs(pv["v"] - 28.0) < 0.01,
          "v=" + str(pv["v"] if pv else None))
    check("回显 src=web_ack", pv is not None and pv.get("src") == "web_ack")

    ws_send(s, {"t": "cmd", "c": 8})     # 急停
    msgs = drain(s, 2.5)
    stm = next((m for m in msgs if m.get("t") == "st"), None)
    check("急停 → alarm bit0x4", stm is not None and (stm.get("alarm", 0) & 4) != 0,
          "alarm=" + str(stm.get("alarm") if stm else None))
    check("急停 → fan 全开", stm is not None and stm.get("fan") == 1)
    check("急停 → mode 手动", stm is not None and stm.get("mode") == 0)

    ws_send(s, {"t": "cmd", "c": 9})     # 人工确认恢复
    msgs = drain(s, 2.5)
    stm = next((m for m in msgs if m.get("t") == "st"), None)
    check("ack → alarm 清零", stm is not None and stm.get("alarm", 0xFFFF) == 0,
          "alarm=" + str(stm.get("alarm") if stm else None))
    s.close()

    print("==== web_e2e RESULT: PASS=" + str(ok) + " FAIL=" + str(fail)
          + (" ALL GREEN" if not fail else " HAS FAIL"))
    sys.exit(1 if fail else 0)


if __name__ == "__main__":
    main()
