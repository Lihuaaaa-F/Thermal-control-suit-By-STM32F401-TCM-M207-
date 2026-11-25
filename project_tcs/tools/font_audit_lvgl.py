#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""font_audit_lvgl.py — LVGL 字体管线缺字门禁（G5⑥ / TL-18 的机器部分）

背景：既有 `font_audit.py` 只审计旧点阵库 `tcs_font_cn.inc`，**完全没覆盖 LVGL 字体表**
（lv20/lv24/lv32/lv48/lv96/ext/ext32）。因此"LVGL 管线 font_audit 全绿"此前无证据；
2026-10-26 曾因"换"字不在 ext 里出现豆腐块，即属这一类缺陷。

本门禁做三件事：
  A 字面量覆盖率：`App/lvgl_ui/*.c` 所有字符串字面量的字符，必须落在 24px 可达链
    （lv24 ∪ ext ∪ lv32 ∪ ext32）内——24px 是默认文本档，链尾覆盖全部 CJK。
  B 无 fallback 档位：lv20 / lv48 / lv96 的 fallback=NULL，渲染到缺字会直接空白；
    对这三档断言"数字/符号/单位/汉字"白名单必须齐全。
  C 诊断页字符集：`tui_charset_data.h` 的 7 组码点非空，且 24/32 两链覆盖全部 UI 字面量。
  D 字形度量（U 线 S1）：24/32px 字表的 ASCII/符号必须保持比例步进——重生成字库后忘跑
    `tools/font_metrics_patch.py` 会退回全角等宽（屏上出现 `5 . 0`），这里直接拦下。

