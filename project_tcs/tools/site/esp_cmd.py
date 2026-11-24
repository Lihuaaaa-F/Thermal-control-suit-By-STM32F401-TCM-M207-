#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""esp_cmd.py — 经桥给 ESP32 发一条控制台命令，打印回显（去掉周期性日志行）

用法：python project_tcs/tools/site/esp_cmd.py "status" [超时秒，默认 4]
前提：esp_bridge.py 在后台运行。
"""
import io
import os
import sys
import time
from pathlib import Path

SITE = Path(os.environ.get("TCS_SITE_DIR") or (Path(os.environ.get("TEMP", ".")) / "tcs_site"))
LOG = SITE / "esp_out.log"
CMD = SITE / "esp_cmd.txt"
# 周期性探针行（不参与回显）
NOISE = ("SNAP: [SNAPP]", "TMP117: [TMP117P]", "M207: [M207P]", "M207: verify OK")


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmd = sys.argv[1]
    timeout = float(sys.argv[2]) if len(sys.argv) > 2 else 4.0
    if not LOG.exists():
        print("no log at %s (bridge not running?)" % LOG)
        return 3
    start = os.path.getsize(LOG)
    SITE.mkdir(parents=True, exist_ok=True)
    io.open(CMD, "w", encoding="utf-8").write(cmd + "\n")
    t0 = time.time()
    buf = b""
    marker = ("\n>>> " + cmd + "\n").encode("utf-8")
    while time.time() - t0 < timeout:
        with open(LOG, "rb") as f:
            f.seek(start)
            buf = f.read()
        if marker in buf:
            tail = buf.split(marker, 1)[1]
            # 回显结束标志：下一条以 "tcs " 开头的提示符行，或空行后的提示符
            if "\ntcs " in tail.decode("utf-8", "replace") or "\r\ntcs " in tail.decode("utf-8", "replace"):
                break
        time.sleep(0.15)
    text = buf.decode("utf-8", "replace").replace("\r", "")
    if marker not in buf:
        print("[esp_cmd] no echo within %.1fs: %s" % (timeout, cmd))
        for line in text.splitlines()[-8:]:
            print("  " + line[:160])
        return 1
    body = text.split(">>> " + cmd + "\n", 1)[1]
    lines = body.splitlines()
    while lines and not lines[0].strip():        # 回显前可能有一个空行
        lines = lines[1:]
    # 第一行是芯片回显的命令本身，跳过
    if lines and lines[0].strip() == cmd:
        lines = lines[1:]
    for line in lines:
        s = line.strip()
        if s == "tcs" or s.startswith("tcs "):   # 提示符 = 命令执行完（后面的日志可能接在同一行）
            break
        if not s:
            continue
        if any(k in s for k in NOISE):
            continue
        print(s[:200])
    return 0


if __name__ == "__main__":
    sys.exit(main())
