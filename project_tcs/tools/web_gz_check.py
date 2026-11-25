#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""web_gz_check.py — 网页静态资源与 .gz 同步门禁（R1 M14-F-05）

固件把 `web_files/*.gz` 内嵌进 app，改了源文件忘记重压会让浏览器拿到旧版。
本脚本逐个解压比对；不一致/缺文件 → 退出码 1，run_all 阻断。

用法：python project_tcs/tools/web_gz_check.py            # 只校验
      python project_tcs/tools/web_gz_check.py --fix      # 把不一致/缺失的 .gz 重压后再校验
（任意 cwd；压 缩固定 gzip-9、mtime=0，产物只取决于内容，便于可复现构建比对。）
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
# U 线（2026-11-24）：Chart.js 换成自绘 chart.js（首屏 gzip 从约 80KB 降到约 21KB）
FILES = ["index.html", "style.css", "app.js", "chart.js"]


def main() -> int:
    fix = "--fix" in sys.argv
    bad = []
    for name in FILES:
        src, gz = WEB / name, WEB / (name + ".gz")
        if not src.exists():
            bad.append(f"{name}: 源文件缺失")
            continue
        want = gzip.compress(src.read_bytes(), 9, mtime=0)
        if not gz.exists():
            if fix:
                gz.write_bytes(want)
                continue
            bad.append(f"{name}.gz: 缺失（改完源文件必须重压）")
            continue
        try:
            # 比对前把 CRLF 归一成 LF：.gz 里的内容按 LF 提交。仓库根 .gitattributes 已让 web_files
            # 检出时不做行尾转换，这里再兜一层（2026-11-24：autocrlf 下全新检出实测 4/4 假红）。
            def norm(b: bytes) -> bytes:
                return b.replace(b"\r\n", b"\n")
            same = norm(gzip.decompress(gz.read_bytes())) == norm(src.read_bytes())
        except OSError as e:
            same = False
            bad.append(f"{name}.gz: 解压失败 {e}")
            continue
        if not same:
            if fix:
                gz.write_bytes(want)
                continue
            bad.append(f"{name}.gz: 与源文件不一致（陈旧）")
    if bad:
        print("web_gz: FAIL")
        for b in bad:
            print("  -", b)
        print("  修复：python project_tcs/tools/web_gz_check.py --fix")
        return 1
    print(f"web_gz: PASS ({len(FILES)} 个资源与 .gz 一致)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
