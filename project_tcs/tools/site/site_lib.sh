#!/bin/bash
# site_lib.sh — 真机脚本的公共函数（S 线 i3，条目 12；D6 第八节的教训落成门禁）。由真机脚本 source。
#
# 为什么有它（D6 第八节，2026-11-26）：真机脚本跑到一半、有人在同一个工作树里合并了新代码，
# 而 `idf.py flash` **会先构建再烧**——"烧回交付档"实际烧的是新源码。两端协议版本从此不配对，
# 屏拒收全部快照（表现为"主控连接中断"），而脚本输出里写的"已烧回交付档"不成立。
#
# 三条规则（真机脚本必须遵守，见同目录 README 的「真机脚本的护栏」）：
#   1) 真机脚本运行期间不改同一个工作树（不合并、不改源码、不切分支）；
#   2) 烧录前先过一次 `guard <期望HEAD> [档名]`：HEAD 必须等于期望值、已跟踪文件必须干净、
#      固件必须不早于源码；不满足就返回 1，调用方必须 `|| exit 1`；
#   3) 烧录前后各算一次固件 sha256（`FLASH` 会做）：变了就打一行 REBUILT——这一步的
#      "自动重建 + 烧录"结果不能当证据，要重新登记。
#
# 用法（真机脚本开头）：
#   . "$(dirname "$0")/site_lib.sh"
#   guard "$(git -C "$R" rev-parse --short=9 HEAD)" default || exit 1
#   FLASH default d7-step0            # idf.py -B build-matrix-default flash 并做 sha256 前后比对
#
# 环境变量：
#   TCS_SITE_REPO  工作树根（缺省：本文件所在仓库的根）
#   TCS_SITE_PORT  串口（缺省 COM3）
#   TCS_SITE_LOG   日志目录（缺省系统临时目录下的 tcs_site/）
#   TCS_SITE_BAUD  烧录波特率（缺省 921600）

SITE_LIB_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
R=${TCS_SITE_REPO:-$(git -C "$SITE_LIB_DIR" rev-parse --show-toplevel 2>/dev/null)}
PORT=${TCS_SITE_PORT:-COM3}
BAUD=${TCS_SITE_BAUD:-921600}
T=${TCS_SITE_LOG:-${TMPDIR:-/tmp}/tcs_site}
[ -n "$R" ] || { echo "!! site_lib：找不到工作树（设 TCS_SITE_REPO 或在本仓库里 source）"; return 1 2>/dev/null || exit 1; }
mkdir -p "$T"
export PYTHONUTF8=1
ts() { date '+%T'; }

# guard <期望HEAD> [档名] —— 档名用于“固件必须不早于源码”那一条（build-matrix-<档>/tcs_wireless.bin）
guard() {
    local h d
    h=$(git -C "$R" rev-parse --short=9 HEAD)
    d=$(git -C "$R" status --porcelain --untracked-files=no | wc -l)
    echo "guard: HEAD=$h dirty=$d expected=$1 $(ts)"
    if [ "$h" != "$1" ] || [ "$d" != "0" ]; then
        echo "!! guard FAIL：工作树与预期不符（真机脚本运行期间不得改动同一个工作树），停。"
        return 1
    fi
    # 固件必须比最新的源码新：源码改过而没重建，烧上去的就是旧二进制
    local prof=$2 bin newest
    bin=$R/esp32/build-matrix-$prof/tcs_wireless.bin
    if [ -n "$prof" ] && [ -f "$bin" ]; then
        newest=$(git -C "$R" ls-files -- esp32/main esp32/components protocol | grep -a -E '\.(c|h|cpp|inc|ld|pem)$|CMakeLists\.txt$|Kconfig|sdkconfig' \
                 | xargs -I{} stat -c '%Y {}' "$R/{}" 2>/dev/null | sort -rn | head -1)   # 只看会进固件的文件，文档不算
        if [ -n "$newest" ] && [ "${newest%% *}" -gt "$(stat -c '%Y' "$bin")" ]; then
            echo "!! guard FAIL：有源码比 $bin 新（${newest#* }），先重建再烧。"
            return 1
        fi
    fi
    return 0
}

# IDF <命令...> —— 在 IDF 环境里、在 $R/esp32 下执行
IDF()  { ( source ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/activate_idf.sh >/dev/null 2>&1; cd "$R/esp32" && "$@" ); }

# FLASH <default|diag|site|selfloop> <标签>
# 注意：idf.py flash **会先构建**。烧录前后各算一次 sha256，变了就说明这次烧的是新构建——
# 输出里打 REBUILT，这一步的结果不得直接写进证据（要重新登记）。
FLASH() {
    local prof=$1 tag=$2 bin b a rc
    bin=$R/esp32/build-matrix-$prof/tcs_wireless.bin
    b=$(sha256sum "$bin" 2>/dev/null | cut -c1-16)
    IDF idf.py -B "build-matrix-$prof" -D "SDKCONFIG=sdkconfig.matrix.$prof" -p "$PORT" -b "$BAUD" flash \
        > "$T/site_flash_$tag.out" 2>&1
    rc=$?
    a=$(sha256sum "$bin" 2>/dev/null | cut -c1-16)
    echo "flash($prof/$tag) rc=$rc sha_before=${b:-none} sha_after=${a:-none} $(ts)"
    if [ "${b:-x}" != "${a:-y}" ]; then
        echo "!! REBUILT：烧的固件与烧录前不是同一份（idf.py flash 先构建）——这一步的结果不能当证据"
    fi
    return $rc
}
