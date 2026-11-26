#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""assert_count.py — 断言数 / 用例数基线门禁（S-G x3 / 审查条目 X06-F-06）

为什么要有这道门：2026-11-26 的审计发现"断言强度"这件事没有任何门禁看着——
把一条断言删掉、把 for 循环从 10 次改成 1 次、把语料抽掉一半，run_all 仍然全绿。
本脚本逐个运行主机单测可执行文件，从它**自己打印的计数**里取"执行了多少条断言/多少例样本"，
与入库基线比：低于基线即红。

口径：
  asserts  = 测试自己打印的 PASS=<n>（每条 CHECK 通过一次算一条，含循环里重复执行的）
  cases    = 语料/样本驱动的测试自己打印的样本数（corpus=<n> / fed=<n>）
基线文件 tests/baseline/assert_counts.tsv；提高基线必须在提交说明里写理由（这是本门禁的
唯一维护约定——否则"加断言"和"删断言"看起来一样）。

局限（写在这里，免得被当成全能门禁）：
  - run_all.sh 里用 heredoc 现场生成的 test_tmp117.c 不在此列（它没有自己的文件，不计入"测试可执行行"）。
  - ESP32 侧套件（esp32/tests/run_m207_tests.sh）由它自己的入口管，本脚本不重复跑。
"""
import argparse
import io
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
BUILD = PROJECT / "build"
BASELINE = HERE / "baseline" / "assert_counts.tsv"

PASS_N = r"PASS=(\d+)"
CORPUS_N = r"corpus=(\d+)"
FED_N = r"fed=(\d+)"

# (label, kind, 可执行文件名（不含 build/ 目录）, 取数正则)
REGISTRY = [
    ("m207",        "asserts", "tests_m207.exe",          PASS_N),
    ("m207golden",  "cases",   "tests_m207golden.exe",    CORPUS_N),
    ("registry",    "asserts", "tests_registry.exe",      PASS_N),
    ("control",     "asserts", "tests_control.exe",       PASS_N),
    ("post",        "asserts", "tests_post.exe",          PASS_N),
    ("site_logic",  "asserts", "tests_site_logic.exe",    PASS_N),
    ("proto_comm",  "asserts", "tests_proto_comm.exe",    PASS_N),
    ("fuzz",        "cases",   "tests_fuzz.exe",          FED_N),
    ("touchsession", "asserts", "tests_touchsession.exe", PASS_N),
    ("qr",          "asserts", "tests_qr.exe",            PASS_N),
    ("uimodel",     "asserts", "tests_uimodel.exe",       PASS_N),
    ("linkinfo",    "asserts", "tests_linkinfo.exe",      PASS_N),
    ("drvpure",     "asserts", "tests_drvpure.exe",       PASS_N),
    ("linkdir",     "asserts", "tests_linkdir.exe",       PASS_N),
    ("link_send",   "asserts", "tests_link_send.exe",     PASS_N),
    ("m207txq",     "asserts", "tests_m207txq.exe",       PASS_N),
    ("auditring",   "asserts", "tests_auditring.exe",     PASS_N),
    ("perfhist",    "asserts", "tests_perfhist.exe",      PASS_N),
    ("proto_m1",    "asserts", "tests_proto_m1.exe",      PASS_N),
    ("ui_known",    "asserts", "tests_ui_known.exe",      PASS_N),
    ("ui_svc",      "asserts", "tests_ui_svc.exe",        PASS_N),
    ("ui_mismatch", "asserts", "tests_ui_mismatch.exe",   PASS_N),
    ("prodlink",    "asserts", "tests_prodlink.exe",      PASS_N),
    ("linkfollow_prod", "asserts", "tests_linkfollow_prod_exe.exe", PASS_N),
    ("auditring_prod",  "asserts", "tests_auditring_prod_exe.exe",  PASS_N),
    ("histbody",    "asserts", "tests_histbody.exe",      PASS_N),
    ("ws_json",     "asserts", "tests_ws_json.exe",       PASS_N),
]


def load_baseline(path):
    base = {}
    if not path.is_file():
        print("[assert_count] FAIL 基线文件缺失：%s" % path)
        return None
    for line in io.open(path, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split()
        if len(parts) != 2:
            print("[assert_count] FAIL 基线里有坏行：%r" % line)
            return None
        base[parts[0]] = int(parts[1])
    return base


def run_one(label, kind, exe, regex):
    p = BUILD / exe
    if not p.is_file():
        return None, "缺少可执行文件 %s（先跑完 run_all 的编译步骤）" % p
    try:
        r = subprocess.run([str(p)], cwd=str(PROJECT), capture_output=True, text=True,
                           encoding="utf-8", errors="replace", timeout=300)
    except OSError as e:
        return None, "运行失败：%s" % e
    out = (r.stdout or "") + (r.stderr or "")
    m = re.search(regex, out)
    if not m:
        return None, "输出里没有找到 %s 计数（改了打印格式就要同步本脚本）" % kind
    if r.returncode != 0:
        return None, "用例自己返回了 %d（那不是计数问题，先看 run_all）" % r.returncode
    return int(m.group(1)), None


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--baseline", default=str(BASELINE))
    ap.add_argument("--update", action="store_true",
                    help="用当前实测值重写基线（提高基线必须在提交说明里写理由）")
    a = ap.parse_args(argv)

    # --update 允许基线文件还不存在（首次入库时就是这样）
    base = {} if a.update else load_baseline(Path(a.baseline))
    if base is None:
        return 1

    rows, bad, total = [], [], {"asserts": 0, "cases": 0}
    for label, kind, exe, regex in REGISTRY:
        got, err = run_one(label, kind, exe, regex)
        want = base.get(label)
        if err:
            bad.append("%s: %s" % (label, err))
            continue
        if want is None and not a.update:
            bad.append("%s: 基线里没有这一行（新增测试必须同时登记基线）" % label)
            continue
        total[kind] += got
        if want is not None and got < want:
            bad.append("%s: %d < 基线 %d（%s 少了 %d）" % (label, got, want, kind, want - got))
        rows.append((label, kind, want, got))

    if a.update:
        with io.open(a.baseline, "w", encoding="utf-8", newline="\n") as f:
            f.write("# 断言数/用例数基线（S-G x3 / X06-F-06）\n")
            f.write("# label<TAB>min   asserts=PASS=n（执行了的断言数）；cases=corpus=/fed=（语料样本数）\n")
            for label, kind, exe, regex in REGISTRY:
                got = next((g for l, k, w, g in rows if l == label), None)
                if got is not None:
                    f.write("%s\t%d\n" % (label, got))
        print("[assert_count] 基线已更新：%s（%d 行）" % (a.baseline, len(rows)))
        return 0

    if bad:
        for b in bad:
            print("[assert_count] FAIL " + b)
        print("[assert_count] FAIL（%d 项）" % len(bad))
        return 1
    print("[assert_count] PASS（%d 个测试可执行行：%d 条断言 + %d 例语料样本，均不低于基线）"
          % (len(rows), total["asserts"], total["cases"]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
