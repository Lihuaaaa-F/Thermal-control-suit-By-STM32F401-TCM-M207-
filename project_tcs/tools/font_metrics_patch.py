#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""font_metrics_patch.py — 把 LVGL 字库里的 ASCII/符号字形从"全角等宽"改成按墨迹的比例步进

背景（U 线 S1，2026-11-24）：24px / 32px 四张表的每个字形步进都被写成 1 个字号宽
（lv24 adv=24px ×265、lv32 adv=32px ×138），于是屏上出现 `5 . 0`、`° C`、`○停止` 这种
被撑开、墨迹偏在格子左边的样子；界面代码又按"字数 × 字号"估算宽度做居中，偏差叠加。

本脚本只改**度量**（glyph_dsc 的 adv_w / ofs_x），不动任何位图：
  - 汉字与按全角排版的符号（— … ℃ 等）：步进不变；
  - 数字 0–9：统一表列宽 = 最宽数字墨迹 + 两侧边距（表格数字，变化时不抖）；
  - 其余 ASCII 与 ° ± · ‹ › × –：步进 = 墨迹宽 + 两侧边距，下限 1/4 字宽；
  - 空格：0.28 字宽；
  - ofs_x 同步改成"墨迹在步进里左右居中"。
可重复执行（结果只取决于位图墨迹，与当前度量无关）。

用法（在 project_tcs/ 下）：
  python tools/font_metrics_patch.py            # 改写四张表
  python tools/font_metrics_patch.py --check    # 只校验：与期望不一致 → 退出码 1（门禁用）
  python tools/font_metrics_patch.py <font.c>   # 只处理指定文件
重生成字库后必须重跑（regen_ext_fonts.py 末尾已串接；--check 已挂进字体门禁）。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FONTS = ROOT / "App" / "lvgl_ui" / "fonts"
DEFAULT = ["tcs_font_lv24.c", "tcs_font_lv32.c", "tcs_font_lv_ext.c", "tcs_font_lv_ext32.c"]

# 按全角排版的符号：在中文语境里它们本来就是一个字宽
KEEP_FULL = {0x2014, 0x2026, 0x2018, 0x2019, 0x201C, 0x201D, 0x2103, 0x00A0}
# 汉字 / 假名 / 全角形区的码位范围（保持全角）
CJK_RANGES = [(0x2E80, 0x9FFF), (0xF900, 0xFAFF), (0xFE30, 0xFE4F), (0xFF00, 0xFFEF)]
# 汉字之外、也要按墨迹收窄的符号
NARROW_SYMBOLS = {0x00B0, 0x00B1, 0x00B7, 0x00D7, 0x2013, 0x2039, 0x203A}
MARK = "font_metrics_patch.py"

DSC_RE = re.compile(
    r"\{\s*\.bitmap_index\s*=\s*(\d+)\s*,\s*\.adv_w\s*=\s*(\d+)\s*,\s*\.box_w\s*=\s*(\d+)\s*,"
    r"\s*\.box_h\s*=\s*(\d+)\s*,\s*\.ofs_x\s*=\s*(-?\d+)\s*,\s*\.ofs_y\s*=\s*(-?\d+)\s*\}")


def is_cjk(cp):
    return any(lo <= cp <= hi for lo, hi in CJK_RANGES)


def classify(cp, em):
    """返回 (类别, 目标步进上限说明)。类别：keep / digit / narrow / space"""
    if cp == 0x20:
        return "space"
    if cp in KEEP_FULL or is_cjk(cp) or cp >= 0x3000 or (0xE000 <= cp <= 0xF8FF):
        return "keep"
    if 0x30 <= cp <= 0x39:
        return "digit"
    if 0x21 <= cp <= 0x7E or cp in NARROW_SYMBOLS:
        return "narrow"
    return "keep"          # 其余（拉丁扩展等）没有把握：保持原样


def parse_int_list(text, decl):
    """取 `name[] = {…};` 数组字面量里的整数（必须是数组声明，不能是结构体里的同名字段引用）"""
    m = re.search(r"\b" + re.escape(decl) + r"\s*\[\s*\]\s*=\s*\{(.*?)\};", text, flags=re.S)
    if not m:
        raise SystemExit("找不到数组声明 " + decl)
    body = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return [int(x, 0) for x in re.findall(r"\b(0x[0-9a-fA-F]+|\d+)\b", body)]


def parse_font(text):
    rs = re.search(r"\.range_start\s*=\s*(\d+)", text)
    if not rs:
        raise SystemExit("找不到 range_start")
    if "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY" not in text:
        raise SystemExit("只支持 SPARSE_TINY 字符映射（本仓库四张表都是）")
    base = int(rs.group(1))
    return {
        "range_start": base,
        # SPARSE_TINY：第 k 个码点 = range_start + unicode_list_0[k]，对应字形下标 k+1
        "cps": [base + v for v in parse_int_list(text, "unicode_list_0")],
        "bitmap": parse_int_list(text, "glyph_bitmap"),
        "dsc": [tuple(int(v) for v in m.groups()) for m in DSC_RE.finditer(text)],
    }


