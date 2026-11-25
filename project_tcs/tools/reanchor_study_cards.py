#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""reanchor_study_cards.py — 研学卡 ③ 调用面 HEAD 重锚工具（W8 维护用）

背景：源码演进后，研学卡 ③ 表的「文件:行」与命中数会漂移，card_surface 门禁
（docs/ref/研学卡_行数基线.txt）随之变红。本工具按代码签名把旧卡的行语义映射到
新 HEAD 的行号，新增行自动补录，重算 ④ 的 #行号 引用、页脚与表头计数，并更新基线。

用法：在 project_tcs/ 目录下运行
    python tools/reanchor_study_cards.py            # 重锚 UART/GPIO 两卡
    python tools/reanchor_study_cards.py --check    # 只打印差集，不落盘
"""
import datetime, io, os, re, subprocess, sys, shutil

TODAY = datetime.date.today().isoformat()   # 2026-11-25 起：重锚日期取运行当天（原先写死 2026-10-30）

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))  # project_tcs/
REPO = os.path.dirname(ROOT)
REF = os.path.join(REPO, "docs", "ref")
BASE = os.path.join(REF, "研学卡_行数基线.txt")
GREP = shutil.which("grep") or "grep"

CARDS = {
    "研学卡_HAL_UART":      os.path.join(REF, "研学卡_HAL_UART.md"),
    "研学卡_HAL_GPIO_EXTI": os.path.join(REF, "研学卡_HAL_GPIO_EXTI.md"),
    # 2026-11-25 纳入：同一张表格式；此前只能手工改，行号早就漂了
    "研学卡_HAL_PWR_RCC":    os.path.join(REF, "研学卡_HAL_PWR_RCC.md"),
}

def load_baseline():
    defs = {}
    for line in io.open(BASE, encoding="utf-8"):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        parts = line.split(";;")
        if len(parts) != 4:
            continue
        name, dirs, pat, want = parts[0], parts[1].split(), parts[2], parts[3].split()[0]
        defs[name] = (dirs, pat, int(want))
    return defs

def grep_matches(dirs, pat):
    out = subprocess.run([GREP, "-rnE", pat] + dirs + ["--include=*.c"],
                         cwd=ROOT, capture_output=True, text=True).stdout
    rows = []
    for ln in out.splitlines():
        if not ln:
            continue
        # path:line:text（路径可能含盘符/反斜杠；行号后第一个冒号）
        m = re.match(r"^(.*?):(\d+):(.*)$", ln)
        if not m:
            continue
        path = m.group(1).replace("\\", "/")
        rows.append((path, int(m.group(2)), m.group(3)))
    return rows

def parse_card_rows(text):
    """返回 ③ 节的旧表行 [(num, path, api_col, sem_col)]"""
    rows = []
    for m in re.finditer(r"^\|\s*(\d+)\s*\|\s*(project_tcs/[^\s|]+)\s*\|\s*(.*?)\s*\|\s*(.*?)\s*\|\s*$", text, re.M):
        path = re.sub(r":\d+$", "", m.group(2))   # 去掉 ":行号" 后缀，仅留文件路径
        rows.append((int(m.group(1)), path, m.group(3), m.group(4)))
    return rows

def norm(s):
    return re.sub(r"\s+", "", s)

def is_comment_row(api_col):
    return ("注释" in api_col) or ("非调用点" in api_col)

def code_part(api_col):
    return api_col.replace("（注释提及）", "").replace("(注释提及)", "").strip()

def match_old_to_new(old_rows, matches):
    """把旧行映射到新命中的下标；返回 (assign: old_idx->new_idx, used: set(new_idx))"""
    assign, used = {}, set()
    # 第一遍：代码签名精确匹配（同文件）
    for oi, (_, path, api_col, _sem) in enumerate(old_rows):
        if is_comment_row(api_col):
            continue
        cp = norm(code_part(api_col))
        if not cp:
            continue
        # 签名取到第一个 '(' 后 40 字符内做前缀匹配，太短则用整串
        sig = cp[:80]
        for ni, (npath, _nline, ntext) in enumerate(matches):
            if ni in used or npath != path.replace("project_tcs/", ""):
                continue
            if sig in norm(ntext):
                assign[oi] = ni
                used.add(ni)
                break
    # 第二遍：注释行——同文件内未被占用的注释行，含旧行里的 API 名
    for oi, (_, path, api_col, _sem) in enumerate(old_rows):
        if oi in assign or not is_comment_row(api_col):
            continue
        api = re.sub(r"[（(].*$", "", api_col).strip()
        rel = path.replace("project_tcs/", "")
        for ni, (npath, _nline, ntext) in enumerate(matches):
            if ni in used or npath != rel:
                continue
            st = ntext.strip()
            if api in st and (st.startswith("*") or st.startswith("/*") or st.startswith("//")):
                assign[oi] = ni
                used.add(ni)
                break
    # 第三遍：兜底——同文件 + API 名，最近行号
    for oi, (num, path, api_col, _sem) in enumerate(old_rows):
        if oi in assign:
            continue
        api = re.sub(r"[（(].*$", "", api_col).strip()
        rel = path.replace("project_tcs/", "")
        best = None
        for ni, (npath, nline, ntext) in enumerate(matches):
            if ni in used or npath != rel or api not in ntext:
                continue
            if best is None or abs(nline - num) < abs(matches[best][1] - num):
                best = ni
        if best is not None:
            assign[oi] = best
            used.add(best)
    return assign, used

def excerpt(ntext, limit=88):
    t = re.sub(r"\s+", " ", ntext.strip())
    if len(t) > limit:
        t = t[: limit - 1] + "…"
    return t.replace("|", "\\|")

def build_table(matches, old_rows, assign, used):
    """按 grep 顺序输出新表行文本；返回 (rows_text_list, old2new)"""
    old_by_new = {}
    for oi, ni in assign.items():
        old_by_new[ni] = oi
    lines, old2new = [], {}
    for ni, (path, nline, ntext) in enumerate(matches):
        oi = old_by_new.get(ni)
        if oi is not None:
            _num, _p, api_col, sem = old_rows[oi]
            old2new[old_rows[oi][0]] = ni + 1
            lines.append((path, nline, api_col, sem, False))
        else:
            st = ntext.strip()
            is_cmt = st.startswith("*") or st.startswith("/*") or st.startswith("//")
            api = re.match(r"\s*[/*\s]*(?:__)?(HAL_[A-Za-z0-9_]+|DMA2D_[A-Za-z0-9_]+)", st)
            api_name = api.group(1) if api else ("（注释提及）" if is_cmt else "—")
            sem = ("重锚新增（注释提及）：" if is_cmt else "重锚新增：") + "代码摘录见左列"
            lines.append((path, nline, api_name + ("（注释提及）" if is_cmt else ""), sem, True))
    rows = []
    for i, (path, nline, api_col, sem, is_new) in enumerate(lines, 1):
        rows.append("| %d | project_tcs/%s:%d | %s | %s |" % (i, path, nline, api_col, sem))
    return rows, old2new

def renumber_refs(section_text, old2new):
    """把 ④ 里 #N / #a-b 引用映射到新行号"""
    def rep(m):
        a = int(m.group(1))
        b = int(m.group(2)) if m.group(2) else None
        na = old2new.get(a)
        if na is None:
            return m.group(0)
        if b is None:
            return "#%d" % na
        nb = old2new.get(b, na)
        return "#%d-%d" % (na, nb)
    return re.sub(r"#(\d+)(?:-(\d+))?", rep, section_text)

