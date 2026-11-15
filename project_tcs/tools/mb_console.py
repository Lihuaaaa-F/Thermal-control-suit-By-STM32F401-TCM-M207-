#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mb_console.py — 主机邮箱控制台 + RTT 环直读（WP-D 前半，无 COM/USB-TTL 交互通道）

原理：
  1. console.c 邮箱（符号 s_mb，布局 v2）：probe-rs write RAM 注入命令；
  2. printf 输出镜像进 RTT 上行环（_SEGGER_RTT.aUp[0]），本脚本按 WrOff 增量直读，
     全程只用 probe-rs read/write，不占探针独占会话。

邮箱布局 v2（M07-F-07；与 console.c:mb_mailbox_t 同源，改一处必须同步另一处）：
  s_mb+0   u32 magic  'CMST'  ← 最后写
  s_mb+4   u32 len    1..96
  s_mb+8   u32 crc16  CRC-16/CCITT-FALSE(init 0xFFFF)，覆盖 cmd[0..len)
  s_mb+12  char cmd[96]

用法：python tools/mb_console.py "ver" ["stat" ...]   （在 project_tcs 根目录）
      python tools/mb_console.py --help               （打印布局与向量）
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
MB_LAYOUT_VER = 2
MB_OFF_MAGIC, MB_OFF_LEN, MB_OFF_CRC, MB_OFF_CMD = 0, 4, 8, 12
MB_CMD_MAX = 96


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE：poly 0x1021 / init 0xFFFF / 不反射 / 末不异或。
    与 console.c:mb_crc16 逐位等价；"123456789" → 0x29B1。"""
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


assert crc16_ccitt(b"123456789") == 0x29B1, "CRC16 vector mismatch (C/Python 不同源)"


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
    return {"pbuf": w[1], "size": w[2], "wroff": w[3], "rd_off": w[4],
            "cb": cb}


def rtt_set_rd(new: int):
    """回写 RdOff（RTT 协议要求主机读取后推进；否则固件写满即永久静默）"""
    cb = nm_addr("_SEGGER_RTT")
    pr_write(cb + 40, (new & 0xFFFFFFFF).to_bytes(4, 'little'))


def rtt_fetch(old: int, new: int) -> bytes:
    """取 (old,new] 区间字节（环形，处理回绕），读后推进 RdOff"""
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
    rtt_set_rd(new)          # 关键：推进 RdOff，否则缓冲满后固件永久静默
    return bytes(out)


def send_cmd(cmdline: str, wait=3.0) -> bytes:
    mb_a, seq_a = nm_addr("s_mb"), nm_addr("s_mb_seq")
    # 先清积压并推进 RdOff（避免历史日志混入、或缓冲假满导致输出丢失）
    b0 = rtt_buf_info()
    if b0["wroff"] != b0["rd_off"]:
        try:
            rtt_fetch(b0["rd_off"], b0["wroff"])
        except Exception:
            pass
    seq0 = pr_read("b32", seq_a, 1)[0]
    wr0 = rtt_buf_info()["wroff"]
    payload = cmdline.encode('utf-8')[:MB_CMD_MAX]          # 不强制带 '\0'：长度由 len 表达
    # 顺序：cmd → len → crc → magic（magic 最后写，固件见 magic 才消费整条记录）
    pr_write(mb_a + MB_OFF_CMD, payload)
    pr_write(mb_a + MB_OFF_LEN, len(payload).to_bytes(4, 'little'))
    pr_write(mb_a + MB_OFF_CRC, crc16_ccitt(payload).to_bytes(4, 'little'))
    pr_write(mb_a + MB_OFF_MAGIC, MB_MAGIC_RUN.to_bytes(4, 'little'))
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
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        print(f"邮箱布局版本 v{MB_LAYOUT_VER}；CRC16 '123456789' = 0x{crc16_ccitt(b'123456789'):04X}")
        return
    for c in sys.argv[1:]:
        out = send_cmd(c)
        sys.stdout.write(out.decode('utf-8', errors='replace'))
        if out and not out.endswith(b"\n"):
            print()


if __name__ == "__main__":
    main()
