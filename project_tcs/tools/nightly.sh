#!/bin/bash
# tools/nightly.sh — H11 回归基线套件（任务书 v7.3 §2 H11）
#
# 步骤：①双构建 → ②PC 单测 run_all → ③活性 wp_check → ④基准帧 diff（三页入库
# tests/baseline，UI 意外变化 FAIL；兼作取证工具自验证——fb_dump 损坏必在此暴露）
# → ⑤parser 万条 fuzz → ⑥压力性能快照（sim 全开 worst-case，回填 §5）
# → ⑦M207 间歇丢包注入（10% × 10min 状态机不乱；NIGHTLY_SHORT=1 → 60s 快速档）
#
# 用法：bash tools/nightly.sh [--init]    （板载 sim 固件；--init 仅生成帧基线）
# 全部步骤结果汇总输出，任一 FAIL 退出码非零。
set -u
cd "$(dirname "$0")/.." || exit 1
FAIL=0
LOG=build/nightly.log
: > "$LOG"

step() {  # step <名> <命令...>
    local name="$1"; shift
    echo "=== [$name] $* ===" | tee -a "$LOG"
    if "$@" >> "$LOG" 2>&1; then
        echo "[$name] PASS" | tee -a "$LOG"
    else
        echo "[$name] FAIL (见 $LOG 尾部)" | tee -a "$LOG"
        FAIL=1
    fi
}

echo "=== nightly @ $(date '+%F %T') ===" | tee -a "$LOG"

# ① 双构建（正式版零泄漏 + sim 版）
cmake --build build >> "$LOG" 2>&1 && echo "[build] PASS" | tee -a "$LOG" || { echo "[build] FAIL"; FAIL=1; }
cmake --build build-sim >> "$LOG" 2>&1 && echo "[build-sim] PASS" | tee -a "$LOG" || { echo "[build-sim] FAIL"; FAIL=1; }

# ② PC 单测
step run_all bash tests/run_all.sh

# ③ 活性（板上 sim 固件 → 用 sim 符号表；重试 1 次——重负载后首个探针会话
#    可能将 CPU 拖过 IWDG 窗口致计数复位，属接口现象非产品缺陷，见台账）
wpck() { TCS_ELF=build-sim/tcs_main.elf bash tools/wp_check.sh || { sleep 5; TCS_ELF=build-sim/tcs_main.elf bash tools/wp_check.sh; }; }
step wp_check wpck
sleep 2

# ⑤ parser 万条 fuzz（确定性种子）
cl_fuzz() {
    cat > build/cl_fuzz.bat <<'EOF'
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "%~dp0."
cl /nologo /W4 /std:c11 /utf-8 /DTCS_UNIT_TEST /I..\App\tcs "..\tests\fuzz_m207.c" "..\App\tcs\m207.c" /Fe:fuzz_m207.exe
fuzz_m207.exe
exit /b %ERRORLEVEL%
EOF
    cmd //c "build\\cl_fuzz.bat"
}
step fuzz cl_fuzz

sleep 2
# ④ 基准帧 diff（fb_dump 自验证同源；--init 只生成基线）
if [ "${1:-}" = "--init" ]; then
    step frames python tools/nightly_frames.py init
    echo "[frames] 基线已生成，重跑 nightly 做比对" | tee -a "$LOG"
    exit $FAIL
fi
step frames python tools/nightly_frames.py diff

# ⑥ 压力性能快照：sim 全开 worst-case（COOL 态卡片+横幅活动 + M207 应答 + 体温源）
probe-rs reset --chip STM32H743II --probe 0d28:0204 >/dev/null 2>&1
sleep 4
TCS_ELF=build-sim/tcs_main.elf python tools/mb_console.py \
    "p defaults Y" "sim body 38.5" "sim arm" "p set 0x06 1" >/dev/null 2>&1
sleep 20   # worst-case 稳态（500ms 状态扫描 + 横幅 + 卡片重绘持续活动）
PERF=""
for _t in 1 2 3; do
    PERF=$(TCS_ELF=build-sim/tcs_main.elf python tools/mb_console.py "perf" 2>/dev/null)
    echo "$PERF" | grep -q "work avg=" && break
    sleep 3
done
echo "$PERF" | tee -a "$LOG"
echo "$PERF" | grep -q "work avg=" || { echo "[perf] FAIL: no perf output"; FAIL=1; }
# 稳态预算口径（§5）：每拍工作均值 ≤2000us、UI 全屏 ≤250(×0.1ms=25ms)、循环 ≥90/s。
# ovf（>10ms 突发）= 整页事件（切页/弹窗/布防沿整页重绘）的合理计数，记录不判 FAIL。
PERF_PASS=1
echo "$PERF" | grep -qE "work avg=([0-9]{1,3}|1[0-9]{3}|2000)us" || PERF_PASS=0
echo "$PERF" | awk '{for(i=1;i<=NF;i++) if($i ~ /ui_full=/){split($i,a,"="); if(a[2]+0>250) exit 1}}' || PERF_PASS=0
echo "$PERF" | grep -qE "rate=(9[0-9]|[1-9][0-9]{2,})/s" || PERF_PASS=0
[ "$PERF_PASS" = 1 ] || { echo "[perf] FAIL: 预算超标（见上行快照）"; FAIL=1; }
mkdir -p tests/baseline
echo "$PERF" > tests/baseline/perf_snapshot.txt

# ⑦ M207 间歇丢包注入：10% × 10min（快速档 60s）状态机不乱
DROP_MIN=10
[ "${NIGHTLY_SHORT:-0}" = "1" ] && DROP_MIN=1
TCS_ELF=build-sim/tcs_main.elf python tools/mb_console.py "sim m207 drop 10" >/dev/null 2>&1
sleep $((DROP_MIN * 60))
TCS_ELF=build-sim/tcs_main.elf python tools/mb_console.py "sim m207 drop 0" >/dev/null 2>&1
sleep 2
ST=""
for _t in 1 2 3; do
    ST=$(TCS_ELF=build-sim/tcs_main.elf python tools/mb_console.py "stat" 2>/dev/null)
    echo "$ST" | grep -q "tc1=" && break
    sleep 3
done
echo "$ST" | tee -a "$LOG"
# 状态机不乱判据：结束时 M207 在线、无失联报警、不在 SAFE
if echo "$ST" | grep -q "m207=1" && echo "$ST" | grep -q "alarm=0x0000" \
   && ! echo "$ST" | grep -q "ctrl=3"; then
    echo "[drop_inject] PASS (10% × ${DROP_MIN}min 状态机稳定)" | tee -a "$LOG"
else
    echo "[drop_inject] FAIL: $ST" | tee -a "$LOG"
    FAIL=1
fi

echo "=== nightly 结束：$([ $FAIL = 0 ] && echo ALL GREEN || echo HAS FAILURES) ===" | tee -a "$LOG"
exit $FAIL
