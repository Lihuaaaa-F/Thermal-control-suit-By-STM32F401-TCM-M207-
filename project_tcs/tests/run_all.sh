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

echo "==================="
if [ "$FAIL" = 0 ]; then echo "run_all: ALL GREEN"; else echo "run_all: HAS FAILURES"; fi
exit $FAIL