def ink_of(font, row):
    """字形墨迹的左右列（盒内坐标，右开区间）。返回 (left, right)，无墨迹返回 None。"""
    bidx, _adv, bw, bh, _ox, _oy = row
    if bw <= 0 or bh <= 0:
        return None
    rb = (bw + 7) // 8
    left, right = None, None
    for y in range(bh):
        base = bidx + y * rb
        rowbits = font["bitmap"][base:base + rb]
        for x in range(bw):
            if (rowbits[x >> 3] >> (7 - (x & 7))) & 1:
                if left is None or x < left:
                    left = x
                if right is None or x + 1 > right:
                    right = x + 1
    return (left, right) if left is not None else None


def target_metrics(font):
    """算出每个字形（按 dsc 下标）的新 (adv_px, ofs_x)。返回 {dsc_index: (adv_w_16, ofs_x)}。"""
    cps, dsc = font["cps"], font["dsc"]
    if len(cps) + 1 != len(dsc):
        raise SystemExit("码点数 %d 与字形数 %d 对不上（下标 0 是空字形）" % (len(cps), len(dsc) - 1))
    em = dsc[1][2] if dsc[1][2] > 0 else dsc[1][1] // 16
    pad = max(1, em // 24)
    ink = [ink_of(font, r) for r in dsc]
    widths = [0 if i is None else i[1] - i[0] for i in ink]

    # 自检：码点→字形的对应是否可信（对应错了，下面所有度量都是乱点鸳鸯谱）
    def w(ch):
        return widths[1 + cps.index(ord(ch))] if ord(ch) in cps else None
    if "M" in "".join(map(chr, cps)) and "." in "".join(map(chr, cps)):
        if not (w("M") > w(".") and w(".") <= em // 3):
            raise SystemExit("自检失败：M 与 . 的墨迹宽度不合常理（%s vs %s），码点表与字形表可能错位"
                             % (w("M"), w(".")))
    digits = [widths[1 + i] for i, cp in enumerate(cps) if 0x30 <= cp <= 0x39]
    if digits:
        tab = max(digits) + 2 * pad
        if not (em * 0.35 <= tab <= em * 0.8):
            raise SystemExit("自检失败：数字表列宽 %.1f 超出字号 %.1f 的 35%%–80%%" % (tab, em))
    else:
        tab = None

    out = {}
    for i, cp in enumerate(cps):
        k = classify(cp, em)
        row = dsc[1 + i]
        adv = row[1] / 16.0
        if k == "space":
            new = round(em * 0.28)
        elif k == "digit":
            new = tab
        elif k == "narrow":
            if ink[1 + i] is None:
                continue                      # 没墨迹的字形不动
            new = max(em // 4, widths[1 + i] + 2 * pad)
        else:
            continue
        if ink[1 + i] is None:
            ofs = row[4]
        else:
            ofs = int(round((new - widths[1 + i]) / 2.0)) - ink[1 + i][0]
        out[1 + i] = (int(round(new * 16)), ofs)
    return out


def apply(text, targets):
    """按出现顺序把新度量写回 glyph_dsc（第 n 个匹配 = dsc 下标 n）。"""
    n = -1
    def sub(m):
        nonlocal n
        n += 1
        if n not in targets:
            return m.group(0)
        adv, ox = targets[n]
        return ("{.bitmap_index = %s, .adv_w = %d, .box_w = %s, .box_h = %s, .ofs_x = %d, .ofs_y = %s}"
                % (m.group(1), adv, m.group(3), m.group(4), ox, m.group(6)))
    new = DSC_RE.sub(sub, text)
    if MARK not in new:
        # 文件头块注释**内部**（第一个 "*/" 之前）注明：度量由本脚本后处理，重生成后必须重跑
        k = new.find("*/")
        if k < 0:
            raise SystemExit(str(path) + "：找不到文件头块注释的结尾 */")
        new = new[:k] + " * 度量后处理：" + MARK + "（ASCII/符号按墨迹比例步进；重生成后必须重跑）\n " + new[k:]
    return new


def process(path, check):
    text = path.read_text(encoding="utf-8")
    font = parse_font(text)
    want = target_metrics(font)
    cur = {i: (r[1], r[4]) for i, r in enumerate(font["dsc"]) if i in want}
    diff = [i for i in want if want[i] != cur[i]]
    cps = font["cps"]
    sample = lambda s: sum(want.get(1 + cps.index(ord(c)), (font["dsc"][1 + cps.index(ord(c))][1],))[0]
                           for c in s if ord(c) in cps) / 16.0
    print("%-22s 字号 %2d  收窄 %3d 字  \"25.0\"=%5.1fpx \"°C\"=%5.1fpx \"M207\"=%5.1fpx  %s"
          % (path.name, font["dsc"][1][2], len(want), sample("25.0"), sample("°C"), sample("M207"),
             "已是期望" if not diff else ("需改 %d 字" % len(diff))))
    if diff and not check:
        path.write_text(apply(text, want), encoding="utf-8", newline="\n")
    return not diff


def main():
    args = sys.argv[1:]
    check = "--check" in args
    files = [FONTS / a for a in args if not a.startswith("--")] or [FONTS / a for a in DEFAULT]
    results = [process(p, check) for p in files]       # 不短路：四张表都要报
    ok = all(results)
    if not ok:
        print("font_metrics: " + ("FAIL（度量与期望不一致：重跑 python tools/font_metrics_patch.py）"
                                  if check else "已改写"))
    else:
        print("font_metrics: PASS")
    return 0 if ok or not check else 1


if __name__ == "__main__":
    sys.exit(main())
