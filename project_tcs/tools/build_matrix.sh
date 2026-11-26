#!/bin/bash
# tools/build_matrix.sh — 多档位 + 从零构建矩阵（v2 架构；S 线 C02）
#
# 做什么：每个档位都删掉构建目录重新 configure + build（"从零"），逐档记录
#        退出码 / error 数 / 告警数 / FLASH、DTCMRAM、AXIRAM 占用。
# 档位（H743）：
#        PROD  交付档（TCS_PROFILE=PROD）        BENCH  台架档（本地闭环 + 台架命令）
#        SIM   仿真档（TCS_PROFILE=SIM）          SIMWG  板内仿真器（-DTCS_SIM=ON，WP-G）
# 可选：  --esp32      再做 ESP32 默认档从零构建（先 source IDF 环境）
#        --diag       再做 ESP32 诊断档（sdkconfig.defaults + CONFIG_TCS_DIAG=y；需同时加 --esp32）
#        --site       再做 ESP32 现场档（sdkconfig.site：PROD + CONFIG_TCS_SIM=y；文件不入库，需自备）
#        三个 ESP32 档各用一份自己的 sdkconfig.matrix.<档>（副本），工作树里的 sdkconfig / sdkconfig.site
#        只读不写。2026-11-24 教训：条件编译的代码必须在让它生效的那一档上编译——link_task.h 缺
#        esp_err.h 就是"默认档 0 告警、SIM/DIAG 档编译失败"。
# 门禁②：交付档干净核对（tools/site/verify_prod_clean.py）——PROD 产物里不得有调试/台架钩子与命令，
#        台架/仿真/诊断档里它们必须还在（证明核对工具查得出来）。任一不符 → 退出码 1。
#        --twice      PROD 档再从零构建一次，比对两次 tcs_main.bin 的 md5（可复现构建；
#                     __DATE__/__TIME__ 与绝对路径会让 md5 不同，所以同时给出"剔除后的可比 md5"见下）；
#                     加了 --esp32 时 ESP32 默认档也再从零构建一次，比对 app / bootloader / 分区表的 sha256
#        --esp32-only 跳过 H743 四档，只做 ESP32（H743 四档每次从零约 5 分钟；等价于 --esp32 再加跳过）
#        --keep       结束时不删 build-matrix/ 目录（默认保留，方便接着烧录；加 --clean 才删）
# 判定：任一档 build 失败、或告警数超过 tools/build_matrix_baseline.txt 里的基线 → 退出码 1。
#       基线文件第一次由本脚本生成（--init-baseline），之后新增告警必须显式更新基线并在提交里说明。
# 可复现 md5：对 ELF 做 arm-none-eabi-objcopy -O binary 之前，先把 .comment 段与含路径的调试段丢掉
#       （objcopy --remove-section=.comment --strip-debug）。
#       2026-11-24（审查 X05-F-06）起本脚本不再强设 SOURCE_DATE_EPOCH：CMake 在没有这个环境变量时
#       取 HEAD 提交时间生成构建标识（App/tcs/build_info.h.in），所以这里核对的就是默认路径——
#       同一提交两次从零构建，产物必须相同。工作树有未提交改动时版本串带 -dirty，内容相同仍然可比。
# 用法：bash tools/build_matrix.sh [--esp32-only | --esp32] [--diag] [--site] [--twice] [--init-baseline] [--clean]
#       产物目录 project_tcs/build-matrix/<档>/（已被 .gitignore 的 build-*/ 覆盖），日志在同目录 build.log
set -u
cd "$(dirname "$0")/.." || exit 1

CHIP=STM32H743II
TOOLCHAIN=arm-none-eabi-gcc.cmake
OUT=build-matrix
BASE=tools/build_matrix_baseline.txt
DO_ESP32=0; DO_DIAG=0; DO_SITE=0; DO_TWICE=0; INIT_BASE=0; CLEAN=0; DO_H743=1
for a in "$@"; do
    case "$a" in
        --esp32) DO_ESP32=1 ;;
        --esp32-only) DO_ESP32=1; DO_H743=0 ;;   # 只做 ESP32（H743 四档每次都是从零，约 5 分钟）
        --diag) DO_DIAG=1 ;;
        --site) DO_SITE=1 ;;
        --twice) DO_TWICE=1 ;;
        --init-baseline) INIT_BASE=1 ;;
        --clean) CLEAN=1 ;;
        *) echo "未知参数 $a"; sed -n '2,25p' "$0"; exit 2 ;;
    esac
