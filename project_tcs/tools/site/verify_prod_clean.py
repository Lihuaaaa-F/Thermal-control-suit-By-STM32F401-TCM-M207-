#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""verify_prod_clean.py — 交付固件"干净"核对：PROD 档产物里不许出现的符号与命令名。

背景（S 线 C01，2026-11-24）：审查条目 M03-F-12 / X05-F-03~05 要求"交付的镜像和取证的镜像
不是同一个"。只把源码套上 #if 不算数——要拿产物说话：符号表里查不到钩子、只读数据段里查不到
台架命令名，才算隔离成功。

用法（路径随意）：
  python tools/site/verify_prod_clean.py --elf <H743 PROD 档 .elf>
  python tools/site/verify_prod_clean.py --esp32 <ESP32 默认档 app .bin>
  python tools/site/verify_prod_clean.py --elf ... --esp32 ...      两个都查
  反过来核对（证明本工具查得出来，build_matrix.sh 每次都做）：
    --expect-hooks            只对 --elf：台架/仿真档产物里这些钩子**必须在**
    --esp32-expect <键>       只对 --esp32：这条命令必须在这个档里（键见 ESP32_STRINGS），可重复
    --esp32-expect-all        只对 --esp32：全部台架命令都必须在（DIAG 档用）

退出码：0 通过 / 1 不符（查到不该有的，或该有的不在）/ 2 用法或文件错误。
只读：不写任何文件，不碰设备。
"""
import argparse
import os
import re
import struct
import subprocess
import sys

# ---- H743：PROD 档不该有的符号（arm-none-eabi-nm 全表里按"全名相等"查）----
# 清单只放"一定会有符号"的东西：会被内联的小函数、没有调用者而被链接器丢弃的函数不在此列。
H743_SYMBOLS = {
    "走查命令 g_ui_cmd（M03-F-12）": ["g_ui_cmd"],
    "诊断页 tui_diag（M03-F-12）": ["tui_diag_build", "g_diag_stat"],
    "走查钩子函数": ["tui_step_param", "tui_sel_next", "tui_slider_center"],
    "G4 测试钩子变量": ["g_lvgl_freeze", "g_lvgl_drain", "g_tap_inject", "g_tap_drop",
                        "g_estop_calls", "g_estop_dbg"],
    "屏链波形采集": ["link_wave_capture", "link_tx_wave_capture", "g_wave_ts", "g_wave_n",
                     "g_wave_lvl0", "g_wave_span", "g_txwave_n"],
    "接收字节环（rxraw 用）": ["g_dbg_rx_ring", "g_dbg_rx_pos"],
}
H743_SYMBOL_PREFIXES = {"LVGL demos": ["lv_demo_", "lvgl_demo"]}   # 只查"不得有"

# ---- H743：PROD 档不该有的控制台命令名。只查 .rodata 里 NUL 分隔的字符串字面量 ----
H743_CMDS = ["txwave", "pindrv", "pinrel", "pinscan", "wdttest", "rxraw", "wave"]

# ---- H743：PROD 档不该有的回复文本（S-G l1，2026-11-25：审查 C03-新发现、M01-F-05 残余）。
# 交付档上这些命令只回 "?"，实现体不编译，所以它们的回复文本 / 日志标签不得再出现在产物里。
# 按子串查；--expect-hooks 时反过来看它们在台架档里还在（证明本工具查得出来）。----
H743_STRINGS = {
    "p defaults（交付档没有本地参数真值，恢复默认只会动屏的镜像、保存必然失败）":
        ["defaults restored+saved", "confirm: p defaults"],
    "p save（交付档不做本地持久化，真值在主控）": ["save rv="],
    "ee（交付档没有本地 EEPROM，恒回 offline）": ["ee offline"],
    "TEMP-DIAG2 首帧取证日志（M01-F-05）：上电往控制台打 [R8] / [A1..A4]": ["[R8]", "[A1]"],
}

# ---- ESP32：键 → (说明, 帮助文本里独有的片段)。bin 里没有符号表，只能查文本 ----
ESP32_STRINGS = {
    "linkmute": ("链路静默；SIM 或 DIAG 档", ["linkmute"]),
    "linkloop": ("TX→RX 回环；SIM 或 DIAG 档", ["linkloop"]),
    "webtest": ("网页端点自检；SIM 或 DIAG 档", ["回环 GET"]),
    "linkrepin": ("重下发 UART 引脚映射；DIAG 档", ["linkrepin"]),
    "gedge": ("引脚边沿探测；DIAG 档", ["gedge"]),
    "ltx": ("屏链自检码型；DIAG 档", ["ltx: wrote"]),
    "m207raw": ("m207 raw 台架直查/注入；DIAG 档", ["台架直查"]),
    "mode": ("台架直接手动控制；DIAG 档", ["手动/自动；台架"]),
    "tec": ("台架直接驱动 TEC；DIAG 档", ["<on 0|1>（台架）"]),
    "fan": ("台架直接设定占空比；DIAG 档", ["占空比%，低=开；台架"]),
    "mstat": ("模块保护状态 + 读数抖动的只读命令；DIAG 或 SIM 档（S 线 i3）", ["读数抖动"]),
}

# ---- ESP32：只在 SIM 档编译的命令（DIAG 档没有）。默认档不得出现；--esp32-expect 可以点名要它，
#      --esp32-expect-all 不含它们（那是 DIAG 档的清单）。S 线 i3（2026-11-26）。----
ESP32_STRINGS_SIM = {
    "water": ("恒定水温注入（注入在生效的那一路卡死判据豁免）；SIM 档", ["恒定水温注入"]),
}

NM = "arm-none-eabi-nm"


def check_recorder(elf):
    """正向核对（S 线 i3，条目 9；e3a 第五节方案第 4 条）：交付档 ELF 里**必须有**故障记录器与状态迁移环，
    且都在 .noinit。现在的门禁只查"不该有的符号"——记录器被哪次清理误删、或从 .noinit 挪进 .bss，
    都不会被发现（上电即被清零，取证价值归零）。"""
    bad = []
    try:
        out = subprocess.run(["arm-none-eabi-objdump", "-t", elf], capture_output=True, text=True,
                             errors="replace").stdout
    except OSError as e:
        return ["  [记录器] 找不到 arm-none-eabi-objdump（%s）" % e]
    need = {"g_fault_rec": None, "g_trace_ring": None}
    for ln in out.splitlines():
        parts = ln.split()
        if len(parts) < 5:
            continue
        name = parts[-1]
        if name in need:
            need[name] = parts[-3]          # 形如：200xxxxx l  O .noinit  00000064 g_fault_rec
    for n, sec in need.items():
        if sec is None:
            bad.append("  [记录器] 符号 %s 不在交付档 ELF 里（被清理误删？）" % n)
        elif not sec.startswith(".noinit"):
            bad.append("  [记录器] %s 的段是 %s，必须在 .noinit（否则上电即被清零）" % (n, sec))
    if not bad:
        try:
            hdr = subprocess.run(["arm-none-eabi-objdump", "-h", elf], capture_output=True, text=True,
                                 errors="replace").stdout
        except OSError:
            hdr = ""
        # .noinit 必须是 NOBITS（不占镜像、启动不清零）：objdump -h 对 NOBITS 段的标志行只有 "ALLOC"，
        # 不会出现 CONTENTS / LOAD。有内容 = 被当普通数据段链接进去了（上电会被初值覆盖）。
        in_noinit = False
        for ln in hdr.splitlines():
            if ".noinit" in ln and len(ln.split()) >= 3 and ln.split()[0].rstrip(".").isdigit():
                in_noinit = True
                continue
            if in_noinit:
                if "CONTENTS" in ln or re.search(r"\bLOAD\b", ln):
                    bad.append("  [记录器] .noinit 段不是 NOBITS（标志行：%s）——上电会覆盖记录" % ln.strip()[:60])
                break
        else:
            if not in_noinit:
                bad.append("  [记录器] 交付档 ELF 里没有 .noinit 段")
    return bad


def h743_symbols(elf):
    try:
        out = subprocess.run([NM, elf], capture_output=True, text=True, errors="replace").stdout
    except OSError as e:
        sys.exit("找不到 %s（%s）——H743 交叉工具链要在 PATH 里" % (NM, e))
    return {ln.split()[-1] for ln in out.splitlines() if ln.strip()}


def elf_rodata_strings(path):
    """取 ELF 里名字以 .rodata 开头的所有段，按 NUL 切成字符串字面量集合。
    自己解析节头（ELF32 / ELF64 小端），不落临时文件，也不依赖 readelf。"""
    b = open(path, "rb").read()
    if b[:4] != b"\x7fELF":
        sys.exit("不是 ELF 文件：%s" % path)
    is64 = b[4] == 2
    if is64:
        e_shoff, = struct.unpack_from("<Q", b, 0x28)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", b, 0x3A)
        fmt = "<IIQQQQIIQQ"      # name type flags addr off size link info align entsize
    else:
        e_shoff, = struct.unpack_from("<I", b, 0x20)
        e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", b, 0x2E)
        fmt = "<IIIIIIIIII"
    fields = struct.calcsize(fmt)

    def shdr(i):
        return struct.unpack_from(fmt, b, e_shoff + i * e_shentsize)
    nameoff = shdr(e_shstrndx)[4]
    strs = set()
    for i in range(e_shnum):
        name, _typ, _flg, _addr, off, size = shdr(i)[:6]
        end = b.index(b"\x00", nameoff + name)
        nm = b[nameoff + name:end].decode("ascii", "replace")
        if not nm.startswith(".rodata"):
            continue
        for s in re.split(rb"\x00+", b[off:off + size]):
            if s:
                strs.add(s)
    if not strs:
        sys.exit("ELF 里没有找到 .rodata 段（节头解析有误？）")
    return strs


def check_h743(elf, expect):
    bad = []
    syms = h743_symbols(elf)
    for why, names in H743_SYMBOLS.items():
        for n in names:
            present = n in syms
            if present != expect:
                bad.append("  [%s] 符号 %s：%s" % (why, n, "在" if present else "不在"))
    if not expect:   # demos 只在显式要求调试场景时才编译，所以不做"必须在"的方向
        for why, prefixes in H743_SYMBOL_PREFIXES.items():
            hit = sorted(s for s in syms for p in prefixes if s.startswith(p))
            if hit:
                bad.append("  [%s] %d 个符号（如 %s）" % (why, len(hit), hit[0]))
    # 命令名用"整串相等或作为整串的结尾"匹配：链接器会把 "wave" 合并成 "txwave" 的尾部，
    # 字面量 "wave" 就不再单独存在。只读数据里不会有以这些词结尾的正当字符串。
    strs = elf_rodata_strings(elf)
    for c in H743_CMDS:
        present = any(t == c.encode() or t.endswith(c.encode()) for t in strs)
        if present != expect:
            bad.append("  [台架控制台命令] 命令名 \"%s\"：%s" % (c, "在" if present else "不在"))
    # 回复文本按子串查（S-G l1）：这些命令在交付档只回 "?"，回复文本本身就是实现体还在的证据。
    for why, needles in H743_STRINGS.items():
        hit = sorted(n for n in needles if any(n.encode() in t for t in strs))
        present = bool(hit)
        if present != expect:
            bad.append("  [%s] 回复文本 %s：%s" % (why, hit or needles, "在" if present else "不在"))
    return bad


def check_esp32(path, expect_keys):
    data = open(path, "rb").read()
    bad = []
    both = dict(ESP32_STRINGS)
    both.update(ESP32_STRINGS_SIM)          # SIM 档命令同样是"默认档不得有、点名才许有"
    for key, (why, needles) in both.items():
        present = any(n.encode("utf-8") in data for n in needles)
        if present != (key in expect_keys):
            bad.append("  [ESP32 %s（%s）]：%s" % (key, why, "在" if present else "不在"))
    return bad


def main():
    ap = argparse.ArgumentParser(description="交付固件干净核对（PROD 档不得含调试/台架钩子）")
    ap.add_argument("--elf", help="H743 .elf（arm-none-eabi-nm 取符号表 + 解析 .rodata）")
    ap.add_argument("--esp32", help="ESP32 app .bin（不要传 bootloader / partition-table）")
    ap.add_argument("--expect-hooks", action="store_true",
                    help="反过来核对（只对 --elf）：台架档产物里这些钩子必须在")
    ap.add_argument("--expect-recorder", action="store_true",
                    help="正向核对（只对 --elf）：故障记录器 g_fault_rec 与状态迁移环 g_trace_ring 必须在，"
                         "且都在 .noinit（S 线 i3，条目 9）")
    ap.add_argument("--esp32-expect", action="append", default=[], metavar="键",
                    help="这条 ESP32 命令必须在这个档里（可重复）：linkmute linkloop webtest "
                         "linkrepin gedge ltx m207raw mode tec fan")
    ap.add_argument("--esp32-expect-all", action="store_true",
                    help="全部 ESP32 台架命令都必须在这个档里（DIAG 档用）")
    a = ap.parse_args()
    if not a.elf and not a.esp32:
        ap.print_help()
        return 2
    for p in (a.elf, a.esp32):
        if p and not os.path.isfile(p):
            print("文件不存在：%s" % p)
            return 2
    unknown = [k for k in a.esp32_expect if k not in ESP32_STRINGS and k not in ESP32_STRINGS_SIM]
    if unknown:
        print("未知的 --esp32-expect 键：%s（可选：%s）"
              % (unknown, " ".join(list(ESP32_STRINGS) + list(ESP32_STRINGS_SIM))))
        return 2
    expect = bool(a.expect_hooks)
    expect_keys = set(ESP32_STRINGS) if a.esp32_expect_all else set(a.esp32_expect)
    bad = []
    if a.elf:
        bad += check_h743(a.elf, expect)
        if a.expect_recorder:
            bad += check_recorder(a.elf)
    if a.esp32:
        bad += check_esp32(a.esp32, expect_keys)
    what = "台架档产物必须含钩子" if expect else "交付（PROD）档产物不得含调试/台架钩子"
    if a.esp32:
        what += "；ESP32 命令集%s" % ("=全部台架命令" if a.esp32_expect_all else
                                    ("含 " + ",".join(sorted(expect_keys)) if expect_keys else "=无台架命令"))
    if bad:
        print("[prod-clean] FAIL（%s）" % what)
        print("\n".join(bad))
        return 1
    print("[prod-clean] PASS（%s）" % what)
    return 0


if __name__ == "__main__":
    sys.exit(main())
