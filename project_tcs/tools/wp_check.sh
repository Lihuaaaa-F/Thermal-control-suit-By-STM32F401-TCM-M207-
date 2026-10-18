#!/bin/bash
# tools/wp_check.sh — 6.1 关单三件套之"活性证据"（WP-C.5）
# reset → 5s → 两次读 g_stat_loop_passes 判增长 → RTT 抓 boot/复位源行 → PASS/FAIL 单行。
# 用法：bash tools/wp_check.sh   （在 project_tcs 根目录；需 probe-rs 在 PATH）
set -u
cd "$(dirname "$0")/.." || exit 1
CHIP=STM32H743II
PROBE=0d28:0204
ELF=${TCS_ELF:-build/tcs_main.elf}   # 板上跑 sim 固件时：TCS_ELF=build-sim/tcs_main.elf

[ -f "$ELF" ] || { echo "WP-CHECK FAIL: $ELF not found (build first)"; exit 1; }
ADDR=$(arm-none-eabi-nm "$ELF" | awk '/ g_stat_loop_passes/{print "0x"$1}')
[ -n "$ADDR" ] || { echo "WP-CHECK FAIL: symbol g_stat_loop_passes not found"; exit 1; }

probe-rs reset --chip "$CHIP" --probe "$PROBE" >/dev/null 2>&1
sleep 5
A=$(probe-rs read --chip "$CHIP" --probe "$PROBE" b32 "$ADDR" 1 2>/dev/null | awk 'NR==1{print $2}')
sleep 2
B=$(probe-rs read --chip "$CHIP" --probe "$PROBE" b32 "$ADDR" 1 2>/dev/null | awk 'NR==1{print $2}')

# probe-rs 输出为无前缀十六进制 → 转十进制再比较
AD=$((16#${A:-0}))
BD=$((16#${B:-0}))

# H3 教训：probe-rs reset 后首个后续会话可能泄放一次延迟复位（B<A），再往后稳定。
# 判据：B>A 即 PASS；否则等 3s 重测一次（此时板已稳定）。
if [ "$AD" -gt 0 ] 2>/dev/null && [ "$BD" -gt "$AD" ] 2>/dev/null; then
    echo "WP-CHECK PASS loop_passes $AD -> $BD (growing, main loop alive)"
    timeout 2 probe-rs attach --chip "$CHIP" --probe "$PROBE" 2>/dev/null | head -5
    exit 0
fi
sleep 3
A=$(probe-rs read --chip "$CHIP" --probe "$PROBE" b32 "$ADDR" 1 2>/dev/null | awk 'NR==1{print $2}')
sleep 2
B=$(probe-rs read --chip "$CHIP" --probe "$PROBE" b32 "$ADDR" 1 2>/dev/null | awk 'NR==1{print $2}')
AD=$((16#${A:-0})); BD=$((16#${B:-0}))
if [ "$AD" -gt 0 ] 2>/dev/null && [ "$BD" -gt "$AD" ] 2>/dev/null; then
    echo "WP-CHECK PASS(retry) loop_passes $AD -> $BD (growing, main loop alive)"
    exit 0
fi
echo "WP-CHECK FAIL loop_passes A=${A:-none} B=${B:-none}"
exit 1