done
mkdir -p "$OUT"

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
CLEAN_NOTE="未做（--esp32-only）"
REPRO_NOTE="未做（--esp32-only）"
if [ "$DO_H743" = 1 ]; then
h743 PROD  -DTCS_PROFILE=PROD;  check_base PROD
h743 BENCH -DTCS_PROFILE=BENCH; check_base BENCH
h743 SIM   -DTCS_PROFILE=SIM;   check_base SIM
h743 SIMWG -DTCS_PROFILE=BENCH -DTCS_SIM=ON; check_base SIMWG

# ---------- 交付档干净核对（S 线 C01，2026-11-24）----------
CLEAN_NOTE="未做（需要 python 与 arm-none-eabi-nm）"
if command -v python >/dev/null 2>&1 && command -v arm-none-eabi-nm >/dev/null 2>&1; then
    export PYTHONUTF8=1
    CLEAN_NOTE="PASS"
    python tools/site/verify_prod_clean.py --elf "$OUT/PROD/tcs_main.elf" \
        || { CLEAN_NOTE="FAIL（交付档产物里有钩子，见上方明细）"; FAIL=1; }
    for p in BENCH SIM SIMWG; do
        python tools/site/verify_prod_clean.py --elf "$OUT/$p/tcs_main.elf" --expect-hooks \
            || { CLEAN_NOTE="FAIL（$p 档里钩子不全，见上方明细）"; FAIL=1; }
    done
    # tools/mb_console.py 的"板上固件身份核对"离线自测：同源镜像 0 处不符、异源镜像必有不符
    python tools/site/test_mb_fwcheck.py "$OUT" || { CLEAN_NOTE="FAIL（固件身份核对自测不过）"; FAIL=1; }
fi

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
fi   # DO_H743

