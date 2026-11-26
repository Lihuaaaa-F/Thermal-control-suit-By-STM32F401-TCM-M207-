#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dlg_timing.py — 量"注入点击到某个像素变色"的时间，每次探测都标起止时刻

用法：python project_tcs/tools/site/dlg_timing.py <点击x> <点击y> <探测x> <探测y> [次数=8]
输出每次读数的起止时刻（主机时钟，自注入写完算起）与像素值（RGB565）。
教训（2026-11-24）：一次轮询读 3 个点、把最后一个点的时刻套给第一个点，误报过"弹窗 2 秒才画完"。
探测一次约 0.35 秒，所以分辨率就是 0.35 秒；要更准请看固件里的计时埋点。
帧缓冲地址固定 0xC0000000（LTDC CFBAR），1024x600 RGB565。
"""
import sys
import time

import site_common as sc

FB = 0xC0000000


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    tx, ty, px, py = (int(v) for v in sys.argv[1:5])
    n = int(sys.argv[5]) if len(sys.argv) > 5 else 8
    mb = sc.load_mb()
    addr = FB + (py * 1024 + px) * 2

    def pix():
        return mb.pr_read("b16", addr, 1)[0]

    before = pix()
    # S-G x3（2026-11-26）：注入统一走 site_common.tap_guarded（模式页执行器禁区在这里同样生效）。
    sc.tap_guarded(mb, tx, ty, 1)
    t0 = time.time()
    prev = None
    for i in range(n):
        ts = time.time() - t0
        v = pix()
        te = time.time() - t0
        if v != prev:
            print("read %d: %5.2f..%5.2fs  (%d,%d)=%04X%s" % (i, ts, te, px, py, v, "  <- changed" if v != before else ""))
            prev = v
        time.sleep(0.05)
    print("before=%04X" % before)
    return 0


if __name__ == "__main__":
    sys.exit(main())
