#!/bin/bash
# build_bench.sh — 构建台架档 H743 固件到 build-bench/（现场工具要用的那一档）
#
# 交付档（PROD）自 2026-11-24 起不含调试/测试钩子，project_tcs/tools/site/ 的探针类工具
# （tap / wvar / estop_matrix / tune_refresh_check …）需要这些钩子，所以要烧台架档。
# 本脚本只构建和核对，不碰设备；烧录用 tools/flash_lvgl.sh。
# 用法：bash tools/build_bench.sh
set -u
cd "$(dirname "$0")/.." || exit 1
# 每次都重新配置（两秒左右）。版本串里的 git 号是配置时生成的：只在目录不存在时才配置的话，
# 提交之后增量构建出来的固件，版本串还写着上一次的提交号（C06 第七节）；新增的源文件也不会被编进来
# （2026-11-26：模式页新增 tui_p4.c 之后门禁在这里失败——tui_p4_* 未定义）。
if ! cmake -S . -B build-bench -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake -DTCS_PROFILE=BENCH \
        > build-bench.cfg.log 2>&1; then
    echo "[bench-build] configure FAIL（见 build-bench.cfg.log）"; exit 1
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
