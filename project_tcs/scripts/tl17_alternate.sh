#!/usr/bin/env bash
# tl17_alternate.sh — TL-17 双栈交替全清重建 ×10
# 判定：每轮 =0 bin md5 == 规范基线（9d97cb5e…，SOURCE_DATE_EPOCH=1792989165 + prefix-map）
#       每轮 =1 构建绿（lvgl 全源 ARM 编译）
set -e
cd "$(dirname "$0")/.."
export SOURCE_DATE_EPOCH=1792989165
CANON="6554439153553ef0244d229d13cb893d"
LOG="${1:-tl17_run.log}"
: > "$LOG"
for i in $(seq 1 10); do
    rm -rf build-tl17 build-tl17lv
    cmake -S . -B build-tl17 -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake >> "$LOG" 2>&1
    cmake --build build-tl17 >> "$LOG" 2>&1
    M=$(md5sum build-tl17/tcs_main.bin | cut -d' ' -f1)
    if [ "$M" != "$CANON" ]; then
        echo "[$(date +%T)] round $i =0 MISMATCH $M" | tee -a "$LOG"
        exit 1
    fi
    cmake -S . -B build-tl17lv -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake -DTCS_USE_LVGL=ON >> "$LOG" 2>&1
    cmake --build build-tl17lv >> "$LOG" 2>&1
    if [ ! -f build-tl17lv/tcs_main.bin ]; then
        echo "[$(date +%T)] round $i =1 BUILD FAIL" | tee -a "$LOG"
        exit 1
    fi
    echo "[$(date +%T)] round $i OK (=0 identity, =1 green)" | tee -a "$LOG"
done
echo "TL-17 PASS: 10/10 rounds" | tee -a "$LOG"
