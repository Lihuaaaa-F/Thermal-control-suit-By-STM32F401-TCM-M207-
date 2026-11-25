#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mutation_control.py — control.c 判别性验证：旧实现 + 变异必须被单测抓红

两种证明，缺一不可：
  1. 旧实现：把**U 线基线版本**的 control.c 配上现在的 tests/test_control_esp32.c 编译运行，
     新增的 W2 用例必须有足够多条失败（否则测试没有判别力，旧代码照样过 = 假绿）。
     基线固定是 d061041（U 线开工前的最后一个提交）——它是这批 W2 用例的对照物，
     以后不会随分支前进而变；要对照别的版本用 --old 指定路径。
  2. 变异：对现行 control.c 逐个施加"改坏一处"的变异（见 MUTATIONS），每个变异
     都必须让单测变红。没被抓住 = 该路径缺用例，或者这处代码与另一道防线等价。
     等价的必须在表里以"等价"开头注明理由，并且配一个"把等价的几道防线一起删掉"的
     变异（D 开头），后者必须被抓住——否则"等价"只是没测到的托词。

用法（在 esp32/ 根目录）：python tests/mutation_control.py [--old <旧 control.c 路径>]
编译器与 tests/run_m207_tests.sh 相同（MSVC cl + vcvars64）。
退出码 0 = 全部按预期（旧实现红、每个非等价变异红、原版绿）。
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
BASELINE = "d061041"

_A = "if (!g_tcs.m207_online || g_tcs.m207_sensor_open\n"
_B = "            || (s_safety_latch & (uint8_t)~SAFETY_BIT_BODY_LOST)) {"
_SAFE_CHECK = _A + _B                      # SAFE 期间逐拍检查（三条件）
_RESTORE = "if (s_restore_tec2_ok && s_exit_by_recover) out_tec_sw(2, s_tec2_before_safe);"

