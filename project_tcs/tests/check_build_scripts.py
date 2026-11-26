#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_build_scripts.py — 构建脚本回归门禁（S-G x3；起因 2026-11-26 台架档增量构建失败）

那天的事实：305856d 上 H743 四档、ESP32 三档、主机全量都过，**只有台架档**（build-bench）
增量构建失败——模式页新增了 tui_p4.c，而 `App/lvgl_ui/*.c` 的 GLOB 没带 CONFIGURE_DEPENDS，
旧构建目录不会因为多了一个源文件而重新配置，链接时 tui_p4_* 未定义。
同一类坑还有两个入口：build_bench.sh 曾经"目录不存在才配置"（版本串里的提交号会过期），
以及 proto_sync.py 第 9 项（对外报的协议版本必须取自 TCS_PROTO_VER）只在运行时才验。

三条判据：
  ① project_tcs/CMakeLists.txt 里 `App/lvgl_ui` 的 file(GLOB …) 必须带 CONFIGURE_DEPENDS；
  ② project_tcs/tools/build_bench.sh 必须**无条件**执行 `cmake -S . -B build-bench …`
     （不许再用"目录不存在才配置"的写法：那会让版本串与源文件清单都过期）；
  ③ `python ../tools/proto_sync.py --check` 的输出里必须有
     "对外报的协议版本取自 TCS_PROTO_VER"（= 第 9 项在）。

用法：python project_tcs/tests/check_build_scripts.py [--root <project_tcs>]
"""
import argparse
import io
import re
import subprocess
import sys
from pathlib import Path

DEFAULT_ROOT = Path(__file__).resolve().parents[1]


def check_cmake(root):
    p = root / "CMakeLists.txt"
    if not p.is_file():
        return ["① CMakeLists.txt 不存在：%s" % p]
    txt = io.open(p, encoding="utf-8", errors="replace").read()
    out = []
    # 找出所有覆盖 App/lvgl_ui/*.c 的 file(GLOB …) 语句（可能跨行，用 ; 结尾切）
    stmts = re.findall(r"file\s*\(\s*GLOB[^)]*\)", txt, re.S)
    hit = [s for s in stmts if "App/lvgl_ui" in s.replace("\\", "/") or "lvgl_ui/*.c" in s]
    if not hit:
        out.append("① CMakeLists.txt 里找不到 App/lvgl_ui 的 file(GLOB …)——判据失效，需人看")
        return out
    for s in hit:
        if "CONFIGURE_DEPENDS" not in s:
            out.append("① App/lvgl_ui 的 GLOB 没带 CONFIGURE_DEPENDS（新增 .c 不会触发重新配置）：%s"
                       % " ".join(s.split())[:120])
    return out


def check_build_bench(root):
    p = root / "tools" / "build_bench.sh"
    if not p.is_file():
        return ["② tools/build_bench.sh 不存在：%s" % p]
    lines = io.open(p, encoding="utf-8", errors="replace").read().splitlines()
    out = []
    idx = [i for i, l in enumerate(lines)
           if re.search(r"\bcmake\s+-S\s+\.\s+-B\s+build-bench\b", l)]
    if not idx:
        out.append("② build_bench.sh 里没有 `cmake -S . -B build-bench` 配置行")
        return out
    first = idx[0]
    # 前面若有"目录存在性"判断（-d/-f build-bench），说明又被包进条件里了
    for i in range(0, first):
        if re.search(r"\bif\b.*(?:-d|-f)\b.*build-bench", lines[i]):
            out.append("② 配置行被条件包住了（第 %d 行：%s）——必须每次无条件重新配置"
                       % (i + 1, lines[i].strip()[:90]))
    return out


def check_proto_sync(root):
    script = root.parent / "tools" / "proto_sync.py"
    if not script.is_file():
        return ["③ tools/proto_sync.py 不存在：%s" % script]
    try:
        r = subprocess.run([sys.executable, "-X", "utf8", str(script), "--check"],
                           cwd=str(root), capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=120)
    except OSError as e:
        return ["③ proto_sync.py --check 跑不起来：%s" % e]
    out = (r.stdout or "") + (r.stderr or "")
    bad = []
    if r.returncode != 0:
        bad.append("③ proto_sync.py --check 自己返回了 %d：%s"
                   % (r.returncode, " / ".join(out.strip().splitlines()[:3])))
    if "对外报的协议版本取自 TCS_PROTO_VER" not in out:
        bad.append("③ proto_sync 输出里没有第 9 项结论（对外报的协议版本取自 TCS_PROTO_VER）——"
                   "有人删了那条检查，或改了输出文案")
    return bad


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=str(DEFAULT_ROOT))
    a = ap.parse_args(argv)
    root = Path(a.root)
    findings = []
    findings += check_cmake(root)
    findings += check_build_bench(root)
    findings += check_proto_sync(root)
    for f in findings:
        print("[build_scripts] FAIL " + f)
    print("[build_scripts] %s（3 条判据：GLOB CONFIGURE_DEPENDS / build_bench 无条件配置 / proto_sync 第 9 项）"
          % ("PASS" if not findings else "FAIL"))
    return 0 if not findings else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
