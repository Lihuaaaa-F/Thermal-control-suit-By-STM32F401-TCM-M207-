#!/bin/bash
# factory_reset.sh — H4 出厂复位一键（defaults + auto布防 + save）
# 板上需烧 sim 或正式固件（均带邮箱控制台）。用法：bash tools/factory_reset.sh
set -e
cd "$(dirname "$0")/.." || exit 1
export TCS_ELF="${TCS_ELF:-build-sim/tcs_main.elf}"
echo "[factory] reset board ..."
probe-rs reset --chip STM32H743II --probe 0d28:0204 >/dev/null 2>&1
sleep 4
python tools/mb_console.py "p defaults Y" "ack" "p list" "stat" 2>&1 | grep -vE "^\[MB\]" 
echo "[factory] done: 参数=默认, 布防=auto(0x08=0), 已固化 EEPROM"