退出码 0=PASS 1=FAIL（可直接入 run_all/nightly）。
"""
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI = ROOT / "App/lvgl_ui"
# 参数表也要扫（审查条目 M03-F-09）：参数的中文显示名与单位会上屏，此前只扫界面目录，
# 参数表里有 13 个字不在字库里而门禁报"缺 0 个"。两端各有一份注册表，都扫。
# H743 这份必须存在——它是本工程的文件；找不到就把门禁判红，不能静默少扫。
REGISTRY_H743 = ROOT / "App/tcs/param_registry.c"
REGISTRY_ESP32 = ROOT.parent / "esp32" / "components" / "tcs_core" / "param_registry.c"
FONTS = UI / "fonts"
CHARSET_H = UI / "tui_charset_data.h"

LIT_RE = re.compile(r'"((?:[^"\\]|\\.)*)"')


def strip_comments(t: str) -> str:
    t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
    t = re.sub(r"//[^\n]*", "", t)
    return t


def load_cp_list(p: Path):
    out = []
    for ln in p.read_text(encoding="utf-8", errors="ignore").splitlines():
        ln = ln.strip()
        if not ln:
            continue
        try:
            out.append(int(ln, 16))
        except ValueError:
            pass
    return set(out)


def call_args(txt, fname):
    """产出每个 fname(...) 调用的实参列表（已按最外层逗号切开；跳过字符串/字符字面量里的括号与逗号）"""
    pos = 0
    head = fname + "("
    while True:
        i = txt.find(head, pos)
        if i < 0:
            return
        k, depth, start, args = i + len(head), 0, i + len(head), []
        while k < len(txt):
            c = txt[k]
            if c == '"' or c == "'":
                q = c
                k += 1
                while k < len(txt) and txt[k] != q:
                    k += 2 if txt[k] == "\\" else 1
            elif c == "(":
                depth += 1
            elif c == ")":
                if depth == 0:
                    args.append(txt[start:k])
                    break
                depth -= 1
            elif c == "," and depth == 0:
                args.append(txt[start:k])
                start = k + 1
            k += 1
        yield args
        pos = k


def registry_files():
    """返回 (路径, 归因名)。H743 那份必须存在，由调用方判红；ESP32 那份是邻居工程，不在就只少扫它。"""
    out = [(REGISTRY_H743, "param_registry.c(H743)")]
    if REGISTRY_ESP32.exists():
        out.append((REGISTRY_ESP32, "param_registry.c(ESP32)"))
    return out


def ui_literals():
    lits = []
    for f, name in ([(f, f.name) for f in sorted(UI.glob("*.c"))] + registry_files()):
        if not f.exists():
            continue
        txt = strip_comments(f.read_text(encoding="utf-8", errors="ignore"))
        for m in LIT_RE.finditer(txt):
            s = m.group(1)
            if s:
                lits.append((name, s))
    return lits


def main():
    sets = {
        "lv20":  load_cp_list(FONTS / "tcs_font_lv20_cp.txt"),
        "lv24":  load_cp_list(FONTS / "tcs_font_lv24_cp.txt"),
        "ext24": load_cp_list(FONTS / "tcs_font_lv_ext_cp.txt"),
        "lv32":  load_cp_list(FONTS / "tcs_font_lv32_cp.txt"),
        "ext32": load_cp_list(FONTS / "tcs_font_lv_ext32_cp.txt"),
        "lv48":  load_cp_list(FONTS / "tcs_font_lv48_cp.txt"),
        "lv96":  load_cp_list(FONTS / "tcs_font_lv96_cp.txt"),
    }
    chain24 = sets["lv24"] | sets["ext24"] | sets["lv32"] | sets["ext32"]
    lits = ui_literals()
    used = set()
    for _, s in lits:
        used |= set(s)

    fails = []

    # A 字面量覆盖率（附逐文件归因，便于定位是哪段文案没进字库）
    # 扫描范围的最小样本断言：界面目录或 H743 参数表找不到 = 这道门禁等于没扫，直接判红
    n_ui = len(list(UI.glob("*.c")))
    scanned_reg = [n for _, n in registry_files()]
    print(f"[A] 扫描范围：界面 {n_ui} 个 .c；参数表 {len(scanned_reg)} 份（{'、'.join(scanned_reg) or '无'}）")
    if n_ui < 5:
        fails.append(("scope", ["界面目录 App/lvgl_ui/*.c 少于 5 个文件（%d）" % n_ui]))
    if not REGISTRY_H743.exists():
        fails.append(("scope", ["H743 参数表 App/tcs/param_registry.c 不存在，参数名/单位没有被扫到"]))
    missA = sorted(c for c in used if ord(c) > 0x20 and ord(c) not in chain24)
    print(f"[A] 字面量 {len(lits)} 条 / 字符 {len(used)} 个 → 24px 链缺 {len(missA)} 个")
    per = {}
    for fn, s in lits:
        m = [c for c in set(s) if ord(c) > 0x20 and ord(c) not in chain24]
        if m:
            per.setdefault(fn, set()).update(m)
    for fn, cs in sorted(per.items()):
        print(f"      {fn}: 缺 {len(cs)} → {''.join(sorted(cs))[:40]}")
    if missA:
        fails.append(("A", [(c, hex(ord(c))) for c in missA[:20]]))

    # B 无 fallback 档位白名单
    # 48/96 只用于数值大字（"--.-" / "36.8"），其字体表就是 12 个字符；
    # 单位 °C 走 32px 单独 label，不在本档
    need48 = set("0123456789.-")
    need96 = set("0123456789.-")
    for name, need in (("lv48", need48), ("lv96", need96)):
        miss = sorted(c for c in need if ord(c) not in sets[name])
        print(f"[B] {name}（fallback=NULL）缺 {len(miss)} 个: {''.join(miss) or '无'}")
        if miss:
            fails.append((name, miss))
    # lv20 是辅助文本档（fallback=NULL）：只审计真正传给 ui_aux20 的字面量
    aux = set()
    for f in sorted(UI.glob("*.c")):
        txt = strip_comments(f.read_text(encoding="utf-8", errors="ignore"))
        # 按实参解析：只认第 4 个实参**本身是字符串字面量**的调用。
        # （旧写法取"ui_aux20( 之后的第一对引号"——参数是变量时会误抓后面的无关字符串，
        #   2026-11-24 U 线把 "45°C 阈值" 误判成 20px 缺字。）
        for args in call_args(txt, "ui_aux20"):
            if len(args) >= 4:
                m = LIT_RE.match(args[3].strip())
                if m:
                    aux |= set(m.group(1))
    miss20 = sorted((c for c in aux if ord(c) > 0x20 and ord(c) not in sets["lv20"]),
                    key=lambda c: ord(c))
    print(f"[B] lv20（fallback=NULL）缺非 ASCII {len(miss20)} 个")
    if miss20:
        fails.append(("lv20", miss20[:20]))

    # C 诊断页字符集
    if not CHARSET_H.exists():
        fails.append(("charset", ["tui_charset_data.h 缺失"]))
        print("[C] tui_charset_data.h 缺失")
    else:
        t = CHARSET_H.read_text(encoding="utf-8")
        sizes = {k: int(v) for k, v in re.findall(r"#define TUI_(CP\w+)_N\s+(\d+)", t)}
        print(f"[C] 诊断页字符集：{sizes}")
        if not sizes or min(sizes.values()) == 0:
            fails.append(("charset", ["空集合"]))
        # 24px 链必须覆盖 UI 字面量（已由 A 保证），此处只校验定义存在
        if "CP24CORE" not in sizes or "CP32EXT" not in sizes:
            fails.append(("charset", ["关键数组缺失"]))

    # D 字形度量：tools/font_metrics_patch.py --check（与期望不一致 → 非零退出）
    r = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "font_metrics_patch.py"), "--check"],
        capture_output=True, text=True, encoding="utf-8", errors="replace")
    for ln in r.stdout.splitlines():
        print("      " + ln)
    if r.returncode != 0:
        # 两类失败分开报：度量没打补丁（重跑即可）与检查工具自己出错（先修工具，
        # 否则码点表错位之类的问题会被误报成"重跑补丁"）。
        if "font_metrics: FAIL" in r.stdout and not r.stderr.strip():
            fails.append(("D", ["字形度量不是比例步进：重跑 python tools/font_metrics_patch.py"]))
        else:
            fails.append(("D", ["font_metrics_patch.py --check 异常退出（rc=%d）：%s"
                                % (r.returncode, (r.stderr or r.stdout).strip()[-300:])]))

    print()
    if fails:
        print("FONT_AUDIT_LVGL: FAIL")
        for k, v in fails:
            print("  -", k, v)
        return 1
    print("FONT_AUDIT_LVGL: PASS — 字面量全覆盖，三档无 fallback 字体白名单齐全，字形度量为比例步进")
    return 0


if __name__ == "__main__":
    sys.exit(main())
