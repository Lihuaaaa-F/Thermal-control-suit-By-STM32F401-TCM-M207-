#!/bin/bash
# bench.sh — 台架一键（v2 架构）
#   主机侧：PROD 档增量构建（没配置过就先 configure）→ PC 单测 run_all
#   加 --device 才碰设备：烧录 → 多点回读校验 → 复位 → 活性 → 控制台体检
# 用法：bash tools/bench.sh [--device]
#   全量从零、多档位、可复现比对：用 tools/build_matrix.sh。
#   2026-11-24 重写（S 线 D04）：旧版依赖 2026-11-15 已删除的 build-sim 目录和 v1 的
#   "烧 sim 固件 → post 体检"流程，在那之后的目录结构下必然失败。
set -u
cd "$(dirname "$0")/.." || exit 1
DO_DEV=0
for a in "$@"; do case "$a" in --device) DO_DEV=1 ;; *) echo "未知参数 $a"; exit 2 ;; esac; done
FAIL=0

echo "[bench 1/2] PROD 档构建"
if [ ! -f build/CMakeCache.txt ]; then
    cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake -DTCS_PROFILE=PROD > build.cfg.log 2>&1 \
        || { echo "  configure FAIL（见 build.cfg.log）"; exit 1; }
fi
cmake --build build > build.bench.log 2>&1 && echo "  build OK（增量构建；从零的告警数以 tools/build_matrix.sh 为准）" \
    || { echo "  build FAIL（见 build.bench.log）"; FAIL=1; }

echo "[bench 2/2] PC 单测"
if bash tests/run_all.sh > build.bench_tests.log 2>&1 && tail -1 build.bench_tests.log | grep -q "ALL GREEN"; then
    echo "  run_all ALL GREEN（$(grep -acE '\] PASS' build.bench_tests.log) 项）"
else
    echo "  run_all FAIL（见 build.bench_tests.log）"; FAIL=1
fi

if [ "$DO_DEV" = 1 ]; then
    echo "[bench 3] 烧录 + 回读校验 + 复位"
    bash tools/flash_lvgl.sh build/tcs_main.elf || FAIL=1
    echo "[bench 4] 活性与控制台"
    TCS_ELF=build/tcs_main.elf bash tools/wp_check.sh | head -1 || FAIL=1
    TCS_ELF=build/tcs_main.elf python tools/mb_console.py "ver" "stat" 2>&1 | grep -vE "^\[(MB|NET)\]" | head -6
fi

[ "$FAIL" -eq 0 ] && echo "[bench] PASS" || { echo "[bench] FAIL"; exit 1; }
