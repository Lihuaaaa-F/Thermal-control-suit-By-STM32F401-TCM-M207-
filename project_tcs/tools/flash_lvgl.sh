#!/usr/bin/env bash
# flash_lvgl.sh — H743 固件烧录：probe-rs 下载 → 多点回读校验 → 复位 → 运行态核对
#
# 用法：bash tools/flash_lvgl.sh [ELF，默认 build/tcs_main.elf] [--check-only]
#   --check-only   不烧录、不复位，只做回读校验和运行态核对——用来确认"板上跑的是不是这份 ELF"。
#                  探针每次 attach 会让 CPU 短暂停一下（屏上可能闪一下"主控断开"），属测量干扰。
# 规则（用户规定）：烧录用 probe-rs，不用 pyocd（pyocd 增量写会留下不能启动的半写镜像，
#   2026-10-29 实测，事故记录在 docs/results 与台账）；烧完必须复位并核对运行时长。
# 2026-11-24 重写（S 线 D04）：旧版默认指向已删除的 build-g4lv 目录，且用 pyocd 烧录。
# 状态：主机侧语法与路径检查已做；真机上的完整一遍待设备空闲时补（见 S 线计划 C03）。
set -u
cd "$(dirname "$0")/.." || exit 1
CHIP=STM32H743II
PROBE=0d28:0204
ELF="build/tcs_main.elf"
CHECK_ONLY=0
for a in "$@"; do
    case "$a" in
        --check-only) CHECK_ONLY=1 ;;
        *) ELF="$a" ;;
    esac
done
[ -f "$ELF" ] || { echo "找不到 ELF：$ELF"; exit 1; }
export TCS_ELF="$ELF"

echo "镜像 $ELF  $(stat -c %s "$ELF") 字节  md5=$(md5sum "$ELF" | cut -d' ' -f1)"

if [ "$CHECK_ONLY" = 0 ]; then
    echo "== 下载（probe-rs，擦写后自带校验）=="
    probe-rs download "$ELF" --chip $CHIP --probe $PROBE 2>&1 | tail -1 || { echo "下载失败"; exit 1; }
    echo "== 复位 =="
    probe-rs reset --chip $CHIP --probe $PROBE 2>&1 | tail -1
    sleep 4
fi

echo "== 多点回读校验（Flash 内容对 ELF 的 bin）=="
BIN=$(mktemp -t tcs_flash_XXXXXX.bin)
arm-none-eabi-objcopy -O binary "$ELF" "$BIN" || exit 1
python - "$BIN" "$CHIP" "$PROBE" <<'PY'
import struct, subprocess, sys
b = open(sys.argv[1], 'rb').read()
chip, probe = sys.argv[2], sys.argv[3]
def rd(addr, n=4):
    out = subprocess.run(['probe-rs', 'read', '--chip', chip, '--probe', probe,
                         'b32', hex(addr), str(n)], capture_output=True, text=True).stdout
    for ln in out.splitlines():
        if ':' in ln:
            try:
                return [int(t, 16) for t in ln.split(':', 1)[1].split()][:n]
            except ValueError:
                pass
    return []
bad = 0
for off in sorted({0, 0x20000, 0x40000, 0x60000, 0x78000, max(0, len(b) - 16)}):
    if off + 16 > len(b):
        continue
    want = list(struct.unpack('<4I', b[off:off + 16]))
    got = rd(0x08000000 + off)
    ok = got == want
    bad += 0 if ok else 1
    print('  0x%08X  %s' % (0x08000000 + off, 'OK' if ok else 'MISMATCH got=%s want=%s' % (got, want)))
sys.exit(1 if bad else 0)
PY
RC=$?
rm -f "$BIN"
[ "$RC" -ne 0 ] && { echo "回读校验失败：板上内容与 $ELF 不一致"; exit 1; }

echo "== 运行态核对（版本串 + 主循环计数）=="
mkdir -p build
python tools/mb_console.py "ver" "stat" 2>&1 | grep -vE "^\[(MB|NET)\]" | head -4 | tee build/flash_verify.txt
if grep -qE "loop=[1-9][0-9]* " build/flash_verify.txt && grep -qE "^tcs v" build/flash_verify.txt; then
    echo "[flash] PASS"
else
    echo "[flash] FAIL：没读到版本串或主循环没在跑（loop=0 / 控制台无应答）"; exit 1
fi
