#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""mutation_web.py — web_status.c 判别性验证：逐个施加"改坏一处"的变异，每个都必须让
tests/test_web_status.c 变红。有变异没被抓住 = 该路径缺用例。

与 mutation_control.py 同款做法；差别是这里没有"旧实现必须红"这一步——
web_status.c 是 U 线新建的文件，没有旧版本可对照。

用法（在 esp32/ 根目录）：python tests/mutation_web.py
编译器与 tests/run_m207_tests.sh 相同（MSVC cl + vcvars64；cJSON 用 IDF 自带的同一份源，
路径可用环境变量 IDF_CJSON 覆盖）。
退出码 0 = 原版绿 且 每个变异都红。
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.dirname(HERE)
COMP = os.path.join(ESP, "components")
VARS64 = r"C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
CJSON_DIR = os.environ.get("IDF_CJSON") or "D:/Espressif/frameworks/esp-idf-v5.5.5/components/json/cJSON"
SRC = os.path.join(COMP, "tcs_web", "web_status.c")
TEST = os.path.join(HERE, "test_web_status.c")

# (名称, 文件内唯一原文, 替换文)。原文必须在 web_status.c 里恰好出现一次。
MUTATIONS = [
    ("M01 两位小数舍入改截断",
     "return (double)(int64_t)(x >= 0.0 ? x * 100.0 + 0.5 : x * 100.0 - 0.5) / 100.0;",
     "return (double)(int64_t)(x * 100.0) / 100.0;"),
    ("M02 非有限数不写 0",
     "if (!isfinite(v)) return 0.0;",
     "if (!isfinite(v)) return 1.0;"),
    ("M03 st 少一个字段 screen",
     '    ADDN("screen",    s->screen ? 1 : 0);\n',
     "\n"),
    ("M04 标志位不归一成 0/1",
     'ADDN("tec1",      s->tec1 ? 1 : 0);',
     'ADDN("tec1",      s->tec1);'),
    ("M05 变化检测漏掉 mode",
     "a->mode != b->mode || a->state != b->state ||",
     "a->state != b->state ||"),
    ("M06 时间戳起伏也算变化",
     "            r2(a->body) != r2(b->body) ||\n",
     "            r2(a->body) != r2(b->body) || a->ts_ms != b->ts_ms ||\n"),
    ("M07 空指针按无变化处理",
     "if (!a || !b) return 1;",
     "if (!a || !b) return 0;"),
    ("M08 首份带着故障也记事件",
     "if (!prev || !now || prev->ts_ms == 0) return 0;",
     "if (!prev || !now) return 0;"),
    ("M09 事件方向写反",
     "if (!was && now) { web_log_add(ts, lvl, ev_on,  msg_on);  return 1; }\n"
     "    if (was && !now) { web_log_add(ts, 'i', ev_off, msg_off); return 1; }",
     "if (was && !now) { web_log_add(ts, lvl, ev_on,  msg_on);  return 1; }\n"
     "    if (!was && now) { web_log_add(ts, 'i', ev_off, msg_off); return 1; }"),
    ("M10 恢复事件沿用故障级别",
     "if (was && !now) { web_log_add(ts, 'i', ev_off, msg_off); return 1; }",
     "if (was && !now) { web_log_add(ts, lvl, ev_off, msg_off); return 1; }"),
    ("M11 级别为 0 不落到 info",
     "e.lvl = (uint8_t)(lvl ? lvl : 'i');",
     "e.lvl = (uint8_t)lvl;"),
    ("M12 环下标错一位",
     "s_ring[s_total % WEB_LOG_N] = e;",
     "s_ring[(s_total + 1u) % WEB_LOG_N] = e;"),
    ("M13 已被覆盖的序号也能取到",
     "if (abs_idx < s_total && s_total - abs_idx <= WEB_LOG_N) {",
     "if (abs_idx < s_total) {"),
    ("M14 UTF-8 截断按字节劈汉字",
     "if (n + len >= cap) break;",
     "if (n + 1 >= cap) break;"),
    ("M15 残缺的半个汉字也留下",
     "        if (copied < len) break;",
     "        if (copied < len) n += 0;"),
    ("M16 log 的 from 不抬到最旧",
     "if (from + WEB_LOG_N < total) from = total - WEB_LOG_N;",
     "if (from + WEB_LOG_N + 1u < total) from = total - WEB_LOG_N;"),
    ("M17 log 区间少取最后一条",
     "for (uint32_t k = from; k < to; k++) {",
     "for (uint32_t k = from; k + 1u < to; k++) {"),
    ("M18 log 级别名 alarm/warn 写反",
     "return (lvl == 'a') ? \"alarm\" : (lvl == 'w') ? \"warn\" : \"info\";",
     "return (lvl == 'a') ? \"warn\" : (lvl == 'w') ? \"alarm\" : \"info\";"),
    ("M19 log 串结尾不带 NUL",
     "if (len < cap) { memcpy(buf, j, len + 1); out = len; }   /* 连结尾 NUL 一起拷 */",
     "if (len < cap) { memcpy(buf, j, len); out = len; }"),
    ("M20 link 串结尾不带 NUL",
     "if (len < cap) { memcpy(buf, j, len + 1); out = len; }   /* 装不下（含结尾 NUL）就回 0，不回半包 */",
     "if (len < cap) { memcpy(buf, j, len); out = len; }"),
    ("M21 link 容量判定多算 1 字节",
     "if (len < cap) { memcpy(buf, j, len + 1); out = len; }   /* 装不下（含结尾 NUL）就回 0，不回半包 */",
     "if (len <= cap) { memcpy(buf, j, len + 1); out = len; }"),
    ("M22 link 的热点名不转义",
     'if (!cJSON_AddStringToObject(root, "ssid", ssid) ||',
     'if (!cJSON_AddRawToObject(root, "ssid", ssid) ||'),
    ("M23 link 的密码不转义",
     '!cJSON_AddStringToObject(root, "pass", pass) ||',
     '!cJSON_AddRawToObject(root, "pass", pass) ||'),
    ("M24 link 少一个计数却照样返回成功",
     "        for (k = 0; k < WEB_LINK_N; k++)\n"
     "            if (!cJSON_AddNumberToObject(root, web_link_keys[k], (double)cnt[k])) break;\n"
     "        if (k != WEB_LINK_N) break;\n",
     "        for (k = 0; k + 1u < WEB_LINK_N; k++)\n"
     "            if (!cJSON_AddNumberToObject(root, web_link_keys[k], (double)cnt[k])) break;\n"),
    # ---- 2026-11-24 固件 WS 复核后补 ----
    ("M25 变化检测又把采样龄算进去（st 回到约 10Hz）",
     "    return (r2(a->tc1_act) != r2(b->tc1_act) || r2(a->tc2_act) != r2(b->tc2_act) ||\n"
     "            r2(a->body) != r2(b->body) ||\n",
     "    return (r2(a->tc1_act) != r2(b->tc1_act) || r2(a->tc2_act) != r2(b->tc2_act) ||\n"
     "            r2(a->body) != r2(b->body) || a->age_ms != b->age_ms ||\n"),
    ("M26 变化检测按原始 float 比（末位抖动也算变化）",
     "    return (r2(a->tc1_act) != r2(b->tc1_act) || r2(a->tc2_act) != r2(b->tc2_act) ||\n"
     "            r2(a->body) != r2(b->body) ||\n",
     "    return (a->tc1_act != b->tc1_act || a->tc2_act != b->tc2_act ||\n"
     "            a->body != b->body ||\n"),
    ("M27 注册表的 step 不做数值归一（带出 float32 尾数）",
     'cJSON_AddNumberToObject(it, "step",  web_num_short(p->step)) != NULL &&',
     'cJSON_AddNumberToObject(it, "step",  p->step) != NULL &&'),
    ("M28 数值归一固定用 6 位有效数字（1234567 写成 1.23457e+06）",
     '        snprintf(b, sizeof b, "%.*g", p, (double)v);',
     '        snprintf(b, sizeof b, "%.6g", (double)v); if (p >= 1) p = 9;'),
    ("M29 日志条目少一个字段也当成功（分配失败时的半条）",
     '                !cJSON_AddStringToObject(it, "msg", e.msg) ||\n',
     ''),
    ("M30 日志条数不齐也当成功（分配失败时的半包）",
     "        if (cJSON_GetArraySize(arr) != (int)(to - from)) break;   /* 上面任何一处 break 都走到这 */",
     "        ;"),
]


