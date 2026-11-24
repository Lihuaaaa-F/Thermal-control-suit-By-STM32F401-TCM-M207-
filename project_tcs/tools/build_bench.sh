#!/bin/bash
# build_bench.sh — 构建台架档 H743 固件到 build-bench/（现场工具要用的那一档）
#
# 交付档（PROD）自 2026-11-24 起不含调试/测试钩子，project_tcs/tools/site/ 的探针类工具
# （tap / wvar / estop_matrix / tune_refresh_check …）需要这些钩子，所以要烧台架档。
# 本脚本只构建和核对，不碰设备；烧录用 tools/flash_lvgl.sh。
# 用法：bash tools/build_bench.sh
set -u
cd "$(dirname "$0")/.." || exit 1
if [ ! -f build-bench/CMakeCache.txt ]; then
    cmake -S . -B build-bench -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake -DTCS_PROFILE=BENCH \
        > build-bench.cfg.log 2>&1 || { echo "[bench-build] configure FAIL（见 build-bench.cfg.log）"; exit 1; }
fi
if ! cmake --build build-bench > build-bench.build.log 2>&1; then
    echo "[bench-build] build FAIL（见 build-bench.build.log）"; exit 1
fi
echo "[bench-build] build OK"
export PYTHONUTF8=1
python tools/site/verify_prod_clean.py --elf build-bench/tcs_main.elf --expect-hooks || {
    echo "[bench-build] FAIL：台架档产物里钩子不全，现场工具会用不了"; exit 1; }
echo "[bench-build] 钩子齐全。接着："
echo "  bash tools/flash_lvgl.sh build-bench/tcs_main.elf"
echo "  export TCS_ELF=build-bench/tcs_main.elf"
