#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_font_cn.py — 从 msyh.ttc 提取字形 → tcs_font_cn.inc（任务书 v5.1 WP-A.2a/WP-B）

字符集**自动收集**：扫描 UI 源码（App/tcs/tcs_ui.c）全部字符串字面量里的
非 ASCII 字符 + 可见 ASCII 95 个，UI 改文案后重跑本脚本即可，不再手工维护清单。
配套 tools/font_audit.py 永久门禁（run_all.sh）兜底缺字。

产出两档位（WP-B 字体层级）：
  - 24px：CJK + 全角符号 + ASCII（标题/正文/按钮）
  - 32px：ASCII95 + ° ℃（数值大字专用；UI 侧 2× 最近邻放大 = 64px 温度大字）
  - 16px 档沿用 Core/Inc/font8x16.inc（ASCII）；16px 中文由 24px 表 draw 到
    UI 自行处理——正文中文一律走 24px，16px 仅 ASCII（WP-B 布局按此设计）。

输出格式：1bpp 行优先 MSB 在左，24px 每字 72B / 32px 每字 128B；码表升序，UI 二分查找。
用法：python tools/gen_font_cn.py [输出路径，默认 Core/Inc/tcs_font_cn.inc]
依赖：Pillow（pip install pillow）。失败降级预案 R5：改用英文 key 显示（功能不受损）。
"""
import sys
import re
from pathlib import Path
from PIL import Image, ImageFont, ImageDraw

FONT = r"C:\Windows\Fonts\msyh.ttc"
ROOT = Path(__file__).resolve().parent.parent
# 上屏字符串来源：UI 实现 + 参数注册表（调参页渲染 p->name/unit——WP-A 实测漏洞）
UI_SOURCES = [ROOT / "App/tcs/tcs_ui.c", ROOT / "App/tcs/param_registry.c"]

# 24px 固定基础集：可见 ASCII（ui_text24/ui_text48 也走 24px 表渲染 ASCII）
# + '?' 兜底字形（缺字时 UI 回落画 '?'，本字必须恒在——H3 教训）
CHARSET_24_FIXED = "".join(chr(c) for c in range(0x20, 0x7F))

# 32px 数值档固定集：可见 ASCII + 度/摄氏符号
# + v9.5 K2：32px 中文（dock 标签/卡片标题/弹窗标题/枚举值，1.2"中文 24/32px"）
CHARSET_32_FIXED = ("".join(chr(c) for c in range(0x20, 0x7F)) + "°℃"
                    + "调参切手动自动连接急停贴身水箱环境体温风扇"
                      "确认？到模式设定超过阈值恢复默认参数强制布防不")

STR_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
ESCAPE_RE = re.compile(r'\\(?:x[0-9A-Fa-f]{1,2}|[0-7]{1,3}|u[0-9A-Fa-f]{4}|.)')



from c_strip import strip_c_comments


def collect_ui_chars():
    """扫 UI 源码字符串字面量 → 非 ASCII 码点集合（自动收集，取代手工清单）"""
    from font_audit import screen_only_registry   # 与门禁同一口径（PF_SCREEN 才上屏）
    chars = set()
    for src in UI_SOURCES:
        txt = src.read_text(encoding='utf-8')
        if src.name == "param_registry.c":
            txt = screen_only_registry(txt)   # M207 参数屏不显=D6 红线，不进字库
        txt = strip_c_comments(txt)   # 注释内引号短语不收集（2026-10-26 误报教训）
        for m in STR_RE.finditer(txt):
            raw = m.group(1)
            i = 0
            out = bytearray()
            while i < len(raw):
                if raw[i] == '\\':
                    mm = ESCAPE_RE.match(raw, i)
                    if not mm:
                        i += 1
                        continue
                    esc = mm.group(0)
                    i = mm.end()
                    if esc.startswith('\\x') or esc[1].isdigit():
                        try:
                            out.append(int(esc[1:], 16) & 0xFF)
                        except ValueError:
                            pass
                    elif esc.startswith('\\u'):
                        chars.add(int(esc[2:], 16))
                    elif esc in ('\\n', '\\t', '\\r'):
                        out.append(0x20)
                    elif esc in ('\\"', '\\\\', "\\'"):
                        out.append(ord(esc[-1]))
                else:
                    out.extend(raw[i].encode('utf-8'))
                    i += 1
            try:
                text = out.decode('utf-8')
            except UnicodeDecodeError:
                continue
            for ch in text:
                if ord(ch) > 0x7E:
                    chars.add(ord(ch))
    # '?') 兜底字形恒在（ASCII95 已含）
    return chars


def render(cp: int, font: ImageFont.FreeTypeFont, size: int) -> bytes:
    img = Image.new("1", (size, size), 0)
    d = ImageDraw.Draw(img)
    # 用 bbox 居中（CJK 与 ASCII 通吃）
    ch = chr(cp)
    d.text((size // 2, size // 2), ch, font=font, fill=1, anchor="mm")
    bits = bytearray(size * size // 8)
    px = img.load()
    for y in range(size):
        for x in range(size):
            if px[x, y]:
                bits[y * (size // 8) + x // 8] |= 0x80 >> (x % 8)
    return bytes(bits)


def cmap_guard(cps):
    """缺字硬失败（.notdef 空框混入字库的防线；▸◂ 实测教训）"""
    from fontTools.ttLib import TTCollection
    tt = TTCollection(FONT)
    cmap = tt.fonts[0].getBestCmap()
    missing = [cp for cp in sorted(set(cps)) if cp > 0x7E and cp not in cmap]
    if missing:
        raise SystemExit("[gen_font_cn] 缺字: " + " ".join(f"U+{c:04X}" for c in missing))


def build_set(cps):
    charset, seen = [], set()
    for cp in cps:
        if cp not in seen:
            seen.add(cp)
            charset.append(cp)
    charset.sort()
    return charset


def main():
    out_path = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "Core/Inc/tcs_font_cn.inc"

    auto = collect_ui_chars()
    cmap_guard([ord(c) for c in CHARSET_24_FIXED] + list(auto)
               + [ord(c) for c in CHARSET_32_FIXED])
    if not auto:
        print("[WARN] UI 源码未收集到非 ASCII 字符（检查 UI_SOURCES 路径）")

    cjk24 = build_set([ord(c) for c in CHARSET_24_FIXED] + list(auto))   # ASCII+自动收集
    set32 = build_set([ord(c) for c in CHARSET_32_FIXED] + [c for c in auto if c <= 0x7E])

    f24 = ImageFont.truetype(FONT, 24)
    f32 = ImageFont.truetype(FONT, 32)

    g24 = [render(cp, f24, 24) for cp in cjk24]
    g32 = [render(cp, f32, 32) for cp in set32]

    blank24 = [cp for cp, g in zip(cjk24, g24) if not any(g)]
    blank32 = [cp for cp, g in zip(set32, g32) if not any(g)]
    if blank24:
        print("[WARN] 24px blank glyphs:", " ".join(hex(c) for c in blank24))
    if blank32:
        print("[WARN] 32px blank glyphs:", " ".join(hex(c) for c in blank32))

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("/* tcs_font_cn.inc — 由 tools/gen_font_cn.py 生成，勿手改\n")
        f.write(" * 字符集自动收集自 App/tcs/tcs_ui.c（WP-A.2a）；24px=标题/正文/按钮，\n")
        f.write(" * 32px=数值大字（UI 2x 放大=64px，WP-B 层级）；1bpp 行优先 MSB 在左，\n")
        f.write(" * 24px 每字 72B / 32px 每字 128B；码表升序（UI 二分查找）。字源 msyh.ttc\n")
        f.write(" * 24px %d 字 / 32px %d 字；改 UI 文案后重跑生成+font_audit 门禁 */\n"
                % (len(cjk24), len(set32)))
        for name, charset, glyphs, size in (
                ("tcs_font24", cjk24, g24, 24), ("tcs_font32", set32, g32, 32)):
            bpr = size // 8
            gsz = bpr * size
            f.write("static const unsigned int %s_cp[%d] = {\n" % (name, len(charset)))
            for i in range(0, len(charset), 8):
                f.write("    " + ",".join("0x%04X" % c for c in charset[i:i + 8]) + ",\n")
            f.write("};\n")
            f.write("static const unsigned char %s_glyphs[%d][%d] = {\n"
                    % (name, len(charset), gsz))
            for cp, g in zip(charset, glyphs):
                f.write("    /* U+%04X %s */ {" % (cp, chr(cp) if cp >= 0x20 else " "))
                f.write(",".join("0x%02X" % b for b in g))
                f.write("},\n")
            f.write("};\n")
    print("OK 24px=%d glyphs, 32px=%d glyphs -> %s"
          % (len(cjk24), len(set32), out_path))


if __name__ == "__main__":
    main()
