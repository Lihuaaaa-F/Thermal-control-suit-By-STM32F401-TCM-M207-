#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""mutation_link_dir.py — 链路方向闸门（R5-01）与屏写参数走 ARQ（R5-10）的变异验证。

把 protocol/tcs_proto.h、两端的 comm.c、esp32 的 link_task.c、屏端 link_send.c 逐个改坏
（每个变异一处），确认编译了那份文件的主机用例每次都变红：
  h743_dir   project_tcs/tests/test_link_dir.c  + App/comm/comm.c + App/proto/proto.c
  esp_dir    esp32/tests/test_link_dir.c        + components/tcs_link/{link_task,comm,proto}.c（IDF 用桩）
  h743_send  project_tcs/tests/test_link_send.c + App/comm/link_send.c + comm.c + proto.c
有变异体活下来 = 用例有盲区；编译不过 = 变异锚点过期（实现改了，清单要跟着改）。两种情况退出码都是 1。

头文件 protocol/tcs_proto.h 被两端的包装文件和 protocol/proto_layout.h 按**相对路径** include，
-I 拦不住。所以改它的变异体在一个临时目录里按仓库的相对布局复制一棵最小的"协议树"
（唯一源 + proto_layout.h + 两端包装 + 两端 proto.c），让 include 的解析顺序与真实构建相同。

