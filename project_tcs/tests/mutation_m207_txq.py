#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""mutation_m207_txq.py — M207 下发指令队列（M07-F-10）的变异验证。

把 App/tcs/m207_txq.c 逐个改坏（每个变异一处），按 tests/run_all.sh 相同的方式编译
tests/test_m207_txq.c，确认每次都变红：
  截断代替拒绝 / 上限差一 / 条数在回绕后算错 / 满了还写 / 空指令也入队 / 取走不前移。
有变异体活下来 = 用例有盲区；编译不过 = 变异锚点过期。两种情况退出码都是 1。
本脚本只读 work tree：变异体写在 build/mut_m207_txq/ 下的副本里。

用法（在 project_tcs 下）：python tests/mutation_m207_txq.py [--verbose]
"""
import io
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = io.open(os.path.join(ROOT, "App", "tcs", "m207_txq.c"), encoding="utf-8").read()
WORK = os.path.join(ROOT, "build", "mut_m207_txq")
GCC = shutil.which("gcc")
VCVARS = os.environ.get(
    "TCS_VCVARS64",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat")
FLAGS = "/nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST"
INCS = [os.path.join(ROOT, "App", "tcs"), os.path.join(ROOT, "App", "proto"), os.path.join(ROOT, "App", "comm")]
SRCS = [os.path.join(ROOT, "tests", "test_m207_txq.c"), None,          # None = 变异副本
        os.path.join(ROOT, "App", "tcs", "m207.c")]

LEN_LINE = "    if (n > M207_CMD_MAX) return -2;\n"
FULL_LINE = "    if (next == q->tail) return -1;            /* 满 */\n"
EMPTY_LINE = "    if (!cmd || !*cmd) return -1;\n"
COUNT_LINE = "    return (uint8_t)((q->head + M207_TXQ_DEPTH - q->tail) % M207_TXQ_DEPTH);\n"
POP_LINE = "    q->tail = (uint8_t)((q->tail + 1u) % M207_TXQ_DEPTH);\n"

# (编号, 说明, 原文, 改成)
MUTANTS = [
    ("Q1", "过长的指令截断后照旧入队（修复前的行为）", LEN_LINE, "    if (n > M207_CMD_MAX) n = M207_CMD_MAX;\n"),
    ("Q2", "上限差一（24 字符也放进去）", LEN_LINE, "    if (n > M207_CMD_MAX + 1u) return -2;\n"),
    ("Q3", "条数不算回绕（先转 uint8_t 再相减，回绕后得到 255）", COUNT_LINE,
     "    return (uint8_t)(q->head - q->tail);\n"),
    ("Q4", "队列满了还往里写", FULL_LINE, "    if (0) return -1;\n"),
    ("Q5", "空指令也入队", EMPTY_LINE, "    if (!cmd) return -1;\n"),
    ("Q6", "取走队头不前移", POP_LINE, "    if (0) q->tail = (uint8_t)((q->tail + 1u) % M207_TXQ_DEPTH);\n"),
]


def win(p):
    """给 cmd 用的 Windows 路径（正斜杠会被 cl 当成开关）。"""
    return os.path.abspath(p).replace("/", "\\")


_TOOLS = []


def msvc_tools():
    """vcvars64 之后的 (环境变量, cl.exe 绝对路径)，整个脚本只取一次（见 mutation_link_dir.py）。"""
    if _TOOLS:
        return _TOOLS[0]
    try:
        r = subprocess.run('call "%s" >nul 2>&1 && set' % VCVARS, shell=True, capture_output=True, timeout=90)
        env = {k.upper(): v for k, v in os.environ.items()}
        for raw in r.stdout.splitlines():
            line = raw.decode("mbcs", "replace")
            if line.startswith("=") or "=" not in line:
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


def compile_and_run(tag, src_c):
    d = os.path.join(WORK, tag)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    mut = os.path.join(d, "m207_txq.c")
    io.open(mut, "w", encoding="utf-8", newline="").write(src_c)
    srcs = [mut if s is None else s for s in SRCS]
    exe = os.path.join(d, "t.exe")
    if GCC:
        r = subprocess.run([GCC, "-std=c11", "-Wall", "-Wextra", "-DTCS_UNIT_TEST"]
                           + ["-I" + p for p in INCS] + srcs + ["-o", exe], capture_output=True)
        if r.returncode != 0:
            io.open(os.path.join(d, "cc.log"), "wb").write(r.stdout + r.stderr)
            return "COMPILE_FAIL", ""
        t = subprocess.run([exe], capture_output=True)
        return ("PASS" if t.returncode == 0 else "FAIL"), t.stdout.decode("utf-8", "replace")
    if not os.path.isfile(VCVARS):
        print("[mut-txq] FAIL：PATH 里没有 gcc，也找不到 vcvars64.bat（%s）；可用环境变量 TCS_VCVARS64 指定" % VCVARS)
        sys.exit(1)
    tools = msvc_tools()
    if tools:
        env, cl = tools
        args = FLAGS.split() + ["/I" + win(p) for p in INCS] + [win(s) for s in srcs] + ["/Fe:" + win(exe)]
        r = subprocess.run([cl] + args, cwd=d, env=env, capture_output=True)
        if r.returncode != 0:
            io.open(os.path.join(d, "cl.log"), "wb").write(r.stdout + r.stderr)
            return "COMPILE_FAIL", ""
        t = subprocess.run([win(exe)], capture_output=True)
        return ("PASS" if t.returncode == 0 else "FAIL"), t.stdout.decode("utf-8", "replace")
    cl = "cl " + FLAGS + " " + " ".join('/I"%s"' % win(p) for p in INCS) + " " + \
        " ".join('"%s"' % win(s) for s in srcs) + ' /Fe:"%s"' % win(exe)
    bat = os.path.join(d, "mut_cl.bat")   # 不能叫 cl.bat；末尾必须有换行
    io.open(bat, "w", encoding="ascii", newline="").write("\r\n".join([
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
    base, info = compile_and_run("base", SRC)
    if base != "PASS":
        print("[mut-txq] FAIL：未变异的实现自己就没过（%s）" % base)
        if base == "FAIL":
            print(info)
        return 1
    print("未变异：%s（%s）" % (base, info.strip().splitlines()[-1] if info.strip() else ""))
    bad = 0
    for mid, desc, old, new in MUTANTS:
        if SRC.count(old) != 1:
            print("  %-3s 锚点匹配 %d 次（应为 1）——实现改了，清单要跟着改：%r" % (mid, SRC.count(old), old[:50]))
            bad += 1
            continue
        res, out = compile_and_run(mid, SRC.replace(old, new))
        if res == "FAIL":
            first = [ln for ln in out.splitlines() if ln.startswith("FAIL:")][:2]
            print("  %-3s 被杀  %s%s" % (mid, desc, ("  ← " + "；".join(f[6:60] for f in first)) if first else ""))
        elif res == "COMPILE_FAIL":
            print("  %-3s 编译失败（见 %s）  %s" % (mid, os.path.join(WORK, mid, "cl.log"), desc))
            bad += 1
        else:
            print("  %-3s 活下来  %s" % (mid, desc))
            bad += 1
        if verbose and res == "FAIL":
            print(out.rstrip())
    print("mutation_m207_txq: %d 个变异体，%d 个异常（活下来 / 编译失败 / 锚点过期）" % (len(MUTANTS), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