# 每项：(名称, [(原文, 替换文), ...], 备注)。每段原文必须在 control.c 里恰好出现一次。
# 备注以"等价"开头 = 预期存活（与别的防御等价），否则必须被单测抓红。
MUTATIONS = [
    ("M01 手动模式退出 SAFE 风扇一律关",
     [("out_fan((s_auto_before_safe && !auto_now) ? 0u : s_fan_before_safe);", "out_fan(0);")], ""),
    ("M02 退出 SAFE 不复原 TEC2",
     [(_RESTORE, ";")], ""),
    ("M03 模块掉线不作废 TEC2 复原",
     [(_SAFE_CHECK, "if (g_tcs.m207_sensor_open\n" + _B)], ""),
    ("M04 重申间隔跟节拍变密",
     [("(uint32_t)(now_ms - s_safe_assert_ms) >= CTRL_SAFE_REASSERT_MS) {",
       "(uint32_t)(now_ms - s_safe_assert_ms) >= 100u) {")], ""),
    ("M05 急停不再区分自动被切手动",
     [("out_fan((s_auto_before_safe && !auto_now) ? 0u : s_fan_before_safe);",
       "out_fan((0 && !auto_now) ? 0u : s_fan_before_safe);")], ""),
    ("M06 不记忆故障前模式",
     [("s_auto_before_safe = (uint8_t)(g_tcs.mode != 0);", "s_auto_before_safe = 1;")], ""),
    ("M07 每次重申都重记故障前输出",
     [("    if (s_state != CTRL_SAFE) safe_remember_outputs();\n    safe_outputs();",
       "    safe_remember_outputs();\n    safe_outputs();")], ""),
    ("M08 退出后不强制当拍评估",
     [("    s_last_eval = now_ms - CTRL_EVAL_MS;      /* 让评估节拍闸放行：当拍就评估 */\n    goto_state(CTRL_IDLE);",
       "    goto_state(CTRL_IDLE);")], ""),
    ("M09 上电首拍不重申",
     [("        s_safe_assert_ms = now_ms - CTRL_SAFE_REASSERT_MS;",
       "        s_safe_assert_ms = now_ms;")], ""),
    ("M10 急停不记忆故障前输出（直接进 SAFE）",
     [("    if (s_state != CTRL_SAFE) safe_remember_outputs();\n    s_restore_tec2_ok = 0;",
       "    s_restore_tec2_ok = 0;")], ""),
    ("M11 自动模式退出 SAFE 复原故障前风扇（P1-1 回归）",
     [("        out_fan(0);                 /* 风扇归状态机管：先归零，本函数末尾当拍评估 */",
       "        out_fan(s_fan_before_safe);")], ""),
    ("M12 自动模式退出 SAFE 不当拍评估",
     [("    if (auto_now) auto_evaluate(now_ms);", "    ;")], ""),
    ("M13 人工确认退出也复原 TEC2（P1-3）",
     [(_RESTORE, "if (s_restore_tec2_ok) out_tec_sw(2, s_tec2_before_safe);")], ""),
    ("M14 TEC2 复原恒为开",
     [(_RESTORE, "if (s_restore_tec2_ok && s_exit_by_recover) out_tec_sw(2, 1);")], ""),
    ("M15 TEC2 故障前值采样恒为开",
     [("s_tec2_before_safe = g_tcs.tec2_on;", "s_tec2_before_safe = 1;")], ""),
    ("M16 SAFE 期间探头断线不作废 TEC2 复原",
     [(_SAFE_CHECK, "if (!g_tcs.m207_online\n" + _B)], ""),
    ("M17 自愈时间 2000ms 改 1900ms",
     [("#define COMM_AUTO_RECOVER_MS   2000u", "#define COMM_AUTO_RECOVER_MS   1900u")], ""),
    ("M18 自愈时间 2000ms 改 2100ms",
     [("#define COMM_AUTO_RECOVER_MS   2000u", "#define COMM_AUTO_RECOVER_MS   2100u")], ""),
    ("M19 毫秒换算回到 32 位截断（P1-2）",
     [("return (uint32_t)((uint64_t)us / 1000u);",
       "return (uint32_t)((int32_t)us / 1000);")], ""),
    # ---- 2026-11-24 网页复核后补：命令来源、锁存期间禁切自动 ----
    ("M23 急停/超温锁存期间可以切自动（绕过'复位后单独启动'）",
     [("    if (id == PARAM_ID_MODE && v != 0.0f\n"
       "        && (s_safety_latch & (uint8_t)(SAFETY_BIT_ESTOP | SAFETY_BIT_OVERTEMP))) return -2;",
       "    if (0) return -2;")], ""),
    ("M24 通信类锁存也拦着不让切自动（过严）",
     [("        && (s_safety_latch & (uint8_t)(SAFETY_BIT_ESTOP | SAFETY_BIT_OVERTEMP))) return -2;",
       "        && s_safety_latch) return -2;")], ""),
    ("M25 网页切模式一律记成'控制台'",
     [("    case TCS_CMD_MODE_AUTO:     rv = param_set(PARAM_ID_MODE, 1.0f, src); return rv < 0 ? rv : 0;",
       "    case TCS_CMD_MODE_AUTO:     rv = param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_CONSOLE); return rv < 0 ? rv : 0;")], ""),
    ("M26 急停强制切手动一律记成'控制台'",
     [("    case TCS_CMD_ESTOP:         tcs_emergency_stop_src(src); return 0;",
       "    case TCS_CMD_ESTOP:         tcs_emergency_stop_src(PARAM_SRC_CONSOLE); return 0;")], ""),
    # ---- 以下三处各有两道防线，单删一处与另一处等价；D01 把两道一起删，必须被抓住 ----
    ("M20 置位时统一作废 TEC2 复原资格",
     [("    if (bit & (uint8_t)~SAFETY_BIT_BODY_LOST) s_restore_tec2_ok = 0;", "    ;")],
     "等价：SAFE 期间的逐拍检查（M21 的那段）还会作废；两道一起删见 D01"),
    ("M21 SAFE 期间逐拍检查不再看\"别的锁存位\"",
     [(_SAFE_CHECK, "if (!g_tcs.m207_online || g_tcs.m207_sensor_open) {")],
     "等价：safety_trip 置位时已统一作废（M20 的那段）；两道一起删见 D01"),
    ("M22 急停路径不再显式作废 TEC2 复原",
     [("    s_restore_tec2_ok = 0;                  /* 急停不复原 TEC（safety_trip 已作废，这里再写一次表明意图）*/\n", "")],
     "等价：tcs_emergency_stop 先调 safety_trip(ESTOP)，那里已作废"),
    ("D01 置位不作废 + 逐拍检查不看别的锁存位（两道防线一起删）",
     [("    if (bit & (uint8_t)~SAFETY_BIT_BODY_LOST) s_restore_tec2_ok = 0;", "    ;"),
      (_SAFE_CHECK, "if (!g_tcs.m207_online || g_tcs.m207_sensor_open) {")], ""),
    # ---- 2026-11-25 S 线：未布防时体温源不计入"解除急停 / 退出 SAFE"（真机发现：交付固件上急停解除不了）----
    ("M27 未布防时解除急停仍要求体温源恢复（回到修前的口径）",
     [("    need = (uint8_t)(commissioned() ? SAFETY_SENSOR_MASK\n"
       "                                    : (SAFETY_SENSOR_MASK & (uint8_t)~SAFETY_BIT_BODY_LOST));",
       "    need = (uint8_t)SAFETY_SENSOR_MASK;")], ""),
    ("M28 未布防时退出 SAFE 仍要求体温源恢复",
     [("    if (commissioned() && !g_tcs.body_sensor_ok) return 0;",
       "    if (!g_tcs.body_sensor_ok) return 0;")], ""),
    ("M29 已布防时解除急停不再要求体温源",
     [("    need = (uint8_t)(commissioned() ? SAFETY_SENSOR_MASK\n"
       "                                    : (SAFETY_SENSOR_MASK & (uint8_t)~SAFETY_BIT_BODY_LOST));",
       "    need = (uint8_t)(!commissioned() ? SAFETY_SENSOR_MASK\n"
       "                                    : (SAFETY_SENSOR_MASK & (uint8_t)~SAFETY_BIT_BODY_LOST));")], ""),
    ("M30 退出 SAFE 时的布防判断写反（未布防才要求体温源）",
     [("    if (commissioned() && !g_tcs.body_sensor_ok) return 0;",
       "    if (!commissioned() && !g_tcs.body_sensor_ok) return 0;")], ""),
    # ---- 2026-11-25 S 线 e3a：读数卡死 → SAFE（M09-F-09 / X04-F-06）。用例 K1–K17 在 test_control_esp32.c ----
    ("M31 卡死窗口无限长（永远判不出卡死）",
     [("#define CTRL_STUCK_WINDOW_MS    30000u", "#define CTRL_STUCK_WINDOW_MS    0xFFFFFFFFu")], ""),
    ("M32 卡死窗口 30000ms 改 30100ms",
     [("#define CTRL_STUCK_WINDOW_MS    30000u", "#define CTRL_STUCK_WINDOW_MS    30100u")], ""),
    ("M33 体温卡死受布防门控（未布防就不判）",
     [("        body_stuck = (uint8_t)(g_tcs.mode != 0 && (s_state == CTRL_HEAT || s_state == CTRL_COOL)",
       "        body_stuck = (uint8_t)(commissioned() && g_tcs.mode != 0 && (s_state == CTRL_HEAT || s_state == CTRL_COOL)")], ""),
    ("M34 水温卡死不看 TEC 是否开着",
     [("        tc1_stuck  = (uint8_t)(g_tcs.tec1_on && stuck_is(&s_stuck_tc1, now_ms, s_tec_on_since[0]));",
       "        tc1_stuck  = (uint8_t)(m207_src_ok && stuck_is(&s_stuck_tc1, now_ms, now_ms - CTRL_STUCK_WINDOW_MS));")], ""),
    ("M35 卡死窗口不把 TEC 开沿算在内（读数冻够了立刻判）",
     [("    if (!c->last_change || stuck_live(c, now_ms) || !on_since) return 0;",
       "    if (!c->last_change || stuck_live(c, now_ms)) return 0;"),
      ("    return (uint8_t)((uint32_t)(now_ms - on_since) >= CTRL_STUCK_WINDOW_MS);",
       "    (void)on_since; return 1;")], ""),
    ("M36 源不健康期间不清卡死样本（陈旧读数被当成卡死）",
     [("        c->last_change = 0;", "        ;")], ""),
    ("M37 STUCK 自愈不看读数是否恢复变化（还冻着也 2s 自清）",
     [("                          | (stuck_recovered(now_ms, stuck_chn) ? SAFETY_BIT_STUCK : 0u));",
       "                          | SAFETY_BIT_STUCK);")], ""),
    ("M38 ack 清掉卡死借用的报警位（设备停在 SAFE 而屏上说不出原因）",
     [("    if (!(lat & SAFETY_BIT_BODY_LOST) && !(stuck_chn & 1u) && g_tcs.body_sensor_ok)",
       "    if (!(lat & SAFETY_BIT_BODY_LOST) && g_tcs.body_sensor_ok)")], ""),
    ("M39 ack 不看读数是否恢复变化就清 STUCK",
     [("        safety_set_ok(SAFETY_BIT_STUCK, stuck_recovered(s_poll_now_ms, stuck_chn));",
       "        safety_set_ok(SAFETY_BIT_STUCK, 1);")], ""),
    ("M40 STUCK 位落盘",
     [("#define SAFETY_PERSIST_MASK     ((uint8_t)~SAFETY_BIT_STUCK)",
       "#define SAFETY_PERSIST_MASK     (0xFFu)")], ""),
    ("M41 上电不丢弃存储里留着的 STUCK 位",
     [("        s_safety_latch = (uint8_t)(lat & SAFETY_PERSIST_MASK);",
       "        s_safety_latch = lat;")], ""),
    ("M42 体温卡死不分模式（手动模式下任何 TEC 开着就判）",
     [("        body_stuck = (uint8_t)(g_tcs.mode != 0 && (s_state == CTRL_HEAT || s_state == CTRL_COOL)",
       "        body_stuck = (uint8_t)((g_tcs.tec1_on || g_tcs.tec2_on)"),
      ("                               && stuck_is(&s_stuck_body, now_ms, s_tec_on_since[0]));",
       "                               && stuck_is(&s_stuck_body, now_ms, now_ms - CTRL_STUCK_WINDOW_MS));")], ""),
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


def compile_run(control_src_text):
    d = tempfile.mkdtemp(prefix="tcs_mut_")
    c = os.path.join(d, "control.c")
    with open(c, "w", encoding="utf-8", newline="\n") as f:
        f.write(control_src_text)
    return run_cl(d, c)


def fails_of(out):
    return re.findall(r"^FAIL: (.+?) \(line", out, flags=re.M)


def main():
    cur_path = os.path.join(CORE, "control.c")
    with open(cur_path, encoding="utf-8") as f:
        cur = f.read()

    bad = 0

    # 0) 原版必须绿
    ok, out, rc = compile_run(cur)
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
                              BASELINE + ":esp32/components/tcs_core/control.c"],
                             capture_output=True, text=True, encoding="utf-8").stdout
    # 基线里没有 U 线之后新增的这几个函数：补上"什么都不改"的实现只为让测试链接得过。
    # W2-21 测的是 control_ms_from_us 本身、W2-22/23 测的是新加的行为，它们在旧实现上绿是正常的。
    if "control_ms_from_us" not in old:
        old += ("\n/* mutation_control.py: 基线没有这些函数，为链接补上 */\n"
                "uint32_t control_ms_from_us(int64_t us) { return (uint32_t)((uint64_t)us / 1000u); }\n"
                "int control_param_check(uint16_t id, float v) { (void)id; (void)v; return 0; }\n"
                "int control_apply_cmd_src(uint8_t cmd, uint8_t src) { (void)src; return control_apply_cmd(cmd); }\n"
                "void tcs_emergency_stop_src(uint8_t src) { (void)src; tcs_emergency_stop(); }\n")
    ok, out, rc = compile_run(old)
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

    # 2) 变异必须红（注明"等价"的允许存活）
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
            print("[mut] %-44s SKIP: 原文出现 %d 次（变异表过期）: %r" % (name, stale[1], stale[0][:50]))
            bad += 1
            continue
        ok, out, rc = compile_run(mutated)
        verbose = bool(os.environ.get("MUT_VERBOSE"))
        equivalent = note.startswith("等价")
        if ok and rc != 0:
            fl = fails_of(out)
            print("[mut] %-44s CAUGHT by %d checks, e.g. %s" % (name, len(fl), fl[0] if fl else "?"))
            if verbose:
                print(out[-800:])
        elif ok:
            if equivalent:
                print("[mut] %-44s survives as expected (%s)" % (name, note))
            else:
                print("[mut] %-44s SURVIVED —— 没有任何用例抓到" % name)
                bad += 1
        else:
            print("[mut] %-44s COMPILE FAIL" % name)
            print(out[-800:])
            bad += 1

    print("mutation_control: %s" % ("ALL AS EXPECTED" if not bad else "HAS PROBLEMS (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
