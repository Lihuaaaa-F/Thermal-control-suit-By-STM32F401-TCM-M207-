#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""mutation_estop_gate.py — 急停屏入口判定的变异验证。

把 App/tcs/estop_gate.c / .h 逐个改坏（每个变异一处），确认 tests/test_site_logic.c 每次都变红。
有变异体活下来 = 测试有盲区；编译不过 = 变异锚点过期（实现改了，清单要跟着改）。两种情况退出码都是 1。

为什么要有它：这是安全路径上唯一的判定逻辑，单测"全绿"不说明测试有没有牙。
用法（在 project_tcs 下）：python tests/mutation_estop_gate.py [--verbose]
run_all.sh 每次都跑。编译器与 run_all.sh 相同（gcc 优先，没有就用 MSVC cl）。
"""
import io
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GATE_C = io.open(os.path.join(ROOT, "App", "tcs", "estop_gate.c"), encoding="utf-8").read()
GATE_H = io.open(os.path.join(ROOT, "App", "tcs", "estop_gate.h"), encoding="utf-8").read()
WORK = os.path.join(ROOT, "build", "mut_estop")
GCC = shutil.which("gcc")
VCVARS = os.environ.get(
    "TCS_VCVARS64",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat")
FLAGS = "/nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /DTCS_PROFILE=1"

# (编号, 说明, 文件, 原文, 改成)
MUTANTS = [
    ("G1", "所有页面都按首页规则（去掉 on_home 条件）", "c", "    if (on_home) {", "    if (1) {"),
    ("G2", "首页也按其它页面规则（on_home 恒假）", "c", "    if (on_home) {", "    if (0) {"),
    ("G3", "首页卡死不立即触发（去掉 !lvgl_alive 分支）", "c", "        if (!lvgl_alive) {", "        if (0) {"),
    ("G4", "卡死判据取反（心跳推进了才算卡死）", "c", "(uint8_t)(beat != st->beat0)", "(uint8_t)(beat == st->beat0)"),
    ("G5", "首页长按不看\"确认急停\"弹窗", "c",
     "held >= TCS_ESTOP_FORCE_HOLD_MS && !estop_dlg_open) {", "held >= TCS_ESTOP_FORCE_HOLD_MS) {"),
    ("G6", "首页长按门限 >= 改成 >", "c",
     "held >= TCS_ESTOP_FORCE_HOLD_MS && !estop_dlg_open) {", "held > TCS_ESTOP_FORCE_HOLD_MS && !estop_dlg_open) {"),
    ("G7", "首页分支落进其它页面逻辑（去掉首页的 return）", "c",
     "        return ESTOP_GATE_IDLE;\n    }\n\n    /* 其它页面", "    }\n\n    /* 其它页面"),
    ("G8", "其它页面：\"在处理\"不再要求界面活着", "c",
     "(uint8_t)(lvgl_alive && pressed_reads != st->reads0)", "(uint8_t)(pressed_reads != st->reads0)"),
    ("G9", "其它页面：读次数判据取反", "c",
     "(uint8_t)(lvgl_alive && pressed_reads != st->reads0)", "(uint8_t)(lvgl_alive && pressed_reads == st->reads0)"),
    ("G10", "其它页面：不看读次数（界面活着就算在处理）", "c",
     "(uint8_t)(lvgl_alive && pressed_reads != st->reads0)", "(uint8_t)(lvgl_alive)"),
    ("G11", "其它页面：没在处理就立即触发（去掉按住 600ms）", "c",
     "held >= TCS_ESTOP_FORCE_HOLD_MS && !handled) {", "!handled) {"),
    ("G12", "其它页面长按门限 >= 改成 >", "c",
     "held >= TCS_ESTOP_FORCE_HOLD_MS && !handled) {", "held > TCS_ESTOP_FORCE_HOLD_MS && !handled) {"),
    ("G13", "登记时不记读次数基准", "c", "        st->reads0  = pressed_reads;\n", ""),
    ("G14", "登记时不清自锁（再按不能触发）", "c", "        st->fired   = 0;\n", ""),
    ("G15", "去掉自锁（一次按压反复触发）", "c", "    if (st->fired) return ESTOP_GATE_IDLE;\n", ""),
    ("G16", "宽限期 5ms 改成 0", "h", "#define TCS_ESTOP_GRACE_MS        5u", "#define TCS_ESTOP_GRACE_MS        0u"),
    ("G17", "宽限期 5ms 改成 50", "h", "#define TCS_ESTOP_GRACE_MS        5u", "#define TCS_ESTOP_GRACE_MS        50u"),
    ("G18", "离开热区不复位", "c", "        st->in_zone = 0;", "        ;"),
    ("G19", "按住时长不按起点算", "c", "uint32_t held = now - st->since;", "uint32_t held = now;"),
    ("G20", "松手不处理（热区状态不随松手清）", "c", "    if (!pressed_in_zone) {", "    if (0) {"),
]


def win(p):
    """给 cmd 用的 Windows 路径（正斜杠会被 cl 当成开关）。"""
    return os.path.abspath(p).replace("/", "\\")


def compile_and_run(tag, gate_c, gate_h):
    d = os.path.join(WORK, tag)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    io.open(os.path.join(d, "estop_gate.c"), "w", encoding="utf-8", newline="").write(gate_c)
    io.open(os.path.join(d, "estop_gate.h"), "w", encoding="utf-8", newline="").write(gate_h)
    exe = os.path.join(d, "t.exe")
    incs = [d, os.path.join(ROOT, "App", "tcs"), os.path.join(ROOT, "App", "proto"),
            os.path.join(ROOT, "App", "comm"), os.path.join(ROOT, "Core", "Inc")]
    srcs = [os.path.join(ROOT, "tests", "test_site_logic.c"), os.path.join(d, "estop_gate.c")]
    if GCC:      # 与 run_all.sh 同一顺序：PATH 里有 gcc 先用 gcc
        r = subprocess.run([GCC, "-std=c11", "-Wall", "-Wextra", "-DTCS_UNIT_TEST", "-DTCS_PROFILE=1"]
                           + ["-I" + p for p in incs] + srcs + ["-o", exe], capture_output=True)
        if r.returncode != 0:
            log = os.path.join(d, "cc.log")
            io.open(log, "wb").write(r.stdout + r.stderr)
            return "COMPILE_FAIL", log
        t = subprocess.run([exe], capture_output=True)
        return ("PASS" if t.returncode == 0 else "FAIL"), t.stdout.decode("utf-8", "replace")
    if not os.path.isfile(VCVARS):
        print("[mut-estop] FAIL：PATH 里没有 gcc，也找不到 vcvars64.bat（%s）；可用环境变量 TCS_VCVARS64 指定" % VCVARS)
        sys.exit(1)
    cl = "cl " + FLAGS + " " + " ".join('/I"%s"' % win(p) for p in incs) + " " + \
        " ".join('"%s"' % win(s) for s in srcs) + ' /Fe:"%s"' % win(exe)
    bat = os.path.join(d, "mut_cl.bat")   # 不能叫 cl.bat：工作目录里有一个同名批处理时，命令行的 cl 会先找到它自己，无限自我调用后静默退出 255
    # 末尾必须有换行：cmd 对没有换行结尾的最后一行（exit /b）不执行，退出码会是 255
    io.open(bat, "w", encoding="ascii", newline="").write("\r\n".join([
        "@echo off",
        'call "%s" >nul 2>&1' % VCVARS,
        'cd /d "%s"' % win(d),
        cl,
        "exit /b %ERRORLEVEL%",
    ]) + "\r\n")
    r = subprocess.run(["cmd", "/c", win(bat)], capture_output=True)
    if r.returncode != 0:
        log = os.path.join(d, "cl.log")
        io.open(log, "wb").write(r.stdout + r.stderr)
        return "COMPILE_FAIL", log
    t = subprocess.run([win(exe)], capture_output=True)
    return ("PASS" if t.returncode == 0 else "FAIL"), t.stdout.decode("utf-8", "replace")


def main():
    verbose = "--verbose" in sys.argv
    base, info = compile_and_run("base", GATE_C, GATE_H)
    if base != "PASS":
        print("[mut-estop] FAIL：未变异的实现自己就没过（%s）%s" % (base, info if base == "PASS" else ""))
        return 1
    print("未变异：%s（%s）" % (base, info.strip().splitlines()[-1] if info else ""))
    bad = 0
    for mid, desc, which, old, new in MUTANTS:
        src = GATE_C if which == "c" else GATE_H
        if src.count(old) != 1:
            print("  %-4s 锚点匹配 %d 次（应为 1）——实现改了，清单要跟着改：%r" % (mid, src.count(old), old[:50]))
            bad += 1
            continue
        res, info = compile_and_run(mid, src.replace(old, new) if which == "c" else GATE_C,
                                    GATE_H if which == "c" else src.replace(old, new))
        if res == "FAIL":
            first = [ln for ln in info.splitlines() if ln.startswith("FAIL:")][:1]
            print("  %-4s 被杀  %s%s" % (mid, desc, ("  ← " + first[0][6:]) if first else ""))
        elif res == "COMPILE_FAIL":
            print("  %-4s 编译失败（见 %s）  %s" % (mid, info, desc))
            bad += 1
        else:
            print("  %-4s 活下来  %s" % (mid, desc))
            bad += 1
        if verbose and res == "FAIL":
            print(info.rstrip())
    print("mutation_estop_gate: %d 个变异体，%d 个异常（活下来 / 编译失败 / 锚点过期）" % (len(MUTANTS), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
