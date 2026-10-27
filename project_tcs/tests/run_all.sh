#!/bin/bash
# tests/run_all.sh — PC 单元测试一键全绿（任务书 §5.1）
# 编译器自动选择：gcc（首选）→ MSVC cl（经 vcvars64）。
# 用法：bash tests/run_all.sh   （在 project_tcs 根目录）
set -u
cd "$(dirname "$0")/.." || exit 1
FAIL=0

IFLAGS="-IApp/tcs -IApp/proto -IApp/comm -ICore/Inc"
INC_CL="/IApp\\tcs /IApp\\proto /IApp\\comm"
VARS64="C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat"

cl_run() {  # cl_run <out.exe> <src...> — bat 内 cd 到 build，产物/对象全落 build
    local out="$1"; shift
    local cmd_line='cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /I..\App\tcs /I..\App\proto /I..\App\comm /I..\Core\Inc'
    for s in "$@"; do
        cmd_line="$cmd_line \"$(cygpath -w "$PWD/$s")\""
    done
    cmd_line="$cmd_line /Fe:$(basename "$out")"
    {
        echo '@echo off'
        echo "call \"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat\" >nul 2>&1"
        echo "cd /d \"$(cygpath -w "$PWD/build")\""
        echo "$cmd_line"
        echo 'exit /b %ERRORLEVEL%'
    } > build/cl_compile.bat
    cmd //c "build\\cl_compile.bat" > build/cl.log 2>&1
}

if command -v gcc >/dev/null 2>&1; then
    GCC=${GCC:-gcc}
    CFLAGS="-Wall -Wextra -std=c11 -O1 -DTCS_UNIT_TEST $IFLAGS"
    MODE=gcc
else
    MODE=cl
    mkdir -p build
    echo "[toolchain] gcc not found, using MSVC cl (vcvars64)"
fi

run() {  # run <name> <test.c> <sources...>
    local name="$1" test="$2"; shift 2
    local bin="build/tests_$name.exe"
    mkdir -p build
    if [ "$MODE" = gcc ]; then
        if ! $GCC $CFLAGS "$test" "$@" -o "$bin" 2> "build/tests_$name.err"; then
            echo "[$name] COMPILE FAIL"; cat "build/tests_$name.err"; FAIL=1; return
        fi
    else
        if ! cl_run "$bin" "$test" "$@"; then
            echo "[$name] COMPILE FAIL"; cat build/cl.log; FAIL=1; return
        fi
    fi
    if "./$bin"; then echo "[$name] PASS"; else echo "[$name] FAIL"; FAIL=1; fi
}

run m207        tests/test_m207_parse.c       App/tcs/m207.c
run m207golden  tests/test_m207_golden.c      App/tcs/m207.c
run registry    tests/test_param_registry.c   App/tcs/param_registry.c App/proto/proto.c
run control     tests/test_control.c          App/tcs/control.c App/tcs/param_registry.c App/tcs/post.c App/proto/proto.c
run post        tests/test_post.c             App/tcs/post.c App/tcs/param_registry.c App/proto/proto.c
run proto_comm  tests/test_proto_tcs.c        App/proto/proto.c App/comm/comm.c

run touchsession tests/test_touch_session.c   App/tcs/touch_session.c Core/Src/ft5446.c
run qr          tests/test_qr.c               App/tcs/qr/qrcodegen.c

# H13 二维码解码回读（PC 出图 → zxing-cpp 解码比对）
PYBIN=$(command -v python3 || command -v python)
if [ -n "$PYBIN" ]; then
    if "$PYBIN" tools/qr_verify.py build/test_qr.pbm "http://192.168.4.1"; then :; else FAIL=1; fi
else
    echo "[qr_verify] SKIP: python not found"; FAIL=1
fi

