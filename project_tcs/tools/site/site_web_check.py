#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""site_web_check.py — 网页服务端的真机检查（无第三方依赖，自带最小 WebSocket 客户端）

前提：**运行本脚本的机器已经连上设备热点**（默认网关 192.168.4.1）。
本脚本不改任何 WiFi 设置。它只验证服务端行为；弹窗、toast、二次确认这类纯前端行为
要用浏览器看（上机卡 §2 的第 9、14 行）。

用法：python project_tcs/tools/site/site_web_check.py [--host 192.168.4.1] [--with-save]
    --with-save   额外执行 W13（cmd 10：保存到模块，会写 M207 的非易失存储）。默认不跑。

检查项（每项打印数字）：
  W1a GET /                  → 200 且是 HTML
  W1b GET /app.js (gzip)     → 解压后与仓库 web_files/app.js 逐字节一致（忽略行尾）
  W2  GET /api/status        → 字段齐全、valid=true、ver=1、stale=false
  W3  WS 连上即收注册表      → 条数、id 唯一、每条有 id/min/max
  W4  状态推送节拍           → 连续 6 个间隔都在 350–650ms（标称 500ms）
  W5  set body_hot(0x03)     → pv(src=web_ack) 值生效；随后改回原值并核对
  W6  set 超上限             → pv 带 clamped=true 且值=上限；随后改回原值
  W7  set 只读参数 0x127     → perr 且 rv<0
  W8  cmd 8（急停）→ cmd 9   → 急停后 alarm 含 0x0004；确认后清除（只做关断方向，不驱动 TEC）
  W9  空文本帧               → 连接不断，之后仍能收到状态
  W10 ping                   → 1 秒内收到内容相同的 pong
  W11 约 300 字节的文本帧    → 服务端关闭这条连接（不流错位），新连接照常可用
