#!/bin/bash
# tools/build_matrix.sh — 多档位 + 从零构建矩阵（v2 架构；S 线 C02）
#
# 做什么：每个档位都删掉构建目录重新 configure + build（"从零"），逐档记录
#        退出码 / error 数 / 告警数 / FLASH、DTCMRAM、AXIRAM 占用。
# 档位（H743）：
#        PROD  交付档（TCS_PROFILE=PROD）        BENCH  台架档（本地闭环 + 台架命令）
#        SIM   仿真档（TCS_PROFILE=SIM）          SIMWG  板内仿真器（-DTCS_SIM=ON，WP-G）
# 可选：  --esp32      再做 ESP32 默认档从零构建（先 source IDF 环境）
#        --site       再做 ESP32 现场档（sdkconfig.site：PROD + CONFIG_TCS_SIM=y；文件不入库，需自备）
#        --twice      PROD 档再从零构建一次，比对两次 tcs_main.bin 的 md5（可复现构建；
#                     __DATE__/__TIME__ 与绝对路径会让 md5 不同，所以同时给出"剔除后的可比 md5"见下）
#        --keep       结束时不删 build-matrix/ 目录（默认保留，方便接着烧录；加 --clean 才删）
# 判定：任一档 build 失败、或告警数超过 tools/build_matrix_baseline.txt 里的基线 → 退出码 1。
#       基线文件第一次由本脚本生成（--init-baseline），之后新增告警必须显式更新基线并在提交里说明。
# 可复现 md5：对 ELF 做 arm-none-eabi-objcopy -O binary 之前，先把 .comment 段与含路径的调试段丢掉
#       （objcopy --remove-section=.comment --strip-debug），并固定 SOURCE_DATE_EPOCH。
#       固件里的版本字符串用 __DATE__/__TIME__，跨天构建必然不同——所以只在同一天内可比；
#       脚本两次构建是背靠背做的，天不换就应一致，不一致会如实报出来。
# 用法：bash tools/build_matrix.sh [--esp32] [--site] [--twice] [--init-baseline] [--clean]
#       产物目录 project_tcs/build-matrix/<档>/（已被 .gitignore 的 build-*/ 覆盖），日志在同目录 build.log
set -u
cd "$(dirname "$0")/.." || exit 1

CHIP=STM32H743II
TOOLCHAIN=arm-none-eabi-gcc.cmake
OUT=build-matrix
BASE=tools/build_matrix_baseline.txt
DO_ESP32=0; DO_SITE=0; DO_TWICE=0; INIT_BASE=0; CLEAN=0
for a in "$@"; do
    case "$a" in
        --esp32) DO_ESP32=1 ;;
        --site) DO_SITE=1 ;;
        --twice) DO_TWICE=1 ;;
        --init-baseline) INIT_BASE=1 ;;
        --clean) CLEAN=1 ;;
        *) echo "未知参数 $a"; sed -n '2,25p' "$0"; exit 2 ;;
    esac
done
mkdir -p "$OUT"
export SOURCE_DATE_EPOCH="${SOURCE_DATE_EPOCH:-1792989165}"   # 与 scripts/tl17_alternate.sh 同一锚点

declare -A BASE_N=()
if [ -f "$BASE" ] && [ "$INIT_BASE" = 0 ]; then
    while read -r k v; do [ -n "$k" ] && BASE_N[$k]=$v; done < "$BASE"
fi

FAIL=0
declare -A NOW_N=()

h743() {  # h743 <档名> <cmake 额外参数...>
    local name="$1"; shift
    local dir="$OUT/$name"
    echo "=== [$name] 从零构建：rm -rf $dir && cmake && build"
    rm -rf "$dir"
    if ! cmake -S . -B "$dir" -G Ninja -DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN "$@" > "$OUT/$name.cfg.log" 2>&1; then
        echo "[$name] CONFIGURE FAIL（见 $OUT/$name.cfg.log）"; FAIL=1; return 1
    fi
    cmake --build "$dir" > "$OUT/$name.build.log" 2>&1
    local rc=$?
    local err warn
    err=$(grep -acE '(^|[^a-z])error:|Error |FAILED:' "$OUT/$name.build.log")
    warn=$(grep -acE 'warning:' "$OUT/$name.build.log")
    NOW_N[$name]=$warn
    # 占用取自链接器的 Memory region 表（行首有空格，先剥掉回车与颜色转义）
    local size=""
    size=$(grep -aE "FLASH:|DTCMRAM:|AXIRAM:" "$OUT/$name.build.log" | tr -d '\r' | sed -E 's/\x1b\[[0-9;]*m//g' \
           | awk '{printf "%s%s %s %s; ", $1, "", $2, $4}' | sed -E 's/; *$//')
    echo "[$name] exit=$rc error=$err warning=$warn ${size:-（没有产物）}"
    [ "$rc" -ne 0 ] && FAIL=1
    [ "$err" -ne 0 ] && FAIL=1
    return 0
}

