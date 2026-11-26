#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_tautology.py — 恒真断言静态扫描门禁（S-G x3 / 审查条目 X06-F-06）

背景：2026-11-26 的审计在 4 处断言里找到"永远不会红"的写法，其中 `== 0 || 1` 是字面恒真：
comm 队列丢帧、proto_self_test 错 9/10 子项、布防状态机卡死时这 4 条断言仍然绿。
这类断言不制造假红，只制造假绿——比缺测试更危险，因为它看起来有测试。

扫描对象（只看断言）：
  project_tcs/tests/*.c   的 CHECK(...)
  esp32/tests/*.c         的 TB_CHECK(...) / CHECK(...) / assert(...)
  project_tcs/tests/*.py  project_tcs/tools/*.py  project_tcs/tools/site/*.py  esp32/tests/*.py
                          的 assert ... / self.assertTrue(...) 里的 `or 1`

抓的写法（每条都在 --self-test 里有样例）：
  T1 `|| 1` / `|| 1u` / `|| true` / `or 1`          恒真析取项：整个条件因为它永远成立
  T2 `CHECK(1,` / `CHECK(true,`（含 TB_CHECK）       恒真条件
  T3 `<无符号或枚举量> >= 0`                          对不可能为负的值做非负断言
  T4 循环条件为 `*p` 时，体内 `|| *p == '\\0'`        死项：进到体内必然非 0（2026-11-26 实际出现）

T3 需要类型：脚本从仓库头文件里收集"返回无符号 / 枚举类型的函数名"和"无符号类型 typedef 名"，
只对查得到的符号判红——查不到类型的一律不动（宁可漏，不要假红）。
局限声明：这是源码模式门禁，不是 C 语义分析；它证明的是"这几类写法不再出现"。
"""
import io
import os
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent            # project_tcs/tests/
PROJECT = HERE.parent                             # project_tcs/
REPO = PROJECT.parent                             # 仓库根

UNSIGNED_TYPES = {
    "unsigned", "unsigned int", "unsigned char", "unsigned short", "unsigned long",
    "uint8_t", "uint16_t", "uint32_t", "uint64_t",
    "int8_t", "int16_t",          # 故意不含：有符号，>= 0 是有意义的断言
    "size_t", "uintptr_t",
}
# 上表里 int8_t/int16_t 是有符号的，别误收
UNSIGNED_TYPES.discard("int8_t")
UNSIGNED_TYPES.discard("int16_t")

ASSERT_RE = re.compile(r"\b(?:CHECK|TB_CHECK|REQUIRE|assert)\s*\(\s*(.*)$")

T1_RE = re.compile(r"\|\|\s*(?:1[uU]?|true)\s*(?:[,)]|$)")
T2_RE = re.compile(r"\b(?:CHECK|TB_CHECK|REQUIRE)\s*\(\s*(?:1|true)\s*,")
T3_RE = re.compile(r"(>=)\s*0(?![0-9a-zA-Z_.])")
T4_RE = re.compile(r"\|\|\s*\*\s*(\w+)\s*==\s*'\\0'")


def read(p):
    return io.open(p, encoding="utf-8", errors="replace").read()


def strip_comments(text):
    """去掉 /* */ 与 // 注释（字符串内容可能含 //，只做保守处理：引号内不剥）。"""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"':
            out.append(c)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == "\\":
                    if i + 1 < n:
                        out.append(text[i + 1])
                        i += 2
                        continue
                elif text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            # 保留换行数：行号要能直接对到源文件（诊断信息里的 :行 必须可信）
            out.append(" " + "\n" * text.count("\n", i, j))
            i = j
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        out.append(c)
        i += 1
    return "".join(out)


def build_symtab():
    """返回 (无符号/枚举返回值函数名集合, 无符号类型名集合)。扫全仓头文件。"""
    enum_types, unsigned_fns, unsigned_typedefs = set(), set(), set()
    hdrs = []
    for root in (PROJECT / "App", PROJECT / "Core", REPO / "esp32" / "components",
                 REPO / "protocol", REPO / "esp32" / "main"):
        if not root.is_dir():
            continue
        for d, _, fs in os.walk(str(root)):
            for f in fs:
                if f.endswith(".h"):
                    hdrs.append(Path(d) / f)
    for h in hdrs:
        try:
            t = strip_comments(read(h))
        except OSError:
            continue
        for m in re.finditer(r"typedef\s+enum\s*(?:\w+\s*)?\{(.*?)\}\s*([A-Za-z_]\w*)\s*;", t, re.S):
            enum_types.add(m.group(2))
        for m in re.finditer(r"typedef\s+((?:unsigned(?:\s+\w+)?)|u?int\d+_t|size_t)\s+([A-Za-z_]\w*)\s*;", t):
            if m.group(1).strip() in UNSIGNED_TYPES:
                unsigned_typedefs.add(m.group(2))
        # 函数声明：返回类型 名字(参数);  （排除函数指针与宏）
        for m in re.finditer(r"^[ \t]*((?:unsigned(?:\s+\w+)?)|[A-Za-z_]\w*)\s+([A-Za-z_]\w*)\s*\(([^;{)]*)\)\s*;",
                             t, re.M):
            ret = m.group(1).strip()
            if ret in UNSIGNED_TYPES or ret in enum_types or ret in unsigned_typedefs:
                unsigned_fns.add(m.group(2))
    return unsigned_fns, unsigned_typedefs | enum_types