def reanchor(name, card_path, defs, check_only):
    dirs, pat, want = defs[name]
    matches = grep_matches(dirs, pat)
    text = io.open(card_path, encoding="utf-8").read()
    old_rows = parse_card_rows(text)
    assign, used = match_old_to_new(old_rows, matches)
    table, old2new = build_table(matches, old_rows, assign, used)

    print("[%s] 旧表 %d 行 / 新命中 %d / 语义保留 %d / 新增 %d" %
          (name, len(old_rows), len(matches), len(assign), len(matches) - len(assign)))
    if check_only:
        return len(old_rows), len(matches)

    # 替换 ③ 节表体（## ③ 到 ## ④ 之间）：保留前言，重写表
    m = re.search(r"(## ③[^\n]*\n)(.*?)(\n## ④)", text, re.S)
    assert m, name + " ③ 节未找到"
    head, body, tail = m.group(1), m.group(2), m.group(3)
    # 保留旧 ③ 前言里 vendor 域说明等（取第一段代码块之前的内容）
    preamble = body.split("| # |")[0] if "| # |" in body else ""
    new_body = "\n" + preamble + "\n| # | 文件:行 | API | 语义一句话 |\n|---|---|---|---|\n" + "\n".join(table) + "\n"
    text = text[: m.start()] + head + new_body + tail + text[m.end():]

    # 表头计数、④ 行号引用、页脚
    n = len(matches)
    text = re.sub(r"本卡 \*\*\d+ 行 == \d+ 命中\*\*", "本卡 **%d 行 == %d 命中**" % (n, n), text)
    text = text.replace("（R2 链路 RX 自愈 +1，2026-10-30）", "（2026-10-30 v2 迁移重锚）")
    # ③ 前言里的代码块计数（→ N 命中 / 下表 N 行）
    text = re.sub(r"→ \d+ 命中（[^）]*）", "→ %d 命中（%s 重锚）" % (n, TODAY), text, count=1)
    text = re.sub(r"下表 \d+ 行", "下表 %d 行" % n, text, count=1)
    mt = re.search(r"(## ④.*)$", text, re.S)
    if mt:
        text = text[: mt.start(1)] + renumber_refs(mt.group(1), old2new)
    text = re.sub(r"——研学卡完（\d+/\d+[^）]*）", "——研学卡完（%d/%d，%s 重锚）" % (n, n, TODAY), text)
    io.open(card_path, "w", encoding="utf-8", newline="\n").write(text)
    return len(old_rows), n

