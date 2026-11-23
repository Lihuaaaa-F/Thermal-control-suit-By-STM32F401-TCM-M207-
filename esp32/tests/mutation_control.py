#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mutation_control.py — control.c 判别性验证：旧实现 + 变异必须被单测抓红

两种证明，缺一不可：
  1. 旧实现：把**基线版本**的 control.c 配上现在的 tests/test_control_esp32.c 编译运行，
     新增的 W2 用例必须失败（否则测试没有判别力，旧代码照样过 = 假绿）。
  2. 变异：对现行 control.c 逐个施加"改坏一处"的变异（见 MUTATIONS），每个变异
     都必须让单测变红；有变异没被抓住 = 该路径缺用例，或变异与别的防御等价（需在
     表里注明"等价"）。

用法（在 esp32/ 根目录）：python tests/mutation_control.py [--old <旧 control.c 路径>]
  --old 缺省时用 `git show d061041:esp32/components/tcs_core/control.c`。
编译器与 tests/run_m207_tests.sh 相同（MSVC cl + vcvars64）。
退出码 0 = 全部按预期（旧实现红、每个变异红、原版绿）。
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.dirname(HERE)
CORE = os.path.join(ESP, "components", "tcs_core")
VARS64 = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
TEST = os.path.join(HERE, "test_control_esp32.c")

# (名称, 文件内唯一原文, 替换文, 备注)。原文必须在文件里恰好出现一次。
MUTATIONS = [
    ("M01 退出 SAFE 风扇一律关",
     "out_fan(estop_forced_manual ? 0u : s_fan_before_safe);",
     "out_fan(0);"),
    ("M02 退出 SAFE 不复原 TEC2",
     "if (s_restore_tec2_ok) out_tec_sw(2, s_tec2_before_safe);",
     ";"),
    ("M03 模块掉线不作废 TEC2 复原",
     "if (!g_tcs.m207_online || g_tcs.m207_sensor_open\n"
     "            || (s_safety_latch & (uint8_t)~SAFETY_BIT_BODY_LOST)) {",
     "if (g_tcs.m207_sensor_open\n"
     "            || (s_safety_latch & (uint8_t)~SAFETY_BIT_BODY_LOST)) {"),
    ("M04 重申间隔跟节拍变密",
     "(uint32_t)(now_ms - s_safe_assert_ms) >= CTRL_SAFE_REASSERT_MS) {",
     "(uint32_t)(now_ms - s_safe_assert_ms) >= 100u) {"),
    ("M05 急停不再区分自动被切手动",
     "uint8_t estop_forced_manual = (uint8_t)(s_auto_before_safe && g_tcs.mode == 0);",
     "uint8_t estop_forced_manual = 0;"),
    ("M06 不记忆故障前模式",
     "s_auto_before_safe = (uint8_t)(g_tcs.mode != 0);",
     "s_auto_before_safe = 1;"),
    ("M07 每次重申都重记故障前输出",
     "    if (s_state != CTRL_SAFE) safe_remember_outputs();\n    safe_outputs();",
     "    safe_remember_outputs();\n    safe_outputs();"),
    ("M08 退出后不强制下一拍评估",
     "    s_last_eval = now_ms - CTRL_EVAL_MS;\n    goto_state(CTRL_IDLE);",
     "    goto_state(CTRL_IDLE);"),
    ("M09 上电首拍不重申",
     "        s_safe_assert_ms = now_ms - CTRL_SAFE_REASSERT_MS;",
     "        s_safe_assert_ms = now_ms;"),
    ("M10 急停不记忆故障前输出（直接进 SAFE）",
     "    if (s_state != CTRL_SAFE) safe_remember_outputs();\n    s_restore_tec2_ok = 0;",
     "    s_restore_tec2_ok = 0;"),
]


