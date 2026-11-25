#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mutation_core.py — tcs_core 里 control.c 之外的纯逻辑：变异必须被单测抓红

control.c 的变异在 mutation_control.py；这里管本批次（S 线 e3a）新增 / 触及的纯逻辑：
  - boot_record.c  上次运行记录（X04-F-03 / R5-13）        用例 B1–B9
  - nvs_store.c    安全记录 v2 编解码（X04-F-03）          用例 N1–N4
每个变异 = 对一个源文件"改坏一处"，编译 tests/test_tcs_core.c 并运行，必须变红；
没被抓住 = 该路径缺用例。原版必须绿。锚点（原文）必须在文件里恰好出现一次。

用法（在 esp32/ 根目录）：python tests/mutation_core.py
编译器与 tests/run_m207_tests.sh 相同（MSVC cl + vcvars64）。
退出码 0 = 原版绿、每个变异都红。
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.dirname(HERE)
CORE = os.path.join(ESP, "components", "tcs_core")
VARS64 = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
TEST = os.path.join(HERE, "test_tcs_core.c")
SRC = ("boot_record.c", "nvs_store.c", "svc_status.c")

# 每项：(名称, 源文件名, [(原文, 替换文), ...])
MUTATIONS = [
    # ---- boot_record.c：上次运行记录 ----
    ("C01 不校验 CRC（只看魔数 / 版本 / 长度）", "boot_record.c",
     [("    return rec->crc == rec_crc(rec);", "    return 1;")]),
    ("C02 不校验版本", "boot_record.c",
     [("    if (rec->ver != BOOTREC_VER) return 0;", "    ;")]),
    ("C03 不校验记录长度", "boot_record.c",
     [("    if (rec->size != (uint16_t)sizeof(boot_record_t)) return 0;", "    ;")]),
    ("C04 不校验魔数", "boot_record.c",
     [("    if (rec->magic != BOOTREC_MAGIC) return 0;", "    ;")]),
    ("C05 四道校验一起删（随机内容当记录）", "boot_record.c",
     [("    if (rec->magic != BOOTREC_MAGIC) return 0;", "    ;"),
      ("    if (rec->ver != BOOTREC_VER) return 0;", "    ;"),
      ("    if (rec->size != (uint16_t)sizeof(boot_record_t)) return 0;", "    ;"),
      ("    return rec->crc == rec_crc(rec);", "    return 1;")]),
    ("C06 刷新后不重算 CRC（刷新即坏记录）", "boot_record.c",
     [("    rec->crc        = rec_crc(rec);", "    ;")]),
    ("C07 刷新不记运行时长", "boot_record.c",
     [("    rec->uptime_s   = uptime_s;", "    ;")]),
    ("C08 上一轮的记录不抄出（永远当作首次运行）", "boot_record.c",
     [("        prev->valid       = 1;", "        ;")]),
    ("C09 启动序号不递增", "boot_record.c",
     [("        seq = old.seq + 1u;", "        ;")]),
    ("C10 正常软复位也当成异常结束", "boot_record.c",
     [("    case BOOTREC_RST_SW:", "    case 255:")]),
    ("C11 PANIC 不算异常结束", "boot_record.c",
     [("    case BOOTREC_RST_POWERON:", "    case BOOTREC_RST_POWERON:\n    case BOOTREC_RST_PANIC:")]),
    ("C12 CRC 多项式走样", "boot_record.c",
     [("0xEDB88320u", "0xEDB88321u")]),
    ("C13 不保存本次启动的复位原因", "boot_record.c",
     [("    rec->reset_reason = reset_reason;", "    rec->reset_reason = 0;")]),
    ("C14 上一轮的报警位不抄出", "boot_record.c",
     [("        prev->alarm       = old.alarm;", "        ;")]),
    ("C15 上一轮的运行时长不抄出", "boot_record.c",
     [("        prev->uptime_s    = old.uptime_s;", "        ;")]),
    ("C16 status 的“上次”字段恒写成“没有记录”", "boot_record.c",
     [("    if (prev && prev->valid) {", "    if (0) {")]),
    ("C17 status 不带上一轮的运行时长", "boot_record.c",
     [("                     (int)prev->reset_reason, prev->uptime_s, (unsigned)prev->alarm,",
       "                     (int)prev->reset_reason, 0u, (unsigned)prev->alarm,")]),
    ("C18 status 不带上一轮的报警位", "boot_record.c",
     [("                     (unsigned)prev->latch, prev->seq, (unsigned)nvalm, (unsigned)core_dump);",
       "                     (unsigned)prev->latch, prev->seq, 0u, (unsigned)core_dump);")]),
    # ---- nvs_store.c：安全记录 v2 ----
    ("C20 报警位的反码不校验", "nvs_store.c",
     [("        if ((uint8_t)~lo != b[6] || (uint8_t)~hi != b[7]) return -1;", "        ;")]),
    ("C21 v2 记录解不出报警位", "nvs_store.c",
     [("        if (alarm) *alarm = (uint16_t)(lo | ((uint16_t)hi << 8));",
       "        if (alarm) *alarm = 0;")]),
    ("C22 坏记录被当成“无锁存”", "nvs_store.c",
     [("    if (latch) *latch = 0xFF;", "    if (latch) *latch = 0;")]),
    ("C23 不认 v1 老记录", "nvs_store.c",
     [("    if (b[1] == SAFETY_REC_VER1) {", "    if (0) {")]),
    ("C24 锁存的反码不校验", "nvs_store.c",
     [("    if ((uint8_t)~b[2] != b[3]) return -1;", "    ;")]),
    ("C25 截断的 v2 记录也认", "nvs_store.c",
     [("    if (b[1] != SAFETY_REC_VER2 || len < SAFETY_REC_SIZE) return -1;",
       "    if (b[1] != SAFETY_REC_VER2) return -1;")]),
    ("C26 报警位反码写错", "nvs_store.c",
     [("    out[6] = (uint8_t)~(alarm & 0xFFu);", "    out[6] = (uint8_t)(alarm & 0xFFu);")]),
    # ---- svc_status.c：服务启动结果汇总（P1-12）----
    ("C30 启动失败不置位", "svc_status.c",
     [("    if (rv != 0) s->failed |= bit;", "    if (0) s->failed |= bit;")]),
    ("C31 有失败也打成功行", "svc_status.c",
     [("        if (s->failed == 0 && s->unverified == 0)", "        if (1)")]),
    ("C32 报错行不说谁失败了", "svc_status.c",
     [("            svc_names(s->failed, names, sizeof names);", "            names[0] = 0;")]),
    ("C33 未核实项不提示", "svc_status.c",
     [("            log(arg, SVC_LOG_W, line);", "            (void)line;")]),
    ("C34 未核实项被当成失败", "svc_status.c",
     [("    s->unverified |= bit;", "    s->failed |= bit;")]),
    ("C35 成功行把未核实项也说成已起", "svc_status.c",
     [("        uint32_t up = s->noted & ~(s->failed | s->unverified);",
       "        uint32_t up = s->noted & ~s->failed;")]),
    ("C36 报告不返回失败位", "svc_status.c",
     [("    return s->failed;", "    return 0;")]),
    ("C37 名字表错位（m207 报成 ctrl）", "svc_status.c",
     [('    { SVC_BIT_M207,    "m207"    },', '    { SVC_BIT_M207,    "ctrl"    },')]),
    ("C38 坏位也被登记", "svc_status.c",
     [("    if (!s || !bit || (bit & (bit - 1u)) || (bit & ~SVC_BIT_ALL)) return -1;", "    if (!s) return -1;")]),
]


