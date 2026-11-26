#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mutation_ui_model.py — ui_model.c 判别性验证：逐个"改坏一处"，单测必须变红

ui_model.c 是新文件（没有旧实现可对照），所以判别性用变异证明：
把每个变异单独施加到 ui_model.c 上，配 tests/test_ui_model.c 编译运行，必须失败。
有变异存活 = 该路径缺用例（或变异与别的防御等价，需在下表注明"等价"）。

用法（在 project_tcs/ 下）：python tests/mutation_ui_model.py
编译器与 tests/run_all.sh 相同（MSVC cl + vcvars64）。退出码 0 = 全部按预期。
"""
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJ = HERE.parent
SRC = PROJ / "App" / "lvgl_ui" / "ui_model.c"
TEST = HERE / "test_ui_model.c"
VARS64 = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"

# (名称, 文件内唯一原文, 替换文)。原文必须在 ui_model.c 里恰好出现一次。
MUTATIONS = [
    ("M01 快照判活的 500ms 边界写成 <",
     "ever && in->snap_age_ms <= UI_LINK_STALE_MS",
     "ever && in->snap_age_ms < UI_LINK_STALE_MS"),
    ("M02 主控断开时不把三个源置为未知（沿用陈旧值）",
     "        v.module = UI_SRC_UNKNOWN;\n        v.body = UI_SRC_UNKNOWN;\n        v.down_ms",
     "        v.down_ms"),
    ("M03 不标记陈旧",
     "v.stale = (uint8_t)(v.link != UI_LINK_UP);",
     "v.stale = 0;"),
    ("M04 链路断开的横幅不再压过陈旧报警位",
     "    if (lv->link != UI_LINK_UP) {\n        char detail[80];",
     "    if (0) {\n        char detail[80];"),
    # ---- r7f（独立复核 R7-06，2026-11-26）：链路不新鲜时风扇入口也不可用。用例 test_ui_model.c 的 F4 / E ----
    ("R06-1 模式页风扇键不看链路（回到修前：链路断了照发 0x15）",
     "    if (s->link != UI_LINK_UP) return \"主控连接中断，现在不能操作\";\n    /* 只有\"想关\"才拦",
     "    /* 只有\"想关\"才拦"),
    ("R06-2 首页风扇卡片不看链路（回到修前：无条件发 0x15）",
     "    if (s->link != UI_LINK_UP) return \"主控连接中断，现在不能操作\";\n    return NULL;\n}",
     "    return NULL;\n}"),
    ("M05 通信类横幅画成红色",
     'set_banner(out, UI_BN_AUTO, 0, UI_BN_TAP_HINT, auto_title,',
     'set_banner(out, UI_BN_AUTO, 1, UI_BN_TAP_HINT, auto_title,'),
    ("M06 通信类横幅点击去发确认（源没好时必然被拒）",
     'set_banner(out, UI_BN_AUTO, 0, UI_BN_TAP_HINT, auto_title,',
     'set_banner(out, UI_BN_AUTO, 0, UI_BN_TAP_ACK, auto_title,'),
    ("M07 急停横幅的\"还有 N 条\"多算自己",
     '"TEC 已停 · 风扇全速 · 点击解除", extra);',
     '"TEC 已停 · 风扇全速 · 点击解除", (uint8_t)(extra + 1u));'),
    ("M08 通信类横幅的优先级错位（探头压过体温）",
     'if (a & ALM_BODY_SENSOR)      auto_title = "体温传感器失联";',
     'if (0)      auto_title = "体温传感器失联";'),
    ("M09 确认反馈的超时边界写错（差 1ms 就判没回话）",
     "if ((uint32_t)(in->now_ms - a->sent_ms) < UI_ACK_TIMEOUT_MS) return UI_ACK_WAIT;",
     "if ((uint32_t)(in->now_ms - a->sent_ms) <= UI_ACK_TIMEOUT_MS) return UI_ACK_WAIT;"),
    ("M10 部分解除时不带回残留位",
     "    if (still) *still = in->alarm;",
     "    if (still) *still = 0;"),
    ("M11 残留原因说错",
     'if (still & ALM_BODY_SENSOR) return "体温传感器仍失联";',
     'if (still & ALM_BODY_SENSOR) return "x";'),
    ("M12 主页差分漏掉风扇",
     "if (was->fan != now->fan)     m |= UI_H_FAN;",
     "if (was->fan != now->fan)     ;"),
    ("M13 主控断开时输出/模式/急停不归零（陈旧值上屏当实时）",
     "    if (lv->link != UI_LINK_UP) return;",
     "    if (0) return;"),
    ("M14 主控断开的灯仍是绿",
     '            *led = UI_LED_RED;\n            break;\n        }\n    }\n}\n\nvoid ui_status_module',
     '            *led = UI_LED_GREEN;\n            break;\n        }\n    }\n}\n\nvoid ui_status_module'),
    ("M15 主控断开时模块胶囊显示\"在线\"",
     'snprintf(text, cap, "模块未知");',
     'snprintf(text, cap, "模块在线");'),
    ("M16 去掉确认后的静置期（拿在途旧帧下结论）",
     "if ((uint32_t)(in->now_ms - a->sent_ms) < UI_ACK_SETTLE_MS) return UI_ACK_WAIT;",
     "if (0) return UI_ACK_WAIT;"),
    # ---- 2026-11-24 独立复核后补：急停文案须经主控确认；复核抽查出的存活变异 ----
    ("M17 急停位恒 0（急停键永远不变灰）",
     "    out->estop = in->estop_local ? 1u : 0u;",
     "    out->estop = 0;"),
    ("M18 主控断开时急停位归零（没送到也显示成红键）",
     "    out->estop = in->estop_local ? 1u : 0u;\n"
     "    /* 主控不在时，执行器/模式都是陈旧值：保持 0，屏幕按\"未知\"画，不做乐观显示 */\n"
     "    if (lv->link != UI_LINK_UP) return;",
     "    /* 主控不在时，执行器/模式都是陈旧值：保持 0，屏幕按\"未知\"画，不做乐观显示 */\n"
     "    if (lv->link != UI_LINK_UP) return;\n"
     "    out->estop = in->estop_local ? 1u : 0u;"),
    ("M19 急停没送到主控时不出专门横幅（被“连接中断”盖掉）",
     "        if (in->estop_local) {",
     "        if (0) {"),
    ("M20 主控没确认急停时也说“TEC 已停”",
     "        if (in->estop_peer && !in->tec1 && !in->tec2 && in->fan)",
     "        if (1)"),
    ("M21 不看快照里的风扇状态就写“风扇全速”",
     "        if (in->estop_peer && !in->tec1 && !in->tec2 && in->fan)",
     "        if (in->estop_peer)"),
    ("M22 TEC2 还开着也说“TEC 已停”",
     "        if (in->estop_peer && !in->tec1 && !in->tec2 && in->fan)",
     "        if (in->estop_peer && !in->tec1 && in->fan)"),
    ("M23 急停未确认的横幅点击去发确认",
     'set_banner(out, UI_BN_ESTOP_PENDING, 1, UI_BN_TAP_HINT, "急停指令已发出",',
     'set_banner(out, UI_BN_ESTOP_PENDING, 1, UI_BN_TAP_ACK, "急停指令已发出",'),
    ("M24 急停没送达的横幅点击去发确认",
     'set_banner(out, UI_BN_ESTOP_UNSENT, 1, UI_BN_TAP_HINT, "急停尚未送达主控", detail, 0);',
     'set_banner(out, UI_BN_ESTOP_UNSENT, 1, UI_BN_TAP_ACK, "急停尚未送达主控", detail, 0);'),
    ("M25 断开多久都只说秒（分钟分支不可达）",
     '            if (s < UI_DOWN_MIN_S)\n'
     '                snprintf(detail, sizeof detail, "数据停在 %us 前 · 自动重连中", (unsigned)s);',
     '            if (1)\n'
     '                snprintf(detail, sizeof detail, "数据停在 %us 前 · 自动重连中", (unsigned)s);'),
    ("M26 超温横幅的“还有 N 条”不算别的报警",
     '"已停止加热 · 降下来后点击解除", (uint8_t)(alarm_count(a) - 1u));',
     '"已停止加热 · 降下来后点击解除", 0);'),
    ("M27 点击后断链仍拿陈旧报警位下结论",
     "    if (in->snap_age_ms > UI_LINK_STALE_MS) {",
     "    if (0) {"),
    ("M28 放弃等待后仍在等",
     "void ui_ack_cancel(ui_ack_t *a)\n{\n    a->waiting = 0;",
     "void ui_ack_cancel(ui_ack_t *a)\n{\n    a->waiting = 1;"),
    ("M29 “主控未连接”的灯是绿",
     '            snprintf(text, cap, "主控未连接");\n            *led = UI_LED_RED;',
     '            snprintf(text, cap, "主控未连接");\n            *led = UI_LED_GREEN;'),
]


def compile_run(src_text):
    d = tempfile.mkdtemp(prefix="tcs_uimut_")
    c = os.path.join(d, "ui_model.c")
    with open(c, "w", encoding="utf-8", newline="\n") as f:
        f.write(src_text)
    bat = os.path.join(d, "run.bat")
    inc = [PROJ / "App" / "tcs", PROJ / "App" / "proto", PROJ / "App" / "comm",
           PROJ / "App" / "lvgl_ui", PROJ / "Core" / "Inc"]
    cmd = ("cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /DTCS_PROFILE=1 "
           + " ".join('/I"%s"' % p for p in inc)
           + ' "%s" "%s" /Fe:t.exe' % (TEST, c))
    with open(bat, "w", encoding="ascii", newline="\r\n") as f:
        f.write("@echo off\ncall \"%s\" >nul 2>&1\ncd /d \"%s\"\n%s\nexit /b %%ERRORLEVEL%%\n"
                % (VARS64, d, cmd))
    cp = subprocess.run(["cmd", "/c", bat], capture_output=True, text=True, errors="replace",
                        timeout=300)
    if cp.returncode != 0:
        return False, cp.stdout + cp.stderr, -1
    rp = subprocess.run([os.path.join(d, "t.exe")], capture_output=True, text=True,
                        errors="replace", timeout=120, cwd=d)
    return True, rp.stdout + rp.stderr, rp.returncode


def fails_of(out):
    """测试打印的每个失败项：CHECK 的"名称 (line N)"与 STREQ 的"名称 (line N): 得到 …"都算"""
    return [x.strip() for x in re.findall(r"^FAIL: (.+)$", out, flags=re.M)]


def main():
    cur = SRC.read_text(encoding="utf-8")
    ok, out, rc = compile_run(cur)
    m = re.search(r"PASS=(\d+) FAIL=(\d+)", out)
    if not ok or rc != 0:
        print("[current] NOT GREEN (compile_ok=%s rc=%s)" % (ok, rc))
        print(out[-1500:])
        return 1
    print("[current] PASS=%s FAIL=%s" % (m.group(1), m.group(2)))

    bad = 0
    for name, a, b in MUTATIONS:
        n = cur.count(a)
        if n != 1:
            print("[mut] %-44s SKIP: 原文出现 %d 次（变异表过期）" % (name, n))
            bad += 1
            continue
        ok, out, rc = compile_run(cur.replace(a, b))
        if ok and rc != 0:
            fl = fails_of(out)
            print("[mut] %-44s CAUGHT by %d checks, e.g. %s" % (name, len(fl), fl[0] if fl else "?"))
        elif ok:
            print("[mut] %-44s SURVIVED —— 没有任何用例抓到" % name)
            bad += 1
        else:
            print("[mut] %-44s COMPILE FAIL" % name)
            bad += 1
    print("mutation_ui_model: %s" % ("ALL AS EXPECTED" if not bad else "HAS PROBLEMS (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
