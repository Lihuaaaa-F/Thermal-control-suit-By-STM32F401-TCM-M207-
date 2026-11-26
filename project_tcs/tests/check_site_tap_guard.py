#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_site_tap_guard.py — 现场工具点击禁区的静态门禁（S-G x3；起因 D7 事故）

带的判据（都对 project_tcs/tools/site/*.py 做文本扫描）：
  R1 只有 site_common.py 可以出现 g_tap_inject（唯一的注入实现）。例外是两个"审计"工具，
     它们只是把这个名字当成"交付档里不许存在的符号"来核对（verify_prod_clean.py /
     test_mb_fwcheck.py），不写探针——例外必须列在下方并写明理由。
  R2 执行器矩形表只能有一份：除 site_common.py 外出现 ACTUATOR_RECTS 即红
     （D7 事故的形态就是"某个工具自己带了一份禁区判断"，各写各的）。
  R3 会注入点击的四个工具必须调用 tap_guarded——只删掉禁区调用、留下别的路径，会被这条抓住。

用法：python project_tcs/tests/check_site_tap_guard.py [--dir <site 目录>]
  --dir 是给"判别性验证"用的：指向改坏/旧的目录跑一遍，门禁必须变红。
"""
import argparse
import io
import re
import sys
from pathlib import Path

DEFAULT_DIR = Path(__file__).resolve().parents[1] / "tools" / "site"

# R1 判据只看"取符号地址 / 写探针"这两种代码上下文——注释与文档里提到钩子名不算
# （例如 tap.py 的说明文字一定会写它；把说明文字也判红只会逼人去删有用的注释）。
R1_PAT = re.compile(r'(?:nm_addr\s*\(\s*["\']g_tap_inject["\'])|(?:pr_write\s*\([^\n]*g_tap_inject)')
# R1 例外（只读审计，不注入）
R1_ALLOW = {
    "site_common.py": "唯一实现：写 g_tap_inject 的代码在这里",
    "verify_prod_clean.py": "把名字当\"交付档禁止出现的符号\"来核对，只读 ELF 符号表",
    "test_mb_fwcheck.py": "同一件事的自测：交付档里找到 g_tap_inject 就是 FAIL",
}
# R3：已知会注入点击的工具，必须经由 tap_guarded
TAP_TOOLS = ("estop_matrix.py", "tune_refresh_check.py", "tap.py", "dlg_timing.py")


def scan(d):
    findings = []
    for p in sorted(Path(d).glob("*.py")):
        src = io.open(p, encoding="utf-8", errors="replace").read()
        if R1_PAT.search(src) and p.name not in R1_ALLOW:
            findings.append("R1 %s 直接取 g_tap_inject / 写探针（唯一的注入实现应在 site_common.tap_guarded）"
                            % p.name)
        if p.name != "site_common.py" and re.search(r"\bACTUATOR_RECTS\b", src):
            findings.append("R2 %s 自带执行器矩形表（禁区只能有一份实现，在 site_common.py）" % p.name)
        if p.name in TAP_TOOLS and "tap_guarded" not in src:
            findings.append("R3 %s 会注入点击，但没有调用 site_common.tap_guarded（可能绕过了禁区）"
                            % p.name)
    return findings


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=str(DEFAULT_DIR))
    a = ap.parse_args(argv)
    d = Path(a.dir)
    if not d.is_dir():
        print("[tap_guard_gate] FAIL 目录不存在：%s" % d)
        return 1
    findings = scan(d)
    for f in findings:
        print("[tap_guard_gate] FAIL " + f)
    n = len(list(d.glob("*.py")))
    print("[tap_guard_gate] %s（%d 个 site 工具，%d 处违规）"
          % ("PASS" if not findings else "FAIL", n, len(findings)))
    return 0 if not findings else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