def main():
    check = "--check" in sys.argv
    defs = load_baseline()
    results = {}
    for name, path in CARDS.items():
        results[name] = reanchor(name, path, defs, check)
    if check:
        return
    # 更新基线 want 与注释。
    # 2026-11-25 起原地更新：原先每运行一次就多插一行说明，文字还是旧的（"2026-10-30 v2 迁移重锚"）；
    # 基线行里的行内注释（如 R1 的 +17）保留，只改数字。
    notes = {
        "研学卡_HAL_UART":      "# UART 37→38（R2 自愈+1）→%d（%s 重锚）",
        "研学卡_HAL_GPIO_EXTI": "# GPIO 54→58（2026-10-30 v2 迁移重锚：main/it/console 新增调用面）→%d（%s 重锚）",
        "研学卡_HAL_PWR_RCC":   "# PWR/RCC →%d（%s 重锚）",
    }
    prefixes = {"研学卡_HAL_UART": "# UART ", "研学卡_HAL_GPIO_EXTI": "# GPIO ",
                "研学卡_HAL_PWR_RCC": "# PWR/RCC "}
    lines = io.open(BASE, encoding="utf-8").read().split("\n")
    out, done = [], set()
    for ln in lines:
        for name, fmt in notes.items():
            if ln.startswith(prefixes[name]):              # 该卡的说明行
                if name in done:                            # 旧工具把说明行插在基线行后面，会残留一行旧的：删掉
                    ln = None
                    break
                else:                                      # 第一条说明行：换成新的
                    ln = fmt % (results[name][1], TODAY)
                    done.add(name)
            elif ln.startswith(name + ";;") and name not in done:   # 没有说明行：在基线行前补一行
                out.append(fmt % (results[name][1], TODAY))
                done.add(name)
        if ln is None:
            continue
        for name in notes:
            if ln.startswith(name + ";;"):
                parts = ln.split(";;")
                fourth = parts[3].split("#", 1)
                comment = ("   #" + fourth[1]) if len(fourth) > 1 else ""
                ln = ";;".join(parts[:3]) + ";;%d%s" % (results[name][1], comment)
        if ln is not None:
            out.append(ln)
    io.open(BASE, "w", encoding="utf-8", newline="\n").write("\n".join(out))
    print("基线已更新:", results, "（重锚日期 %s）" % TODAY)


if __name__ == "__main__":
    main()
