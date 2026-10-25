#!/usr/bin/env bash
# esp32/tests/run_m207_tests.sh — R1b ESP32 侧纯函数单测一键跑（Q6 扩服务层）
# 与 project_tcs/tests/run_all.sh 同款编译器选择（gcc 缺省走 MSVC vcvars64）。
# 用法：bash tests/run_m207_tests.sh   （在 esp32 根目录）
set -u
cd "$(dirname "$0")/.." || exit 1
FAIL=0

IFLAGS="-Icomponents/tcs_m207/include"
VARS64="C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat"
OUT=build/tests_m207
mkdir -p "$OUT"

cl_run() {  # cl_run <out.exe> <src...> — bat 内 cd 到 OUT，产物/对象全落 OUT
    local exe="$1"; shift
    local cmd_line='cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I..\..\components\tcs_m207\include'
    for s in "$@"; do
        cmd_line="$cmd_line \"$(cygpath -w "$PWD/$s")\""
    done
    cmd_line="$cmd_line /Fe:$(basename "$exe")"
    {
        echo '@echo off'
        echo "call \"$VARS64\" >nul 2>&1"
        echo "cd /d \"$(cygpath -w "$PWD/$OUT")\""
        echo "$cmd_line"
        echo 'exit /b %ERRORLEVEL%'
    } > "$OUT/cl_compile.bat"
    cmd //c "$(cygpath -w "$PWD/$OUT")\\cl_compile.bat" > "$OUT/cl.log" 2>&1
}

run() {  # run <name> <test.c> <sources...>
    local name="$1" test="$2"; shift 2
    local bin="$OUT/tests_$name.exe"
    if ! cl_run "$bin" "$test" "$@"; then
        echo "[$name] COMPILE FAIL"; cat "$OUT/cl.log"; FAIL=1; return
    fi
    if "./$bin"; then echo "[$name] PASS"; else echo "[$name] FAIL"; FAIL=1; fi
}

run m207proto tests/test_m207_proto.c  components/tcs_m207/m207_proto.c
run m207golden tests/test_m207_golden.c components/tcs_m207/m207_proto.c

[ $FAIL = 0 ] && echo "m207-esp32-tests: ALL GREEN" || echo "m207-esp32-tests: HAS FAILURES"
exit $FAIL
