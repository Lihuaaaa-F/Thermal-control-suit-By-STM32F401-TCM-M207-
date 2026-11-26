#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_link_contract.py — X01-F-05 的源码门禁（S 线 e1，2026-11-26）。

主机用例（tests/test_link_contract.c）只能验证"编进来的那一份 comm.h"；这个脚本把
**两份** comm.h 与协议头放在一起核对，并核对注释（注释进不了二进制）：

  1. protocol/tcs_proto.h 里有 TCS_MAX_TXQ，值 16，注释里的容量模型是"8 条等 ACK 在途 + 8 条余量"；
  2. 两端 comm.h 都写 `#define MAX_TXQ TCS_MAX_TXQ`（从契约取值，不再各写一个字面量）；
  3. 两端 comm.h 都有两条 _Static_assert（相等 + >= COMM_MAX_PENDING + 8）；
  4. 两端 comm.h 都 include 了 tcs_proto.h（在 MAX_TXQ 之前）；
  5. 旧容量模型文本"注册表 8 条目"不再出现。

用法：python -I tests/test_link_contract.py   （在 project_tcs 根目录；只依赖标准库）
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
PROTO = os.path.join(ROOT, "protocol", "tcs_proto.h")
COMM_H = [
    os.path.join(ROOT, "project_tcs", "App", "comm", "comm.h"),
    os.path.join(ROOT, "esp32", "components", "tcs_link", "comm.h"),
]

fails = 0
checks = 0


def check(cond, msg):
    global fails, checks
    checks += 1
    if cond:
        print("  ok   " + msg)
    else:
        print("  FAIL " + msg)
        fails += 1


def read(path):
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def main():
    proto = read(PROTO)
    m = re.search(r"^#define\s+TCS_MAX_TXQ\s+(\d+)u?\s*$", proto, re.M)
    check(m is not None, "protocol/tcs_proto.h defines TCS_MAX_TXQ")
    if m:
        check(int(m.group(1)) == 16, "TCS_MAX_TXQ = %s (expect 16)" % m.group(1))
    # 容量模型的说明文字（16 = 8 + 8）必须在协议头里，且不再是"注册表 8 条目"
    check("COMM_MAX_PENDING" in proto and "8 = 同时等 ACK 的在途帧上限" in proto,
          "protocol header documents the 8 pending + 8 periodic-frames capacity model")
    check("注册表 8 条目" not in proto, "protocol header has no stale 'registry 8 entries' model")

    for path in COMM_H:
        name = os.path.relpath(path, ROOT).replace("\\", "/")
        src = read(path)
        check(re.search(r"^#define\s+MAX_TXQ\s+TCS_MAX_TXQ\s*$", src, re.M) is not None,
              "%s: MAX_TXQ follows TCS_MAX_TXQ (no local literal)" % name)
        check("_Static_assert(MAX_TXQ == TCS_MAX_TXQ" in src,
              "%s: static assert MAX_TXQ == TCS_MAX_TXQ" % name)
        check(re.search(r"_Static_assert\(MAX_TXQ\s*>=\s*COMM_MAX_PENDING\s*\+\s*8", src)
              is not None,
              "%s: static assert MAX_TXQ >= COMM_MAX_PENDING + 8" % name)
        inc = src.find('#include "tcs_proto.h"')
        defpos = src.find("#define MAX_TXQ")
        check(inc >= 0 and 0 <= inc < defpos,
              "%s: includes tcs_proto.h before MAX_TXQ" % name)
        check("注册表 8 条目" not in src,
              "%s: no stale 'registry 8 entries' capacity comment" % name)
        check(re.search(r"^#define\s+MAX_TXQ\s+\d+\s*$", src, re.M) is None,
              "%s: no numeric MAX_TXQ literal" % name)

    print("test_link_contract: PASS=%d FAIL=%d" % (checks - fails, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
