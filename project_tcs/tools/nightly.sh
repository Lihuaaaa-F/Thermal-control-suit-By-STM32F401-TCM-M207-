#!/bin/bash
# tools/nightly.sh — 夜间回归（v2 架构）
#
#   ① 从零多档位构建矩阵：H743 PROD/BENCH/SIM/SIMWG 各自从零构建并核对告警基线；
#      PROD 再构建一次比对 md5（可复现构建）；ESP32 默认档与现场档从零构建
#   ② PC 单测 run_all（含 fuzz、源码字节门禁、审计只读核对）
#   ③ 汇总；任一步 FAIL → 退出码非零
#
# 用法：先 source ESP-IDF 环境，再 bash tools/nightly.sh          （ESP32 那两步需要 idf.py）
#       现场档需要 esp32/sdkconfig.site（不入库）；没有就跳过现场档并如实写出。
#       耗时：约 25–35 分钟（全部从零）。
#
# 2026-11-24 重写（S 线 D04）。旧版的三项不再保留，原因如下：
#   - 旧 UI 的"三页基准帧 diff"：基准帧是 2026-11-15 已删除的旧 tcs_ui 栈的截图，永远对不上；
#   - "sim 全开 worst-case 压测快照"与"M207 间歇丢包注入"：都是 v1（H743 直连 M207、
#     H743 跑本地闭环）的口径，对应代码已随架构换向删除。M207 链路的丢包回归现在由
#     esp32/tests 的主机测试覆盖（run_m207_tests.sh，已并入 run_all）。
#   旧脚本内容在 git 历史里（提交信息 "chore(tools): v2 构建矩阵脚本；退役 v1 台架脚本" 之前）。
set -u
cd "$(dirname "$0")/.." || exit 1
FAIL=0
LOG=build/nightly.log
mkdir -p build
: > "$LOG"
t0=$(date +%s)
echo "=== nightly @ $(date '+%F %T') ===" | tee -a "$LOG"

echo "--- ① 构建矩阵 ---" | tee -a "$LOG"
ARGS="--twice"
command -v idf.py >/dev/null 2>&1 && ARGS="$ARGS --esp32" || echo "（没有 idf.py：跳过 ESP32，只做 H743）" | tee -a "$LOG"
[ -f ../esp32/sdkconfig.site ] && command -v idf.py >/dev/null 2>&1 && ARGS="$ARGS --site"
bash tools/build_matrix.sh $ARGS > build/nightly_matrix.txt 2>&1
RC=$?
cat build/nightly_matrix.txt >> "$LOG"
grep -aE "^\[[A-Za-z0-9]+\] exit=|可复现构建|ESP32：|build_matrix: (PASS|FAIL)" build/nightly_matrix.txt | tee -a "$LOG"
[ "$RC" -ne 0 ] && FAIL=1

echo "--- ② PC 单测 ---" | tee -a "$LOG"
if bash tests/run_all.sh > build/nightly_tests.txt 2>&1 && tail -1 build/nightly_tests.txt | grep -q "ALL GREEN"; then
    echo "[run_all] PASS（$(grep -acE '\] PASS' build/nightly_tests.txt) 项）" | tee -a "$LOG"
else
    echo "[run_all] FAIL（见 build/nightly_tests.txt）" | tee -a "$LOG"; FAIL=1
fi

echo "=== nightly 结束：$([ $FAIL = 0 ] && echo ALL GREEN || echo HAS FAILURES)，用时 $(( ($(date +%s) - t0) / 60 )) 分钟 ===" | tee -a "$LOG"
exit $FAIL
