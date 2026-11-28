#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""web_mock.py — 调温服网页控制台主机模拟台（WS_PROTOCOL.md v2；仅标准库，Python 3.8+）
在本机复现 ESP32 tcs_web 的 HTTP + WS 行为，没有设备时用它开发/验证 web_files 前端；
参数表直接解析固件 components/tcs_core/param_registry.c（与真机逐条一致）。

用法（在 esp32 目录）：python tools/web_mock.py [端口]  →  浏览器打开启动后打印的网址（Ctrl-C 停止）
    端口缺省 8124；被系统保留而绑定失败时自动顺延（18124/18125/18126），页面不依赖端口。

测试控制（仅回环地址）：
  GET /__fault?body_lost=0&m207_off=0&sens_open=0&screen=1&stall=0[&overtemp=0]   写故障标志（可只给子集）
      stall=1：停止 st 推送但保持连接。overtemp=1：把贴身水温顶到 49.5°C（验证"水温过高"锁存），清掉后快速回落
  GET /__reset   恢复默认（参数、故障、锁存、日志）并断开所有 WS 客户端，等同设备重启
  GET /__frames  最近 300 条 客户端→服务端 帧的 JSON（frames = 原样文本）
  GET /__audit   模拟器内部状态快照
"""
import base64, collections, hashlib, json, math, os, socket, struct, sys, threading, time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

HOST, PORT, FW = "127.0.0.1", 8124, "1.2.0-mock"
GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
HERE = os.path.dirname(os.path.abspath(__file__))
WEB = os.path.normpath(os.path.join(HERE, "..", "components", "tcs_web", "web_files"))
REGISTRY_C = os.path.normpath(os.path.join(HERE, "..", "components", "tcs_core", "param_registry.c"))
MIME = {".html": "text/html; charset=utf-8", ".css": "text/css; charset=utf-8",
        ".js": "application/javascript; charset=utf-8"}
STATIC = ("index.html", "style.css", "app.js", "chart.js")     # 允许提供的静态文件白名单
# 报警位 / 锁存位（WS_PROTOCOL.md §3，与 tcs_core/control.h 同源）
A_BODY, A_M207, A_ESTOP, A_OT, A_EEP, A_OPEN = 1, 2, 4, 8, 16, 32
L_OT, L_BODY, L_M207, L_ESTOP = 1, 2, 4, 8
COMM = A_BODY | A_M207 | A_OPEN
FAULTS = ("body_lost", "m207_off", "sens_open", "screen", "stall", "overtemp")
KEYS = ("tec1", "tec2", "fan", "mode", "state", "alarm", "latch", "armed",     # st 中"变化即推"的离散字段
        "m207", "body_ok", "sens_open", "screen")
# 状态沿日志表 key: (ev_on, msg_on, lvl_on, ev_off, msg_off)。**逐条照抄固件 web_status.c 的
# web_status_diff_log**（事件码、文案、级别）：模拟器自己编一套的话，页面上看到的日志和真机对不上，
# 之前的浏览器实测就是这么被带偏的（2026-11-24 独立复核）。固件不发 cool_on/heat_on。
LOG = {
    "tec0": ("tec1_on", "贴身水箱 TEC 开", "info", "tec1_off", "贴身水箱 TEC 关"),
    "tec1": ("tec2_on", "环境水箱 TEC 开", "info", "tec2_off", "环境水箱 TEC 关"),
    "fan": ("fan_on", "风扇 开", "info", "fan_off", "风扇 关"),
    "mode": ("mode", "切换到自动模式", "info", "mode", "切换到手动模式"),
    "safe": ("safe_in", "进入安全态", "alarm", "safe_out", "退出安全态"),
    "estop": ("estop_on", "急停触发：TEC 全部停止，风扇全速", "alarm", "estop_off", "急停已解除"),
    "ot": ("overtemp_on", "水温过高：已停止加热", "alarm", "overtemp_off", "水温回落，超温已解除"),
    "body_lost": ("body_lost", "体温传感器失联", "warn", "body_ok", "体温传感器恢复"),
    "m207_off": ("m207_offline", "温控模块通信中断", "warn", "m207_online", "温控模块回线"),
    "sens_open": ("sens_open", "水温探头断线", "warn", "sens_closed", "水温探头恢复"),
    "scr": ("screen_down", "屏幕链路断开（主控继续独立运行）", "warn", "screen_up", "屏幕已连接"),
}

def _extract_registry():
    """解析固件 param_registry.c 的 s_params[] 表（声明处到 #define PARAM_N_REGISTERED 为止）。
    行形态：{ 0xID, "key", "name", "unit", TYPE, FLAGS..., min, max, step, value, 0, 0 }。
    纯字符串解析；返回项含 id/type/flags/min/max/step/def/key/name/unit（def = 当前值）。"""
    txt = open(REGISTRY_C, encoding="utf-8").read()
    txt = txt[txt.index("static param_desc_t s_params"):txt.index("#define PARAM_N_REGISTERED")]
    pf = {"PF_RW": 1, "PF_SCREEN": 2, "PF_WEB": 4, "PF_PERSIST": 8, "PF_M207": 16, "PF_DANGER": 32}
    ty = {"PARAM_TYPE_F32": 0, "PARAM_TYPE_U8": 1, "PARAM_TYPE_U16": 2, "PARAM_TYPE_BTN": 3}
    out, pos = [], 0
    while True:
        i = txt.find("{ 0x", pos)
        if i < 0:
            break
        j = txt.find("},", i)
        block, pos = txt[i:j], j + 2
        q, k = [], 0                                   # 前 6 个引号：key / name / unit
        while len(q) < 6:
            k = block.index('"', k)
            q.append(k)
            k += 1
        nums = [float(t.strip()[:-1]) for t in block.split(",") if t.strip().endswith("f")]
        out.append({"id": int(block[2:block.index(",")].strip(), 16),
                    "type": next((v for k, v in ty.items() if k in block), 0),
                    "flags": sum(v for k, v in pf.items() if k in block),
                    "min": nums[0], "max": nums[1], "step": nums[2], "def": nums[3],
                    "key": block[q[0] + 1:q[1]], "name": block[q[2] + 1:q[3]], "unit": block[q[4] + 1:q[5]]})
    return out

class Sim:
    """设备仿真：水温物理 + 安全逻辑（镜像 control.c）+ 布防三态机 + 事件日志（50 条环）。
    除 __init__ 外，所有方法都要求调用方已持 SIM.lock。"""

    def __init__(self):
        self.lock = threading.RLock()
        self.reset()

    def reset(self):
        self.params = _extract_registry()              # 各项的 def 字段 = 当前值
        self.pid = {p["id"]: p for p in self.params}
        self.t0 = time.time()                          # "开机"时刻；ts / up_s 由此推算
        self.tc, self.body, self.sample_t = [22.0, 24.0], 36.8, 0
        self.tec, self.fan, self.state, self.seq, self.run = [0, 0], 0, 0, 0, 0
        self.alarm = self.latch = self.armed = 0
        self.fault = dict.fromkeys(FAULTS, 0)
        self.fault["screen"] = 1                       # 默认：屏幕在线
        self.comm_ok_since, self.prev, self.last_disc = None, {}, None
        self.log, self.log_total, self.log_sent = [], 0, 0
        self._log("boot", "设备上电（模拟器复位）")
        for key in ("estop", "ot", "body_lost", "m207_off", "sens_open", "scr", "mode"):
            self._edge(key, False)                    # 记初值（上电时各故障均未发生、手动模式）
        self._tec(0, 0)
        self._tec(1, 0)
        self._fan(0)
        self._goto(0)                                 # 同上：输出与状态的初值

    def ms(self):                                     # 开机以来的毫秒数
        return int((time.time() - self.t0) * 1000)

    def pv(self, pid):                                # 参数当前值
        return self.pid[pid]["def"]

    def mode(self):                                   # 0 手动 / 1 自动
        return int(self.pv(6))

    def _log(self, ev, msg, lvl="info"):
        self.log.append({"ts": self.ms(), "lvl": lvl, "ev": ev, "msg": msg})
        self.log_total += 1                           # 累计条数；log_total - log_sent = 未广播条数
        del self.log[:-50]

    def _edge(self, key, active):
        """状态沿 → 日志（文案见 LOG 表）；首次只记初值，不记日志。"""
        if key not in self.prev:
            self.prev[key] = active
        elif self.prev[key] != active:
            self.prev[key] = active
            on, m_on, lvl, off, m_off = LOG[key]
            self._log(on if active else off, m_on if active else m_off, lvl if active else "info")

    def _tec(self, ch, on):                           # 输出原语（带沿日志）；锁存期"开"被压成 0
        self.tec[ch] = int(bool(on) and not self.latch)
        self._edge("tec%d" % ch, self.tec[ch])

    def _fan(self, on):
        self.fan = 1 if on else 0
        self._edge("fan", self.fan)

    def _goto(self, st):                              # 0 待机 / 1 制冷 / 2 制热 / 3 安全态
        self.state = st
        for key, n in (("cool", 1), ("heat", 2), ("safe", 3)):
            self._edge(key, st == n)

    def _safe(self):                                  # 安全态：TEC 全停 + 风扇全速
        self._tec(0, 0)
        self._tec(1, 0)
        self._fan(1)
        self._goto(3)

    def _physics(self, now):                          # 100ms 一步
        f, tc = self.fault, self.tc
        if not f["body_lost"]:                        # 体温：36.8 附近的慢正弦（周期 100s）
            self.body = 36.8 + 0.6 * math.sin(now / 16000.0)
        if f["overtemp"]:                             # 演练钩子：把贴身水温顶到 49.5°C
            tc[0] += (49.5 - tc[0]) * 0.2
        if f["m207_off"] or now - self.sample_t < 500:
            return
        self.sample_t = now                           # 水温 500ms 采样一次（age = 距上次采样）
        for i in (0, 1):
            tgt = self.pv(i + 1) if self.tec[i] else 25.0
            k = 0.05 if self.tec[i] else 0.01         # TEC 开：向设定值靠拢（τ≈10s）；关：向室温缓漂
            if i == 0 and tc[i] > 48.0 and not f["overtemp"]:
                k = 0.2                               # 超温演练：清障后快速回落
            tc[i] += (tgt - tc[i]) * k + 0.01 * math.sin(now / 3000.0 + i * 2)

    def _safety(self):                                # 镜像 control_poll 前半（过热/锁存/安全态）
        f, now = self.fault, self.ms()
        hot = self.tc[0] > 48.0                       # 过热硬切断：任意模式、任意布防态
        if hot:
            self.alarm |= A_OT
            self.latch |= L_OT
        self._edge("ot", hot)
        for k in ("body_lost", "m207_off", "sens_open"):
            self._edge(k, f[k])
        self._edge("scr", not f["screen"])
        self._edge("mode", self.mode() == 1)
        ok = not (f["body_lost"] or f["m207_off"] or f["sens_open"] or hot)
        if not self.latch or not ok:                  # 通信类故障：源持续健康 2s 自动清锁存
            self.comm_ok_since = None
        if self.latch & (L_BODY | L_M207):
            if self.comm_ok_since is None:
                self.comm_ok_since = now
            elif now - self.comm_ok_since >= 2000:
                self.latch &= ~(L_BODY | L_M207)
                self.alarm &= ~COMM
        if self.latch:                                # 锁存期：不再评估，停在安全态
            self._safe()
            return
        lost = f["m207_off"] or f["sens_open"]
        if self.armed and (f["body_lost"] or lost):   # 未布防：只记状态，不报警不进安全态
            self.alarm |= A_BODY * f["body_lost"] | A_M207 * f["m207_off"] | A_OPEN * f["sens_open"]
            self.latch |= L_BODY * f["body_lost"] | L_M207 * lost
            self._safe()
        elif self.state == 3 and not self.alarm:      # 源全恢复且无待确认报警 → 回待机
            self._fan(0)
            self._goto(0)

    def _control(self):                               # 自动模式迁移表（镜像 control_poll 后半，含滞回）
        if self.state == 3:
            return                                    # 安全态的进出由 _safety 负责
        hot, cold, hyst, b = self.pv(3), self.pv(4), self.pv(5), self.body
        want = 1 if b > hot + hyst or self.state == 1 and b >= hot - hyst \
            else 2 if b < cold - hyst or self.state == 2 and b <= cold + hyst else 0
        if not self.mode():                           # 手动：状态机旁路，输出保持人工值
            self._goto(0)
        elif self.fault["body_lost"]:                 # 无有效体温样本不得继续自动冷热
            if self.state:
                self._tec(0, 0)
                self._fan(0)
                self._goto(0)
        elif want != self.state:                      # 0 待机 / 1 制冷 / 2 制热；风扇随启停
            self._tec(0, want > 0)
            self._fan(1 if want else 0)
            self._goto(want)

    def _arming(self):
        """布防三态机（post_arm_step）：0 未布防 / 1 已布防 / 2 布防但源丢失报警中。"""
        f, m = self.fault, int(self.pv(8))
        healthy = not (f["body_lost"] or f["m207_off"] or f["sens_open"])
        if m == 2:
            self.armed, self.run = 0, 0               # 强制不布防
        elif self.armed == 0:
            self.armed, self.run = int(m == 1 or healthy), 0   # 强制布防 / 自动布防（源健康即升入）
        elif self.armed == 1:
            self.armed, self.run = 2 - int(healthy), 0
        else:
            self.run = self.run + 1 if healthy else 0
            if not self.alarm & COMM and self.run >= 10:
                self.armed = 1                        # 上线补入：报警已清 + 连续 10 拍健康

    def settle(self):                                 # 不推进物理，只跑安全/控制/布防
        self._safety()
        self._control()
        self._arming()

    def status(self):                                 # 一条 st（seq 由 broadcast 填）
        f, now, tc = self.fault, self.ms(), self.tc
        return {"t": "st", "seq": self.seq, "ts": now, "ver": 3, "fw": FW,
                "tc1": round(tc[0], 2), "tc2": round(tc[1], 2), "body": round(self.body, 2),
                "age": 65535 if f["m207_off"] else min(65535, now - self.sample_t),
                "tc1_set": self.pv(1), "tc2_set": self.pv(2),
                "tec1": self.tec[0], "tec2": self.tec[1], "fan": self.fan, "mode": self.mode(),
                "state": self.state, "alarm": self.alarm, "latch": self.latch, "armed": self.armed,
                "m207": 1 - f["m207_off"], "body_ok": 1 - f["body_lost"], "sens_open": f["sens_open"],
                "screen": f["screen"], "up_s": now // 1000, "reset": 1,
                "heap": int(8500000 + 12000 * math.sin(now / 7000.0))}

    def cmd(self, c):
        """执行网页命令，返回 ack.rv（0 成功 / -1 未知 / -2 安全态或模块离线拒绝 / 10 = 已下发条数）。"""
        rv = 0
        if c in (1, 2, 3, 4, 5) and self.state == 3:
            return -2                                 # 安全态拒绝 TEC/风扇类命令
        if c in (1, 2, 3, 4):                         # TEC 开/关：c//3 选通道，奇数开、偶数关
            self._tec(c // 3, c & 1)
        elif c == 5:
            self._fan(1 - self.fan)                   # 风扇切换
        elif c in (6, 7):                             # 切自动 / 切手动（与固件同走参数 6）
            if not self.set_param(6, float(7 - c), "web", 0) and c == 6:
                return -2                             # 急停/超温锁存期间不得切自动（control_param_check）
        elif c == 8:                                  # 急停：TEC 全停 + 风扇全速 + 切手动 + 锁存
            self._edge("estop", True)
            self.alarm |= A_ESTOP
            self.latch |= L_ESTOP
            self.set_param(6, 0.0, "console", 0)
            self._safe()
        elif c == 9:
            self.ack()
        elif c == 10:                                 # 保存到模块：rv = 已下发条数
            if self.fault["m207_off"]:
                return -2                             # 模块离线未保存
            rv = sum(p["id"] >= 0x100 and p["flags"] & 0x11 == 0x11 for p in self.params)   # 可写且需下发模块的条数
            self._log("m207_save", "参数已保存到模块（%d 条）" % rv)
        else:
            return -1
        self.settle()
        return rv

    def ack(self):
        """人工确认（tcs_alarm_ack）：只清"源已恢复"的锁存；急停须其余源全部健康。"""
        f = self.fault
        ok = (L_OT if self.tc[0] <= 48.0 else 0) | (L_BODY if not f["body_lost"] else 0) \
            | (L_M207 if not f["m207_off"] and not f["sens_open"] else 0)
        drop = self.latch & ok & ~L_ESTOP
        if not (self.latch & ~drop & ~L_ESTOP) and ok == L_OT | L_BODY | L_M207:
            drop |= self.latch & L_ESTOP
        self.latch &= ~drop
        for lat, alm, healthy in ((L_OT, A_OT, self.tc[0] <= 48.0), (L_BODY, A_BODY, not f["body_lost"]),
                                  (L_M207, A_M207, not f["m207_off"]), (L_M207, A_OPEN, not f["sens_open"]),
                                  (L_ESTOP, A_ESTOP, True)):
            if not self.latch & lat and healthy:      # ALM 显示位跟随锁存，不比锁存更早消失
                self.alarm &= ~alm
        if not self.latch & L_ESTOP:
            self._edge("estop", False)
        self.alarm &= ~A_EEP                          # 存储校验报警仅提示性

    def set_param(self, pid, v, src, ref, sock=None):
        """镜像 param_set：钳制 → 步进取整 → 跨参约束 → 板级安全校验；**必回一帧 pv**。
        与固件同一口径（websocket.c handle_set / ws_param_changed）：
          - 带 ref 的 pv 只回给请求者；别的在线页面收到 ref=0 的副本（它们的 ref 从 1 数起，会撞号）；
          - 不重推整张 reg（固件只推 pv；以前这里改一次推一次 7KB，掩盖了前端"忘了把 pv 应用到 reg"的回归）。"""
        p, ok, err, clamped, c = self.pid.get(pid), 0, 0, 0, 0.0
        if p is None or not p["flags"] & 0x01:
            err = -1                                  # 未知 id / 只读
        elif v is None or not math.isfinite(v):
            err = -2                                  # NaN / Inf
        else:
            c = min(p["max"], max(p["min"], v))
            clamped = int(c != v)
            c = round(p["min"] + round((c - p["min"]) / p["step"]) * p["step"], 6)
            if p["type"] in (1, 2):
                c = float(int(c))
            err = -2 if pid == 3 and c <= self.pv(4) or pid == 4 and c >= self.pv(3) else 0   # 过冷阈值必须小于过热阈值
            # control_param_check：急停/超温锁存期间不得切到自动
            if not err and pid == 6 and c != 0 and self.latch & (L_OT | L_ESTOP):
                err = -2
            ok = 0 if err else 1
        if not ok:
            c = p["def"] if p else 0.0
        if ok and c != p["def"]:
            p["def"] = c
            self._log("param", "%s：%s 改为 %g%s" % ({"web": "网页"}.get(src, "系统"), p["name"], c, p["unit"]))
            if sock is not None:
                push({"t": "pv", "id": pid, "v": c, "src": src, "clamped": clamped, "ref": ref, "ok": ok, "err": err}, sock)
                others = [x for x in CLIENTS if x is not sock]
                for x in others:
                    push({"t": "pv", "id": pid, "v": c, "src": src, "clamped": clamped, "ref": 0, "ok": ok, "err": err}, x)
            else:
                push({"t": "pv", "id": pid, "v": c, "src": src, "clamped": clamped, "ref": ref, "ok": ok, "err": err})
        else:
            # 值没变 / 被拒绝：固件在这里补发回执（handle_set 末尾的 !s_web_seen 分支）
            push({"t": "pv", "id": pid, "v": c, "src": src, "clamped": clamped, "ref": ref, "ok": ok, "err": err},
                 sock if sock is not None else None)
        return ok

# ====== WS 帧收发（服务端帧不掩码；客户端帧带掩码）======
CLIENTS, SEND, FRAMES = [], threading.RLock(), collections.deque(maxlen=300)

def _frame(sock, data, op=1):
    n = len(data)                                     # reg 帧约 6KB：两字节长度足够
    hdr = bytes([0x80 | op, n]) if n < 126 else bytes([0x80 | op, 126]) + struct.pack(">H", n)
    sock.sendall(hdr + data)

def _exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            return None                               # 对端已关闭
        buf += chunk
    return buf

def ws_recv(sock):
    """读一条完整消息，返回 bytes；对端关闭/出错返回 None。处理分片与 ping/pong/close。"""
    frag = b""
    while True:
        hdr = _exact(sock, 2)
        if hdr is None:
            return None
        fin, op, ln = hdr[0] & 0x80, hdr[0] & 0x0F, hdr[1] & 0x7F
        ext = {126: 2, 127: 8}.get(ln)
        if ext:
            b = _exact(sock, ext)
            if b is None:
                return None
            ln = struct.unpack(">H" if ext == 2 else ">Q", b)[0]
        if ln > 65536:                                # 超长帧：直接断开（不做流式丢弃）
            return None
        mask = _exact(sock, 4) if hdr[1] & 0x80 else None
        data = _exact(sock, ln) or b""
        if mask:
            data = bytes(c ^ mask[i & 3] for i, c in enumerate(data))
        if op == 8:                                   # close：回 close 后结束
            _frame(sock, data[:2], 8)
            return None
        if op == 9:                                   # ping → pong（pong 丢弃）
            _frame(sock, data, 10)
        elif op < 3:                                  # 文本/二进制首帧（op 1,2）或续帧（op 0）
            frag = data if op else frag + data
            if fin:
                return frag

def push(obj, only=None):
    """推 JSON 帧：only = 只发给该连接；缺省群发，发送失败的连接即除名并关闭。"""
    data = _json(obj)
    with SEND:
        for c in [only] if only else list(CLIENTS):
            try:
                _frame(c, data)
            except OSError:
                if c in CLIENTS:
                    CLIENTS.remove(c)
                c.close()

def _json(obj):
    return json.dumps(obj, ensure_ascii=False, separators=(",", ":")).encode("utf-8")

def broadcast(force=False):
    """日志增量即推；st 在业务字段（含两位小数的水温/体温）相对上次推送有变化、或 force 时推；
    stall 注入时不推 st。与固件 web_status_changed 同口径：age/运行时长不算变化。"""
    if SIM.log_total > SIM.log_sent:
        push({"t": "log", "list": SIM.log[-(SIM.log_total - SIM.log_sent):]})
        SIM.log_sent = SIM.log_total
    st = SIM.status()
    disc = [st[k] for k in KEYS] + [round(st.get(k, 0) or 0, 2) for k in ("tc1", "tc2", "body", "tc1_set", "tc2_set")]
    if SIM.fault["stall"] or not (force or disc != SIM.last_disc):
        return
    SIM.last_disc, SIM.seq = disc, SIM.seq + 1
    st["seq"] = SIM.seq
    push(st)

def _num(x):
    """JSON 数值校验：非数字（含布尔/字符串）抛 ValueError，调用方回 err（与固件 cJSON_IsNumber 一致）。"""
    if isinstance(x, bool) or not isinstance(x, (int, float)):
        raise ValueError("not a number: %r" % (x,))
    return x

def _int(x, lo, hi):
    """固件 json_int_range：必须是整数且落在 [lo, hi]，否则 ValueError。"""
    if x != int(x) or not lo <= x <= hi:
        raise ValueError("not an integer in [%d, %d]: %r" % (lo, hi, x))
    return int(x)

def on_frame(text, sock):
    """处理一条客户端→服务端文本帧（调用方持 SIM.lock）。错误码与固件一致：
    bad_set / bad_cmd / unknown_type / bad_json。"""
    FRAMES.append(text)
    code = "bad_json"
    try:
        msg = json.loads(text)
        if msg["t"] == "set":
            code = "bad_set"
            pid = _int(_num(msg["id"]), 1, 65535)
            ref = _int(_num(msg.get("ref", 0)), 0, 4294967295)
            v = _num(msg["v"])
            SIM.set_param(pid, v, "web", ref, sock)
            SIM.settle()
        elif msg["t"] == "cmd":
            code = "bad_cmd"
            c = _int(_num(msg["c"]), 1, 10)            # 网页只放行 1..10（11 是屏链路的内部命令）
            push({"t": "ack", "c": c, "rv": SIM.cmd(c), "ref": _int(_num(msg.get("ref", 0)), 0, 4294967295)}, sock)
        elif msg["t"] == "ping":
            ref = _int(_num(msg.get("ref", 0)), 0, 4294967295) if "ref" in msg else 0
            push({"t": "pong", "ts": SIM.ms(), "ref": ref}, sock)
            return
        else:
            code = "unknown_type"
            raise ValueError("unknown t")
    except (ValueError, TypeError, KeyError, AttributeError) as e:   # JSON 语法错 / 非对象 / 字段缺失或类型不符
        push({"t": "err", "code": code, "msg": str(e)[:80]}, sock)
    broadcast()

def drop_all():
    """断开全部 WS 客户端（/__reset 等同设备重启）。先 shutdown 再 close：只 close 不会唤醒对方
    线程里阻塞着的 recv，连接会半开着留到超时。各收发线程随后自行收尾。"""
    with SEND:
        socks, CLIENTS[:] = list(CLIENTS), []
    for c in socks:
        try:
            c.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
        c.close()

def ws_serve(sock):
    """握手完成后：入册 → 推 reg + 日志全量 → 收帧循环。返回时连接已关闭并除名。"""
    try:
        with SIM.lock:
            with SEND:
                CLIENTS.append(sock)
            push({"t": "reg", "list": SIM.params}, sock)   # 连上即推注册表 + 日志全量
            push({"t": "log", "list": SIM.log}, sock)
            SIM.log_sent = SIM.log_total                   # 全量已覆盖积压事件：游标抬到此刻（与固件一致），否则增量会重发
            SIM._log("ws_open", "网页已连接")              # 固件在全量推完之后才记：下一拍的增量把它带到
        while True:
            try:
                raw = ws_recv(sock)
            except socket.timeout:
                continue                             # 读超时只是空闲，不是断开
            if raw is None:
                break
            with SIM.lock:
                on_frame(raw.decode("utf-8", "replace"), sock)
    except OSError:
        pass
    finally:
        with SEND:
            if sock in CLIENTS:
                CLIENTS.remove(sock)
        sock.close()

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    timeout = 30                                      # 空闲 keep-alive 连接的回收时间

    def reply(self, code, body, ctype="application/json; charset=utf-8", cache="no-store"):
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", cache)
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        u = urlparse(self.path)
        path, q = u.path, parse_qs(u.query)
        name = "index.html" if path == "/" else path.lstrip("/")
        if path == "/ws":
            self.ws()
        elif path.startswith("/__"):
            self.ctrl(path, q)
        elif path.startswith("/api/"):
            with SIM.lock:
                obj = {"t": "reg", "list": SIM.params} if path == "/api/registry" else \
                    {"t": "log", "list": SIM.log} if path == "/api/log" else \
                    {"t": "link", "rx_bytes": 769101, "rx_frames": 29665, "crc_err": 40, "len_err": 0,
                     "gap_resets": 0, "dup": 1, "retry": 77, "txq_drop": 3, "uart_txdrop": 0, "echo_drop": 5,
                     "pending_evict": 0, "proto_mismatch": 0,
                     "ssid": "TCS-9A3C", "pass": "12345678", "url": "http://192.168.4.1"} if path == "/api/linkstats" else SIM.status()
            return self.reply(200, _json(obj))
        fn = os.path.join(WEB, name)                  # 每次读盘：改前端文件无需重启
        if name in STATIC and os.path.isfile(fn):
            self.reply(200, open(fn, "rb").read(), MIME[os.path.splitext(name)[1]], "no-cache")   # 与固件一致：资源 no-cache，接口 no-store
        else:
            self.reply(204 if name == "favicon.ico" else 404, b"" if name == "favicon.ico" else b'{"err":"not found"}')

    def ctrl(self, path, q):
        """测试控制端点（仅回环）。"""
        if self.client_address[0] != HOST:
            return self.reply(403, b'{"err":"loopback only"}')
        if path == "/__frames":
            return self.reply(200, _json({"count": len(FRAMES), "frames": list(FRAMES)}))
        with SIM.lock:
            if path == "/__audit":                    # 内部状态快照
                return self.reply(200, _json({"st": SIM.status(), "fault": SIM.fault, "tc": SIM.tc,
                                              "body": round(SIM.body, 2), "clients": len(CLIENTS),
                                              "log": SIM.log}))
            if path not in ("/__fault", "/__reset"):
                return self.reply(404, b'{"err":"unknown test endpoint"}')
            if path == "/__reset":
                SIM.reset()
            else:                                     # 只改给出的标志，其余保持
                for k, v in q.items():
                    if k in SIM.fault:
                        SIM.fault[k] = int(v[0] not in ("0", "false", ""))
            SIM.settle()
            if path == "/__reset":
                drop_all()                              # 等同设备重启：页面重连后重新收到 reg 与日志全量
            else:
                broadcast(True)
        self.reply(200, b'{"ok":1}')

    def ws(self):
        key = self.headers.get("Sec-WebSocket-Key")
        if not key or "websocket" not in (self.headers.get("Upgrade") or "").lower():
            return self.reply(400, b'{"err":"not a websocket handshake"}')
        self.send_response(101, "Switching Protocols")
        self.send_header("Upgrade", "websocket")
        self.send_header("Connection", "Upgrade")
        self.send_header("Sec-WebSocket-Accept", base64.b64encode(hashlib.sha1((key + GUID).encode()).digest()).decode())
        self.end_headers()
        self.close_connection = True
        sock = self.connection
        sock.settimeout(2.0)                          # 半开连接：send 最多阻塞 2s
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        ws_serve(sock)

    def log_message(self, *a):
        pass

class Server(ThreadingHTTPServer):
    daemon_threads = True

    def handle_error(self, request, client_address):
        if not isinstance(sys.exc_info()[1], ConnectionError):   # 客户端提前断开（刷新/关页）不刷屏
            ThreadingHTTPServer.handle_error(self, request, client_address)

SIM = Sim()

def ticker():
    """100ms 节拍：物理推进 → 广播；st 500ms 周期推，其间离散字段有变立即推。"""
    last = 0.0
    while True:
        time.sleep(0.1)
        with SIM.lock:
            SIM._physics(SIM.ms())
            SIM.settle()
            due = time.time() - last >= 0.5
            last = time.time() if due else last
            broadcast(due)

if __name__ == "__main__":
    # 8123/8124 在部分 Windows 机器上落在系统保留端口段里（WinError 10013，本开发机就是）：
    # 指定端口绑不上就顺延到备选端口；实际端口以启动后打印出来的网址为准（页面不依赖端口）。
    want = [PORT]
    if len(sys.argv) > 1:
        try:
            want = [int(sys.argv[1])]
        except ValueError:
            raise SystemExit("web_mock: 端口必须是 0–65535 的整数，得到 %r" % sys.argv[1])
    want += [p for p in (18124, 18125, 18126) if p not in want]
    srv, errs = None, []
    for p in want:
        try:
            srv = Server((HOST, p), Handler)
            PORT = p
            break
        except (OSError, OverflowError, ValueError) as e:   # 保留端口 / 超出范围 都换下一个
            errs.append("%d (%s)" % (p, e))
    if srv is None:
        raise SystemExit("web_mock: cannot bind %s on any port tried: %s. "
                         "See 'netsh interface ipv4 show excludedportrange protocol=tcp', "
                         "then run: python tools/web_mock.py <port>" % (HOST, "; ".join(errs)))
    threading.Thread(target=ticker, daemon=True).start()
    print("web_mock: http://%s:%d/  (Ctrl-C to stop)  test: /__fault /__reset /__frames /__audit" % (HOST, PORT), flush=True)
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        srv.server_close()
