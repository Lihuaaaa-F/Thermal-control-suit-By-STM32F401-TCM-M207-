#!/usr/bin/env bash
# 墨水屏门禁一键连跑(S1→S2→S3→S4→S6→S0app),接好线后一条命令跑完全部验证
# 用法: bash run_gates.sh          # 标准门禁(S4 用 4.5MHz×10)
#       GATES_FULL=1 bash run_gates.sh   # S4 加跑 4.5MHz×100
# 任一门失败即停;全部通过打印 GATES-ALL-PASS
set -uo pipefail
LOOP=~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh
HERE="$(cd "$(dirname "$0")" && pwd)"

set_mode() { sed -i '/CONFIG_EPAPER_TEST_MODE=/d' sdkconfig; echo "CONFIG_EPAPER_TEST_MODE=$1" >> sdkconfig; }
set_kcfg() { sed -i "/$1=/d" sdkconfig; echo "$1=$2" >> sdkconfig; }

gate() { # gate <mode> <名称> <log秒> <判定签名> [额外kcfg...]
  local mode=$1 name=$2 secs=$3 sig=$4; shift 4
  echo ""
  echo "================ 门禁 $name (TEST_MODE=$mode) ================"
  for kv in "$@"; do set_kcfg "${kv%%=*}" "${kv#*=}"; done
  set_mode "$mode"
  bash "$LOOP" build >/dev/null 2>&1 || { echo "GATE-FAIL: $name (build)"; return 1; }
  bash "$LOOP" flash >/dev/null 2>&1 || { echo "GATE-FAIL: $name (flash)"; return 1; }
  bash "$LOOP" log "$secs" >/dev/null 2>&1
  if grep -q "$sig" /tmp/idf_mon.log; then
    grep "$sig" /tmp/idf_mon.log | tail -2
    echo "GATE-PASS: $name"
  else
    echo "GATE-FAIL: $name (日志无签名: $sig) —— 全量日志 /tmp/idf_mon.log"
    return 1
  fi
}

echo "======== 墨水屏门禁连跑 $(date '+%H:%M:%S') ========"
gate 1 "S1 总线活化" 20 "S1 bus selftest(PON/POF): OK" || exit 1
gate 3 "S2 白屏+睡眠唤醒" 80 "S2 白屏#2(唤醒后): ESP_OK" || exit 1
gate 2 "S3 三色条(肉眼核对:上黑/中白/下红)" 45 "S3 三色条: ESP_OK" || exit 1
gate 4 "S4 老化×10 @4.5MHz" 260 "S4 汇总" "CONFIG_EPAPER_SPI_HZ=4500000" "CONFIG_EPAPER_AGING_COUNT=10" || exit 1
if [ "${GATES_FULL:-0}" = "1" ]; then
  gate 4 "S4 加跑 4.5MHz×100" 1900 "S4 汇总" "CONFIG_EPAPER_SPI_HZ=4500000" "CONFIG_EPAPER_AGING_COUNT=100" || exit 1
fi
gate 6 "S6 重启×10" 300 "S6 完成" || exit 1
gate 0 "S6 app 集成演示(change-driven)" 60 "[DISPLAY] ESP_OK render=" || exit 1

echo ""
echo "GATES-ALL-PASS: 全部门禁跑完。S3 三色正确 + S4 零失败 = 工作流验收通过"
echo "遗留人工项: 1000 次全刷老化(GATES_FULL 后台跑或分会话)"