用法（在 project_tcs 下）：python tests/mutation_link_dir.py [--verbose]
run_all.sh 每次都跑。编译器与 run_all.sh 相同（gcc 优先，没有就用 MSVC cl）。
"""
import io
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PT = os.path.dirname(HERE)
REPO = os.path.dirname(PT)
ESP = os.path.join(REPO, "esp32")
TL = os.path.join(ESP, "components", "tcs_link")

# 键 → 源文件
SRC = {
    "proto":  os.path.join(REPO, "protocol", "tcs_proto.h"),
    "layout": os.path.join(REPO, "protocol", "proto_layout.h"),
    "hwrap":  os.path.join(PT, "App", "tcs", "tcs_proto.h"),
    "ewrap":  os.path.join(TL, "tcs_proto.h"),
    "hproto": os.path.join(PT, "App", "proto", "proto.c"),
    "eproto": os.path.join(TL, "proto.c"),
    "hcomm":  os.path.join(PT, "App", "comm", "comm.c"),
    "ecomm":  os.path.join(TL, "comm.c"),
    "ltask":  os.path.join(TL, "link_task.c"),
    # M11-F-15（S 线 e1）：link_task.c 按职责拆开，主控端用例要编全部新文件；
    # 各文件的变异体指向各自的新归属（谁的内容变了就改谁）。
    "ldispatch": os.path.join(TL, "link_dispatch.c"),
    "lregistry": os.path.join(TL, "link_registry.c"),
    "lsend_esp": os.path.join(TL, "link_send.c"),
    "lstatus":  os.path.join(TL, "link_status.c"),
    "ldemo":    os.path.join(TL, "link_demo.c"),
    "lselftest": os.path.join(TL, "link_selftest.c"),
    "lsend":  os.path.join(PT, "App", "comm", "link_send.c"),
}
TEXT = {k: io.open(p, encoding="utf-8").read() for k, p in SRC.items()}
WORK = os.path.join(PT, "build", "mut_link_dir")
GCC = shutil.which("gcc")
VCVARS = os.environ.get(
    "TCS_VCVARS64",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat")
FLAGS = "/nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST"

INC_H743 = [os.path.join(PT, "App", "tcs"), os.path.join(PT, "App", "proto"), os.path.join(PT, "App", "comm")]
INC_ESP = [os.path.join(TL, "include"), TL, os.path.join(ESP, "tests", "host_stubs")]

# 用例：名字 → (哪一端, include 目录, [(源文件键或绝对路径)])
TARGETS = {
    "h743_dir": ("h743", INC_H743, [os.path.join(PT, "tests", "test_link_dir.c"), "hcomm", "hproto"]),
    "esp_dir": ("esp", INC_ESP, [os.path.join(ESP, "tests", "test_link_dir.c"),
                                  "ltask", "ldispatch", "lregistry", "ldemo", "lselftest",
                                  "lsend_esp", "lstatus", "ecomm", "eproto"]),
    "h743_send": ("h743", INC_H743, [os.path.join(PT, "tests", "test_link_send.c"), "lsend", "hcomm", "hproto"]),
}
# 被变异的文件 → 要跑的用例
RUN_FOR = {"proto": ["h743_dir", "esp_dir"], "hcomm": ["h743_dir"], "ecomm": ["esp_dir"],
           "ltask": ["esp_dir"], "ldispatch": ["esp_dir"], "lsend_esp": ["esp_dir"],
           "lsend": ["h743_send"]}
# 头文件变异时那棵"最小协议树"里各键的相对位置
TREE = {
    "proto": "protocol/tcs_proto.h", "layout": "protocol/proto_layout.h",
    "hwrap": "project_tcs/App/tcs/tcs_proto.h", "ewrap": "esp32/components/tcs_link/tcs_proto.h",
    "hproto": "project_tcs/App/proto/proto.c", "eproto": "esp32/components/tcs_link/proto.c",
}
# 头文件变异时，各端要前置的 include 目录（放包装文件的那一层）与换掉的 proto.c
TREE_FRONT = {"h743": ("project_tcs/App/tcs", "hproto"), "esp": ("esp32/components/tcs_link", "eproto")}

GATE_CALL = "    if (comm_rx_gate(L, payload, plen)) return 0;\n"
DEDUP_MARK = "    /* 去重：同 msg_type 同 seq = 重复。"
CRC_LINE = "            L->tx_queue[t][len - 1] = proto_crc8(&L->tx_queue[t][2], len - 3);\n"


def _comm(which, mid, desc, edits):
    """两端 comm.c 的同一个变异（各算一个变异体）：说明里注明是哪一端。"""
    return (mid, desc + "（" + ("屏端" if which == "hcomm" else "主控端") + " comm.c）", which, edits)


# (编号, 说明, 文件键, [(原文, 改成), ...])
MUTANTS = [
    ("D1", "方向取反：把自己的帧当成对端的、对端的当成回声", "proto",
     [("    if (sender == self) return TCS_RX_ECHO;\n", "    if (sender != self) return TCS_RX_ECHO;\n")]),
    ("D2", "只对参数帧判方向，ACK 回声不判", "proto",
     [("    if (sender == self) return TCS_RX_ECHO;\n",
       "    if (sender == self && tcs_frame_mt(mt_wire) == MT_TCS_PARAM) return TCS_RX_ECHO;\n")]),
    ("D3", "回声判定附带长度条件（按长度过滤的残留）", "proto",
     [("    if (sender == self) return TCS_RX_ECHO;\n", "    if (sender == self && plen == 9u) return TCS_RX_ECHO;\n")]),
    ("D4", "方向表把 0x11 改成双向（主控重新接受屏方向的 0x11 当写入）", "proto",
     [("#define MT_TCS_PARAM_DIR           TCS_DIR_ESP32_ONLY",
       "#define MT_TCS_PARAM_DIR           TCS_DIR_BOTH")]),
    ("D5", "方向表把 0x12 改成双向（屏重新接受主控方向的 0x12）", "proto",
     [("#define MT_TCS_SET_PARAM_DIR       TCS_DIR_H743_ONLY",
       "#define MT_TCS_SET_PARAM_DIR       TCS_DIR_BOTH")]),
    ("D6", "去掉 ACK 被确认消息号的核对", "proto",
     [("        if (plen >= 4u && !tcs_mt_sent_by(tcs_frame_mt(payload[3]), self))\n", "        if (0)\n")]),
    _comm("hcomm", "C1", "去掉方向判定（闸门一帧都不丢）", [(GATE_CALL, "")]),
    _comm("ecomm", "C1", "去掉方向判定（闸门一帧都不丢）", [(GATE_CALL, "")]),
    _comm("hcomm", "C2", "丢弃挪到 ACK 处理之后", [(GATE_CALL, ""), (DEDUP_MARK, GATE_CALL + DEDUP_MARK)]),
    _comm("ecomm", "C2", "丢弃挪到 ACK 处理之后", [(GATE_CALL, ""), (DEDUP_MARK, GATE_CALL + DEDUP_MARK)]),
    _comm("hcomm", "C3", "发送不打方向位", [("        if (mt_wire != frame[3]) {\n", "        if (0) {\n")]),
    _comm("ecomm", "C3", "发送不打方向位", [("        if (mt_wire != frame[3]) {\n", "        if (0) {\n")]),
    _comm("hcomm", "C4", "打了方向位但不重算 CRC", [(CRC_LINE, "")]),
    _comm("ecomm", "C4", "打了方向位但不重算 CRC", [(CRC_LINE, "")]),
    _comm("hcomm", "C5", "交给上层的消息号不去方向位", [("    out[0] = mt;\n", "")]),
    # 主控端没有 C5 的对应变异：那是等价变异。主控接受的帧都来自屏，屏的帧 bit6 恒为 0，
    # "去掉方向位再交付"在主控端没有可观察的差异（屏端接受的帧可以带 bit6=1，所以屏端的 C5 被杀）。
    ("L1", "镜像帧改用 0x12 发（主控发了只有屏会发的消息号）", "lsend_esp",
     [("    size_t fn = link_frame_build(MT_TCS_PARAM, comm_next_seq(link_ctx()), pl, n, frame, sizeof(frame));",
       "    size_t fn = link_frame_build(MT_TCS_SET_PARAM, comm_next_seq(link_ctx()), pl, n, frame, sizeof(frame));")]),
    ("L2", "主控的本端身份写成屏（方向位极性反）", "ltask",
     [("              link_crit_enter, link_crit_exit, TCS_SIDE_ESP32);",
       "              link_crit_enter, link_crit_exit, TCS_SIDE_H743);")]),
    ("L3", "任务循环里按长度丢帧（发送窗过滤的残留写法）", "ltask",
     [("                        s_rx_frames++;\n                        link_dispatch_frame(pl, plen);",
       "                        if (plen == 9) continue;   /* 变异 */\n                        s_rx_frames++;\n                        link_dispatch_frame(pl, plen);")]),
    ("S1", "屏写参数退回变更即推（不等 ACK）", "lsend",
     [("    if (comm_send(L, frame, n, MT_TCS_SET_PARAM, NULL) != 0) return -1;",
       "    comm_txq_push(L, frame, n); return 0;")]),
    ("S2", "广播钩子对 M207 参数重复发", "lsend",
     [("    if (flags & PF_M207) return 0;", "    if (0) return 0;")]),
    # —— M04-F-04 / M04-F-06 / X03-F-09（同属链路层，放在同一个脚本里）——
    ("S3", "序号 255 仍被当成失败（旧 comm_send 的语义）", "lsend",
     [("    if (comm_send(L, frame, n, MT_TCS_SET_PARAM, NULL) != 0) return -1;",
       "    { uint8_t s_ = 0xFF; int r_ = comm_send(L, frame, n, MT_TCS_SET_PARAM, &s_); "
       "if (r_ != 0 || s_ == 0xFF) return -1; }")]),
    _comm("hcomm", "X1", "发送失败照样出队（不留在队头重试）",
          [("        if (++L->tx_fail_streak < COMM_TX_MAX_FAIL) return;\n", "        if (0) return;\n")]),
    _comm("hcomm", "K1", "对端的非 ACK 帧不恢复 link_ok", [("    comm_note_peer(L);\n", "")]),
    _comm("ecomm", "K1", "对端的非 ACK 帧不恢复 link_ok", [("    comm_note_peer(L);\n", "")]),
]


def win(p):
    """给 cmd 用的 Windows 路径（正斜杠会被 cl 当成开关）。"""
    return os.path.abspath(p).replace("/", "\\")


_TOOLS = []


def msvc_tools():
    """vcvars64 之后的 (环境变量, cl.exe 绝对路径)，整个脚本只取一次。
    之后每个变异体直接起 cl，不必每次 call vcvars64.bat（那一步每个变异体要 2 秒多，
    20 个变异体就是一分多钟）。取不到返回 None，调用方退回"每次生成 bat"的方式。"""
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


def mutated(key, edits):
    body = TEXT[key]
    for old, new in edits:
        assert body.count(old) == 1, (key, old[:50], body.count(old))
        body = body.replace(old, new)
    return body


def prepare(tag, key, edits):
    """未变异（key=None）或带一个变异，准备工作目录。
    返回 (目录, {端: 前置 include 目录}, {键: 用工作目录里的副本替代原文件})。"""
    d = os.path.join(WORK, tag)
    if os.path.isdir(d):
        shutil.rmtree(d)
    os.makedirs(d)
    front, swapped = {}, {}
    if key == "proto":
        for k, rel in TREE.items():
            p = os.path.join(d, rel.replace("/", os.sep))
            os.makedirs(os.path.dirname(p), exist_ok=True)
            io.open(p, "w", encoding="utf-8", newline="").write(mutated(k, edits) if k == key else TEXT[k])
        for side, (inc, proto_key) in TREE_FRONT.items():
            front[side] = [os.path.join(d, inc.replace("/", os.sep))]
            swapped[proto_key] = os.path.join(d, TREE[proto_key].replace("/", os.sep))
    elif key is not None:
        p = os.path.join(d, os.path.basename(SRC[key]))
        io.open(p, "w", encoding="utf-8", newline="").write(mutated(key, edits))
        swapped[key] = p
    return d, front, swapped


def compile_and_run(tag, key, edits):
    """返回 (状态, 输出)：状态 PASS / FAIL / COMPILE_FAIL。FAIL = 至少一个用例变红。"""
    names = list(TARGETS) if key is None else RUN_FOR[key]
    d, front, swapped = prepare(tag, key, edits)
    exes, cmds, argl = {}, [], {}
    for name in names:
        side, incs, srcs = TARGETS[name]
        real = [swapped.get(s, SRC.get(s, s)) for s in srcs]
        exe = os.path.join(d, "t_%s.exe" % name)
        exes[name] = exe
        inc = front.get(side, []) + incs
        if GCC:
            cmds.append([GCC, "-std=c11", "-Wall", "-Wextra", "-DTCS_UNIT_TEST"]
                        + ["-I" + p for p in inc] + real + ["-o", exe])
        else:
            argl[name] = (FLAGS.split() + ["/I" + win(p) for p in inc]
                          + [win(s) for s in real] + ["/Fe:" + win(exe)])
            cmds.append("cl " + FLAGS + " " + " ".join('/I"%s"' % win(p) for p in inc) + " " +
                        " ".join('"%s"' % win(s) for s in real) + ' /Fe:"%s"' % win(exe))
    if GCC:
        out = []
        for name, c in zip(names, cmds):
            r = subprocess.run(c, capture_output=True, cwd=d)
            if r.returncode != 0:
                io.open(os.path.join(d, name + ".cc.log"), "wb").write(r.stdout + r.stderr)
                return "COMPILE_FAIL", name
            t = subprocess.run([exes[name]], capture_output=True, cwd=d)
            o = t.stdout.decode("utf-8", "replace")
            out.append("== %s ==\n%s" % (name, o.strip()))
        text = "\n".join(out)
        return ("PASS" if all("FAIL=0" in o for o in out) else "FAIL"), text
    if not os.path.isfile(VCVARS):
        print("[mut-link] FAIL：PATH 里没有 gcc，也找不到 vcvars64.bat（%s）；可用环境变量 TCS_VCVARS64 指定" % VCVARS)
        sys.exit(1)
    tools = msvc_tools()
    if tools:
        env, cl = tools                          # 快路径：直接起 cl（见 msvc_tools）
        for name in names:
            r = subprocess.run([cl] + argl[name], cwd=d, env=env, capture_output=True)
            if r.returncode != 0:
                io.open(os.path.join(d, name + ".cl.log"), "wb").write(r.stdout + r.stderr)
                return "COMPILE_FAIL", name
        out, bad = [], False
        for name in names:
            t = subprocess.run([exes[name]], capture_output=True, cwd=d)
            out.append("== %s ==\n%s" % (name, t.stdout.decode("utf-8", "replace").strip()))
            bad = bad or t.returncode != 0
        return ("PASS" if not bad else "FAIL"), "\n".join(out)
    lines = ["@echo off", 'call "%s" >nul 2>&1' % VCVARS, 'cd /d "%s"' % win(d), "set RC=0"]
    for c in cmds:
        lines += [c, "if errorlevel 1 exit /b 100"]
    for name in names:
        lines += ['echo == %s ==' % name, '"%s"' % win(exes[name]), "if errorlevel 1 set RC=1"]
    lines.append("exit /b %RC%")
    bat = os.path.join(d, "mut_cl.bat")   # 不能叫 cl.bat（见 mutation_estop_gate.py 的说明）；末尾必须有换行
    io.open(bat, "w", encoding="ascii", newline="").write("\r\n".join(lines) + "\r\n")
    r = subprocess.run(["cmd", "/c", win(bat)], capture_output=True, cwd=d)
    text = r.stdout.decode("utf-8", "replace")
    if r.returncode == 100:
        io.open(os.path.join(d, "cl.log"), "wb").write(r.stdout + r.stderr)
        return "COMPILE_FAIL", text
    return ("PASS" if r.returncode == 0 else "FAIL"), text


def killed_by(out):
    """哪些用例因为哪条断言变红（每个用例取第一条 FAIL）。"""
    res, name = [], None
    for ln in out.splitlines():
        if ln.startswith("== ") and ln.endswith(" =="):
            name = ln[3:-3]
        elif name and ln.startswith("FAIL:") and not any(r.startswith(name) for r in res):
            res.append("%s ← %s" % (name, ln[6:60]))
    return res


def main():
    verbose = "--verbose" in sys.argv
    base, info = compile_and_run("base", None, None)
    if base != "PASS":
        print("[mut-link] FAIL：未变异的实现自己就没过（%s）" % base)
        if base != "COMPILE_FAIL":
            print(info)
        return 1
    print("未变异：%s（%s）" % (base, "；".join(ln.strip() for ln in info.splitlines() if ": PASS=" in ln)))
    bad = 0
    for mid, desc, key, edits in MUTANTS:
        stale = [old for old, new in edits if TEXT[key].count(old) != 1]
        if stale:
            print("  %-3s 锚点匹配次数不是 1——实现改了，清单要跟着改：%r" % (mid, stale[0][:50]))
            bad += 1
            continue
        res, out = compile_and_run(mid, key, edits)
        if res == "FAIL":
            print("  %-3s 被杀  %s  ← %s" % (mid, desc, "；".join(killed_by(out))))
        elif res == "COMPILE_FAIL":
            print("  %-3s 编译失败（见 %s）  %s" % (mid, os.path.join(WORK, mid, "cl.log"), desc))
            bad += 1
        else:
            print("  %-3s 活下来  %s" % (mid, desc))
            bad += 1
        if verbose and res == "FAIL":
            print(out.rstrip())
    print("mutation_link_dir: %d 个变异体，%d 个异常（活下来 / 编译失败 / 锚点过期）" % (len(MUTANTS), bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
