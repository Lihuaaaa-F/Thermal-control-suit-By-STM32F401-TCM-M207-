#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""version_bump.py — 固件版本的代改与一致性检查（S 线 e3a，审查条目 X06-F-09）

版本号的唯一源是 protocol/tcs_version.h 里的四个宏（TCS_VER_MAJOR / MINOR / PATCH / SUFFIX）。
其余地方都从它推导：
  - ESP32 应用版本：esp32/CMakeLists.txt 启动时读头文件生成 PROJECT_VER；
  - 数值化的 fw_ver：TCS_FW_VER_BCD（esp32/components/tcs_core/param_registry.c 的 g_tcs 初值）。

用法（仓库根目录下）：
  python tools/version_bump.py --check              一致性检查（esp32/tests/run_m207_tests.sh 会调用）
  python tools/version_bump.py --set 1.3.0          改版本号——只改头文件这一个地方
  python tools/version_bump.py --set 1.3.0-rc2      带后缀
退出码：--check 全部一致为 0。
文档里的版本串（README / 正典 / 交付清单）只报告、不改：最终版本号由用户定，定下来之后再统一。
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HDR = os.path.join(ROOT, "protocol", "tcs_version.h")
DOCS = ["README.md", "docs/现行规格说明书_v1.2.md"]


def rd(path):
    return io.open(path, encoding="utf-8", errors="replace").read()


def parse_header(text):
    def num(name):
        m = re.search(r"^#define\s+%s\s+(\d+)\s*$" % name, text, flags=re.M)
        return m.group(1) if m else None

    m = re.search(r'^#define\s+TCS_VER_SUFFIX\s+"([^"]*)"\s*$', text, flags=re.M)
    maj, mino, pat = num("TCS_VER_MAJOR"), num("TCS_VER_MINOR"), num("TCS_VER_PATCH")
    if not (maj and mino and pat) or not m:
        return None
    return maj, mino, pat, m.group(1)


def check():
    bad = []
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    except (AttributeError, ValueError):
        pass
    v = parse_header(rd(HDR))
    if not v:
        print("version FAIL: protocol/tcs_version.h 读不出版本号")
        return 1
    ver = "%s.%s.%s%s" % v
    print("version: protocol/tcs_version.h = %s (fw_ver BCD 0x%02X%02X%02X)"
          % (ver, int(v[0]), int(v[1]), int(v[2])))
    cmake = rd(os.path.join(ROOT, "esp32", "CMakeLists.txt"))
    if re.search(r'set\(\s*PROJECT_VER\s+"\s*\d', cmake):   # 引号后紧跟数字 = 写死的版本号
        bad.append("esp32/CMakeLists.txt 里又写死了 PROJECT_VER（应只从 protocol/tcs_version.h 推导）")
    if "tcs_version.h" not in cmake:
        bad.append("esp32/CMakeLists.txt 没有读 protocol/tcs_version.h")
    reg = rd(os.path.join(ROOT, "esp32", "components", "tcs_core", "param_registry.c"))
    if ".fw_ver = TCS_FW_VER_BCD" not in reg:
        bad.append("esp32 的 g_tcs.fw_ver 不是从 TCS_FW_VER_BCD 推导")
    if re.search(r"\.fw_ver\s*=\s*0x[0-9A-Fa-f]+", reg):
        bad.append("esp32 的 g_tcs.fw_ver 仍是手写的 BCD 字面量")
    # 2026-11-28：H743 侧也必须从同一头文件推导（此前是手写 0x010200，两端版本对不上）
    h743 = rd(os.path.join(ROOT, "project_tcs", "App", "tcs", "param_registry.c"))
    if ".fw_ver = TCS_FW_VER_BCD" not in h743:
        bad.append("H743 的 g_tcs.fw_ver 不是从 TCS_FW_VER_BCD 推导")
    if re.search(r"\.fw_ver\s*=\s*0x[0-9A-Fa-f]+", h743):
        bad.append("H743 的 g_tcs.fw_ver 仍是手写的 BCD 字面量")
    if "tcs_version.h" not in h743:
        bad.append("H743 的 param_registry.c 没有 include protocol/tcs_version.h")
    for b in bad:
        print("version FAIL: " + b)
    # 文档漂移：只报告
    for d in DOCS:
        p = os.path.join(ROOT, d)
        if not os.path.isfile(p):
            continue
        hits = sorted(set(re.findall(r"v\d+\.\d+\.\d+(?:[-\w.]*)", rd(p))))
        other = [h for h in hits if h != "v" + ver]
        if other:
            print("version NOTE: %s 里的版本串与头文件不同（定下最终版本号后统一）: %s"
                  % (d, ", ".join(other)))
    print("version_check: %s" % ("PASS" if not bad else "FAIL (%d)" % len(bad)))
    return 1 if bad else 0


def setver(spec):
    m = re.match(r"^(\d+)\.(\d+)\.(\d+)(?:-([\w.]+))?$", spec)
    if not m:
        print("用法: --set X.Y.Z[-后缀]   例: --set 1.3.0  /  --set 1.3.0-rc2")
        return 2
    raw = open(HDR, "rb").read()
    nl = "\r\n" if b"\r\n" in raw else "\n"
    t = raw.decode("utf-8").replace("\r\n", "\n")
    for name, val in (("TCS_VER_MAJOR", m.group(1)), ("TCS_VER_MINOR", m.group(2)),
                      ("TCS_VER_PATCH", m.group(3))):
        t, n = re.subn(r"^(#define\s+%s\s+)\d+(\s*)$" % name, r"\g<1>%s\g<2>" % val, t, flags=re.M)
        if n != 1:
            print("ABORT: 头文件里找不到 %s" % name)
            return 2
    t, n = re.subn(r'^(#define\s+TCS_VER_SUFFIX\s+)"[^"]*"\s*$', r'\g<1>"%s"' % (m.group(4) or ""), t, flags=re.M)
    if n != 1:
        print("ABORT: 头文件里找不到 TCS_VER_SUFFIX")
        return 2
    open(HDR, "wb").write(t.replace("\n", nl).encode("utf-8"))
    print("已改 protocol/tcs_version.h -> %s" % spec)
    return check()


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 0
    if a[0] == "--check":
        return check()
    if a[0] == "--set":
        return setver(a[1]) if len(a) > 1 else 2
    print("未知参数：%s" % a[0])
    return 2


if __name__ == "__main__":
    sys.exit(main())
