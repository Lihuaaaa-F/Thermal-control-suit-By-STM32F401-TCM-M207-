#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""hist_read.py — 读 H743 曲线历史环（control.c 的 s_hist_*），按时间顺序打印

每 30 秒采一个点、共 60 点 = 30 分钟。用来在不截屏的情况下核对"曲线页有没有数据"。
用法：python project_tcs/tools/site/hist_read.py
"""
import struct
import sys

import site_common as sc


def main():
    mb = sc.load_mb()
    last = mb.pr_read("b32", mb.nm_addr("s_hist_last"), 1)[0]
    n = mb.pr_read("b32", mb.nm_addr("s_hist_n"), 1)[0]
    head = mb.pr_read("b32", mb.nm_addr("s_hist_head"), 1)[0]
    tc1 = mb.pr_read("b32", mb.nm_addr("s_hist_tc1"), 60)
    tc2 = mb.pr_read("b32", mb.nm_addr("s_hist_tc2"), 60)

    def f(w):
        return struct.unpack("<f", struct.pack("<I", w))[0]

    a1 = [f(tc1[(head - n + i) % 60]) for i in range(n)]
    a2 = [f(tc2[(head - n + i) % 60]) for i in range(n)]
    print("s_hist_last=%dms n=%d head=%d" % (last, n, head))
    if n:
        print("tc1 旧->新:", " ".join("%.1f" % v for v in a1))
        print("tc2 旧->新:", " ".join("%.1f" % v for v in a2))
        print("tc1 零值点=%d 最小=%.2f 最大=%.2f；tc2 最小=%.2f 最大=%.2f" % (
            sum(1 for v in a1 if v == 0.0), min(a1), max(a1), min(a2), max(a2)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