def run_cl(work, control_c):
    """用 cl 编译并运行单测；返回 (编译成功, 运行输出, 运行返回码)"""
    bat = os.path.join(work, "cl_run.bat")
    inc = [os.path.join(ESP, "components", "tcs_m207", "include"),
           os.path.join(ESP, "components", "tcs_tmp117", "include"),
           CORE,
           os.path.join(ESP, "components", "tcs_link", "include"),
           os.path.join(ESP, "components", "tcs_link")]
    srcs = [TEST, control_c,
            os.path.join(CORE, "param_registry.c"),
            os.path.join(CORE, "post_pure.c"),
            os.path.join(ESP, "components", "tcs_link", "proto.c")]
    objs = " ".join('"%s"' % s for s in srcs)
    incs = " ".join('/I"%s"' % i for i in inc)
    lines = ["@echo off",
             'call "%s" >nul 2>&1' % VARS64,
             'cd /d "%s"' % work,
             "cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST %s %s /Fe:test_ctl.exe"
             % (incs, objs),
             "exit /b %ERRORLEVEL%"]
    with open(bat, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    cp = subprocess.run(["cmd", "/c", bat], capture_output=True, text=True,
                        errors="replace", timeout=300)
    log = os.path.join(work, "cl.log")
    with open(log, "w", encoding="utf-8", errors="replace") as f:
        f.write(cp.stdout + cp.stderr)
    if cp.returncode != 0:
        return False, cp.stdout + cp.stderr, -1
    # cl 把 .obj/exe 放在当前目录；运行
    rp = subprocess.run([os.path.join(work, "test_ctl.exe")], capture_output=True,
                        text=True, errors="replace", timeout=120, cwd=work)
    return True, rp.stdout + rp.stderr, rp.returncode


def compile_run(control_src_text, tag):
    d = tempfile.mkdtemp(prefix="tcs_mut_")
    c = os.path.join(d, "control.c")
    with open(c, "w", encoding="utf-8", newline="\n") as f:
        f.write(control_src_text)
    ok, out, rc = run_cl(d, c)
    return ok, out, rc


def fails_of(out):
    return re.findall(r"^FAIL: (.+?) \(line", out, flags=re.M)


def main():
    cur_path = os.path.join(CORE, "control.c")
    with open(cur_path, encoding="utf-8") as f:
        cur = f.read()

    bad = 0

    # 0) 原版必须绿
    ok, out, rc = compile_run(cur, "current")
    if not ok or rc != 0:
        print("[current] NOT GREEN (compile_ok=%s rc=%s)" % (ok, rc))
        print(out[-2000:])
        return 1
    m = re.search(r"PASS=(\d+) FAIL=(\d+)", out)
    print("[current] PASS=%s FAIL=%s" % (m.group(1), m.group(2)))

    # 1) 旧实现必须红，且红在新用例上
    old_arg = None
    if "--old" in sys.argv:
        old_arg = sys.argv[sys.argv.index("--old") + 1]
    if old_arg:
        with open(old_arg, encoding="utf-8") as f:
            old = f.read()
    else:
        old = subprocess.run(["git", "-C", ESP, "show",
                              "d061041:esp32/components/tcs_core/control.c"],
                             capture_output=True, text=True, encoding="utf-8").stdout
    ok, out, rc = compile_run(old, "old")
    if ok:
        fl = fails_of(out)
        m = re.search(r"PASS=(\d+) FAIL=(\d+)", out)
        w2 = [x for x in fl if x.startswith("W2-")]
        print("[old impl] rc=%s PASS=%s FAIL=%s (W2 fails: %d)" % (rc, m and m.group(1), m and m.group(2), len(w2)))
        for x in fl:
            print("    red: " + x)
        if rc == 0 or len(w2) < 5:
            print("[old impl] NOT DISCRIMINATING: 旧实现没有被足够多的新用例抓红")
            bad += 1
    else:
        print("[old impl] COMPILE FAIL（旧实现缺少新接口？不得如此——测试只用旧接口）")
        print(out[-1500:])
        bad += 1

    # 2) 变异必须红
    for name, a, b in MUTATIONS:
        n = cur.count(a)
        if n != 1:
            print("[mut] %-34s SKIP: 原文出现 %d 次（变异表过期）" % (name, n))
            bad += 1
            continue
        ok, out, rc = compile_run(cur.replace(a, b), "mut")
        if ok and rc != 0:
            fl = fails_of(out)
            print("[mut] %-34s CAUGHT by %d checks, e.g. %s" % (name, len(fl), fl[0] if fl else "?"))
        elif ok:
            print("[mut] %-34s SURVIVED —— 没有任何用例抓到" % name)
            bad += 1
        else:
            print("[mut] %-34s COMPILE FAIL" % name)
            bad += 1

    print("mutation_control: %s" % ("ALL AS EXPECTED" if not bad else "HAS PROBLEMS (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
