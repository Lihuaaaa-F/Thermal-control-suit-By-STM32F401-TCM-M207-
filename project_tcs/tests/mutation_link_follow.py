#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""mutation_link_follow.py — control_link_follow（交付档专属的 SAFE 退出判据）的回滚式验证。

R5-06 残余（2026-11-25）：这段判定此前在主机门禁里零覆盖。把 project_tcs/App/tcs/control.c 里
control_link_follow 的判据逐个改回错误写法（每个变异一处），按**交付档**（-DTCS_PROFILE=0）编译
tests/test_link_follow_prod.c，确认每次都变红。
有变异体活下来 = 用例有盲区；编译不过 = 变异锚点过期（control.c 归 h1a 批次在改，锚点过期时
本脚本会明确报出来，届时同步清单）。两种情况退出码都是 1。
本脚本只读 work tree：变异体写在 build/mut_link_follow/ 下的副本里。

用法（在 project_tcs 下）：python tests/mutation_link_follow.py [--verbose]
"""
import io
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CTRL = io.open(os.path.join(ROOT, "App", "tcs", "control.c"), encoding="utf-8").read()
WORK = os.path.join(ROOT, "build", "mut_link_follow")
GCC = shutil.which("gcc")
VCVARS = os.environ.get(
    "TCS_VCVARS64",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat")
FLAGS = "/nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /DTCS_PROFILE=0"
INCS = [os.path.join(ROOT, "App", "tcs"), os.path.join(ROOT, "App", "proto"), os.path.join(ROOT, "App", "comm")]
SRCS = [os.path.join(ROOT, "tests", "test_link_follow_prod.c"), None,          # None = 变异副本
        os.path.join(ROOT, "App", "tcs", "param_registry.c"), os.path.join(ROOT, "App", "tcs", "post.c"),
        os.path.join(ROOT, "App", "proto", "proto.c")]

FLAGS_LINE = "    if (s_estop_latch || s_overtemp_latch || s_estop_tx_pending || s_overtemp_tx_pending) return;\n"
ALARM_LINE = "    if (g_tcs.alarm_bits & (ALM_ESTOP | ALM_OVERTEMP | ALM_BODY_SENSOR | ALM_M207)) return;\n"
STATE_LINE = "    if (s_state != CTRL_SAFE) return;\n"

# (编号, 说明, 原文, 改成)——都落在 control_link_follow 里（函数体只有下面这几行）
MUTANTS = [
    ("F1", "不看本机的四个标志（自持 / 待投递）", FLAGS_LINE, "    if (0) return;\n"),
    ("F2", "四个标志全置才算未清（|| 改成 &&）", FLAGS_LINE,
     "    if (s_estop_latch && s_overtemp_latch && s_estop_tx_pending && s_overtemp_tx_pending) return;\n"),
    ("F3", "不看两个待投递意图", FLAGS_LINE, "    if (s_estop_latch || s_overtemp_latch) return;\n"),
    ("F4", "不看快照里的安全类报警", ALARM_LINE, "    if (0) return;\n"),
    ("F5", "快照报警只看急停 / 超温，漏了传感器失联类", ALARM_LINE,
     "    if (g_tcs.alarm_bits & (ALM_ESTOP | ALM_OVERTEMP)) return;\n"),
    ("F6", "去掉\"只在 SAFE 才判\"的前置（任何状态都会被拉回 IDLE）", STATE_LINE, "    if (0) return;\n"),
    ("F7", "前置条件取反（永不退出 SAFE）", STATE_LINE, "    if (s_state == CTRL_SAFE) return;\n"),
    ("F8", "只在 IDLE 才判（SAFE 永不退出）", STATE_LINE, "    if (s_state != CTRL_IDLE) return;\n"),
]


def win(p):
    """给 cmd 用的 Windows 路径（正斜杠会被 cl 当成开关）。"""
    return os.path.abspath(p).replace("/", "\\")


_TOOLS = []


def msvc_tools():
    """vcvars64 之后的 (环境变量, cl.exe 绝对路径)，整个脚本只取一次。
    之后每个变异体直接起 cl，不必每次 call vcvars64.bat（那一步每个变异体要 2 秒多）。
    取不到返回 None，调用方退回"每次生成 bat"的方式。"""
    if _TOOLS:
        return _TOOLS[0]
    try:
        r = subprocess.run('call "%s" >nul 2>&1 && set' % VCVARS,
                           shell=True, capture_output=True, timeout=90)
        env = {k.upper(): v for k, v in os.environ.items()}
        for raw in r.stdout.splitlines():
            line = raw.decode("mbcs", "replace")
            if line.startswith("=") or "=" not in line:      # "=C:=..." 这类虚拟变量跳过
                continue
            k, v = line.split("=", 1)
            env[k.upper()] = v
        cl = None
        for d in env.get("PATH", "").split(os.pathsep):
            d = d.strip().strip('"')
            if os.path.isfile(os.path.join(d, "cl.exe")):
                cl = os.path.join(d, "cl.exe")
                break
        if cl:
            _TOOLS.append((env, cl))
    except Exception:
        return None
    return _TOOLS[0] if _TOOLS else None


def compile_and_run(tag, ctrl_c):
    d = os.path.join(WORK, tag)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    csrc = os.path.join(d, "control.c")
    io.open(csrc, "w", encoding="utf-8", newline="").write(ctrl_c)
    srcs = [csrc if s is None else s for s in SRCS]
    exe = os.path.join(d, "t.exe")
    if GCC:      # 与 run_all.sh 同一顺序：PATH 里有 gcc 先用 gcc
        r = subprocess.run([GCC, "-std=c11", "-Wall", "-Wextra", "-DTCS_UNIT_TEST", "-DTCS_PROFILE=0"]
                           + ["-I" + p for p in INCS] + srcs + ["-o", exe], capture_output=True)
        if r.returncode != 0:
            io.open(os.path.join(d, "cc.log"), "wb").write(r.stdout + r.stderr)
            return "COMPILE_FAIL", ""
        t = subprocess.run([exe], capture_output=True)
        return ("PASS" if t.returncode == 0 else "FAIL"), t.stdout.decode("utf-8", "replace")
    if not os.path.isfile(VCVARS):
        print("[mut-follow] FAIL：PATH 里没有 gcc，也找不到 vcvars64.bat（%s）；可用环境变量 TCS_VCVARS64 指定" % VCVARS)
        sys.exit(1)
    tools = msvc_tools()
    if tools:
        env, cl = tools                     # 快路径：直接起 cl（见 msvc_tools）
        args = FLAGS.split() + ["/I" + win(p) for p in INCS] + [win(s) for s in srcs] + ["/Fe:" + win(exe)]
        r = subprocess.run([cl] + args, cwd=d, env=env, capture_output=True)
        if r.returncode != 0:
            io.open(os.path.join(d, "cl.log"), "wb").write(r.stdout + r.stderr)
            return "COMPILE_FAIL", ""
        t = subprocess.run([win(exe)], capture_output=True)
        return ("PASS" if t.returncode == 0 else "FAIL"), t.stdout.decode("utf-8", "replace")
    cl = "cl " + FLAGS + " " + " ".join('/I"%s"' % win(p) for p in INCS) + " " + \
        " ".join('"%s"' % win(s) for s in srcs) + ' /Fe:"%s"' % win(exe)
    bat = os.path.join(d, "mut_cl.bat")   # 不能叫 cl.bat（工作目录里同名批处理会自我调用后静默退出 255）
    io.open(bat, "w", encoding="ascii", newline="").write("\r\n".join([   # 末尾必须有换行
        "@echo off",
        'call "%s" >nul 2>&1' % VCVARS,
        'cd /d "%s"' % win(d),
        cl,
        "exit /b %ERRORLEVEL%",
    ]) + "\r\n")
    r = subprocess.run(["cmd", "/c", win(bat)], capture_output=True)
    if r.returncode != 0:
        io.open(os.path.join(d, "cl.log"), "wb").write(r.stdout + r.stderr)
        return "COMPILE_FAIL", ""
    t = subprocess.run([win(exe)], capture_output=True)
    return ("PASS" if t.returncode == 0 else "FAIL"), t.stdout.decode("utf-8", "replace")


def main():
    verbose = "--verbose" in sys.argv
    base, info = compile_and_run("base", CTRL)
    if base != "PASS":
        print("[mut-follow] FAIL：未变异的实现自己就没过（%s）" % base)
        if base == "FAIL":
            print(info)
        return 1
    print("未变异：%s（%s）" % (base, info.strip().splitlines()[-1] if info else ""))
    bad = 0
    for mid, desc, old, new in MUTANTS:
        if CTRL.count(old) < 1:
            print("  %-3s 锚点已不存在——control.c 的 control_link_follow 改了，清单要跟着改：%r" % (mid, old[:60]))
            bad += 1
            continue
        if old == STATE_LINE:
            src = CTRL.replace(old, new, 1)      # control_poll 等处没有同样的行；只改第一处（即 follow 里）
        else:
            src = CTRL.replace(old, new, 1)
        if src == CTRL:
            print("  %-3s 变异没有改动任何文本：%s" % (mid, desc))
            bad += 1
            continue
        res, out = compile_and_run(mid, src)
        if res == "FAIL":
            first = [ln for ln in out.splitlines() if ln.startswith("FAIL:")][:2]
            print("  %-3s 被杀  %s%s" % (mid, desc, ("  ← " + "；".join(f[6:50] for f in first)) if first else ""))
        elif res == "COMPILE_FAIL":
            print("  %-3s 编译失败（见 %s）  %s" % (mid, os.path.join(WORK, mid, "cl.log"), desc))
            bad += 1
        else:
            print("  %-3s 活下来  %s" % (mid, desc))
            bad += 1
        if verbose and res == "FAIL":
            print(out.rstrip())
    print("mutation_link_follow: %d 个变异体，%d 个异常（活下来 / 编译失败 / 锚点过期）" % (len(MUTANTS), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
