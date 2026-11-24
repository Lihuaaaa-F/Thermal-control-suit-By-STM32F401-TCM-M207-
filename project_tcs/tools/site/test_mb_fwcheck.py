# -*- coding: utf-8 -*-
"""mb_console.py 固件身份核对的离线自测（不碰设备）：ELF 取样 vs 固件 .bin。

证明两件事：① 同一份构建的 ELF 取样与它自己的 .bin 逐字相符；② 拿另一档的镜像来比，
至少有一处对不上（即核对能分辨"板上不是这份固件"）。build_matrix.sh 每次构建后都会跑。
用法：python tools/site/test_mb_fwcheck.py [<构建矩阵目录，默认 build-matrix>]
"""
import importlib.util
import os
import struct
import sys
from pathlib import Path

proj = Path(__file__).resolve().parent.parent.parent          # project_tcs/
M = proj / (sys.argv[1] if len(sys.argv) > 1 else "build-matrix")  # 含 PROD/ BENCH/ 子目录


def load(elf):
    os.environ["TCS_ELF"] = str(elf)
    spec = importlib.util.spec_from_file_location("mbc_%d" % len(sys.modules), str(proj / "tools" / "mb_console.py"))
    mb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mb)
    return mb


def words_at(binfile, addr, n):
    b = binfile.read_bytes()
    off = addr - 0x08000000
    if off + 4 * n > len(b):
        return None                      # 超出镜像范围 = 显然不是同一份
    return list(struct.unpack_from("<%dI" % n, b, off))


prod_elf = M / "PROD/tcs_main.elf"
bench_elf = M / "BENCH/tcs_main.elf"
prod_bin = M / "PROD/tcs_main.bin"
bench_bin = M / "BENCH/tcs_main.bin"
res = []
for name, elf, img in (("PROD ELF vs PROD 镜像", prod_elf, prod_bin),
                       ("PROD ELF vs BENCH 镜像", prod_elf, bench_bin),
                       ("BENCH ELF vs PROD 镜像", bench_elf, prod_bin),
                       ("BENCH ELF vs BENCH 镜像", bench_elf, bench_bin)):
    pts = load(elf).elf_flash_samples()
    mism = 0
    for addr, want in pts:
        assert addr % 4 == 0, "采样点没有 4 字节对齐：0x%08X" % addr
        got = words_at(img, addr, len(want))
        mism += (got != want)
    res.append((name, len(pts), mism))
    print("%-24s 采样点 %d，不相等 %d" % (name, len(pts), mism))

ok = res[0][2] == 0 and res[3][2] == 0 and res[1][2] >= 1 and res[2][2] >= 1
print("自测：%s（同源 0 处不符；异源至少 1 处不符）" % ("PASS" if ok else "FAIL"))
try:
    load(prod_elf).nm_addr("g_tap_inject")
    print("nm_addr：交付档里竟找到了 g_tap_inject（FAIL）")
    ok = False
except SystemExit as e:
    msg = str(e)
    hit = "台架" in msg and "TCS_PROFILE=BENCH" in msg
    print("nm_addr 缺钩子时的提示%s" % ("正确（提到烧台架档）" if hit else "不对：" + msg[:80]))
    ok = ok and hit
sys.exit(0 if ok else 1)
