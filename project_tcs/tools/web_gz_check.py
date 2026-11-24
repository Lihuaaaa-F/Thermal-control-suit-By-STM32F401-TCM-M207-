#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""web_gz_check.py — 网页静态资源与 .gz 同步门禁（R1 M14-F-05）

固件把 `web_files/*.gz` 内嵌进 app，改了源文件忘记重压会让浏览器拿到旧版。
本脚本逐个解压比对；不一致/缺文件 → 退出码 1，run_all 阻断。

用法：python project_tcs/tools/web_gz_check.py   （任意 cwd）
"""
import gzip
import sys
from pathlib import Path

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

ROOT = Path(__file__).resolve().parents[2]          # repo/
WEB = ROOT / "esp32" / "components" / "tcs_web" / "web_files"
FILES = ["app.js", "index.html", "style.css", "chart.umd.min.js"]


def main() -> int:
    bad = []
    for name in FILES:
        src, gz = WEB / name, WEB / (name + ".gz")
        if not src.exists():
            bad.append(f"{name}: 源文件缺失")
            continue
        if not gz.exists():
            bad.append(f"{name}.gz: 缺失（改完源文件必须重压）")
            continue
        try:
            # 比对前把 CRLF 归一成 LF：.gz 里的内容按 LF 提交，而 Windows 上 autocrlf 会把检出的
            # 源文件转成 CRLF（2026-11-24 全新检出实测 4/4 假红）。行尾差异对浏览器没有意义。
            # 仓库根 .gitattributes 已把这四个文件钉成 LF，这里再兜一层。
            def norm(b: bytes) -> bytes:
                return b.replace(b"\r\n", b"\n")
            if norm(gzip.decompress(gz.read_bytes())) != norm(src.read_bytes()):
                bad.append(f"{name}.gz: 与源文件不一致（陈旧）")
        except OSError as e:
            bad.append(f"{name}.gz: 解压失败 {e}")
    if bad:
        print("web_gz: FAIL")
        for b in bad:
            print("  -", b)
        print("  修复：gzip -9 -n -c <src> > <src>.gz")
        return 1
    print(f"web_gz: PASS ({len(FILES)} 个资源与 .gz 一致)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
