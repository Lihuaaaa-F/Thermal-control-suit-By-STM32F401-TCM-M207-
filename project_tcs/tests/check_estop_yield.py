#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""check_estop_yield.py — 屏上急停入口不被主循环里的阻塞型工作挡住（M03-F-04，2026-11-25）。

这条入口的时序没法在主机上测（要触摸、要屏），所以用源码门禁把两件事钉住：
  1) main.c 的主循环里，tcs_lvgl_estop_watch() 在 comm_poll_tx() 与 console_poll() **之前**——
     每拍最先看一眼屏上的急停键；
  2) console.c 与 main.c 里每一个循环体不为空的 while 循环，要么循环体内调用了 console_estop_yield()
     （每 10ms 回到急停入口），要么在下面的例外表里登记了理由。没登记的新循环直接判红：
     新增阻塞循环的人必须分类。
关闭条件里的真机部分（按住急停区同时跑 pinscan，急停响应 ≤20ms）由协调员在台架档上测。

用法（在 project_tcs 下）：python tests/check_estop_yield.py      退出码 0 通过 / 1 不符
run_all.sh 每次都跑。只读。
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FILES = {
    "main": os.path.join(ROOT, "Core", "Src", "main.c"),
    "console": os.path.join(ROOT, "App", "tcs", "console.c"),
}

# 允许不让位的循环：(文件, 循环条件（源码原样子串）, 理由)。条件文本变了要同步这里。
EXCEPTIONS = [
    ("console", "n && (line[n-1] == ' ' || line[n-1] == '\\t')", "去行尾空白，≤ 一行命令的长度"),
    ("console", "guard--", "控制台 RX 环排空：每圈只做一个字节的行拼装 / 最多一条命令（≤ 环长 256）"),
    ("main", "guard--", "link_rx_drain：排空链路 RX 环（≤ 512 字节），每圈只是喂一个字节给 comm"),
    ("main", "(g_txwave_n < 512u) && ((DWT->CYCCNT - t0) < limit)", "txwave 采样 ≤ 10ms（台架及以上）"),
    ("main", "huart_link.gState != HAL_UART_STATE_READY && guard--", "txwave 等发送完成 ≤ 2.8ms（64B@230400），空循环体"),
    ("main", "g_tick_ms - last_tick < 10u", "主循环的 10ms 节拍等待，空循环体"),
    ("main", "1", "main() 的主循环本体与 Error_Handler"),
]


def read(key):
    return io.open(FILES[key], encoding="utf-8", errors="replace").read()


def strip_comments_strings(src):
    """把注释和字符串 / 字符字面量抹成空格（保留换行与长度），免得括号 / 关键字出现在注释里干扰匹配。
    抹出来的文本与原文逐字符等长，所以下面按它找到的位置可以直接回到原文取条件。"""
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        elif c == '"' or c == "'":
            j = i + 1
            while j < n and src[j] != c:
                j += 2 if src[j] == "\\" else 1
            out.append(c + " " * (j - i - 1) + (src[j] if j < n else ""))
            i = j + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def while_loops(clean, raw=None):
    """yield (条件文本（默认取抹除后的文本）, 循环体文本)。"""
    for m in re.finditer(r"\bwhile\s*\(", clean):
        i = m.end()
        depth = 1
        while i < len(clean) and depth > 0:
            if clean[i] == "(":
                depth += 1
            elif clean[i] == ")":
                depth -= 1
            i += 1
        src = raw if raw is not None else clean
        cond = re.sub(r"\s+", " ", src[m.end():i - 1].strip())
        j = i
        while j < len(clean) and clean[j] != "{":
            if clean[j] == ";":          # while (...); 空体
                break
            j += 1
        if j >= len(clean) or clean[j] != "{":
            yield cond, ""
            continue
        k = j
        depth = 0
        while k < len(clean):
            if clean[k] == "{":
                depth += 1
            elif clean[k] == "}":
                depth -= 1
                if depth == 0:
                    break
            k += 1
        yield cond, clean[j + 1:k]


def check_order(fails):
    src = strip_comments_strings(read("main"))
    mi = src.find("int main(void)")
    if mi < 0:
        fails.append("main.c 里找不到 int main(void)")
        return
    body = src[mi:]
    watch = [m.start() for m in re.finditer(r"\btcs_lvgl_estop_watch\s*\(\s*\)\s*;", body)]
    tx = re.search(r"\bcomm_poll_tx\s*\(\s*&g_link\s*\)\s*;", body)
    cp = re.search(r"\bconsole_poll\s*\(\s*\)\s*;", body)
    if not tx or not cp:
        fails.append("main.c 的 main() 里找不到 comm_poll_tx(&g_link); 或 console_poll();")
        return
    if not watch:
        fails.append("main.c 的 main() 里没有调用 tcs_lvgl_estop_watch()（屏入口兜底丢了）")
        return
    w = watch[0]
    if not (w < tx.start() and w < cp.start()):
        fails.append("main.c 主循环：tcs_lvgl_estop_watch() 必须在 comm_poll_tx() 与 console_poll() 之前"
                     "（否则阻塞型控制台命令占住主循环期间屏上的急停键没人看）")
    if any(x > cp.start() for x in watch):
        fails.append("main.c 主循环：console_poll() 之后又调了一次 tcs_lvgl_estop_watch()（每拍看一次就够，且应最先看）")


def check_loops(fails):
    n_yield = 0
    for key in ("console", "main"):
        raw = read(key)
        clean = strip_comments_strings(raw)
        seen = set()
        for cond, body in while_loops(clean, raw):
            hit = [x for x in EXCEPTIONS if x[0] == key and x[1] == cond]
            if hit:
                seen.add((key, cond))
            if not body.strip():
                continue                         # 空体循环（while (x);）：没有可阻塞的工作
            if "console_estop_yield" in body:
                n_yield += 1
                continue
            if hit:
                continue
            fails.append("%s: 循环 `while (%s)` 的循环体里没有 console_estop_yield()，也不在例外表里——"
                         "若它会阻塞主循环超过一拍，请让它让位；若不会，把它登记进 tests/check_estop_yield.py "
                         "的 EXCEPTIONS 并写明理由" % (os.path.basename(FILES[key]), cond[:70]))
        for x in EXCEPTIONS:
            if x[0] != key:
                continue
            if (key, x[1]) not in seen:
                fails.append("%s: 例外表里的循环 `while (%s)` 已经不存在或已改成让位了——从 EXCEPTIONS 里删掉这一条"
                             % (os.path.basename(FILES[key]), x[1][:60]))
    return n_yield


def check_yield_impl(fails):
    src = strip_comments_strings(read("console"))
    m = re.search(r"\bvoid\s+console_estop_yield\s*\(\s*void\s*\)\s*\{(.*?)\n\}", src, re.S)
    if not m or "tcs_lvgl_estop_watch" not in m.group(1):
        fails.append("console.c 的 console_estop_yield() 里没有调用 tcs_lvgl_estop_watch()（带屏构建 TCS_USE_LVGL 下）")


def main():
    fails = []
    check_order(fails)
    check_yield_impl(fails)
    n_yield = check_loops(fails)
    if fails:
        print("[estop_yield] FAIL")
        for f in fails:
            print("  - " + f)
        return 1
    print("[estop_yield] PASS (主循环最先看屏急停入口；%d 个阻塞循环体内让位；%d 条登记的例外)"
          % (n_yield, len(EXCEPTIONS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
