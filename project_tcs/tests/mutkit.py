# -*- coding: utf-8 -*-
"""mutkit.py — 变异验证脚本的共用脚手架（2026-11-26，S-G l1；第一个使用者是 mutation_audit_ring.py）。

把"编译一组源文件并运行"收成一件事：变异体写成工作目录里的副本，替换掉源文件表里对应的原文件。
编译器与 run_all.sh 相同（PATH 里有 gcc 先用 gcc，否则 MSVC cl；vcvars 环境整个进程只取一次）。
更早的三个脚本（mutation_estop_gate / mutation_link_dir / mutation_m207_txq）各自带了一份同样的
脚手架，没有回头去改它们——它们正在门禁里跑，改了要重新验证。
"""
import io
import os
import shutil
import subprocess

VCVARS_DEFAULT = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
_TOOLS = []


def win(p):
    """给 cmd 用的 Windows 路径（正斜杠会被 cl 当成开关）。"""
    return os.path.abspath(p).replace("/", "\\")


def msvc_tools(vcvars=None):
    """vcvars64 之后的 (环境变量, cl.exe 绝对路径)，只取一次；取不到返回 None。"""
    vcvars = vcvars or os.environ.get("TCS_VCVARS64", VCVARS_DEFAULT)
    if _TOOLS:
        return _TOOLS[0]
    try:
        r = subprocess.run('call "%s" >nul 2>&1 && set' % vcvars, shell=True, capture_output=True, timeout=90)
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


def run_variant(work_root, tag, mutated, sources, incs, flags="", want_pass=True):
    """编译并运行一次。返回 (状态, 输出)。
      mutated: {原源文件路径: 改写后的全文}——这些文件在工作目录里以同名副本参与编译
      sources: 源文件表（原路径）；incs: include 目录
      状态：PASS / FAIL（可执行文件退出码非零）/ COMPILE_FAIL（want_pass=False 时退出码非零算 PASS）。
    没有变异体时 mutated 传 {}。"""
    d = os.path.join(work_root, tag)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    real = []
    for s in sources:
        if s in mutated:
            p = os.path.join(d, os.path.basename(s))
            io.open(p, "w", encoding="utf-8", newline="").write(mutated[s])
            real.append(p)
        else:
            real.append(s)
    exe = os.path.join(d, "t.exe")
    gcc = shutil.which("gcc")
    if gcc:
        cmd = [gcc, "-std=c11", "-Wall", "-Wextra"] + flags.split() + ["-I" + p for p in incs] + real + ["-o", exe]
        r = subprocess.run(cmd, capture_output=True, cwd=d)
        if r.returncode != 0:
            io.open(os.path.join(d, "cc.log"), "wb").write(r.stdout + r.stderr)
            return "COMPILE_FAIL", ""
        t = subprocess.run([exe], capture_output=True, cwd=d)
    else:
        if not os.path.isfile(os.environ.get("TCS_VCVARS64", VCVARS_DEFAULT)):
            print("[mutkit] FAIL：PATH 里没有 gcc，也找不到 vcvars64.bat；可用环境变量 TCS_VCVARS64 指定")
            raise SystemExit(1)
        args = (flags.split() + ["/nologo", "/W4", "/std:c11", "/utf-8", "/D_CRT_SECURE_NO_WARNINGS"]
                + ["/I" + win(p) for p in incs] + [win(s) for s in real] + ["/Fe:" + win(exe)])
        tools = msvc_tools()
        if tools:
            env, cl = tools
            r = subprocess.run([cl] + args, cwd=d, env=env, capture_output=True)
        else:
            bat = os.path.join(d, "mut_cl.bat")   # 不能叫 cl.bat；末尾必须有换行
            io.open(bat, "w", encoding="ascii", newline="").write("\r\n".join([
                "@echo off",
                'call "%s" >nul 2>&1' % os.environ.get("TCS_VCVARS64", VCVARS_DEFAULT),
                'cd /d "%s"' % win(d),
                "cl " + " ".join('"%s"' % a if " " in a else a for a in args),
                "exit /b %ERRORLEVEL%",
            ]) + "\r\n")
            r = subprocess.run(["cmd", "/c", win(bat)], capture_output=True)
        if r.returncode != 0:
            io.open(os.path.join(d, "cl.log"), "wb").write(r.stdout + r.stderr)
            return "COMPILE_FAIL", ""
        t = subprocess.run([win(exe)], capture_output=True, cwd=d)
    out = t.stdout.decode("utf-8", "replace")
    ok = (t.returncode == 0) == want_pass
    return ("PASS" if ok else "FAIL"), out


def first_fail(out, n=2):
    return [ln[6:70] for ln in out.splitlines() if ln.startswith("FAIL:")][:n]