# TMP117 换算与健康计数（§11.2 PC 单测替代模拟器）
cat > build/test_tmp117.c <<'EOF'
#include <stdio.h>
#include "tmp117.h"
static int raise_cnt; static void raise(void){ raise_cnt++; }
int main(void){
    int fail=0;
    /* raw→°C：0.0078125 每 LSB */
    if (!(tmp117_raw_to_c(0) == 0.0f)) fail++;
    if (!(tmp117_raw_to_c(128) == 1.0f)) fail++;          /* 128 LSB = 1°C */
    if (!(tmp117_raw_to_c(2560) == 20.0f)) fail++;
    if (!(tmp117_raw_to_c(-1024) == -8.0f)) fail++;
    if (!(tmp117_raw_to_c(0x1FFF) > 63.99f && tmp117_raw_to_c(0x1FFF) < 64.01f)) fail++;
    /* 健康计数：10 连错 → 掉线沿 raise 一次；3 连成功 → 恢复数据 */
    tmp117_health_t h; tmp117_health_init(&h); raise_cnt=0;
    for (int i=0;i<10;i++) tmp117_health_step(&h,0,raise);
    if (!(h.ok==0 && raise_cnt==1)) fail++;
    tmp117_health_step(&h,0,raise);
    if (raise_cnt!=1) fail++;                              /* 掉线沿只触发一次 */
    for (int i=0;i<3;i++) tmp117_health_step(&h,1,raise);
    if (!(h.ok==1 && raise_cnt==1)) fail++;                /* 恢复不清报警位（raise 不再触发）*/
    printf("test_tmp117: %s\n", fail? "FAIL":"PASS");
    return fail;
}
EOF
run tmp117 build/test_tmp117.c App/tcs/tmp117.c

# WP-P4/P6：M207 高级参数注入矩阵（PC/sim 层，五行矩阵+golden+50ms+PDF 示例）
if [ -n "$PYBIN" ]; then
    if "$PYBIN" tests/test_m207_adv.py; then :; else FAIL=1; fi
else
    echo "[m207_adv] SKIP: python not found"; FAIL=1
fi

# H3 字库缺字门禁（WP-A.2a，永久）：UI 字符串 × 字库码表，缺字退出非零
PYBIN=$(command -v python3 || command -v python)
if [ -n "$PYBIN" ]; then
    if "$PYBIN" tools/font_audit.py; then :; else FAIL=1; fi
else
    echo "[font_audit] SKIP: python not found"; FAIL=1
fi

# RTT printf 调用点总数对账（W5 立；W4.2 vendor 口径 13→；W1a +3 日志点后=16）
# （App/rtt/ 为 vendor+薄壳域不计；16=业务面 13+W1a i2c4 三行；ltdc.c 4 处为
# HEAD 既有 G 线文件；增删调用点须同步本数并说明）
RTTN=$(grep -rn "SEGGER_RTT_printf" App Core --include=*.c | grep -v "App/rtt/" | wc -l)
if [ "$RTTN" -ne 16 ]; then
    echo "[rtt_count] FAIL: SEGGER_RTT_printf 调用点 $RTTN ≠ 16（增删须同步本门禁并说明）"
    FAIL=1
else
    echo "[rtt_count] PASS (16)"
fi

# 官方库审计门禁（W5，v13.4 §6）：未入清单/未归类文件 → FAIL
# 脚本内部自锚定仓库根（W5 改造）；新增源文件须同 commit 更新 docs/审计文件清单.txt
if [ -n "$PYBIN" ]; then
    if "$PYBIN" ../esp32/tools/official_lib_audit.py; then :; else FAIL=1; fi
else
    echo "[audit] SKIP: python not found"; FAIL=1
fi

# W2 转义向量单测（v13.4 §3.3）：真实 IDF cJSON 源编译（与固件同库）；
# IDF 路径机器相关（与 CMake 工具链绝对路径同性质），缺件即红不静默
CJSON_DIR="${IDF_CJSON:-/d/Espressif/frameworks/esp-idf-v5.5.5/components/json/cJSON}"
if [ -f "$CJSON_DIR/cJSON.c" ]; then
    if [ "$MODE" = gcc ]; then
        if $GCC -Wall -Wextra -std=c11 -O1 "$CJSON_DIR/cJSON.c" \
             tests/test_ws_json_escape.c -I"$CJSON_DIR" \
             -lm -o build/tests_ws_json.exe 2> build/ws_json.err \
           && ./build/tests_ws_json.exe; then :; else
            echo "[ws_json] FAIL"; cat build/ws_json.err 2>/dev/null; FAIL=1
        fi
    else
        CJW=$(cygpath -w "$CJSON_DIR")
        cat > build/ws_json.bat <<EOF
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "$(cygpath -w "$PWD/build")"
cl /nologo /W3 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I"$CJW" "$CJW\\cJSON.c" "..\\tests\\test_ws_json_escape.c" /Fe:tests_ws_json.exe
tests_ws_json.exe
exit /b %ERRORLEVEL%
EOF
        if cmd //c "build\\ws_json.bat"; then :; else echo "[ws_json] FAIL"; FAIL=1; fi
    fi
else
    echo "[ws_json] FAIL: IDF cJSON 源缺失（$CJSON_DIR，可用 IDF_CJSON 覆盖路径）"
    FAIL=1
fi

