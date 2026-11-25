#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""study_cards_check.py — 研学卡 ③ 表逐行核对：每行的"文件:行"处必须真的写着该行的接口名。

背景（2026-11-25，S 线；审查条目 X06-F-07 / X06-F-09 的文档漂移部分）：
`card_surface` 门禁只核对"基线数字 == grep 命中数"，不看研学卡文档本身。结果：
- 研学卡_HAL_PWR_RCC.md 只列了 42 行，而代码里是 59 处调用面（R1 2026-11-15 加了 17 处，文档没跟）；
- 三张卡里的行号从 C01 改过 console.c / main.c 之后就不准了。
两件事门禁都没发现。本脚本补上：逐行核对，对不上就红。

规则：对 tools/reanchor_study_cards.py 登记的每张卡，
  - "文件:行"里的文件必须存在，行号必须在文件范围内；
  - 该行文本（去掉空白后）必须包含"API"列里的接口名（"（注释提及）"行同样核对；API 列是"—"的行只核对文件与行号）。
用法（在 project_tcs/ 下）：python tools/study_cards_check.py        退出码 0=PASS 1=FAIL
只读：不写任何文件。
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.dirname(ROOT)
sys.path.insert(0, os.path.join(ROOT, "tools"))
try:
    import reanchor_study_cards as rac
except Exception as e:                      # noqa: BLE001
    print("[card_lines] FAIL: 无法加载 tools/reanchor_study_cards.py：%s" % e)
    sys.exit(1)


def main():
    bad, total = [], 0
    # 最小张数断言：登记表被清空时下面循环一次都不跑，会直接输出"PASS（0 张卡）"——
    # 正是这道门禁要堵的那类漏洞（2026-11-25 对账复核指出）。
    if len(rac.CARDS) < 3:
        bad.append("tools/reanchor_study_cards.py 的 CARDS 里只有 %d 张卡（应 ≥ 3：UART / GPIO / PWR-RCC）"
                   % len(rac.CARDS))
    for name, card in rac.CARDS.items():
        if not os.path.isfile(card):
            bad.append("%s：卡片文件不存在 %s" % (name, card))
            continue
        text = io.open(card, encoding="utf-8").read()
        rows = rac.parse_card_rows(text)
        if len(rows) < 5:
            bad.append("%s：表体只有 %d 行（少于 5）" % (name, len(rows)))
        # 表里的编号必须是 1..N 连续
        nums = [r[0] for r in rows]
        if nums != list(range(1, len(rows) + 1)):
            bad.append("%s：编号不连续（%s…）" % (name, nums[:12]))
        for _num, path, api_col, _sem in rows:
            total += 1
            full = os.path.join(REPO, path)
            if not os.path.isfile(full):
                bad.append("%s：%s 文件不存在" % (name, path))
                continue
            # 行号从原文里取（parse_card_rows 把它去掉了）
        for m in re.finditer(r"^\|\s*\d+\s*\|\s*(project_tcs/[^\s|]+):(\d+)\s*\|\s*(.*?)\s*\|", text, re.M):
            path, line_no, api_col = m.group(1), int(m.group(2)), m.group(3)
            full = os.path.join(REPO, path)
            if not os.path.isfile(full):
                continue
            lines = io.open(full, encoding="utf-8", errors="replace").read().split("\n")
            if line_no < 1 or line_no > len(lines):
                bad.append("%s：%s:%d 超出文件行数（共 %d 行）" % (name, path, line_no, len(lines)))
                continue
            # API 列的写法不统一：有的是裸接口名，有的是带实参的调用原文，后面还常跟一句中文说明
            # （如"HAL_GPIO_WritePin（宏封装）"）。只取其中第一个标识符来核对——那才是"这一行调用了什么"。
            ident = re.match(r"\s*([A-Za-z_][A-Za-z0-9_]*)", api_col)
            code = ident.group(1) if ident else ""
            if not code:
                continue
            if code not in lines[line_no - 1]:
                bad.append("%s：%s:%d 这一行里找不到 %s" % (name, path, line_no, code))
    if bad:
        print("[card_lines] FAIL（%d 处）" % len(bad))
        for b in bad[:30]:
            print("  " + b)
        return 1
    print("[card_lines] PASS（%d 张卡 / %d 行，文件、行号、接口名逐行对得上）" % (len(rac.CARDS), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
