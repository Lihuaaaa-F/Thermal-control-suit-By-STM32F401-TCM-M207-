#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_ws_client.py — 自测 site_web_check.py 里的 WebSocket 客户端（本机回环，不需要设备）

用一个只实现服务端成帧的最小服务器（与被测客户端是两份独立代码）验证客户端：
握手、带掩码发送、小帧 / 16 位长度 / 64 位长度帧的收发、服务端分片帧的拼接、
ping→pong、空文本帧、按类型过滤读 JSON、超长帧时识别对端关闭。

用法：python project_tcs/tools/site/test_ws_client.py
全部通过打印 test_ws_client: PASS=n FAIL=0，退出码 0。
"""
import base64
import hashlib
import os
import socket
import struct
import sys
import threading

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import site_web_check as swc  # noqa: E402

g_pass = g_fail = 0


def CHECK(cond, name):
    global g_pass, g_fail
    if cond:
        g_pass += 1
    else:
        g_fail += 1
        print("FAIL:", name)


def srv_frame(op, payload, fin=True):
    n = len(payload)
    head = bytes([(0x80 if fin else 0) | op])
    if n < 126:
        return head + bytes([n]) + payload
    if n < 65536:
        return head + bytes([126]) + struct.pack(">H", n) + payload
    return head + bytes([127]) + struct.pack(">Q", n) + payload


def read_exact(conn, n):
    out = b""
    while len(out) < n:
        chunk = conn.recv(n - len(out))
        if not chunk:
            raise ConnectionError
        out += chunk
    return out


def serve(conn):
    """规则：文本帧原样回显，但拆成两片发（检验客户端拼接）；以 {"t":"set" 开头且 ≥128 字节的帧 → 关闭；
    空文本帧 → 回一条 {"t":"st"}；ping → pong。"""
    try:
        buf = b""
        while b"\r\n\r\n" not in buf:
            buf += conn.recv(4096)
        key = [l.split(":", 1)[1].strip() for l in buf.decode("latin-1").split("\r\n")
               if l.lower().startswith("sec-websocket-key")][0]
        acc = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        conn.sendall(("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                      "Sec-WebSocket-Accept: %s\r\n\r\n" % acc).encode())
        while True:
            b1, b2 = read_exact(conn, 2)
            op, ln = b1 & 0x0F, b2 & 0x7F
            if ln == 126:
                ln = struct.unpack(">H", read_exact(conn, 2))[0]
            elif ln == 127:
                ln = struct.unpack(">Q", read_exact(conn, 8))[0]
            if not (b2 & 0x80):
                raise ValueError("客户端帧必须带掩码")
            mask = read_exact(conn, 4)
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(read_exact(conn, ln)))
            if op == swc.OP_CLOSE:
                return
            if op == swc.OP_PING:
                conn.sendall(srv_frame(swc.OP_PONG, payload))
                continue
            if op != swc.OP_TEXT:
                continue
            if ln >= 128 and payload.startswith(b'{"t":"set"'):
                conn.sendall(srv_frame(swc.OP_CLOSE, struct.pack(">H", 1009)))
                conn.close()
                return
            if ln == 0:
                conn.sendall(srv_frame(swc.OP_TEXT, b'{"t":"st"}'))
                continue
            half = len(payload) // 2
            conn.sendall(srv_frame(swc.OP_TEXT, payload[:half], fin=False))
            conn.sendall(srv_frame(0x0, payload[half:], fin=True))     # 续片 op=0
    except (OSError, ValueError, IndexError):
        pass
    finally:
        try:
            conn.close()
        except OSError:
            pass


def accept_loop(srv):
    while True:
        conn, _ = srv.accept()
        threading.Thread(target=serve, args=(conn,), daemon=True).start()


def main():
    srv = socket.socket()
    srv.bind(("127.0.0.1", 0))
    srv.listen(8)
    port = srv.getsockname()[1]
    threading.Thread(target=accept_loop, args=(srv,), daemon=True).start()

    ws = swc.WS("127.0.0.1", port).connect()
    CHECK(True, "握手")

    for n, name in ((5, "小帧"), (125, "125 字节"), (126, "126 字节"), (300, "300 字节(16 位长度)"),
                    (65535, "65535 字节(16 位长度上界)"), (70000, "70000 字节(64 位长度)")):
        payload = ("m%d " % n).encode() * (n // 5 + 1)
        payload = payload[:n]
        ws.send_text(payload.decode("latin-1"))
        op, got = ws.recv(3.0)
        CHECK(op == swc.OP_TEXT and got == payload, "%s 收发一致（服务端分两片发）" % name)

    ws.send_frame(swc.OP_TEXT, b"")
    op, got = ws.recv(2.0)
    CHECK(op == swc.OP_TEXT and got == b'{"t":"st"}', "空文本帧后连接仍在")

    ws.send_text('{"t":"st","seq":7}')
    j = ws.recv_json("st", 2.0, lambda d: d.get("seq") == 7)
    CHECK(j is not None and j.get("seq") == 7, "recv_json 按类型与条件过滤")
    CHECK(ws.recv_json("pv", 0.5) is None, "recv_json 超时返回 None")

    ws.send_ping(b"kp")
    op, got = ws.recv(2.0)
    CHECK(op == swc.OP_PONG and got == b"kp", "ping→pong")

    ws.send_text('{"t":"set","id":3,"v":37.5,"pad":"' + "x" * 200 + '"}')
    closed = False
    try:
        ws.recv(2.0)
    except swc.WSClosed:
        closed = True
    except socket.timeout:
        closed = False
    CHECK(closed, "识别对端关闭")
    ws.close()

    ws2 = swc.WS("127.0.0.1", port).connect()
    ws2.send_text("again")
    op, got = ws2.recv(2.0)
    CHECK(got == b"again", "关闭后可重新握手")
    ws2.close()

    print("test_ws_client: PASS=%d FAIL=%d" % (g_pass, g_fail))
    return 0 if g_fail == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
