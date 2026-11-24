#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""rtt_live.py — 连续直读 H743 的 RTT 上行环并落盘（边读边推进 RdOff）

为什么需要：固件写 RTT 而主机不读时，环写满之后日志会被丢；断断续续读只能看到零星几行。
用法：python project_tcs/tools/site/rtt_live.py <秒数> <输出文件>
"""
import sys
import time

import site_common as sc


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    secs = float(sys.argv[1])
    mb = sc.load_mb()
    out = open(sys.argv[2], "wb")
    t0 = time.time()
    rd = None
    nbytes = polls = 0
    while time.time() - t0 < secs:
        b = mb.rtt_buf_info()
        if rd is None:
            rd = b["rd_off"]
        if b["wroff"] != rd:
            data = mb.rtt_fetch(rd, b["wroff"])
            out.write(data)
            out.flush()
            nbytes += len(data)
            rd = b["wroff"]
        polls += 1
        time.sleep(0.1)
    out.close()
    print("rtt_live: %.0fs, %d bytes, %d polls" % (time.time() - t0, nbytes, polls))
    return 0


if __name__ == "__main__":
    sys.exit(main())