def compile_run(overrides):
    """overrides: {源文件名: 改坏后的全文}；返回 (编译成功, 运行输出, 运行返回码)"""
    work = tempfile.mkdtemp(prefix="tcs_mutcore_")
    srcs = []
    for name in SRC:
        if name in overrides:
            p = os.path.join(work, name)
            with open(p, "w", encoding="utf-8", newline="\n") as f:
                f.write(overrides[name])
            srcs.append(p)
        else:
            srcs.append(os.path.join(CORE, name))
    srcs.extend(EXTRA)
    srcs.insert(0, TEST)
    incs = ' /I"%s" /I"%s" /I"%s"' % (CORE, os.path.join(CORE, "include"),
                                       os.path.join(ESP, "components", "tcs_link"))
    objs = " ".join('"%s"' % s for s in srcs)
    bat = os.path.join(work, "mut_core.bat")
    with open(bat, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(["@echo off",
                           'call "%s" >nul 2>&1' % VARS64,
                           'cd /d "%s"' % work,
                           "cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST %s %s "
                           "/Fe:test_core.exe" % (incs, objs),
                           "exit /b %ERRORLEVEL%", ""]))
    cp = subprocess.run(["cmd", "/c", bat], capture_output=True, text=True,
                        errors="replace", timeout=300)
    log = cp.stdout + cp.stderr
    if cp.returncode != 0:
        return False, log, -1, work
    rp = subprocess.run([os.path.join(work, "test_core.exe")], capture_output=True,
                        text=True, errors="replace", timeout=120, cwd=work)
    return True, rp.stdout + rp.stderr, rp.returncode, work


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    except (AttributeError, ValueError):
        pass
    orig = {}
    for name in SRC:
        with open(os.path.join(CORE, name), encoding="utf-8") as f:
            orig[name] = f.read()

    ok, out, rc, work = compile_run({})
    shutil.rmtree(work, ignore_errors=True)
    m = re.search(r"PASS=(\d+) FAIL=(\d+)", out)
    if not ok or rc != 0:
        print("[current] NOT GREEN (compile_ok=%s rc=%s)" % (ok, rc))
        print(out[-2000:])
        return 1
    print("[current] PASS=%s FAIL=%s" % (m.group(1), m.group(2)))

    bad = 0
    for name, fname, edits in MUTATIONS:
        text = orig[fname]
        stale = None
        for a, b in edits:
            n = text.count(a)
            if n != 1:
                stale = (a, n)
                break
            text = text.replace(a, b)
        if stale:
            print("[mut] %-44s SKIP: 原文出现 %d 次（变异表过期）: %r" % (name, stale[1], stale[0][:60]))
            bad += 1
            continue
        ok, out, rc, work = compile_run({fname: text})
        shutil.rmtree(work, ignore_errors=True)
        fl = re.findall(r"^FAIL: (.+?) \(line", out, flags=re.M)
        if ok and rc != 0:
            print("[mut] %-44s CAUGHT by %d checks, e.g. %s" % (name, len(fl), fl[0] if fl else "?"))
        elif ok:
            print("[mut] %-44s SURVIVED —— 没有任何用例抓到" % name)
            bad += 1
        else:
            print("[mut] %-44s COMPILE FAIL" % name)
            print(out[-800:])
            bad += 1

    print("mutation_core: %s" % ("ALL AS EXPECTED" if not bad else "HAS PROBLEMS (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