"""
import argparse
import base64
import gzip
import hashlib
import json
import os
import socket
import struct
import sys
import time
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
WEB_FILES = os.path.normpath(os.path.join(HERE, "..", "..", "..", "esp32", "components", "tcs_web", "web_files"))

OP_TEXT, OP_CLOSE, OP_PING, OP_PONG = 0x1, 0x8, 0x9, 0xA
ID_BODY_HOT, ID_PUSH_LIST = 0x03, 0x127
ALM_ESTOP = 0x0004


class WSClosed(Exception):
    """对端关闭了连接（TCP 断或收到 close 帧）。"""


class WS:
    """RFC 6455 最小客户端：握手、发文本/ping（客户端帧必须带掩码）、收帧（含分片与 16/64 位长度）。"""

    def __init__(self, host, port=80, path="/ws", timeout=5.0):
        self.host, self.port, self.path, self.timeout = host, port, path, timeout
        self.sock = None
        self.buf = b""

    def connect(self):
        self.sock = socket.create_connection((self.host, self.port), timeout=self.timeout)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall(("GET %s HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                           "Sec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n"
                           % (self.path, self.host, self.port, key)).encode())
        head = b""
        while b"\r\n\r\n" not in head:
            chunk = self.sock.recv(4096)
            if not chunk:
                raise WSClosed("握手阶段连接被关闭")
            head += chunk
        head, _, self.buf = head.partition(b"\r\n\r\n")
        lines = head.decode("latin-1").split("\r\n")
        if " 101 " not in lines[0] + " ":
            raise WSClosed("握手被拒: " + lines[0])
        want = base64.b64encode(hashlib.sha1((key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11").encode()).digest()).decode()
        got = [l.split(":", 1)[1].strip() for l in lines[1:] if l.lower().startswith("sec-websocket-accept")]
        if got != [want]:
            raise WSClosed("握手：Sec-WebSocket-Accept 不对")
        return self

    def _read(self, n):
        while len(self.buf) < n:
            if self.sock is None:
                raise WSClosed("套接字已关闭")
            try:
                chunk = self.sock.recv(65536)
            except socket.timeout:
                raise
            if not chunk:
                raise WSClosed("对端关闭")
            self.buf += chunk
        out, self.buf = self.buf[:n], self.buf[n:]
        return out

    def send_frame(self, op, payload=b""):
        mask = os.urandom(4)
        n = len(payload)
        if n < 126:
            head = bytes([0x80 | op, 0x80 | n])
        elif n < 65536:
            head = bytes([0x80 | op, 0x80 | 126]) + struct.pack(">H", n)
        else:
            head = bytes([0x80 | op, 0x80 | 127]) + struct.pack(">Q", n)
        self.sock.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(payload)))

    def send_text(self, s):
        self.send_frame(OP_TEXT, s.encode("utf-8"))

    def send_ping(self, data=b""):
        self.send_frame(OP_PING, data)

    def recv(self, timeout):
        """返回 (op, payload)。ping 自动回 pong；收到 close 或断线抛 WSClosed；超时抛 socket.timeout。"""
        self.sock.settimeout(timeout)
        op, payload = None, b""
        while True:
            b1, b2 = self._read(2)
            fin, op0 = b1 & 0x80, b1 & 0x0F
            ln = b2 & 0x7F
            if ln == 126:
                ln = struct.unpack(">H", self._read(2))[0]
            elif ln == 127:
                ln = struct.unpack(">Q", self._read(8))[0]
            mask = self._read(4) if (b2 & 0x80) else None
            data = self._read(ln)
            if mask:
                data = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
            if op0 == OP_CLOSE:
                code = struct.unpack(">H", data[:2])[0] if len(data) >= 2 else -1
                raise WSClosed("close 帧 code=%d" % code)
            if op0 == OP_PING:
                self.send_frame(OP_PONG, data)
                continue
            if op0 == OP_PONG:
                return OP_PONG, data
            op = op0 if op is None else op
            payload += data
            if fin:
                return op, payload

    def recv_json(self, want_t, timeout=2.0, pred=None):
        """读第一条 t==want_t（且 pred 为真）的 JSON，其它消息跳过。超时返回 None。"""
        end = time.time() + timeout
        while True:
            left = end - time.time()
            if left <= 0:
                return None
            try:
                op, data = self.recv(left)
            except socket.timeout:
                return None
            if op != OP_TEXT:
                continue
            try:
                j = json.loads(data.decode("utf-8"))
            except ValueError:
                continue
            if j.get("t") == want_t and (pred is None or pred(j)):
                return j

    def close(self):
        try:
            if self.sock is not None:
                self.send_frame(OP_CLOSE, struct.pack(">H", 1000))
                self.sock.close()
        except OSError:
            pass
        self.sock = None


def http_get(host, path, gzip_ok=False, timeout=5.0):
    req = urllib.request.Request("http://%s%s" % (host, path))
    if gzip_ok:
        req.add_header("Accept-Encoding", "gzip")
    with urllib.request.urlopen(req, timeout=timeout) as r:
        return r.status, dict(r.headers), r.read()


class Report:
    def __init__(self):
        self.rows = []
        self.t0 = time.time()

    def add(self, wid, ok, detail):
        self.rows.append((wid, bool(ok)))
        print("%-4s %-4s %6.1fs  %s" % (wid, "PASS" if ok else "FAIL", time.time() - self.t0, detail), flush=True)

    def summary(self):
        n_ok = sum(1 for r in self.rows if r[1])
        print("site_web_check: %d/%d PASS" % (n_ok, len(self.rows)))
        return 0 if n_ok == len(self.rows) and self.rows else 1


def short(j):
    return json.dumps(j, ensure_ascii=False) if j is not None else None


def wait_closed(ws, timeout=3.0):
    """等对端关闭；期间收到的任何非关闭帧都忽略。返回是否在超时内观察到关闭。"""
    end = time.time() + timeout
    while time.time() < end:
        try:
            ws.recv(1.0)
        except socket.timeout:
            continue
        except WSClosed:
            return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="192.168.4.1")
    ap.add_argument("--with-save", action="store_true")
    a = ap.parse_args()
    H, R = a.host, Report()

    # ---- W1 静态资源 ----
    try:
        st, _, body = http_get(H, "/")
        R.add("W1a", st == 200 and b"<html" in body.lower(), "GET / -> %d，%d 字节" % (st, len(body)))
        st, hd, body = http_get(H, "/app.js", gzip_ok=True)
        enc = hd.get("Content-Encoding", "")
        served = gzip.decompress(body) if "gzip" in enc else body
        with open(os.path.join(WEB_FILES, "app.js"), "rb") as f:
            repo = f.read()
        same = served.replace(b"\r\n", b"\n") == repo.replace(b"\r\n", b"\n")
        R.add("W1b", st == 200 and same, "GET /app.js -> %d，编码=%s，%d 字节，与仓库一致=%s"
              % (st, enc or "identity", len(served), same))
    except Exception as e:
        R.add("W1", False, "静态资源请求失败：%r（这台机器连上设备热点了吗？）" % (e,))
        return R.summary()

    # ---- W2 /api/status ----
    try:
        st, _, body = http_get(H, "/api/status")
        j = json.loads(body.decode("utf-8"))
        need = ["t", "stale", "valid", "ver", "tc1_act", "tc2_act", "body", "fan", "mode", "alarm", "boot_s"]
        miss = [k for k in need if k not in j]
        R.add("W2", st == 200 and not miss and j.get("valid") is True and j.get("ver") == 1 and j.get("stale") is False,
              "/api/status -> %d，缺字段=%s，valid=%r ver=%r stale=%r，tc1=%.2f tc2=%.2f body=%.2f alarm=0x%04X"
              % (st, miss, j.get("valid"), j.get("ver"), j.get("stale"), j.get("tc1_act", 0.0),
                 j.get("tc2_act", 0.0), j.get("body", 0.0), j.get("alarm", 0)))
    except Exception as e:
        R.add("W2", False, "/api/status 失败：%r" % (e,))

    # ---- WS 连接 ----
    ws = WS(H)
    try:
        ws.connect()
    except Exception as e:
        R.add("W3", False, "WebSocket 握手失败：%r" % (e,))
        return R.summary()

    back = 37.5          # body_hot 的还原值；W3 读到注册表后用其中的当前值覆盖
    try:
        reg = ws.recv_json("reg", timeout=3.0)
        items = reg.get("list", []) if reg else []
        ids = [it.get("id") for it in items]
        bad = [it for it in items if not all(k in it for k in ("id", "min", "max"))]
        uniq = len(set(ids)) == len(ids)
        R.add("W3", bool(items) and uniq and not bad,
              "注册表 %d 条，id 唯一=%s，字段齐全=%s" % (len(items), uniq, not bad))
        hot = [it for it in items if it.get("id") == ID_BODY_HOT]
        hot_max = float(hot[0].get("max", 45.0)) if hot else 45.0
        if hot and "def" in hot[0]:
            back = float(hot[0]["def"])

        ts = []
        while len(ts) < 7 and time.time() < R.t0 + 30:
            if ws.recv_json("st", timeout=2.0) is None:
                break
            ts.append(time.time())
        gaps = [1000 * (ts[i + 1] - ts[i]) for i in range(len(ts) - 1)]
        R.add("W4", len(gaps) == 6 and all(350 <= g <= 650 for g in gaps),
              "st 间隔(ms)：%s" % " ".join("%.0f" % g for g in gaps))

        # ---- W5 正常写入并还原 ----
        ws.send_text('{"t":"set","id":%d,"v":38.0}' % ID_BODY_HOT)
        pv = ws.recv_json("pv", 2.0, lambda j: j.get("id") == ID_BODY_HOT)
        ok = bool(pv) and abs(float(pv.get("v", 0)) - 38.0) < 1e-6 and pv.get("src") == "web_ack" and not pv.get("clamped")
        R.add("W5a", ok, "set body_hot=38.0 -> %s" % short(pv))
        ws.send_text('{"t":"set","id":%d,"v":%s}' % (ID_BODY_HOT, back))
        pv = ws.recv_json("pv", 2.0, lambda j: j.get("id") == ID_BODY_HOT)
        R.add("W5b", bool(pv) and abs(float(pv.get("v", -1)) - back) < 1e-6, "还原 body_hot=%s -> %s" % (back, short(pv)))

        # ---- W6 超上限 ----
        ws.send_text('{"t":"set","id":%d,"v":99}' % ID_BODY_HOT)
        pv = ws.recv_json("pv", 2.0, lambda j: j.get("id") == ID_BODY_HOT)
        ok = bool(pv) and bool(pv.get("clamped")) and abs(float(pv.get("v", 0)) - hot_max) < 1e-6
        R.add("W6", ok, "set body_hot=99 -> %s（上限 %.1f）" % (short(pv), hot_max))
        if ok:
            ws.send_text('{"t":"set","id":%d,"v":%s}' % (ID_BODY_HOT, back))
            ws.recv_json("pv", 2.0, lambda j: j.get("id") == ID_BODY_HOT)

        # ---- W7 只读参数 ----
        ws.send_text('{"t":"set","id":%d,"v":1}' % ID_PUSH_LIST)
        pe = ws.recv_json("perr", 2.0, lambda j: j.get("id") == ID_PUSH_LIST)
        R.add("W7", bool(pe) and int(pe.get("rv", 0)) < 0, "set 0x127 -> %s" % short(pe))

        # ---- W8 急停 → 确认（关断方向）----
        ws.send_text('{"t":"cmd","c":8}')
        ak = ws.recv_json("ack", 2.0, lambda j: j.get("c") == 8)
        st = ws.recv_json("st", 2.0)
        alarm = int(st.get("alarm", 0)) if st else -1
        R.add("W8a", bool(ak) and int(ak.get("rv", -1)) >= 0 and (alarm & ALM_ESTOP) != 0,
              "cmd 8 -> %s，alarm=0x%04X" % (short(ak), alarm))
        time.sleep(1.0)
        ws.send_text('{"t":"cmd","c":9}')
        ak = ws.recv_json("ack", 2.0, lambda j: j.get("c") == 9)
        st = ws.recv_json("st", 2.0)
        alarm = int(st.get("alarm", -1)) if st else -1
        R.add("W8b", bool(ak) and int(ak.get("rv", -1)) >= 0 and (alarm & ALM_ESTOP) == 0,
              "cmd 9 -> %s，alarm=0x%04X" % (short(ak), alarm))

        # ---- W9 空文本帧 ----
        ws.send_frame(OP_TEXT, b"")
        R.add("W9", ws.recv_json("st", 2.0) is not None, "空帧之后仍收到状态")

        # ---- W10 ping/pong ----
        try:
            ws.send_ping(b"kp")
            pong = None
            end = time.time() + 1.5
            while time.time() < end:
                op, data = ws.recv(0.5)
                if op == OP_PONG:
                    pong = data
                    break
            R.add("W10", pong == b"kp", "ping -> pong=%r" % (pong,))
        except (WSClosed, OSError) as e:
            R.add("W10", False, "ping 之后连接异常：%r" % (e,))

        # ---- W11 超长帧：服务端关连接、不流错位 ----
        closed = False
        try:
            ws.send_text('{"t":"set","id":%d,"v":%s,"pad":"%s"}' % (ID_BODY_HOT, back, "x" * 260))
            closed = wait_closed(ws, 3.0)
        except OSError:
            closed = True
        ws2 = WS(H)
        fresh = False
        try:
            ws2.connect()
            fresh = ws2.recv_json("st", 3.0) is not None
        except Exception:
            fresh = False
        finally:
            ws2.close()
        R.add("W11", closed and fresh, "约 300 字节帧：旧连接被关闭=%s，新连接可用=%s" % (closed, fresh))
    except WSClosed as e:
        R.add("WS", False, "连接意外中断：%r" % (e,))
    except OSError as e:
        R.add("WS", False, "套接字错误：%r" % (e,))
    finally:
        ws.close()

    if a.with_save:
        ws3 = WS(H)
        try:
            ws3.connect()
            ws3.send_text('{"t":"cmd","c":10}')
            ak = ws3.recv_json("ack", 6.0, lambda j: j.get("c") == 10)
            R.add("W13", bool(ak) and int(ak.get("rv", -1)) >= 0, "cmd 10 -> %s（rv=下发条数）" % short(ak))
        except Exception as e:
            R.add("W13", False, "cmd 10 失败：%r" % (e,))
        finally:
            ws3.close()
    return R.summary()


if __name__ == "__main__":
    sys.exit(main())
