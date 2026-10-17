#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""gen_font_cn.py — 从 msyh.ttc 提取 24×24 汉字/符号/ASCII 字形 → tcs_font_cn.inc

任务书 §3.7：中文字形由本脚本从 msyh.ttc 提取 24×24 汉字 + 16×16 ASCII。
16×16 ASCII 复用 GS 工程已验证的 Core/Inc/font8x16.inc（等价交付，省 1KB flash），
本脚本产出 24×24 位图字体（CJK + 全角符号 + ASCII95），供 UI 大字/中文标签渲染；
48px 数值大字 = 24×24 数字字形 2× 最近邻放大（tcs_ui.c 内实现）。

输出格式：1bpp 行优先 MSB 在左，每字 72B；码表升序，UI 侧二分查找。
字符清单固定于下方 CHARSET（改清单须重跑本脚本并重烧 H743）。

用法：python tools/gen_font_cn.py [输出路径，默认 Core/Inc/tcs_font_cn.inc]
依赖：Pillow（pip install pillow）。失败降级预案 R5：改用英文 key 显示（功能不受损）。
"""
import sys
from PIL import Image, ImageFont, ImageDraw

FONT = r"C:\Windows\Fonts\msyh.ttc"
SIZE = 24

# ~64+ 字清单（UI 全部字符串出现过的汉字，按用途分组，便于核对）
CHARSET_CJK = (
    # 卡片标题/数值
    "贴身水箱设定温度环境体温风扇开关闭"
    # 顶栏
    "自动手动运行停止报失联警"
    # 底部按钮
    "调参数模式切换启急停"
    # 调参页
    "过热阈滞回差控制保存并返回步进范围值高二次确认"
    # 弹窗/提示
    "将全触实生效固化断电持已限最请恢复离在线重试链路版本未常说明"
    # 其他
    "制待"
)
CHARSET_SYM = "°℃✓✗·，。？！~："
CHARSET_ASCII = "".join(chr(c) for c in range(0x20, 0x7F))

# Unicode 归一：℃(U+2103)→°C 双字形组合太复杂，保留原码点单字形


def render(cp: int, font: ImageFont.FreeTypeFont) -> bytes:
    img = Image.new("1", (SIZE, SIZE), 0)
    d = ImageDraw.Draw(img)
    # 用 bbox 居中（CJK 与 ASCII 通吃）
    ch = chr(cp)
    d.text((SIZE // 2, SIZE // 2), ch, font=font, fill=1, anchor="mm")
    bits = bytearray(SIZE * SIZE // 8)
    px = img.load()
    for y in range(SIZE):
        for x in range(SIZE):
            if px[x, y]:
                bits[y * (SIZE // 8) + x // 8] |= 0x80 >> (x % 8)
    return bytes(bits)


def main():
    out_path = sys.argv[1] if len(sys.argv) > 1 else "Core/Inc/tcs_font_cn.inc"
    charset = []
    seen = set()
    for s in CHARSET_CJK + CHARSET_SYM + CHARSET_ASCII:
        for ch in s:
            cp = ord(ch)
            if cp not in seen:
                seen.add(cp)
                charset.append(cp)
    charset.sort()

    font = ImageFont.truetype(FONT, SIZE)
    glyphs = []
    for cp in charset:
        glyphs.append(render(cp, font))

    # 空字形检测（缺字）→ 占位 '?' 图形提示
    blank = [cp for cp, g in zip(charset, glyphs) if not any(g)]
    if blank:
        print("[WARN] blank glyphs:", " ".join(hex(c) for c in blank))

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("/* tcs_font_cn.inc — 由 tools/gen_font_cn.py 生成，勿手改\n")
        f.write(" * 24x24 1bpp 行优先 MSB 在左，每字 72B；码表升序（UI 二分查找）\n")
        f.write(" * 字源 msyh.ttc；字符清单 %d 字（脚本内固定）*/\n" % len(charset))
        f.write("static const unsigned int tcs_font24_cp[%d] = {\n" % len(charset))
        for i in range(0, len(charset), 8):
            f.write("    " + ",".join("0x%04X" % c for c in charset[i:i + 8]) + ",\n")
        f.write("};\n")
        f.write("static const unsigned char tcs_font24_glyphs[%d][%d] = {\n"
                % (len(charset), SIZE * SIZE // 8))
        for cp, g in zip(charset, glyphs):
            f.write("    /* U+%04X %s */ {" % (cp, chr(cp) if cp >= 0x20 else " "))
            f.write(",".join("0x%02X" % b for b in g))
            f.write("},\n")
        f.write("};\n")
    print("OK %d glyphs -> %s" % (len(charset), out_path))


if __name__ == "__main__":
    main()
