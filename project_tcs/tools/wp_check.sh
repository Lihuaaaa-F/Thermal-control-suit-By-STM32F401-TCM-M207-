#!/bin/bash
# tools/wp_check.sh — 6.1 关单三件套之"活性证据"（WP-C.5）
# reset → 5s → 两次读 g_stat_loop_passes 判增长 → RTT 抓 boot/复位源行 → PASS/FAIL 单行。
# 用法：bash tools/wp_check.sh   （在 project_tcs 根目录；需 probe-rs 在 PATH）
set -u
cd "$(dirname "$0")/.." || exit 1
CHIP=STM32H743II
PROBE=0d28:0204
ELF=build/tcs_main.elf

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

if [ "$AD" -gt 0 ] 2>/dev/null && [ "$BD" -gt "$AD" ] 2>/dev/null; then
    echo "WP-CHECK PASS loop_passes $AD -> $BD (growing, main loop alive)"
    # RTT 引导行（尽力抓取，非交互管道可能为空，不影响判定）
    timeout 2 probe-rs attach --chip "$CHIP" --probe "$PROBE" 2>/dev/null | head -5
    exit 0
else
    echo "WP-CHECK FAIL loop_passes A=${A:-none} B=${B:-none}"
    exit 1
fi
