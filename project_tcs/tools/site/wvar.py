#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""wvar.py — 用探针写 32 位变量（小端），可以按符号名也可以按地址

用法：python project_tcs/tools/site/wvar.py <符号名|0x地址> <值> [<符号名|0x地址> <值> ...]
例：wvar.py g_lvgl_freeze 1        # 冻结 LVGL 渲染（诊断钩子）
    wvar.py 0x200112b8 0           # 把某个地址清零
值支持十进制 / 0x 十六进制。
"""
import struct
import sys

import site_common as sc


def main():
    args = sys.argv[1:]
    if not args or len(args) % 2:
        print(__doc__)
        return 2
    mb = sc.load_mb()
    for i in range(0, len(args), 2):
        name, val = args[i], int(args[i + 1], 0)
        addr = mb.nm_addr(name) if not name.lower().startswith("0x") else int(name, 0)
        mb.pr_write(addr, struct.pack("<I", val & 0xFFFFFFFF))
        back = mb.pr_read("b32", addr, 1)[0]
        ok = back == (val & 0xFFFFFFFF)
        print("%s @0x%08X = 0x%08X%s" % (name, addr, back, "" if ok else "  (read-back mismatch, wanted 0x%08X)" % (val & 0xFFFFFFFF)))
        if not ok:
            return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
