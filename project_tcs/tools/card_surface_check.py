#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""card_surface_check.py — 按 docs/ref/研学卡_行数基线.txt 的口径（grep -rnE <模式> <目录> --include=*.c | wc -l，cwd=project_tcs）
重新数一遍 7 张卡的调用面，和基线比。只读。用法：在 project_tcs/ 下运行 python <本文件> [--write]
--write：把对不上的基线数字改成实测值，并在该行上方留一行带日期的说明（需要用 --reason 给出理由）。"""
import datetime
import io
import os
import re
import sys

ROOT = os.getcwd()
BASE = os.path.join(os.path.dirname(ROOT), "docs", "ref", "研学卡_行数基线.txt")
write = "--write" in sys.argv
reason = sys.argv[sys.argv.index("--reason") + 1] if "--reason" in sys.argv else ""


def count(dirs, pattern):
    n = 0
    rx = re.compile(pattern)
    for d in dirs.split():
        for dp, _dn, fns in os.walk(os.path.join(ROOT, d)):
            for fn in fns:
                if fn.endswith(".c"):
                    for line in io.open(os.path.join(dp, fn), encoding="utf-8", errors="replace"):
                        if rx.search(line):
                            n += 1
    return n


lines = io.open(BASE, encoding="utf-8").read().split("\n")
out, bad = [], 0
for line in lines:
    if line.startswith("#") or not line.strip():
        out.append(line)
        continue
    body = line.split("#", 1)[0].rstrip()
    name, dirs, pattern, want = body.split(";;")
    got = count(dirs, pattern)
    if got == int(want):
        print("%-24s got=%d want=%d  OK" % (name, got, int(want)))
        out.append(line)
    else:
        bad += 1
        print("%-24s got=%d want=%d  MISMATCH" % (name, got, int(want)))
        if write:
            assert reason, "--write 需要 --reason"
            out.append("# %s 重锚：%d→%d（%s）" % (datetime.date.today().isoformat(), int(want), got, reason))
            out.append(line.replace(";;" + want, ";;" + str(got), 1) if (";;" + want) in line
                       else re.sub(r";;\s*" + re.escape(want) + r"(\s|$)", ";;%d\\1" % got, line, count=1))
        else:
            out.append(line)
print("card_surface:", "PASS (7 cards)" if bad == 0 else "FAIL (%d cards)" % bad)
if write and bad:
    io.open(BASE, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print("baseline written")
sys.exit(0 if (bad == 0 or write) else 1)
