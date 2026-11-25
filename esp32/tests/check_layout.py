#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_layout.py — 源码门禁：分区表布局与现场故障证据的配置（S 线 e3a，X04-F-03）

检查的是**源文件**，不是构建产物（构建产物由 tools/build_matrix.sh 的三档从零构建覆盖）：
  1. partitions-16mb.csv 能解析，分区名唯一；
  2. 现有布局不变量：nvs@0x9000/0x6000、otadata@0xf000/0x2000、ota_0@0x20000/3M、ota_1@0x320000/3M
     ——这些偏移一旦变了，现场设备 otadata 里记的启动槽位、两份 OTA 镜像的位置全都不再成立；
  3. 恰好一个 coredump 分区（data / coredump），不小于 64K：IDF 把每个任务的栈原样导出，
     分区装不下时整份导出被丢弃；
  4. 分区互不重叠，且都落在 16MB 之内；
  5. sdkconfig.defaults 打开了 CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH，并且导出专用栈大小不为 0
     （本工程开着 FREERTOS_TASK_CREATE_ALLOW_EXT_MEM 时 IDF 要求它 > 0，为 0 直接编译报错）；
  6. 工作树里有 sdkconfig.site（现场档，不入库）时，它也要带着 coredump——否则现场档没有故障证据。

用法（在 esp32/ 根目录）：python tests/check_layout.py [--csv <分区表>] [--defaults <defaults>] [--site <site>]
  给 --csv / --defaults 可以指向别的版本（例如修复前的文件），用来证明门禁是红的。
退出码 0 = 通过。
"""
import csv
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.dirname(HERE)
FLASH_BYTES = 16 * 1024 * 1024

# 现场设备的既有布局（改这里等于让所有已烧录的设备升级后找不到自己的数据 / 镜像）
INVARIANTS = [
    ("nvs", 0x9000, 0x6000),
    ("otadata", 0xF000, 0x2000),
    ("ota_0", 0x20000, 3 * 1024 * 1024),
    ("ota_1", 0x320000, 3 * 1024 * 1024),
]
COREDUMP_MIN = 64 * 1024


def size_bytes(tok):
    m = re.match(r"^\s*(0x[0-9A-Fa-f]+|\d+)\s*([KMG]?)\s*$", tok or "")
    if not m:
        return None
    v = int(m.group(1), 0)
    return v * {"": 1, "K": 1024, "M": 1024 * 1024, "G": 1024 * 1024 * 1024}[m.group(2)]


def parse_csv(path):
    parts, off = [], 0x8000                       # 分区表之后的第一块
    text = io.open(path, encoding="utf-8").read()
    for row in csv.reader(io.StringIO(text)):
        if not row or not row[0].strip() or row[0].lstrip().startswith("#"):
            continue
        name, typ, sub = row[0].strip(), row[1].strip(), row[2].strip()
        o = size_bytes(row[3]) if len(row) > 3 and row[3].strip() else off
        s = size_bytes(row[4]) if len(row) > 4 else None
        if o is None or s is None:
            raise SystemExit("FAIL: 无法解析分区行: %r" % (row,))
        off = (o + s + 0x1000 - 1) & ~0xFFF        # IDF 的规则：下一个分区按 4K 向上取整
        parts.append((name, typ, sub, o, s))
    return parts


def cfg_value(path, key):
    try:
        text = io.open(path, encoding="utf-8", errors="replace").read()
    except OSError:
        return None
    m = re.search(r"^%s=(.*)$" % re.escape(key), text, flags=re.M)
    return m.group(1).strip() if m else None


def main():
    # 控制台代码页不是 UTF-8 时（python -I 下忽略 PYTHONIOENCODING）中文输出不能让门禁靠崩溃退出
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    except (AttributeError, ValueError):
        pass
    csv_path, def_path, site_path = os.path.join(ESP, "partitions-16mb.csv"), \
        os.path.join(ESP, "sdkconfig.defaults"), os.path.join(ESP, "sdkconfig.site")
    args = sys.argv[1:]
    for i, a in enumerate(args):
        if a == "--csv":
            csv_path = args[i + 1]
        elif a == "--defaults":
            def_path = args[i + 1]
        elif a == "--site":
            site_path = args[i + 1]
    bad = []

    parts = parse_csv(csv_path)
    by_name = {}
    for p in parts:
        if p[0] in by_name:
            bad.append("分区名重复: %s" % p[0])
        by_name[p[0]] = p
    for name, o, s in INVARIANTS:
        p = by_name.get(name)
        if not p:
            bad.append("缺少分区 %s" % name)
        elif (p[3], p[4]) != (o, s):
            bad.append("%s 的偏移 / 大小变了: 现在 0x%X/0x%X，现场设备是 0x%X/0x%X"
                       % (name, p[3], p[4], o, s))
    cd = [p for p in parts if p[1] == "data" and p[2] == "coredump"]
    if len(cd) != 1:
        bad.append("coredump 分区应当恰好一个，现在 %d 个（没有它，panic 后的核心转储无处可写）" % len(cd))
    elif cd[0][4] < COREDUMP_MIN:
        bad.append("coredump 分区太小: %d 字节 < %d（装不下时 IDF 整份丢弃）" % (cd[0][4], COREDUMP_MIN))
    ordered = sorted(parts, key=lambda p: p[3])
    for a, b in zip(ordered, ordered[1:]):
        if a[3] + a[4] > b[3]:
            bad.append("分区重叠: %s (0x%X+0x%X) 与 %s (0x%X)" % (a[0], a[3], a[4], b[0], b[3]))
    if ordered and ordered[-1][3] + ordered[-1][4] > FLASH_BYTES:
        bad.append("分区表超出 16MB: %s 结束于 0x%X" % (ordered[-1][0], ordered[-1][3] + ordered[-1][4]))

    if cfg_value(def_path, "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH") != "y":
        bad.append("sdkconfig.defaults 没有 CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y")
    stk = cfg_value(def_path, "CONFIG_ESP_COREDUMP_STACK_SIZE")
    if stk is None or int(stk) <= 0:
        bad.append("sdkconfig.defaults 的 CONFIG_ESP_COREDUMP_STACK_SIZE 缺失或为 0: %r" % stk)
    if site_path and os.path.isfile(site_path):
        if cfg_value(site_path, "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH") != "y":
            bad.append("现场档 sdkconfig.site 没有 CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y（现场档就没有故障证据）")

    print("layout: %s" % ", ".join("%s@0x%X/%dK" % (p[0], p[3], p[4] // 1024) for p in ordered))
    if bad:
        for b in bad:
            print("layout FAIL: " + b)
        print("check_layout: FAIL (%d)" % len(bad))
        return 1
    print("check_layout: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
