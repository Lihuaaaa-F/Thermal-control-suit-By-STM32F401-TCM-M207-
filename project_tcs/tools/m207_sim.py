#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""m207_sim.py — M207 温控模块串口模拟器（任务书 §11.2）

用途：M207 实物接线前，WP4 全流程（协议→屏幕→调参→报警）先行完成并验收。
行为：
  - 应答 `TCx:TCACTTEMP?` → 返回按正弦缓变的模拟水温 `TCx:TCACTTEMP=<f>\r`
  - 收到 `TCx:TCADJTEMP=<f>` → 回显同值（供 H743 回读校验）+ 打印
  - 收到 `TCx:TCSW=<0/1>`    → 记录并打印
  - 控制台实时打印全部收发；Ctrl+C 退出

用法：python tools/m207_sim.py COM3 [57600]
台架接线：USB-TTL 模块 TXD→H743 PB11(M207_RX)，RXD→PB10(M207_TX)，GND 共地。
"""
import math
import sys
import time

import serial


class M207Sim:
    def __init__(self):
        self.temp = {1: 25.0, 2: 25.0}      # 模拟水温
        self.setpoint = {1: 25.0, 2: 25.0}
        self.tecsw = {1: 0, 2: 0}
        self.t0 = time.time()

    def evolve(self):
        """水温向设定点一阶趋近 + 1°C 幅度正弦缓变（肉眼可见的屏幕变化）"""
        t = time.time() - self.t0
        for ch in (1, 2):
            target = self.setpoint[ch] + 1.0 * math.sin(t * 2 * math.pi / 60.0)
            self.temp[ch] += (target - self.temp[ch]) * 0.05
            if self.tecsw[ch] == 0:         # 停机时缓慢回到 25
                self.temp[ch] += (25.0 - self.temp[ch]) * 0.01

    def handle(self, line: str) -> list:
        out = []
        line = line.strip().upper()
        if ":" not in line:
            return out
        mod, _, rest = line.partition(":")
        if not mod.startswith("TC") or mod[2:] not in ("1", "2"):
            print(f"[sim] unknown module: {line}")
            return out
        ch = int(mod[2])
        param, sep, val = rest.partition("=")
        if sep and param == "TCACTTEMP" and not val:      # 查询行不应带 =
            pass
        if param == "TCACTTEMP":
            if sep:                                        # 回显（本 sim 不需要）
                out.append(f"TC{ch}:TCACTTEMP={val}\r")
            else:
                self.evolve()
                out.append(f"TC{ch}:TCACTTEMP={self.temp[ch]:.1f}\r")
                print(f"[sim] poll TC{ch} -> {self.temp[ch]:.1f}")
        elif param == "TCADJTEMP":
            try:
                v = float(val)
            except ValueError:
                return out
            self.setpoint[ch] = v
            print(f"[sim] SET TC{ch} TCADJTEMP={v}")
            out.append(f"TC{ch}:TCADJTEMP={v:.1f}\r")      # 回读校验回显
        elif param == "TCSW":
            print(f"[sim] SET TC{ch} TCSW={val}")
            self.tecsw[ch] = 1 if val.strip() == "1" else 0
        else:
            print(f"[sim] unknown param: {line}")
        return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    port = sys.argv[1]
    baud = int(sys.argv[2]) if len(sys.argv) > 2 else 57600
    sim = M207Sim()
    ser = serial.Serial(port, baud, timeout=0.1)
    print(f"[sim] M207 simulator on {port} @ {baud}. Ctrl+C 退出。")
    buf = b""
    try:
        while True:
            data = ser.read(256)
            if data:
                buf += data
                while b"\r" in buf:
                    raw, buf = buf.split(b"\r", 1)
                    line = raw.decode("ascii", "ignore").strip()
                    if not line:
                        continue
                    for rep in sim.handle(line):
                        ser.write(rep.encode("ascii"))
            time.sleep(0.01)
    except KeyboardInterrupt:
        print("\n[sim] bye")
    finally:
        ser.close()


if __name__ == "__main__":
    main()