check_base() {  # check_base <档名>
    local n="$1"
    if [ "$INIT_BASE" = 1 ]; then
        echo "$n ${NOW_N[$n]}" >> "$BASE.new"
        return
    fi
    if [ -z "${BASE_N[$n]:-}" ]; then
        echo "[$n] 没有告警基线（$BASE 里缺这一行）——用 --init-baseline 生成后提交"
        FAIL=1
    elif [ "${NOW_N[$n]}" -gt "${BASE_N[$n]}" ]; then
        echo "[$n] 告警数 ${NOW_N[$n]} 超过基线 ${BASE_N[$n]}（见 $OUT/$n.build.log）"
        FAIL=1
    fi
}

repro_md5() {  # repro_md5 <档目录>
    arm-none-eabi-objcopy --remove-section=.comment --strip-debug -O binary "$1/tcs_main.elf" "$1/tcs_main.repro.bin" \
        && md5sum "$1/tcs_main.repro.bin" | cut -d' ' -f1
}

# ---------- H743 四档 ----------
h743 PROD  -DTCS_PROFILE=PROD;  check_base PROD
h743 BENCH -DTCS_PROFILE=BENCH; check_base BENCH
h743 SIM   -DTCS_PROFILE=SIM;   check_base SIM
h743 SIMWG -DTCS_PROFILE=BENCH -DTCS_SIM=ON; check_base SIMWG

# ---------- 可复现：PROD 再从零构建一次 ----------
REPRO_NOTE="未做（加 --twice）"
if [ "$DO_TWICE" = 1 ]; then
    M1=$(repro_md5 "$OUT/PROD")
    RAW1=$(md5sum "$OUT/PROD/tcs_main.bin" | cut -d' ' -f1)
    h743 PROD2 -DTCS_PROFILE=PROD >/dev/null
    M2=$(repro_md5 "$OUT/PROD2")
    RAW2=$(md5sum "$OUT/PROD2/tcs_main.bin" | cut -d' ' -f1)
    REPRO_NOTE="剔除 .comment 与调试信息后 md5 $M1 / $M2 → $([ "$M1" = "$M2" ] && echo 相同 || echo 不同)；原始 bin md5 $([ "$RAW1" = "$RAW2" ] && echo 相同 || echo 不同)"
    [ "$M1" = "$M2" ] || { echo "[repro] FAIL：同一源码两次从零构建产物不同"; FAIL=1; }
fi

# ---------- ESP32 ----------
ESP_NOTE="未做（加 --esp32 / --site）"
if [ "$DO_ESP32" = 1 ] || [ "$DO_SITE" = 1 ]; then
    if ! command -v idf.py >/dev/null 2>&1; then
        echo "[esp32] FAIL：找不到 idf.py，先 source IDF 环境"; FAIL=1
    else
        (
            cd ../esp32 || exit 1
            if [ "$DO_ESP32" = 1 ]; then
                rm -rf build-matrix sdkconfig
                echo "=== [esp32-default] 从零构建"
                idf.py -B build-matrix set-target esp32s3 > build-matrix.settarget.log 2>&1 && \
                    idf.py -B build-matrix build > build-matrix.build.log 2>&1
                rc=$?
                echo "[esp32-default] exit=$rc error=$(grep -acE ' error:|FAILED:' build-matrix.build.log) warning=$(grep -acE 'warning:' build-matrix.build.log)"
                [ "$rc" -ne 0 ] && exit 1
            fi
            if [ "$DO_SITE" = 1 ]; then
                [ -f sdkconfig.site ] || { echo "[esp32-site] FAIL：esp32/sdkconfig.site 不存在"; exit 1; }
                rm -rf build-matrix-site
                echo "=== [esp32-site] 从零构建（sdkconfig.site）"
                idf.py -B build-matrix-site -D SDKCONFIG=sdkconfig.site set-target esp32s3 > build-matrix-site.settarget.log 2>&1 && \
                    idf.py -B build-matrix-site -D SDKCONFIG=sdkconfig.site build > build-matrix-site.build.log 2>&1
                rc=$?
                echo "[esp32-site] exit=$rc error=$(grep -acE ' error:|FAILED:' build-matrix-site.build.log) warning=$(grep -acE 'warning:' build-matrix-site.build.log)"
                [ "$rc" -ne 0 ] && exit 1
            fi
            exit 0
        ) || FAIL=1
        ESP_NOTE="见上（日志在 esp32/build-matrix*.log）"
    fi
fi

if [ "$INIT_BASE" = 1 ]; then
    mv "$BASE.new" "$BASE"
    echo "基线已写入 $BASE："; cat "$BASE"
    echo "注意：基线只是上限，不是成绩；新增告警要先修，修不了再改基线并在提交里说明。"
fi

if [ "$CLEAN" = 1 ]; then
    rm -rf "$OUT"
    rm -f ../esp32/build-matrix*.log
fi

echo "================ build_matrix 汇总 ================"
echo "可复现构建：$REPRO_NOTE"
echo "ESP32：$ESP_NOTE"
if [ "$FAIL" -ne 0 ]; then
    echo "build_matrix: FAIL"
    exit 1
fi
echo "build_matrix: PASS"
