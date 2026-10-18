#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mb_console.py — 主机邮箱控制台 + RTT 环直读（WP-D 前半，无 COM/USB-TTL 交互通道）

原理：
  1. console.c 邮箱（s_mb_line/s_mb_magic/s_mb_seq）：probe-rs write RAM 注入命令；
  2. printf 输出镜像进 RTT 上行环（_SEGGER_RTT.aUp[0]），本脚本按 WrOff 增量直读，
     全程只用 probe-rs read/write，不占探针独占会话。

用法：python tools/mb_console.py "ver" ["stat" ...]   （在 project_tcs 根目录）
"""
import os
import re
import subprocess
import sys
import time
from pathlib import Path

CHIP = "STM32H743II"
PROBE = "0d28:0204"
ELF = Path(os.environ.get("TCS_ELF", "build/tcs_main.elf"))
MB_MAGIC_RUN = 0x54534D43


def nm_addr(symbol: str) -> int:
    out = subprocess.run(["arm-none-eabi-nm", str(ELF)], capture_output=True,
                         text=True, check=True).stdout
    m = re.search(r'^([0-9a-fA-F]{8}) \w+ ' + re.escape(symbol) + r'$', out, re.M)
    if not m:
        raise SystemExit(f"symbol not found: {symbol}")
    return int(m.group(1), 16)


def pr_read(width: str, addr: int, n: int) -> list:
    out = subprocess.run(["probe-rs", "read", "--chip", CHIP, "--probe", PROBE,
                          width, hex(addr), str(n)], capture_output=True,
                         text=True, check=True, timeout=120).stdout
    w = []
    for line in out.splitlines():
        if ':' in line:
            w.extend(int(t, 16) for t in line.split(':', 1)[1].split())
    return w


def pr_write(addr: int, data: bytes):
    args = ["probe-rs", "write", "--chip", CHIP, "--probe", PROBE,
            "b8", hex(addr)] + [str(b) for b in data]
    subprocess.run(args, check=True, capture_output=True, timeout=120)


def rtt_buf_info():
    cb = nm_addr("_SEGGER_RTT")
    w = pr_read("b32", cb + 24, 5)      # sName pBuffer Size WrOff RdOff
    return {"pbuf": w[1], "size": w[2], "wroff": w[3]}


def rtt_fetch(old: int, new: int) -> bytes:
    """取 (old,new] 区间字节（环形，处理回绕）"""
    if old == new:
        return b""
    b = rtt_buf_info()
    n = (new - old) % b["size"]
    out = bytearray()
    pos = old
    while n > 0:
        chunk = min(n, b["size"] - pos, 512)
        out.extend(pr_read("b8", b["pbuf"] + pos, chunk))
        pos = (pos + chunk) % b["size"]
        n -= chunk
    return bytes(out)


def send_cmd(cmdline: str, wait=3.0) -> bytes:
    line_a, magic_a, seq_a = (nm_addr("s_mb_line"), nm_addr("s_mb_magic"),
                              nm_addr("s_mb_seq"))
    seq0 = pr_read("b32", seq_a, 1)[0]
    wr0 = rtt_buf_info()["wroff"]
    payload = cmdline.encode('utf-8')[:94] + b'\0'
    pr_write(line_a, payload)
    pr_write(magic_a, MB_MAGIC_RUN.to_bytes(4, 'little'))
    t0 = time.time()
    while time.time() - t0 < wait:
        time.sleep(0.15)
        if pr_read("b32", seq_a, 1)[0] != seq0:
            time.sleep(0.3)                     # 让输出写完
            wr1 = rtt_buf_info()["wroff"]
            return rtt_fetch(wr0, wr1)
    print(f"[mb] TIMEOUT: '{cmdline}' not consumed", file=sys.stderr)
    return b""


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    for c in sys.argv[1:]:
        out = send_cmd(c)
        sys.stdout.write(out.decode('utf-8', errors='replace'))
        if out and not out.endswith(b"\n"):
            print()


if __name__ == "__main__":
    main()
