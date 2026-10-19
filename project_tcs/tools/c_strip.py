# -*- coding: utf-8 -*-
"""c_strip.py — C 源码注释剥离（font_audit / gen_font_cn 共用）

注释内的引号短语（如 "正常=安静灰"）不应被当作 UI 字符串收集——
run_all 实测误报教训（v9.5 K6）。字符串与字符字面量原样保留。
"""


def strip_c_comments(src):
    out = []
    i, n = 0, len(src)
    state = 0   # 0=normal 1=str 2=line-comment 3=block-comment 4=char-literal
    while i < n:
        ch = src[i]
        if state == 0:
            if ch == '"':
                out.append(ch)
                state = 1
            elif ch == "'":
                out.append(ch)
                state = 4
            elif ch == "/" and i + 1 < n and src[i + 1] == "/":
                state = 2
                i += 2
                continue
            elif ch == "/" and i + 1 < n and src[i + 1] == "*":
                state = 3
                i += 2
                continue
            else:
                out.append(ch)
        elif state == 1:
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if ch == '"':
                state = 0
        elif state == 2:
            if ch == "\n":
                out.append(ch)
                state = 0
        elif state == 3:
            if ch == "*" and i + 1 < n and src[i + 1] == "/":
                state = 0
                i += 2
                continue
            if ch == "\n":
                out.append("\n")   # 保留行号语义
        else:  # state == 4
            out.append(ch)
            if ch == "\\" and i + 1 < n:
                out.append(src[i + 1])
                i += 2
                continue
            if ch == "'":
                state = 0
        i += 1
    return "".join(out)
