#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mutation_m207_force.py — 安全关断通道（m207_tec_force）的判别性验证：变异必须被单测抓红

S 线 i3（P1-09 的另一半，2026-11-26）。与 mutation_control.py 同款做法，对象是
esp32/components/tcs_m207/m207_core.c 里的安全关断通道：
  - m207_tec_force()：离线也受理，只记保持位（不出帧）；
  - m207_go_online()：复联先补发关断再放行普通队列，判据 = 保持位 ∨ 命令意图是关 ∨ 回显是关；
  - 保持位的解除：读回到关，或该通道来了更新的“开”；
  - N-01 超温路径的服务层关断也走这条通道。
用例 = tests/test_m207_force.c（只编这一份；每个变异都必须让它变红）。
没被抓住 = 该路径缺用例。注明"等价"的允许存活（与另一道防线等价），并配一个把等价的
几道防线一起删掉的变异，后者必须被抓住。

用法（在 esp32/ 根目录）：python tests/mutation_m207_force.py
编译器与 tests/run_m207_tests.sh 相同（MSVC cl + vcvars64）。退出码 0 = 全部按预期。
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.dirname(HERE)
CORE = os.path.join(ESP, "components", "tcs_core")
M207 = os.path.join(ESP, "components", "tcs_m207")
VARS64 = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
TEST = os.path.join(HERE, "test_m207_force.c")

_ON_CLEAR = "    if (on) s_lane[ch].hold = 0;"
_ECHO_CLEAR = "            s_lane[r.module].hold = 0;"

# 每项：(名称, [(原文, 替换文), ...], 备注)。每段原文必须在 m207_core.c 里恰好出现一次。
MUTATIONS = [
    ("F01 force 离线不记保持位（拔线后的安全关断又丢了）",
     [("    s_lane[ch].hold = 1;\n    s_lane[ch].on = 0u;", "    s_lane[ch].on = 0u;")], ""),
    ("F02 force 离线时把槽置成待发（发进没人听的线路，发完即清）",
     [("    if (s_online) s_lane[ch].pending = 1;\n    M207_Q_UNLOCK();\n    return 0;",
       "    s_lane[ch].pending = 1;\n    M207_Q_UNLOCK();\n    return 0;")], ""),
    ("F03 复联不看保持位",
     [("        if (s_lane[ch].hold || !cmd_on || !echo_on) {", "        if (!cmd_on || !echo_on) {")], ""),
    ("F04 复联读回显不读命令意图（晚到的 TCSW=1 应答让关断漏发）",
     [("        if (s_lane[ch].hold || !cmd_on || !echo_on) {", "        if (s_lane[ch].hold || !echo_on) {")], ""),
    ("F05 读回到关不解除保持",
     [(_ECHO_CLEAR, "            ;")],
     "等价：解除保持另有一道——该通道更新的“开”（m207_tec 的 on 分支）同样解除；两道一起删见 F07"),
    ("F06 更新的“开”不解除保持（复联会把用户刚开的通道关掉）",
     [(_ON_CLEAR, "    ;")], ""),
    ("F07 两道解除都删掉（保持位一旦置上永不清）",
     [(_ON_CLEAR, "    ;"), (_ECHO_CLEAR, "            ;")], ""),
    ("F08 force 的“开”也走这条通道（置成保持的关断）",
     [("    if (on) return m207_tec(ch, 1);", "    if (0) return m207_tec(ch, 1);")], ""),
    ("F09 N-01 超温路径的服务层关断不走安全关断通道",
     [("                (void)m207_tec_force(1, 0);", "                m207_lane_set(1, 0);")], ""),
    ("F10 复联时保持的关断被排进普通队列（不再先于队列帧）",
     [("            s_lane[ch].pending = 1;\n            s_lane[ch].on = 0u;\n            M207_Q_UNLOCK();",
       "            s_lane[ch].hold = 0;\n            M207_Q_UNLOCK();")], ""),
]


def run_cl(work, src):
    bat = os.path.join(work, "cl_run.bat")
    inc = [os.path.join(ESP, "components", "tcs_m207", "include"),
           os.path.join(ESP, "components", "tcs_tmp117", "include"),
           CORE,
           os.path.join(ESP, "components", "tcs_link", "include"),
           os.path.join(ESP, "components", "tcs_link")]
    srcs = [TEST, src,
            os.path.join(M207, "m207_proto.c"),
            os.path.join(CORE, "control.c"),
            os.path.join(CORE, "control_manual.c"),
            os.path.join(CORE, "param_registry.c"),
            os.path.join(CORE, "post_pure.c"),
            os.path.join(ESP, "components", "tcs_link", "proto.c")]
    objs = " ".join('"%s"' % s for s in srcs)
    incs = " ".join('/I"%s"' % i for i in inc)
    lines = ["@echo off",
             'call "%s" >nul 2>&1' % VARS64,
             'cd /d "%s"' % work,
             "cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST %s %s /Fe:test_force.exe"
             % (incs, objs),
             "exit /b %ERRORLEVEL%"]
    with open(bat, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    cp = subprocess.run(["cmd", "/c", bat], capture_output=True, text=True,
                        errors="replace", timeout=300)
    if cp.returncode != 0:
        return False, cp.stdout + cp.stderr, -1
    rp = subprocess.run([os.path.join(work, "test_force.exe")], capture_output=True,
                        text=True, errors="replace", timeout=300, cwd=work)
    return True, rp.stdout + rp.stderr, rp.returncode


def compile_run(text):
    d = tempfile.mkdtemp(prefix="tcs_mf_")
    c = os.path.join(d, "m207_core.c")
    with open(c, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return run_cl(d, c)


def fails_of(out):
    return re.findall(r"^FAIL: (.+?) \(line", out, flags=re.M)


def main():
    cur_path = os.path.join(M207, "m207_core.c")
    with open(cur_path, encoding="utf-8") as f:
        cur = f.read().replace("\r\n", "\n")
    bad = 0
    ok, out, rc = compile_run(cur)
    m = re.search(r"PASS=(\d+) FAIL=(\d+)", out)
    if not ok or rc != 0:
        print("[current] NOT GREEN (compile_ok=%s rc=%s)" % (ok, rc))
        print(out[-2000:])
        return 1
    print("[current] PASS=%s FAIL=%s" % (m.group(1), m.group(2)))

    for name, edits, note in MUTATIONS:
        mutated = cur
        stale = None
        for a, b in edits:
            n = mutated.count(a)
            if n != 1:
                stale = (a, n)
                break
            mutated = mutated.replace(a, b)
        if stale:
            print("[mut] %-52s SKIP: 原文出现 %d 次（变异表过期）: %r" % (name, stale[1], stale[0][:50]))
            bad += 1
            continue
        ok, out, rc = compile_run(mutated)
        equivalent = note.startswith("等价")
        if ok and rc != 0:
            fl = fails_of(out)
            print("[mut] %-52s CAUGHT by %d checks, e.g. %s" % (name, len(fl), fl[0] if fl else "?"))
        elif ok:
            if equivalent:
                print("[mut] %-52s survives as expected (%s)" % (name, note))
            else:
                print("[mut] %-52s SURVIVED —— 没有任何用例抓到" % name)
                bad += 1
        else:
            print("[mut] %-52s COMPILE FAIL" % name)
            print(out[-800:])
            bad += 1

    print("mutation_m207_force: %s" % ("ALL AS EXPECTED" if not bad else "HAS PROBLEMS (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
