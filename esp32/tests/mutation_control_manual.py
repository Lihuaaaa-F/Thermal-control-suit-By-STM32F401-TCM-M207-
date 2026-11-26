#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mutation_control_manual.py — m1"模式"页控制内核的判别性验证（变异体）

与 mutation_control.py 同款做法，对象换成 m1 批次新加的逻辑与它在共享文件里的调用点：
  - esp32/components/tcs_core/control_manual.c（两路手动输出映射、风扇联动、清零、写前校验）
  - esp32/components/tcs_core/link_loss_guard.c（屏链路断开 3 秒 = 急停 的纯判定）
  - control.c / param_registry.c 里 m1 的调用点与前置检查块
用例 = tests/test_control_manual.c（只编这一份；每个变异都必须让它变红）。
没被抓住 = 该路径缺用例。注明"等价"的允许存活（与另一道防线等价）。

control_service.c 里"判定 → 急停"那 10 行依赖 RTOS，主机编译不了，不在本脚本范围：
由 tests/test_m1_gates.py 逐字核对。

用法（在 esp32/ 根目录）：python tests/mutation_control_manual.py
编译器与 tests/run_m207_tests.sh 相同（MSVC cl + vcvars64）。退出码 0 = 全部按预期。
"""
import os
import re
import subprocess
import sys
import tempfile

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.dirname(HERE)
CORE = os.path.join(ESP, "components", "tcs_core")
LINK = os.path.join(ESP, "components", "tcs_link")
VARS64 = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
TEST = os.path.join(HERE, "test_control_manual.c")

CM = "control_manual.c"
LL = "link_loss_guard.c"
CT = "control.c"
PR = "param_registry.c"

# 每项：(文件, 名称, 原文, 替换文, 备注)。原文在该文件里必须恰好出现一次。
MUTATIONS = [
    # ---- control_manual.c ----
    (CM, "A01 制冷发的是制热目标", "(req == TEC_DIR_COOL) ? TEC_COOL_TARGET_C : TEC_HEAT_TARGET_C",
     "(req == TEC_DIR_COOL) ? TEC_HEAT_TARGET_C : TEC_COOL_TARGET_C", ""),
    (CM, "A02 每拍重发（不管参数变没变）", "        if (req != s_applied[i]) {", "        if (1) {", ""),
    (CM, "A03 暂停不发关", "            } else {\n                if (s_ops.tec_sw) s_ops.tec_sw((uint8_t)(i + 1u), 0);\n            }",
     "            } else {\n            }", ""),
    (CM, "A04 联动不强制开风扇", "        if (!g_tcs.fan_on && s_ops.fan_set) s_ops.fan_set(1);", "", ""),
    (CM, "A05 联动结束后风扇一律开着", "        if (s_ops.fan_set) s_ops.fan_set(s_fan_user);",
     "        if (s_ops.fan_set) s_ops.fan_set(1);", ""),
    (CM, "A06 不记联动之前用户的风扇选择",
     "            s_fan_user = (uint8_t)(g_tcs.fan_on ? 1u : 0u);", "            s_fan_user = 1;", ""),
    (CM, "A07 模块回读关断后参数不置回暂停（下次会被当成还在输出）",
     "        if (s_applied[i] != TEC_DIR_PAUSE && !tec_on(i)) {\n            if (param_get(pid_of(i)) != 0.0f)",
     "        if (s_applied[i] != TEC_DIR_PAUSE && !tec_on(i)) {\n            if (0)", ""),
    (CM, "A08 清零时不清参数",
     "        s_applied[i] = TEC_DIR_PAUSE;\n        if (param_get(pid_of(i)) != 0.0f)",
     "        s_applied[i] = TEC_DIR_PAUSE;\n        if (0)", ""),
    (CM, "A09 安全态下也放行手动输出", "    if (in_safe || latched) return PARAM_REJECT_SAFE;",
     "    if (0) return PARAM_REJECT_SAFE;", ""),
    (CM, "A10 非手动模式也放行手动输出", "    if (!manual_mode) return PARAM_REJECT_NOT_MANUAL;",
     "    if (0) return PARAM_REJECT_NOT_MANUAL;", ""),
    (CM, "A11 风扇偏好不看联动（解除后风扇回不到用户的选择）", "    if (s_fan_linked) return s_fan_user;", "", ""),
    (CM, "A12 已下发方向取错通道",
     "    return (ch == 1u || ch == 2u) ? s_applied[ch - 1u] : TEC_DIR_PAUSE;",
     "    return (ch == 1u || ch == 2u) ? s_applied[2u - ch] : TEC_DIR_PAUSE;", ""),
    # ---- link_loss_guard.c ----
    (LL, "B01 满不多不少 3 秒不触发（差一拍）", "    if (peer_age_ms < TCS_LINK_LOST_ESTOP_MS) {",
     "    if (peer_age_ms <= TCS_LINK_LOST_ESTOP_MS) {", ""),
    (LL, "B02 从未连上过也触发", "    if (!ever_connected) {", "    if (0) {", ""),
    # R8-04 之后 tripped 不再参与任何判定（“这次断开处理过了”不能挡住重新锁急停）：原来的 B03
    # （按 tripped 提前返回）就是 R8-04 的缺陷本体，反向由 B11 钉住，所以 B03 删除；B05 / B06 只剩
    # 诊断字段的读写，行为等价（理由写在备注里）。
    (LL, "B04 已在安全态照样触发", "    if (already_safe) {\n        g->tripped = 1;\n        return TCS_LINK_LOSS_NONE;\n    }",
     "    if (already_safe) {\n    }", ""),
    (LL, "B05 触发后不记已触发",
     "    g->tripped = 1;\n    return TCS_LINK_LOSS_ESTOP;",
     "    return TCS_LINK_LOSS_ESTOP;",
     "等价：tripped 只作诊断（R8-04 之后不参与判定）；少这一次赋值不改变任何返回/门控，下一拍 already_safe 分支也会把它置 1"),
    (LL, "B06 链路恢复后不重新武装",
     "    if (peer_age_ms < TCS_LINK_LOST_ESTOP_MS) {\n        g->tripped = 0;",
     "    if (peer_age_ms < TCS_LINK_LOST_ESTOP_MS) {\n        ;",
     "等价：tripped 不参与判定；链路恢复后的重新武装由 already_safe=0 决定（L01 / B09 / B11 覆盖）"),
    # ---- link_loss_guard.c：already_safe 只认**急停**位（r7f R7-01 + r8f R8-04）。
    #      用例 L01b / L04 / L05 在 test_control_manual.c。R8-04 之后超温不再算“已在安全态”
    #      （它可以在网页上确认掉），所以 B09 改成“恒为 1”，B10 改成“只认超温”，并新增 B11
    #      钉住“锁存一空、链路仍断 → 必须重新锁急停”。----
    (LL, "B07 already_safe 认任何安全态（回到修前：会自愈的安全态里断链不锁急停）",
     "    return (uint8_t)((safety_latched & (uint8_t)SAFETY_BIT_ESTOP) != 0u);",
     "    return (uint8_t)(safety_latched != 0u);", ""),
    (LL, "B08 already_safe 恒为 0（急停锁着时断链照样重复触发）",
     "    return (uint8_t)((safety_latched & (uint8_t)SAFETY_BIT_ESTOP) != 0u);",
     "    return 0;", ""),
    (LL, "B09 already_safe 恒为 1（断链永远不触发急停）",
     "    return (uint8_t)((safety_latched & (uint8_t)SAFETY_BIT_ESTOP) != 0u);",
     "    return 1;", ""),
    (LL, "B10 already_safe 只认超温（急停锁着时断链会再触发一次）",
     "    return (uint8_t)((safety_latched & (uint8_t)SAFETY_BIT_ESTOP) != 0u);",
     "    return (uint8_t)((safety_latched & (uint8_t)SAFETY_BIT_OVERTEMP) != 0u);", ""),
    (LL, "B11 记了“这次断开处理过”就永不重新武装（R8-04 回退：锁存被确认掉后屏仍断着，再也不锁急停）",
     "    /* 急停位已经在锁存里",
     "    if (g->tripped) return TCS_LINK_LOSS_NONE;\n    /* 急停位已经在锁存里", ""),
    # ---- control.c ----
    (CT, "C01 手动分支不驱动两路输出", "        control_manual_poll();\n", "", ""),
    (CT, "C02 进安全态不清零", "    control_manual_reset(0);\n}", "}", ""),
    (CT, "C03 急停不登记清零", "    s_manual_reset_req = 1;\n}\n\nvoid tcs_emergency_stop(void)",
     "}\n\nvoid tcs_emergency_stop(void)",
     "等价：急停后下一拍锁存块会进 enter_safe，那里同样清零（C02 把这道防线单独删掉必须被抓）"),
    (CT, "C04 切到自动不登记清零", "    if (m != 0u) s_manual_reset_req = 1;", "    if (0) s_manual_reset_req = 1;", ""),
    (CT, "C05 节拍顶不执行登记的清零", "        control_manual_reset(!(s_safety_latch || s_state == CTRL_SAFE));", "        ;", ""),
    (CT, "C06 写前校验对两路手动参数一律放行",
     "        return control_manual_check(g_tcs.mode == 0, s_state == CTRL_SAFE, s_safety_latch != 0);",
     "        return 0;", ""),
    (CT, "C07 输出方向不看状态机", "        if (s_state == CTRL_COOL) return TEC_DIR_COOL;",
     "        if (s_state == CTRL_HEAT) return TEC_DIR_COOL;", ""),
    (CT, "C08 通道关着也报方向", "    if (!on) return TEC_DIR_PAUSE;", "    if (0) return TEC_DIR_PAUSE;", ""),
    (CT, "C09 模式页设的 TEC2 也走自愈复原",
     "    if (control_manual_dir(2) != TEC_DIR_PAUSE) s_tec2_before_safe = 0;",
     "    if (0) s_tec2_before_safe = 0;", ""),
    (CT, "C10 记风扇时不取用户的选择", "    s_fan_before_safe  = control_manual_fan_pref();",
     "    s_fan_before_safe  = g_tcs.fan_on;", ""),
    # ---- control.c：水温卡死窗口 10 分钟（2026-11-26 真机实测之后改的；这里用 M13，自动模式的 K6 / K17 / K18 在
    #      test_control_esp32.c，对应的变异是 mutation_control.py 的 M52–M54）----
    (CT, "C11 水温长窗口那一行不生效（回到 30 秒就判）",
     "        tc1_stuck  = (uint8_t)(tc1_stuck && stuck_water_is(&s_stuck_tc1, now_ms));", "        ;", ""),
    (CT, "C12 水温卡死窗口无限长（永不判）",
     "#define CTRL_STUCK_WATER_WINDOW_MS 600000u", "#define CTRL_STUCK_WATER_WINDOW_MS 0xFFFFFFFFu", ""),
    # ---- S 线 i3（2026-11-26，P1-09 收口）：模块离线时快照的输出方向必须是暂停，不是上一次的方向（M16）----
    (CT, "C13 模块离线时快照仍报上一次的输出方向（屏会把它当成当前状态）",
     "    if (!(tec_known_now() & (uint8_t)(1u << (ch - 1u)))) return TEC_DIR_PAUSE;", "    ;", ""),
    # ---- param_registry.c ----
    (PR, "D01 越界不拒绝（退回钳制）",
     "        if (!(v >= p->min && v <= p->max) || v != (float)(uint8_t)v) return PARAM_REJECT_RANGE;",
     "        if (0) return PARAM_REJECT_RANGE;", ""),
    (PR, "D02 内核清回暂停也过校验（自动 / 安全态下清不掉）",
     "        !param_kernel_pause(p, v, src)) {", "        1) {", ""),
    (PR, "D03 内核清回暂停也过钩子",
     "!param_kernel_pause(p, c, src)\n        && ", "", ""),
    (PR, "D04 拒绝原因码不透传", "            return (rc <= -3) ? rc : -2;", "            return -2;", ""),
    (PR, "D05 上电默认不是暂停",
     "    0.0f, 0.0f,                        /* m1：0x09 / 0x0A 手动输出，上电一律暂停 */",
     "    1.0f, 2.0f,                        /* m1：0x09 / 0x0A 手动输出，上电一律暂停 */", ""),
    (PR, "D06 手动输出参数被持久化",
     "    { 0x09, \"tec1_manual\",  \"TEC1 手动输出\",   \"\",   PARAM_TYPE_U8,\n      PF_RW|PF_SCREEN,",
     "    { 0x09, \"tec1_manual\",  \"TEC1 手动输出\",   \"\",   PARAM_TYPE_U8,\n      PF_RW|PF_SCREEN|PF_PERSIST,", ""),
    # ---- control.c：自动模式 TEC 运行中风扇不许被关（r7f，独立复核 R7-03）。用例 M07b ----
    (CT, "E01 自动模式照样受理关风扇（回到修前）",
     "        if (g_tcs.fan_on && g_tcs.mode != 0 && (s_state == CTRL_COOL || s_state == CTRL_HEAT)) return -2;\n", "", ""),
    (CT, "E02 不再每拍重申\"自动模式 COOL / HEAT 下风扇开\"",
     "    if (!g_tcs.fan_on && g_tcs.mode != 0 && (s_state == CTRL_COOL || s_state == CTRL_HEAT)) out_fan(1);\n", "", ""),
]


def read_src(name):
    with open(os.path.join(CORE, name), encoding="utf-8") as f:
        return f.read().replace("\r\n", "\n")


def run_cl(work, srcs):
    """srcs = {文件名: 源码文本}（control_manual / link_loss_guard / control / param_registry 的变异副本）
    或 None（现行源）。返回 (编译成功, 运行输出, 运行返回码)"""
    paths = {}
    for name, text in (srcs or {}).items():
        p = os.path.join(work, name)
        with open(p, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
        paths[name] = p
    inc = [os.path.join(CORE), os.path.join(LINK, "include"), os.path.join(LINK)]
    files = [TEST,
             paths.get(CT, os.path.join(CORE, CT)),
             paths.get(CM, os.path.join(CORE, CM)),
             paths.get(LL, os.path.join(CORE, LL)),
             paths.get(PR, os.path.join(CORE, PR)),
             os.path.join(CORE, "post_pure.c"), os.path.join(LINK, "proto.c")]
    objs = " ".join('"%s"' % s for s in files)
    incs = " ".join('/I"%s"' % i for i in inc)
    lines = ["@echo off",
             'call "%s" >nul 2>&1' % VARS64,
             'cd /d "%s"' % work,
             "cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST %s %s /Fe:tcm.exe"
             % (incs, objs),
             "exit /b %ERRORLEVEL%"]
    bat = os.path.join(work, "cl_run.bat")     # 不能叫 cl.bat（会自我调用）
    with open(bat, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    cp = subprocess.run(["cmd", "/c", bat], capture_output=True, text=True, errors="replace", timeout=300)
    with open(os.path.join(work, "cl.log"), "w", encoding="utf-8", errors="replace") as f:
        f.write(cp.stdout + cp.stderr)
    if cp.returncode != 0:
        return False, cp.stdout + cp.stderr, -1
    rp = subprocess.run([os.path.join(work, "tcm.exe")], capture_output=True, text=True,
                        errors="replace", timeout=120, cwd=work)
    return True, rp.stdout + rp.stderr, rp.returncode


def fails_of(out):
    return re.findall(r"^FAIL: (.+?) \(line", out, flags=re.M)


def main():
    cur = {n: read_src(n) for n in (CM, LL, CT, PR)}
    bad = 0
    ok, out, rc = run_cl(tempfile.mkdtemp(prefix="tcs_m1_"), None)
    if not ok or rc != 0:
        print("[current] NOT GREEN (compile_ok=%s rc=%s)" % (ok, rc))
        print(out[-2000:])
        return 1
    m = re.search(r"PASS=(\d+) FAIL=(\d)", out)
    print("[current] PASS=%s FAIL=%s" % (m.group(1), m.group(2)))

    for fname, name, a, b, note in MUTATIONS:
        n = cur[fname].count(a)
        if n != 1:
            print("[mut] %-44s SKIP: 原文出现 %d 次（变异表过期）: %r" % (name, n, a[:50]))
            bad += 1
            continue
        ok, out, rc = run_cl(tempfile.mkdtemp(prefix="tcs_m1_"), {fname: cur[fname].replace(a, b)})
        equivalent = note.startswith("等价")
        if ok and rc != 0:
            fl = fails_of(out)
            print("[mut] %-44s CAUGHT by %d checks, e.g. %s" % (name, len(fl), fl[0] if fl else "?"))
        elif ok:
            if equivalent:
                print("[mut] %-44s survives as expected (%s)" % (name, note))
            else:
                print("[mut] %-44s SURVIVED —— 没有任何用例抓到" % name)
                bad += 1
        else:
            print("[mut] %-44s COMPILE FAIL" % name)
            print(out[-600:])
            bad += 1

    print("mutation_control_manual: %s" % ("ALL AS EXPECTED" if not bad else "HAS PROBLEMS (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
