#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""font_audit.py — 字库缺字审计（任务书 v5.1 WP-A.2a，永久门禁）

扫描全部 UI 源码里的字符串字面量 × tcs_font_cn.inc 字库码表：
  - 24px 字库（tcs_font24_cp[]）必须覆盖 UI 字符串中出现的**每一个**可见字符
    （ui_text24/ui_text48 对 ASCII 也走 24px 表；ui_text16 的 8x16 表恒含 ASCII95）
  - '?' 兜底字形必须恒在（UI 缺字回落目标）
缺任一字 → 打印缺失清单并以非零码退出（run_all.sh 门禁项 / CI 可直接调用）。

范围：App/tcs/tcs_ui.c + App/tcs/param_registry.c（注册表 name/unit 上屏于调参页，
      WP-A.3 回归实测发现 '?' → 纳入扫描；console/网页不上屏不扫）。
用法：python tools/font_audit.py   （在 project_tcs 根目录）
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI_SOURCES = [ROOT / "App/tcs/tcs_ui.c", ROOT / "App/tcs/param_registry.c"]
FONT_INC = ROOT / "Core/Inc/tcs_font_cn.inc"

# C 字符串字面量（含转义；不含字符字面量——上屏文本只用字符串）
STR_RE = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
ESCAPE_RE = re.compile(r'\\(?:x[0-9A-Fa-f]{1,2}|[0-7]{1,3}|u[0-9A-Fa-f]{4}|.)')


def decode_literal(raw: str):
    """把 C 字面量原始文本转为 (码点列表, 无法解码的字节数)。
    \\xNN/\\NNN 按单字节处理（UTF-8 尾字节场景），\\uNNNN 按码点，
    \\n\\t\\r 等转 ASCII 跳过，\\\"\\\\ 还原。"""
    cps = []
    bad = 0
    i = 0
    out = bytearray()
    while i < len(raw):
        if raw[i] == '\\':
            m = ESCAPE_RE.match(raw, i)
            if not m:
                i += 1
                bad += 1
                continue
            esc = m.group(0)
            i = m.end()
            if esc.startswith('\\x') or (esc[1].isdigit()):
                try:
                    out.append(int(esc[1:], 16) & 0xFF)
                except ValueError:
                    bad += 1
            elif esc.startswith('\\u'):
                cps.append(int(esc[2:], 16))
            elif esc == '\\n' or esc == '\\t' or esc == '\\r':
                out.append(0x20)   # 占位：控制字符按空格宽度，不算缺字
            elif esc == '\\"' or esc == '\\\\' or esc == "\\'":
                out.append(ord(esc[-1]))
            elif esc == '\\0':
                break
            else:
                bad += 1
        else:
            out.extend(raw[i].encode('utf-8'))
            i += 1
    # 重组：转义产生的字节与原文直接拼接后按 UTF-8 解码（尽力而为）
    if out:
        try:
            text = out.decode('utf-8')
            cps.extend(ord(ch) for ch in text)
        except UnicodeDecodeError:
            bad += len(out)
    return cps, bad


def load_font_cps():
    if not FONT_INC.exists():
        print(f"[font_audit] 字库文件不存在: {FONT_INC}")
        sys.exit(2)
    txt = FONT_INC.read_text(encoding='utf-8')
    m = re.search(r'tcs_font24_cp\[[^\]]*\]\s*=\s*\{(.*?)\};', txt, re.S)
    if not m:
        print("[font_audit] 未找到 tcs_font24_cp[] 码表")
        sys.exit(2)
    return set(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{4,6})', m.group(1)))


def main():
    have = load_font_cps()
    missing = {}
    literals = 0
    for src in UI_SOURCES:
        if not src.exists():
            print(f"[font_audit] UI 源文件不存在: {src}")
            sys.exit(2)
        txt = src.read_text(encoding='utf-8')
        for m in STR_RE.finditer(txt):
            cps, bad = decode_literal(m.group(1))
            literals += 1
            if bad:
                print(f"[font_audit][WARN] {src.name}: 字面量含 {bad} 个无法解码的转义字节")
            for cp in cps:
                if cp < 0x20 or (0x7F <= cp <= 0x9F):
                    continue          # 控制字符跳过
                if cp not in have:
                    missing.setdefault(cp, 0)
                    missing[cp] += 1
    if 0x3F not in have:
        missing[0x3F] = missing.get(0x3F, 0)   # '?' 兜底字形必须恒在
        missing[0x3F] += 1
    if missing:
        print(f"[font_audit] FAIL — {len(missing)} 个缺字（字符串×字库审计, H3 门禁）:")
        for cp in sorted(missing):
            print(f"  U+{cp:04X} '{chr(cp)}'  出现于 {missing[cp]} 处字符串")
        print("修复：python tools/gen_font_cn.py 重生成字库并重烧 H743")
        sys.exit(1)
    print(f"[font_audit] PASS — {literals} 个字符串字面量全部字形在库（码表 {len(have)} 字）")
    sys.exit(0)


if __name__ == "__main__":
    main()
