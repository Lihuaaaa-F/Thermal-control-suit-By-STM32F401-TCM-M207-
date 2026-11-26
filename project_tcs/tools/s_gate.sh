#!/bin/bash
# s_gate.sh — 集成门禁一键跑（S-G x3 条目 5；把原来只在协调者 %TEMP%\tcs_site\i2_gate.sh 里的流程收进仓库）
#
# 用法：bash project_tcs/tools/s_gate.sh <上一个已过门禁的提交>
#   例：bash project_tcs/tools/s_gate.sh 85a009c
#
# 做什么（顺序固定）：
#   1) H743 四档从零构建 + 可复现比对        tools/build_matrix.sh --twice
#   2) 台架档（现场工具要用的那一档）        tools/build_bench.sh
#   3) 主机单测 / 门禁全量                   tests/run_all.sh
#   4) ESP32 三档（默认 / 诊断 / 现场）      tools/build_matrix.sh --esp32 --diag --site
#      —— 只在 esp32/ 下有非文档改动时才跑（改的只是 .md 就别浪费 15 分钟）
#
# 前置：工作树干净（untracked 也算脏）、<上一个已过门禁的提交> 是个可见提交。
# 日志：只写自己命名的文件，都在 project_tcs/build-gate/s_gate_<步骤>.log。
# 结论：最后只打印一行——全过是 "GATE: ALL GREEN"，任何一步非零就是 "GATE: FAIL" 并以非零退出。
#
# 内存纪律：默认限并发 2（开发机 16 GB，空闲常常只有 3 GB）；要提速自己 export 覆盖。
set -u
cd "$(dirname "$0")/.." || { echo "GATE: FAIL"; exit 1; }        # project_tcs/
PROJ=$PWD
REPO=$(git -C "$PROJ" rev-parse --show-toplevel 2>/dev/null) || { echo "GATE: FAIL"; exit 1; }
export CMAKE_BUILD_PARALLEL_LEVEL="${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
export IDF_BUILD_JOBS="${IDF_BUILD_JOBS:-2}"
LOG_DIR="$PROJ/build-gate"
mkdir -p "$LOG_DIR"

fail() {  # fail <步骤名> <日志>
    echo "GATE: FAIL"
    echo "  ↳ 失败步骤：$1（日志：${2#$PROJ/}）" >&2
    exit 1
}

# ---------- 前置检查 ----------
PREV="${1:-}"
if [ -z "$PREV" ]; then
    echo "用法：bash project_tcs/tools/s_gate.sh <上一个已过门禁的提交>" >&2
    echo "GATE: FAIL"
    exit 2
fi
# HEAD 可见 / PREV 可见
git -C "$REPO" rev-parse --verify --quiet "HEAD^{commit}" >/dev/null \
    || fail "HEAD 不可见（不是 git 仓库或没有提交）" /dev/null
git -C "$REPO" rev-parse --verify --quiet "${PREV}^{commit}" >/dev/null \
    || fail "上一个已过门禁的提交 $PREV 不可见" /dev/null
# 工作树必须干净：门禁跑的是"某个提交"，脏工作树会让结论无法对应到提交
DIRTY=$(git -C "$REPO" status --porcelain)
if [ -n "$DIRTY" ]; then
    echo "$DIRTY" | head -10 >&2
    fail "工作树不干净（先提交或 stash；连 untracked 也算脏）" /dev/null
fi

# ---------- 1) H743 四档 + 可复现 ----------
if ! bash tools/build_matrix.sh --twice > "$LOG_DIR/s_gate_h743.log" 2>&1; then
    fail "H743 四档 --twice" "$LOG_DIR/s_gate_h743.log"
fi

# ---------- 2) 台架档 ----------
if ! bash tools/build_bench.sh > "$LOG_DIR/s_gate_bench.log" 2>&1; then
    fail "台架档 build-bench" "$LOG_DIR/s_gate_bench.log"
fi

# ---------- 3) 主机全量 ----------
if ! bash tests/run_all.sh > "$LOG_DIR/s_gate_run_all.log" 2>&1; then
    fail "tests/run_all.sh" "$LOG_DIR/s_gate_run_all.log"
fi

# ---------- 4) ESP32 三档（仅当 esp32/ 有非文档改动）----------
# 路径基准必须是仓库顶层：git diff 的 pathspec 默认相对当前目录，这里用 -C "$REPO" 钉在顶层，
# 否则在 project_tcs/ 里跑时 "-- esp32" 永远匹配不到，这一步会被静默跳过（等于没跑）。
ESP_CHANGES=$(git -C "$REPO" diff --name-only "$PREV" HEAD -- esp32 ':!*.md')
if [ -n "$ESP_CHANGES" ]; then
    if [ ! -f "$REPO/esp32/sdkconfig.site" ]; then
        fail "缺 esp32/sdkconfig.site（现场档配置不入库，需从主工作树复制）" /dev/null
    fi
    # IDF 环境：官方 py 与工具链都在 activate_idf.sh 里
    IDF_ENV="$HOME/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/activate_idf.sh"
    [ -f "$IDF_ENV" ] || IDF_ENV="$HOME/.zcode/skills/embedded-dev-loop/platforms/esp32/scripts/activate_idf.sh"
    if [ ! -f "$IDF_ENV" ]; then
        fail "找不到 activate_idf.sh（IDF 环境）" /dev/null
    fi
    # shellcheck disable=SC1090
    . "$IDF_ENV" > "$LOG_DIR/s_gate_idf_env.log" 2>&1
    if ! bash tools/build_matrix.sh --esp32 --diag --site > "$LOG_DIR/s_gate_esp32.log" 2>&1; then
        fail "ESP32 三档（默认/诊断/现场）" "$LOG_DIR/s_gate_esp32.log"
    fi
fi

echo "GATE: ALL GREEN"
exit 0
