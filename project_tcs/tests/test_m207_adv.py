#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_m207_adv.py — WP-P4/P6 M207 高级参数注入矩阵（PC 层，驱动 m207_sim.M207Core）

任务书 v10.11 P4：
  注入测试矩阵 = 每参数五行 {设→读回一致；越界→拒绝且原值不变；保存→重启保持；
  未保存→重启回旧值；坏帧(缺帧尾/坏#校验/无冒号)→ERROR 不崩} 在 PC/sim 层全跑。
P6：并入 run_all（m207_adv 单测套件）。
工程质量三：golden 语料双实现一致性——同批协议帧喂 sim 与固件解析器逐条一致
（固件侧断言在 tests/test_m207_parse.c，语料文件共享 tests/m207_golden.csv）。

注：CR 用 chr(13) 构造，避免源文件内嵌控制字符。
"""
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "tools"))
import m207_sim  # noqa: E402

T0 = [0.0]
CR = chr(13)


def mkcore(state=None):
    T0[0] = 0.0
    return m207_sim.M207Core(state_file=state)


def h(c, line):
    T0[0] += 0.06   # 60ms 间隔，避开 50ms 铁律
    return c.handle_line(line, now=T0[0])


def rep0(lines):
    return lines[0] if lines else ""


FAIL = 0


def check(cond, msg):
    global FAIL
    if not cond:
        print("  FAIL:", msg)
        FAIL += 1


def main():
    print("[m207_adv] dict size:", len(m207_sim.DICT))
    check(len(m207_sim.DICT) >= 150, "dictionary >=150 params (OEM V6.2.0.1=209)")

    # ---------- 行1：设→读回一致（可写参数抽样 20，样本值 2.5 在全部 BOUNDS 内）----
    writable = [k for k, v in m207_sim.DICT.items() if v[1]]
    sample = [k for k in writable if k.startswith(("TC1:", "PCB:", "PCRS232:"))][:20]
    c = mkcore()
    for key in sample:
        r = rep0(h(c, key + "=2.5"))
        check(r == "CMD:REPLY=1" + CR, "set ok %s -> %r" % (key, r))
        r = rep0(h(c, key + "?"))
        check(r == key + "=2.5" + CR, "set-then-get %s -> %r" % (key, r))

    # ---------- 行2：越界→拒绝且原值不变 ----------
    c = mkcore()
    h(c, "TC1:TCADJTEMP=25")
    r = rep0(h(c, "TC1:TCADJTEMP=999"))
    check(r == "CMD:REPLY=4" + CR, "over-max rejected -> %r" % r)
    r = rep0(h(c, "TC1:TCADJTEMP?"))
    check(r == "TC1:TCADJTEMP=25" + CR, "value unchanged after reject -> %r" % r)
    r = rep0(h(c, "TC1:TCADJTEMP=-99"))
    check(r == "CMD:REPLY=4" + CR, "under-min rejected")
    # 动态值域：提高 MAX（150 在 TCMAXTEMP 自身 BOUNDS 内）后 140 变合法
    h(c, "TC1:TCMAXTEMP=150")
    r = rep0(h(c, "TC1:TCADJTEMP=140"))
    check(r == "CMD:REPLY=1" + CR, "dynamic range accepts after MAX raise -> %r" % r)
    # 显式 BOUNDS
    r = rep0(h(c, "TC1:TCMAXV=99"))
    check(r == "CMD:REPLY=4" + CR, "BOUNDS TCMAXV>12 rejected")

    # ---------- 行3/4：保存→重启保持；未保存→重启回旧值 ----------
    sf = tempfile.mktemp(suffix=".state")
    c = mkcore(sf)
    h(c, "TC1:TCADJTEMP=40")
    h(c, "TC1:TCADJTEMP!")            # 保存 40
    h(c, "TC1:TCADJTEMP=50")          # 改 50 不保存
    del c
    c = mkcore(sf)                    # "重启"
    r = rep0(h(c, "TC1:TCADJTEMP?"))
    check(r == "TC1:TCADJTEMP=40" + CR, "saved value survives restart -> %r" % r)
    os.remove(sf)

    sf = tempfile.mktemp(suffix=".state")
    c = mkcore(sf)
    h(c, "TC2:TCADJTEMP=30")
    h(c, "TC2:TCADJTEMP!")
    h(c, "TC2:TCSTEPTEMP=0.5")        # 不保存
    del c
    c = mkcore(sf)
    r = rep0(h(c, "TC2:TCADJTEMP?"))
    check(r == "TC2:TCADJTEMP=30" + CR, "saved survives (TC2) -> %r" % r)
    r = rep0(h(c, "TC2:TCSTEPTEMP?"))
    check(not r.endswith("0.5" + CR), "unsaved value lost after restart -> %r" % r)
    os.remove(sf)

    # ---------- 行5：坏帧 → ERROR 不崩 ----------
    c = mkcore()
    r = rep0(h(c, "TC1TCADJTEMP=25"))          # 无冒号
    check(r == "CMD:REPLY=6" + CR, "no-colon -> syntax 6 -> %r" % r)
    r = rep0(h(c, "TC1:TCSW=1@0#00"))          # 坏校验
    check(r == "CMD:REPLY=7" + CR, "bad checksum -> 7 -> %r" % r)
    _s = "TC1:TCSW=1#"
    r = rep0(h(c, _s + "%02X" % m207_sim.xor_checksum(_s)))
    check(r == "CMD:REPLY=6" + CR, "checksum without addr -> 6 -> %r" % r)
    r = h(c, "TC1:TCADJTEMP=25")
    check(rep0(r) == "CMD:REPLY=1" + CR, "still alive after bad frames")
    r = rep0(h(c, "TC1:TCADJTEMP^5"))          # ^ 禁用
    check(r == "CMD:REPLY=6" + CR, "CHANGING rejected -> 6")
    r = rep0(h(c, "TC1:TCADJTEMP!"))           # 保存可设参数 → 8
    check(r == "CMD:REPLY=8" + CR, "save ok -> 8")
    r = rep0(h(c, "TC1:TCACTTEMP!"))           # 保存只读参数 → 3
    check(r == "CMD:REPLY=3" + CR, "save read-only -> 3")
    r = h(c, "XX9:NOPE?")                       # 未知模块
    check(rep0(r) == "CMD:REPLY=0" + CR, "unknown module -> 0 -> %r" % rep0(r))
    r = rep0(h(c, "TC1:NOPE?"))
    check(r == "CMD:REPLY=2" + CR, "unknown param -> 2 -> %r" % r)
    r = h(c, "TC1:TCSW=1@3")                    # 地址不符
    check(r == [], "addr mismatch -> silence")

    # ---------- PDF 8.2 官方示例逐字节 ----------
    c = mkcore()
    T0[0] = 0.0
    seg = "TC1:TCSW=1@0#"
    r = c.handle_line(seg + "%02X" % m207_sim.xor_checksum(seg), now=0.0)
    check(r == ["CMD:REPLY=1@0#7D" + CR], "PDF 8.2 worked example -> %r" % r)

    # ---------- 50ms 铁律 ----------
    c = mkcore()
    r1 = c.handle_line("TC1:TCACTTEMP?", now=0.0)
    r2 = c.handle_line("TC1:TCACTTEMP?", now=0.01)   # 10ms 后：违反
    check(r1 and r2 == [], "50ms violation dropped")
    r3 = c.handle_line("TC1:TCACTTEMP?", now=0.2)
    check(r3, "recovers after 50ms")
    check(c.violations_50ms == 1 and c.drops_50ms == 1, "violation counted")

    # ---------- golden 语料双实现一致性 ----------
    golden_path = os.path.join(os.path.dirname(__file__), "m207_golden.csv")
    c = mkcore()
    T0[0] = 0.0
    mism = 0
    with open(golden_path, encoding="utf-8") as f:
        for ln in f:
            ln = ln.strip()
            if not ln or ln.startswith("#"):
                continue
            frame, expect = ln.split(",", 1)
            T0[0] += 0.06
            got = rep0(c.handle_line(frame, now=T0[0])).rstrip(CR)
            if got != expect:
                print("  GOLDEN MISMATCH:", frame, "expect", expect, "got", got)
                mism += 1
    check(mism == 0, "golden corpus %d mismatches" % mism)

    print("===================")
    if FAIL == 0:
        print("test_m207_adv: PASS")
        return 0
    print("test_m207_adv: FAIL (%d)" % FAIL)
    return 1


if __name__ == "__main__":
    sys.exit(main())