def find_in_c(path, unsigned_fns, unsigned_types):
    text = strip_comments(read(path))
    findings = []
    lines = text.splitlines()
    # T3 还要看本文件里的无符号局部变量声明
    local_unsigned = set()
    for m in re.finditer(r"^[ \t]*(?:static\s+|const\s+)*((?:unsigned(?:\s+\w+)?)|[A-Za-z_]\w*)[ \t\*]+([A-Za-z_]\w*)\s*[;=]",
                         text, re.M):
        if m.group(1).strip() in UNSIGNED_TYPES or m.group(1).strip() in unsigned_types:
            local_unsigned.add(m.group(2))
    loop_ptr = None   # 正在遍历的 `for (...; *p; ...)` 的变量名
    for i, line in enumerate(lines, 1):
        m = re.search(r"for\s*\([^;]*;\s*\*\s*(\w+)\s*;", line)
        if m:
            loop_ptr = m.group(1)
        if loop_ptr and re.match(r"\s*\}\s*$", line):
            loop_ptr = None
        if not ASSERT_RE.search(line):
            continue
        if T1_RE.search(line):
            findings.append((path, i, "T1", "恒真析取项（|| 1 / || true）", line.strip()))
        if T2_RE.search(line):
            findings.append((path, i, "T2", "恒真条件（CHECK(1 / CHECK(true）", line.strip()))
        if loop_ptr and T4_RE.search(line) and ("*" + loop_ptr) in line:
            findings.append((path, i, "T4",
                             "死项：循环条件就是 *%s，体内 || *%s == '\\0' 永不成立" % (loop_ptr, loop_ptr),
                             line.strip()))
        for m3 in T3_RE.finditer(line):
            lhs = line[:m3.start()]
            names = re.findall(r"([A-Za-z_]\w*)\s*\(", lhs)
            idents = re.findall(r"\b([A-Za-z_]\w*)\b", lhs)
            hit = None
            for fn in reversed(names):
                if fn in unsigned_fns:
                    hit = "函数 %s() 返回无符号/枚举" % fn
                    break
            if not hit:
                for t in reversed(idents):
                    if t in local_unsigned:
                        hit = "变量 %s 是无符号" % t
                        break
            if hit:
                findings.append((path, i, "T3", "对不可能为负的值断言 >= 0（%s）" % hit, line.strip()))
    return findings


def find_in_py(path):
    findings = []
    for i, line in enumerate(read(path).splitlines(), 1):
        if re.search(r"\bassert\b", line) or "self.assertTrue" in line:
            if re.search(r"\bor\s+1\b", line) or re.search(r"\bor\s+True\b", line):
                findings.append((path, i, "T1", "恒真析取项（or 1 / or True）", line.strip()))
    return findings


def scan():
    unsigned_fns, unsigned_types = build_symtab()
    findings = []
    for p in sorted((PROJECT / "tests").glob("*.c")):
        findings += find_in_c(p, unsigned_fns, unsigned_types)
    esp_tests = REPO / "esp32" / "tests"
    if esp_tests.is_dir():
        for p in sorted(esp_tests.glob("*.c")):
            findings += find_in_c(p, unsigned_fns, unsigned_types)
    for pat, base in (("*.py", PROJECT / "tests"), ("*.py", PROJECT / "tools"),
                      ("*.py", PROJECT / "tools" / "site"), ("*.py", esp_tests)):
        if base.is_dir():
            for p in sorted(base.glob(pat)):
                if p.resolve() == Path(__file__).resolve():
                    continue          # 不扫自己：--self-test 的样例会命中
                findings += find_in_py(p)
    return findings


SELFTEST = {
    "T1": 'static void f(void){ CHECK(n == 0 || 1, "tautology"); }\n',
    "T2": 'static void g(void){ CHECK(1, "always true"); }\n',
    "T3": 'typedef enum { A = 0, B = 1 } st_t;\nst_t st_step(void);\n'
          'static void h(void){ CHECK(st_step() >= 0, "enum never negative"); }\n',
    "T4": 'static void k(const char *j){ const char *p; for (p = j; *p; p++)\n'
          '        CHECK((unsigned char)*p >= 0x20 || *p == \'\\0\', "dead disjunct"); }\n',
}


def selftest():
    """4 个构造样例必须全部被抓出来；漏一个就是扫描器没牙。"""
    import tempfile
    ok = True
    d = tempfile.mkdtemp(prefix="taut_")
    for tag, src in SELFTEST.items():
        p = Path(d) / ("sample_%s.c" % tag)
        p.write_text(src, encoding="utf-8")
        got = find_in_c(p, {"st_step"}, {"st_t"})
        tags = {g[2] for g in got}
        if tag not in tags:
            print("[tautology] SELFTEST FAIL: 样例 %s 没被抓到（%r → %r）" % (tag, src.strip(), tags))
            ok = False
    py = Path(d) / "sample_py.py"
    py.write_text("def t(x):\n    assert x == 0 or 1\n", encoding="utf-8")
    if not find_in_py(py):
        print("[tautology] SELFTEST FAIL: python `assert x == 0 or 1` 没被抓到")
        ok = False
    print("[tautology] SELFTEST %s（%d 个样例）" % ("PASS" if ok else "FAIL", len(SELFTEST) + 1))
    return 0 if ok else 1


def main(argv):
    if "--self-test" in argv:
        return selftest()
    findings = scan()
    for path, line, tag, why, src in findings:
        try:
            rel = path.relative_to(REPO)
        except ValueError:
            rel = path
        print("[tautology] FAIL %s:%d %s %s | %s" % (rel, line, tag, why, src[:100]))
    print("[tautology] %s（%d 处）" % ("PASS" if not findings else "FAIL", len(findings)))
    return 0 if not findings else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
