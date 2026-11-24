#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""esp_bridge.py — ESP32 控制台常驻桥（现场取证）

ESP32-S3 的 USB-Serial-JTAG 每次被打开都会因 DTR/RTS 复位芯片，所以"发一条命令开一次串口"
等于"每条命令重启一次"。本桥只开一次口并常驻：
  - 串口输出实时追加到 <TCS_SITE_DIR>\\esp_out.log
  - <TCS_SITE_DIR>\\esp_cmd.txt 出现内容 → 取走、发给 ESP32，并在日志里插入 ">>> 命令"
用法：
    python project_tcs/tools/site/esp_bridge.py            # 后台常驻
    python project_tcs/tools/site/esp_cmd.py status        # 发一条命令并打印回显
注意：
  - 桥占着串口。**烧 ESP32 之前必须先停掉桥**，否则 esptool 打不开端口。
  - 打开串口本身会让 ESP32 复位一次（硬件行为，不是桥的 bug）。
环境变量：ESP_PORT（默认 COM3）、ESP_BAUD（默认 115200）、TCS_SITE_DIR
"""
import io
import os
import sys
import time
from pathlib import Path

import serial

SITE = Path(os.environ.get("TCS_SITE_DIR") or (Path(os.environ.get("TEMP", ".")) / "tcs_site"))
SITE.mkdir(parents=True, exist_ok=True)
LOG = SITE / "esp_out.log"
CMD = SITE / "esp_cmd.txt"
PORT = os.environ.get("ESP_PORT", "COM3")
BAUD = int(os.environ.get("ESP_BAUD", "115200"))


def open_port():
    return serial.Serial(PORT, BAUD, timeout=0.2, dsrdtr=False, rtscts=False)


def main():
    for s in (sys.stdout, sys.stderr):
        try:
            s.reconfigure(encoding="utf-8", errors="replace")
        except Exception:
            pass
    out = io.open(LOG, "a", encoding="utf-8", newline="\n")

    def log(text):
        out.write(text)
        out.flush()

    try:
        ser = open_port()
    except Exception as e:
        log("[bridge] open %s failed: %s\n" % (PORT, e))
        return 1
    log("\n[bridge] opened %s @ %s (ESP32 resets on open)\n" % (PORT, time.strftime("%H:%M:%S")))
    while True:
        try:
            chunk = ser.read(4096)
        except serial.SerialException as e:
            # 端口消失 = ESP32 复位/被拔/USB 重新枚举：把墙钟时间记下来，再等它回来
            log("[bridge] %s read error: %s\n" % (time.strftime("%H:%M:%S"), str(e)[:120]))
            try:
                ser.close()
            except Exception:
                pass
            time.sleep(1)
            try:
                ser = open_port()
                log("[bridge] %s reopened\n" % time.strftime("%H:%M:%S"))
            except Exception:
                time.sleep(1)
            continue
        if chunk:
            log(chunk.decode("utf-8", "replace"))
        if CMD.exists():
            try:
                cmd = io.open(CMD, encoding="utf-8").read().strip("\r\n")
            except Exception:
                cmd = ""
            try:
                os.remove(CMD)
            except Exception:
                pass
            if cmd:
                log("\n>>> %s\n" % cmd)
                try:
                    ser.write((cmd + "\n").encode("utf-8"))
                    ser.flush()
                except Exception as e:
                    log("[bridge] write error: %s\n" % str(e)[:120])


if __name__ == "__main__":
    sys.exit(main())