# W8 研学卡本地路径存在性门禁（红队建议采纳：41 条抓 1 条的实证抓力；0 bad 才绿）
if [ -n "$PYBIN" ]; then
    if "$PYBIN" - <<'PYEOF'
import io,glob,os,re,sys
bad=[]; n=0
for card in glob.glob('../docs/ref/研学卡_*.md'):
    for line in io.open(card,encoding='utf-8'):
        for m in re.finditer(r'(D:/Espressif/[\w/.-]+|/d/Espressif/[\w/.-]+)', line):
            p=m.group(1); n+=1
            win=p.replace('/d/','D:/',1) if p.startswith('/d/') else p
            win=re.sub(r'\.$','',win)
            if not os.path.exists(win): bad.append((os.path.basename(card),p))
print('[card_paths] %d checked, %d bad %s' % (n,len(bad),bad[:3] if bad else ''))
sys.exit(1 if bad else 0)
PYEOF
    then :; else FAIL=1; fi
else
    echo "[card_paths] SKIP: python not found"; FAIL=1
fi

# W4.2/R7：vendor RTT 完整性门禁（VENDOR_SHA256.txt CRLF 列 sha256sum 式机器比对；
# 终审 R-1：行尾 CR 用 tr 剥离，防 autocrlf/干净克隆把 \r 带进会话名）
VSHA=App/rtt/vendor/VENDOR_SHA256.txt
if [ -f "$VSHA" ]; then
    ok=1
    while IFS= read -r line; do
        line=$(printf '%s' "$line" | tr -d '\r')
        case "$line" in ''|\#*) continue;; esac
        h=$(printf '%s' "$line" | cut -d' ' -f1)
        name=$(printf '%s' "$line" | awk '{print $NF}')
        want=$(printf '%s' "$h" | tr -d ' ')
        got=$(sha256sum "App/rtt/vendor/$name" | cut -d' ' -f1)
        if [ "$want" != "$got" ]; then
            echo "[vendor_sha] FAIL: $name $got != $want"; ok=0
        fi
    done < "$VSHA"
    [ "$ok" = 1 ] && echo "[vendor_sha] PASS (5 files)" || FAIL=1
else
    echo "[vendor_sha] FAIL: $VSHA 缺失"; FAIL=1
fi

# MF-3（终审）：RTT printf 格式面门禁——F14(%f)/F16(%l) 归零（业务面，排除 App/rtt）
FMTF=$(grep -rn "SEGGER_RTT_printf" App Core --include=*.c | grep -v "App/rtt/" | grep -cE "%[0-9.]*[efg]")
FMTL=$(grep -rn "SEGGER_RTT_printf" App Core --include=*.c | grep -v "App/rtt/" | grep -cE "%[0-9.]*l[a-z]")
if [ "$FMTF" -eq 0 ] && [ "$FMTL" -eq 0 ]; then
    echo "[fmt_gate] PASS (%f=0 %l=0)"
else
    echo "[fmt_gate] FAIL: %f=$FMTF %l=$FMTL（浮点/长度修饰符须 snprintf 预格式化，见 F14/F16）"
    FAIL=1
fi

# MF-1（终审）：研学卡 ③ 行数常驻门禁（基线文件 docs/ref/研学卡_行数基线.txt）
CARD_BASE=../docs/ref/研学卡_行数基线.txt
if [ -f "$CARD_BASE" ] && [ -n "$PYBIN" ]; then
    if "$PYBIN" - "$CARD_BASE" <<'PYEOF2'
import io,sys,subprocess,re,shutil
GREP=shutil.which('grep') or 'grep'
bad=0; n=0
for line in io.open(sys.argv[1],encoding='utf-8'):
    line=line.strip()
    if not line or line.startswith('#'): continue
    parts=line.split(';;')
    if len(parts)!=4: continue
    name,dirs,pat,want=parts[0],parts[1].split(),parts[2],int(parts[3])
    n+=1
    out=subprocess.run([GREP,'-rnE',pat]+dirs+['--include=*.c'],
                       capture_output=True,text=True).stdout
    got=len([l for l in out.splitlines() if l])
    if got!=want:
        print('[card_surface] FAIL %s: got=%d want=%d' % (name,got,want)); bad+=1
print('[card_surface] %s (%d cards)' % ('PASS' if not bad else 'FAIL',n))
sys.exit(1 if bad else 0)
PYEOF2
    then :; else FAIL=1; fi
else
    echo "[card_surface] SKIP: 基线或 python 缺失"; FAIL=1
fi

echo "==================="
if [ "$FAIL" = 0 ]; then echo "run_all: ALL GREEN"; else echo "run_all: HAS FAILURES"; fi
exit $FAIL
