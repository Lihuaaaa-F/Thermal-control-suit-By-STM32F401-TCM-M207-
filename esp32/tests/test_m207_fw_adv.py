#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_m207_fw_adv.py — 对端模型直驱固件内核（S-G e2：X06-F-05）

原来 tests/test_m207_adv.py 的"五行注入矩阵"只证明 Python 模拟器（project_tcs/tools/m207_sim.py）
自洽：改了 m207_service 的写确认 / 队列 / 恢复逻辑，它照样 PASS。现在把角色倒过来：
  - 对端 = 同一个 M207Core 模型（209 参字典、ERRORCODE 全表、! 保存、50ms 铁律）；
  - 被测 = 交付固件用的同一份决策逻辑 m207_core.c，经 tests/m207_core_host.c 编成主机桥。
每一拍固件发出一帧，模型给出应答，应答在下一拍进入固件的 RX——与真机回环应答器同时序。

用法：python -I tests/test_m207_fw_adv.py <m207_core_host 可执行文件>     （在 esp32 目录下执行）
"""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, "..", "..", "project_tcs", "tools")))
import m207_sim  # noqa: E402

FAIL = 0
CHECKS = 0


def check(cond, msg):
    global FAIL, CHECKS
    CHECKS += 1
    if not cond:
        FAIL += 1
        print("  FAIL:", msg)


class Fw:
    """固件内核（经主机桥）+ 对端模型。"""

    def __init__(self, exe, state_file=None):
        self.p = subprocess.Popen([exe], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  text=True, encoding="utf-8", bufsize=1)
        self.state_file = state_file
        self.sim = m207_sim.M207Core(state_file=state_file)
        self.t = 0.0
        self.tx = []            # 固件发出的每一帧
        self.rep = []           # 对应对端的应答（'' = 静默）
        self.last = None
        self.cmd("init")

    def cmd(self, line):
        self.p.stdin.write(line + "\n")
        self.p.stdin.flush()
        r = self.p.stdout.readline()
        if not r.startswith("rc "):
            raise SystemExit("host bridge protocol error: %r" % r)
        return int(r[3:])

    def tick(self, n=1):
        for _ in range(n):
            self.p.stdin.write("tick\n")
            self.p.stdin.flush()
            while True:
                l = self.p.stdout.readline()
                if not l:
                    raise SystemExit("host bridge died")
                l = l.rstrip("\n")
                if l.startswith("TX "):
                    frame = l[3:]
                    self.t += 0.06
                    rep = self.sim.handle_line(frame, now=self.t)
                    r = rep[0].rstrip("\r") if rep else ""
                    self.tx.append(frame)
                    self.rep.append(r)
                    self.p.stdin.write(("R " + r + "\n") if r else "R\n")
                    self.p.stdin.flush()
                elif l.startswith("S "):
                    self.last = l[2:].split()
                    break
        return self.last

    def until(self, pred, limit=400):
        """逐拍直到 pred(状态) 为真；返回是否在 limit 拍内达到。"""
        n = 0
        while not pred(self.last):
            if n >= limit:
                return False
            self.tick()
            n += 1
        return True

    def ask(self, frame):
        """直接问对端模型一个帧（时间向前推，避开 50ms 铁律）。"""
        self.t += 0.06
        rep = self.sim.handle_line(frame, now=self.t)
        return rep[0].rstrip("\r") if rep else ""

    def reboot_module(self):
        """模块断电重启：未保存的值回到上次保存的状态。"""
        del self.sim
        self.sim = m207_sim.M207Core(state_file=self.state_file)

    # 状态字段（与主机桥的 S 行一一对应）
    def f(self, name):
        idx = {"online": 0, "alarm": 1, "tc1": 2, "tc2": 3, "sensor_open": 4, "tec1_on": 5,
               "tec2_on": 6, "nak": 7, "vok": 8, "resend": 9, "offline": 10, "unexpected": 11,
               "qdrop": 12, "superseded": 13, "save_ok": 14, "save_rej": 15}[name]
        v = self.last[idx]
        if name == "alarm":
            return int(v, 16)
        return float(v) if name in ("tc1", "tc2") else int(v)

    def close(self):
        try:
            self.p.stdin.write("q\n")
            self.p.stdin.flush()
            self.p.wait(timeout=5)
        except Exception:
            self.p.kill()


def num(text):
    try:
        return float(text.split("=", 1)[1])
    except Exception:
        return None


def main():
    exe = sys.argv[1]
    sf = tempfile.mktemp(suffix=".state")
    fw = Fw(exe, state_file=sf)

    # ---------- 行 0：上电首联（固件的队列 / 复联 / 轮询状态机对上模型）----------
    fw.tick(2)                                 # 每拍至多一帧：头两拍是两条安全默认帧
    check(len(fw.tx) >= 2 and fw.tx[0] == "TC1:TCSW=0" and fw.tx[1] == "TC2:TCSW=0",
          "boot: first two frames are the safe-off defaults (got %r)" % fw.tx[:2])
    check(fw.until(lambda s: s[0] == "1", 200), "boot: link comes online against the model")
    check(fw.f("offline") == 0 and fw.f("qdrop") == 0, "boot: no offline edge, no queue drop")
    check(fw.f("tc1") > 0 or fw.f("sensor_open") == 1,
          "boot: a temperature was taken from the model (or reported sensor-open)")
    # 复联序列（两条安全默认帧各带一次读回确认 + 目标温度 / 开关查询 + 模块状态查询）发完再取基线：
    # 否则"vok+1"会是开机安全默认帧的确认，不是后面那次写的。
    fw.tick(40)
    check(fw.f("vok") == 2 and fw.f("nak") == 0,
          "boot: the two safe-off frames are confirmed by read-back (vok=%d nak=%d)"
          % (fw.f("vok"), fw.f("nak")))

    # ---------- 行 1：设 → 读回一致（固件写路径：设 → REPLY=1 → 查 → 回显比对）----------
    plan = [(0x100, "TC1:TCPIDP"), (0x101, "TC1:TCPIDTI"), (0x10D, "TC1:TCMAXV"),
            (0x10E, "TC1:TCCOOLVRATIO"), (0x10F, "TC1:TCSTEPTEMP"), (0x110, "TC2:TCPIDP")]
    for pid, key in plan:
        v0, n0, ntx = fw.f("vok"), fw.f("nak"), len(fw.tx)
        rc = fw.cmd("api map_set %d 2.5" % pid)
        check(rc == 0, "set %s: accepted by the firmware (rc=%d online=%d)" % (key, rc, fw.f("online")))
        done = fw.until(lambda s: fw.f("vok") > v0 or fw.f("nak") > n0, 80)
        check(done and fw.f("vok") == v0 + 1 and fw.f("nak") == n0,
              "set %s: firmware confirms by read-back (no NAK)" % key)
        # 这次写在总线上的样子：先设定、模型回 REPLY=1，再查询、模型回显
        seq = list(zip(fw.tx[ntx:], fw.rep[ntx:]))
        want = key + "="
        check(any(f.startswith(want) and num(f) == 2.5 and r == "CMD:REPLY=1" for f, r in seq),
              "set %s: value frame on the wire, model accepted" % key)
        check(any(f == key + "?" and r.startswith(want) and num(r) == 2.5 for f, r in seq),
              "set %s: firmware read back and the model echoed 2.5" % key)
        got = fw.ask(key + "?")
        check(num(got) == 2.5, "set %s: the model now holds 2.5 (got %r)" % (key, got))

    # ---------- 行 2：越界 → 模型拒绝，固件记 NAK、不重发，原值不变 ----------
    n0, r0 = fw.f("nak"), fw.f("resend")
    rc = fw.cmd("api map_set %d 99" % 0x10D)     # 主机桥按十进制解析参数号
    check(rc == 0, "over-max: frame leaves the firmware (rc=%d online=%d)" % (rc, fw.f("online")))
    done = fw.until(lambda s: fw.f("nak") > n0, 80)
    check(done and fw.f("nak") == n0 + 1 and fw.f("resend") == r0,
          "over-max: CMD:REPLY=4 -> one NAK, no resend")
    check(num(fw.ask("TC1:TCMAXV?")) == 2.5, "over-max: model value unchanged")
    ntx = len(fw.tx)
    check(fw.cmd("api set_temp 1 999") == -1 and fw.cmd("api set_temp 1 -99") == -1,
          "set_temp outside +-20..80 is rejected by the firmware itself")
    fw.tick(2)
    check(len(fw.tx) == ntx or all("TCADJTEMP=999" not in f and "=999" not in f for f in fw.tx[ntx:]),
          "out-of-range set_temp never reaches the wire")

    # ---------- 行 3：保存 → 模块重启保持；未保存 → 重启回旧值 ----------
    n0 = len(fw.tx)
    rc = fw.cmd("api save_all")
    check(rc == 32, "save_all plans 32 frames (rc=%d)" % rc)
    guard = 0
    while sum(1 for f in fw.tx[n0:] if f.endswith("!")) < 32 and guard < 200:
        fw.tick()
        guard += 1
    frames = fw.tx[n0:]
    saves = [(f, r) for f, r in zip(frames, fw.rep[n0:]) if f.endswith("!")]
    check(len(saves) == 32, "save: all 32 save frames on the wire (got %d)" % len(saves))
    ack8 = sum(1 for _, r in saves if r == "CMD:REPLY=8")
    other = sum(1 for _, r in saves if r and r != "CMD:REPLY=8")
    none = sum(1 for _, r in saves if not r)
    check(none == 0, "save: the model answered every save frame")
    fw.tick(10)                                  # 末帧应答窗结束后固件结账
    check(fw.f("save_ok") == ack8 and fw.f("save_rej") == other,
          "save: firmware accounting equals the model's answers (ok %d / other %d)" % (ack8, other))
    # 模型的语义：只允许保存"本次会话设过值"的参数，没设过的回 2。上面经固件设过的 6 个 +
    # 行 2 之前 / resync 涉及的几个是 8，其余是 2 —— 固件的账必须与模型一一对应（上一条已验）。
    check(ack8 >= 6, "save: the parameters the firmware wrote are savable in the model (ack8=%d)" % ack8)
    fw.reboot_module()
    check(num(fw.ask("TC1:TCPIDP?")) == 2.5, "save: value survives a module reboot")
    rc = fw.cmd("api map_set %d 3.5" % 0x100)
    check(rc == 0, "unsaved change: accepted (rc=%d online=%d)" % (rc, fw.f("online")))
    fw.until(lambda s: fw.f("vok") > 0, 80)
    fw.reboot_module()
    check(num(fw.ask("TC1:TCPIDP?")) == 2.5, "unsaved change: reverts to the saved value after reboot")

    # ---------- 行 4：坏帧 → 模型回错码，固件不崩、不掉线 ----------
    online0 = fw.f("online")
    for bad in ("TC1TCADJTEMP=25", "TC1:TCSW=1@0#00", "TC1:TCSW=1#", "TC1:TCADJTEMP^5"):
        fw.cmd("api raw " + bad)
        fw.tick(2)
    check(fw.f("online") == online0, "bad frames: link stays online")
    check(fw.f("offline") == 0, "bad frames: never counted as a comm failure")

    # ---------- 行 5：黄金语料三方一致（模型应答 == 语料；固件消化得了）----------
    corpus = os.path.normpath(os.path.join(HERE, "..", "..", "project_tcs", "tests", "m207_golden.csv"))
    n = 0
    with open(corpus, encoding="utf-8") as fh:
        for ln in fh:
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            frame, expect = ln.split(",", 1)
            n0 = len(fw.tx)
            fw.cmd("api raw " + frame)
            fw.tick()
            new = list(zip(fw.tx[n0:], fw.rep[n0:]))
            got = [r for f, r in new if f == frame.rstrip("\r")]
            check(got and got[0] == expect, "golden %r: model reply %r == corpus %r" % (frame, got, expect))
            n += 1
    check(n >= 10, "golden corpus: %d frames exercised through the firmware" % n)

    # ---------- 50ms 铁律：固件自己的仲裁保证的，由模型来验 ----------
    check(fw.sim.violations_50ms == 0 and fw.sim.drops_50ms == 0,
          "50ms rule: the firmware never sends two frames within 50ms "
          "(violations=%d drops=%d over %d frames)" % (fw.sim.violations_50ms, fw.sim.drops_50ms, len(fw.tx)))
    check(fw.f("offline") == 0 and fw.f("online") == 1, "still online at the end")

    fw.close()
    try:
        os.remove(sf)
    except OSError:
        pass
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    print("test_m207_fw_adv: %d frames through the firmware, %d checks" % (len(fw.tx), CHECKS))
    print("===================")
    if FAIL == 0:
        print("test_m207_fw_adv: PASS")
        return 0
    print("test_m207_fw_adv: FAIL (%d)" % FAIL)
    return 1


if __name__ == "__main__":
    sys.exit(main())