def run_cl(work, web_status_c):
    """用 cl 编译并运行单测；返回 (编译成功, 运行输出, 运行返回码)"""
    bat = os.path.join(work, "cl_run.bat")
    inc = [os.path.join(COMP, d) for d in ("tcs_m207/include", "tcs_tmp117/include", "tcs_core",
                                           "tcs_link/include", "tcs_link", "tcs_web/include")]
    inc.append(CJSON_DIR)
    srcs = [TEST, web_status_c, os.path.join(CJSON_DIR, "cJSON.c")]
    objs = " ".join('"%s"' % s for s in srcs)
    incs = " ".join('/I"%s"' % i for i in inc)
    lines = ["@echo off",
             'call "%s" >nul 2>&1' % VARS64,
             'cd /d "%s"' % work,
             "cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST %s %s "
             "/Fe:test_web.exe" % (incs, objs),
             "exit /b %ERRORLEVEL%"]
    with open(bat, "w", encoding="ascii", newline="\r\n") as f:
        f.write("\n".join(lines) + "\n")
    cp = subprocess.run(["cmd", "/c", bat], capture_output=True, text=True,
                        errors="replace", timeout=300)
    with open(os.path.join(work, "cl.log"), "w", encoding="utf-8", errors="replace") as f:
        f.write(cp.stdout + cp.stderr)
    if cp.returncode != 0:
        return False, cp.stdout + cp.stderr, -1
    rp = subprocess.run([os.path.join(work, "test_web.exe")], capture_output=True,
                        text=True, errors="replace", timeout=120, cwd=work)
    return True, rp.stdout + rp.stderr, rp.returncode


