#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""src_byte_check.py — 源码字节门禁（防"转义被写成真字符"这类静默损坏）

背景：2026-11-16 一天内出现 6 次同类事故——`\n`/`\r`/`\0`/`'>'` 在编辑时被写成
真实控制字符或写反，编译器/评审各自只抓到一部分（最严重一次是 comm 超时比较
写反倒置，全部门禁全绿）。本门禁只做一件事：源码里出现这些字节就红。

规则（仅 .c/.h/.py/.sh/.js，排除 .git/build/third_party/managed_components）：
  1) NUL 字节 —— 必是损坏
  2) 孤立 CR（后面不是 LF）—— 字符串里的真回车，必是损坏

用法：python project_tcs/tools/src_byte_check.py   （任意 cwd；rc=1 即失败）
"""
import sys
from pathlib import Path

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = Path(__file__).resolve().parents[2]
SKIP_DIRS = {".git", "build", "third_party", "managed_components", "node_modules"}
EXTS = {".c", ".h", ".py", ".sh", ".js"}


def main() -> int:
    bad = []
    for p in ROOT.rglob("*"):
        if p.is_dir() or p.suffix not in EXTS:
            continue
        if any(part in SKIP_DIRS for part in p.parts):
            continue
        b = p.read_bytes()
        nul = b.count(b"\x00")
        lone = b.replace(b"\r\n", b"").count(b"\r")
        if nul or lone:
            bad.append((p.relative_to(ROOT).as_posix(), nul, lone))
    if bad:
        print("src_bytes: FAIL")
        for f, n, c in bad:
            print(f"  - {f}: NUL={n} 孤立CR={c}")
        print("  修复：用编辑器/Edit 工具改，别经 shell 转义；确认后重跑")
        return 1
    print("src_bytes: PASS（无 NUL / 无孤立 CR）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
