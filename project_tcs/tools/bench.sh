#!/bin/bash
# bench.sh — H4 台架一键：构建(双) → 烧录 sim → 复位 → 活性 → 快速体检
# 用法：bash tools/bench.sh
set -e
cd "$(dirname "$0")/.." || exit 1
echo "[bench 1/5] build (release + sim)"
cmake --build build >/dev/null && cmake --build build-sim >/dev/null && echo "  build OK"
echo "[bench 2/5] PC 单测"
bash tests/run_all.sh | tail -2
echo "[bench 3/5] flash sim firmware"
probe-rs download build-sim/tcs_main.elf --chip STM32H743II --probe 0d28:0204 2>&1 | tail -1
echo "[bench 4/5] reset + liveness"
probe-rs reset --chip STM32H743II --probe 0d28:0204 >/dev/null 2>&1
sleep 4
bash tools/wp_check.sh | head -1
echo "[bench 5/5] POST/console 体检"
export TCS_ELF=build-sim/tcs_main.elf
python tools/mb_console.py "ver" "post" "stat" 2>&1 | grep -vE "^\[MB\]"
echo "[bench] DONE"