# ---------- ESP32 ----------
ESP_NOTE="未做（加 --esp32 / --diag / --site）"
if [ "$DO_ESP32" = 1 ] || [ "$DO_DIAG" = 1 ] || [ "$DO_SITE" = 1 ]; then
    if ! command -v idf.py >/dev/null 2>&1; then
        echo "[esp32] FAIL：找不到 idf.py，先 source IDF 环境"; FAIL=1
    else
        (
            cd ../esp32 || exit 1
            APP=tcs_wireless.bin
            # esp32 <档名> <现成的 sdkconfig（只读，脚本复制一份来用）>
            # sdkconfig.matrix.<档> 与 build-matrix-<档> 都是本脚本的产物，先清掉再建。
            esp32() {
                local name="$1" cfg="$2" rc err warn
                rm -rf "build-matrix-$name" "sdkconfig.matrix.$name" "sdkconfig.matrix.$name.old"
                cp "$cfg" "sdkconfig.matrix.$name"
                echo "=== [esp32-$name] 从零构建（配置取自 $cfg，原文件不动）"
                idf.py -B "build-matrix-$name" -D SDKCONFIG="sdkconfig.matrix.$name" build > "build-matrix-$name.build.log" 2>&1
                rc=$?
                err=$(grep -acE ' error:|FAILED:' "build-matrix-$name.build.log")
                warn=$(grep -acE 'warning:' "build-matrix-$name.build.log")
                echo "[esp32-$name] exit=$rc error=$err warning=$warn"
                [ "$rc" -ne 0 ] && return 1
                return 0
            }
            if [ "$DO_ESP32" = 1 ]; then
                # 默认档 = sdkconfig.defaults 生成出来的完整配置。先用 set-target 生成到本脚本自己的
                # sdkconfig.matrix.seed——这一步会重写它指定的那个 sdkconfig，所以绝不能指向工作树的
                # sdkconfig（2026-11-24 首次运行时就是这样把它删掉重建的）。
                rm -rf build-matrix-seed sdkconfig.matrix.seed sdkconfig.matrix.seed.old
                idf.py -B build-matrix-seed -D SDKCONFIG=sdkconfig.matrix.seed set-target esp32s3 > build-matrix-seed.settarget.log 2>&1 || exit 1
                esp32 default sdkconfig.matrix.seed || exit 1
            fi
            if [ "$DO_DIAG" = 1 ]; then
                [ -f sdkconfig.matrix.seed ] || { echo "[esp32-diag] 需要 --esp32 先生成默认配置"; exit 1; }
                { cat sdkconfig.matrix.seed; echo "CONFIG_TCS_DIAG=y"; } > sdkconfig.matrix.diagbase
                esp32 diag sdkconfig.matrix.diagbase || exit 1
                rm -f sdkconfig.matrix.diagbase
            fi
            if [ "$DO_SITE" = 1 ]; then
                [ -f sdkconfig.site ] || { echo "[esp32-site] FAIL：esp32/sdkconfig.site 不存在"; exit 1; }
                esp32 site sdkconfig.site || exit 1
            fi
            # 可复现（审查 X05-F-06 的关单条件："H743 与 ESP32 各一次两次干净构建 sha256 相同"）：
            # 默认档再从零构建一次，逐个比对 app / bootloader / 分区表的 sha256。
            if [ "$DO_TWICE" = 1 ] && [ -f "build-matrix-default/$APP" ]; then
                rm -rf build-matrix-repro sdkconfig.matrix.repro sdkconfig.matrix.repro.old
                cp sdkconfig.matrix.default sdkconfig.matrix.repro
                echo "=== [esp32-repro] 默认档再从零构建一次（可复现构建）"
                idf.py -B build-matrix-repro -D SDKCONFIG=sdkconfig.matrix.repro build > build-matrix-repro.build.log 2>&1 \
                    || { echo "[esp32-repro] exit!=0（见 build-matrix-repro.build.log）"; exit 1; }
                same=1
                for f in "$APP" bootloader/bootloader.bin partition_table/partition-table.bin; do
                    a=$(sha256sum "build-matrix-default/$f" | cut -c1-16); b=$(sha256sum "build-matrix-repro/$f" | cut -c1-16)
                    if [ "$a" = "$b" ]; then echo "[esp32-repro] $f  sha256 $a  相同"
                    else same=0; echo "[esp32-repro] $f  两次构建不同：$a / $b"; fi
                done
                echo "[esp32-repro] 默认档两次从零构建 sha256 $([ $same = 1 ] && echo 相同 || echo 不同)"
                [ "$same" = 1 ] || exit 1
                rm -rf build-matrix-repro sdkconfig.matrix.repro sdkconfig.matrix.repro.old build-matrix-repro.build.log
            fi
            # 干净核对：默认档不得含台架命令；诊断档必须全含（证明查得出来）；
            # 现场档（SIM）只含 linkmute / linkloop / webtest / water 这几条 SIM 也有的（S 线 i3 加 water）。
            V=../project_tcs/tools/site/verify_prod_clean.py
            export PYTHONUTF8=1
            python "$V" --esp32 "build-matrix-default/$APP" || { echo "[esp32-default] 干净核对 FAIL"; exit 1; }
            if [ -f "build-matrix-diag/$APP" ]; then
                python "$V" --esp32 "build-matrix-diag/$APP" --esp32-expect-all || { echo "[esp32-diag] 台架命令不全"; exit 1; }
            fi
            if [ -f "build-matrix-site/$APP" ]; then
                python "$V" --esp32 "build-matrix-site/$APP" --esp32-expect linkmute --esp32-expect linkloop \
                    --esp32-expect webtest --esp32-expect water || { echo "[esp32-site] 命令集与现场档不符"; exit 1; }
            fi
            echo "[esp32] 干净核对 PASS（默认档无台架命令；诊断档 / 现场档命令集相符）"
            exit 0
        ) || FAIL=1
        ESP_NOTE="见上（日志在 esp32/build-matrix-*.log）"
    fi
fi

if [ "$INIT_BASE" = 1 ]; then
    mv "$BASE.new" "$BASE"
    echo "基线已写入 $BASE："; cat "$BASE"
    echo "注意：基线只是上限，不是成绩；新增告警要先修，修不了再改基线并在提交里说明。"
fi

if [ "$CLEAN" = 1 ]; then
    rm -rf "$OUT"
    rm -f ../esp32/build-matrix*.log ../esp32/sdkconfig.matrix.*
fi

echo "================ build_matrix 汇总 ================"
echo "可复现构建：$REPRO_NOTE"
echo "交付档干净核对：$CLEAN_NOTE"
echo "ESP32：$ESP_NOTE"
if [ "$FAIL" -ne 0 ]; then
    echo "build_matrix: FAIL"
    exit 1
fi
echo "build_matrix: PASS"
