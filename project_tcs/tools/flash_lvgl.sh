#!/usr/bin/env bash
# flash_lvgl.sh — H743 LVGL 固件烧录（低速 + 多点回读校验 + 复位）
#
# 为什么必须低速：2026-10-29 实测，pyocd 在 1MHz 下擦写会中途断开，留下"半写镜像"
# （0x08000000 起有效、0x08060000 起全 FF），表现为上电不启动、PC 落进 AXI 池。
# 500kHz 下同一次烧录 4s 完成且校验通过。
#
# 用法：bash tools/flash_lvgl.sh [bin 路径，默认 build-g4lv/tcs_main.bin]
set -euo pipefail
BIN="${1:-build-g4lv/tcs_main.bin}"
PROBE_UID="${PROBE_UID:-0700000132346578000431333937354ea5a5a5a597969908}"
FREQ="${FREQ:-500k}"

[ -f "$BIN" ] || { echo "找不到 $BIN"; exit 1; }
SZ=$(stat -c %s "$BIN")
echo "镜像 $BIN  ${SZ} 字节  md5=$(md5sum "$BIN" | cut -d' ' -f1)"

echo "== 擦除+编程（${FREQ}，sector erase）=="
pyocd flash -t stm32h743xx --probe "$PROBE_UID" -f "$FREQ" -e sector --connect attach \
    "$BIN" --base-address 0x08000000 || echo "[warn] pyocd 退出非零（disconnect 常见报错，继续校验）"

echo "== 多点回读校验 =="
python - "$BIN" <<'PY'
import struct, subprocess, sys
b = open(sys.argv[1], 'rb').read()
def rd(addr, n=4):
    out = subprocess.run(['probe-rs','read','--probe','0d28:0204','--chip','STM32H743IITx',
                          'b32',hex(addr),str(n)], capture_output=True, text=True).stdout
    v = []
    for ln in out.splitlines():
        if ':' in ln:
            for tok in ln.split(':',1)[1].split():
                try: v.append(int(tok,16))
                except ValueError: pass
    return v[:n]
bad = 0
for off in (0, 0x40000, 0x60000, 0x7A000, len(b)-16):
    if off + 16 > len(b):
        continue
    want = list(struct.unpack('<4I', b[off:off+16]))
    got = rd(0x08000000 + off)
    ok = (got[:4] == want)
    print(f"  0x{0x08000000+off:08X}  {'OK' if ok else 'MISMATCH'}  got={[hex(x) for x in got]} want={[hex(x) for x in want]}")
    bad += 0 if ok else 1
sys.exit(1 if bad else 0)
PY

echo "== 复位 =="
probe-rs write --probe 0d28:0204 --chip STM32H743IITx b32 0xE000ED0C 0x05FA0004 || true
sleep 3
echo "== 运行态抽查（g_tcs 前 4 字 / g_tl8 magic）=="
probe-rs read --probe 0d28:0204 --chip STM32H743IITx b32 0x20000a24 4 || true
python - <<'PY'
import subprocess
def nm(n):
    out=subprocess.check_output(['arm-none-eabi-nm','build-g4lv/tcs_main.elf'],text=True)
    for ln in out.splitlines():
        p=ln.split()
        if len(p)==3 and p[2]==n: return int(p[0],16)
a=nm('g_tl8')
print(subprocess.run(['probe-rs','read','--probe','0d28:0204','--chip','STM32H743IITx','b32',hex(a),'2'],capture_output=True,text=True).stdout.strip())
PY