def compile_run(src_text):
    d = tempfile.mkdtemp(prefix="tcs_mutweb_")
    c = os.path.join(d, "web_status.c")
    with open(c, "w", encoding="utf-8", newline="\n") as f:
        f.write(src_text)
    return run_cl(d, c)


def fails_of(out):
    """测试打印的每个失败项（CHECK 的"名称 (line N)"与 parse() 的"JSON 解析失败"都算）"""
    return [x.strip() for x in re.findall(r"^FAIL: (.+)$", out, flags=re.M)]


def main():
    if not os.path.isfile(os.path.join(CJSON_DIR, "cJSON.c")):
        print("cJSON 源缺失：%s（可用环境变量 IDF_CJSON 覆盖路径）" % CJSON_DIR)
        return 1
    with open(SRC, encoding="utf-8") as f:
        cur = f.read()

    bad = 0

    # 0) 原版必须绿
    ok, out, rc = compile_run(cur)
    m = re.search(r"PASS=(\d+) FAIL=(\d+)", out)
    if not ok or rc != 0:
        print("[current] NOT GREEN (compile_ok=%s rc=%s)" % (ok, rc))
        print(out[-2000:])
        return 1
    print("[current] PASS=%s FAIL=%s" % (m.group(1), m.group(2)))

    # 1) 变异必须红
    for name, a, b in MUTATIONS:
        n = cur.count(a)
        if n != 1:
            print("[mut] %-28s SKIP: 原文出现 %d 次（变异表过期）" % (name, n))
            bad += 1
            continue
        ok, out, rc = compile_run(cur.replace(a, b))
        verbose = bool(os.environ.get("MUT_VERBOSE"))
        if ok and rc != 0:
            fl = fails_of(out)
            print("[mut] %-28s CAUGHT by %d checks, e.g. %s" % (name, len(fl), fl[0] if fl else "?"))
            if verbose:
                print(out[-800:])
        elif ok:
            print("[mut] %-28s SURVIVED —— 没有任何用例抓到" % name)
            if verbose:
                print(out[-800:])
            bad += 1
        else:
            print("[mut] %-28s COMPILE FAIL" % name)
            bad += 1

    print("mutation_web: %s" % ("ALL AS EXPECTED" if not bad else "HAS PROBLEMS (%d)" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
