#!/usr/bin/env bash
# esp32/tests/run_m207_tests.sh — R1b ESP32 侧纯函数单测一键跑（Q6 扩服务层）
# 与 project_tcs/tests/run_all.sh 同款编译器选择（gcc 缺省走 MSVC vcvars64）。
# 用法：bash tests/run_m207_tests.sh   （在 esp32 根目录）
set -u
cd "$(dirname "$0")/.." || exit 1
FAIL=0

IFLAGS="-Icomponents/tcs_m207/include -Icomponents/tcs_tmp117/include"
VARS64="C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat"
OUT=build/tests_m207
mkdir -p "$OUT"
# web 契约测试用**真实 cJSON 源**（与固件同库；IDF 路径机器相关，缺件即红不静默）
CJSON_DIR="${IDF_CJSON:-/d/Espressif/frameworks/esp-idf-v5.5.5/components/json/cJSON}"
EXTRA_INC=""        # run 可按用例追加 include（见下方 webstatus）

cl_run() {  # cl_run <out.exe> <src...> — bat 内 cd 到 OUT，产物/对象全落 OUT
    local exe="$1"; shift
    local cmd_line='cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /I..\..\components\tcs_m207\include /I..\..\components\tcs_tmp117\include /I..\..\components\tcs_core /I..\..\components\tcs_link\include /I..\..\components\tcs_link /I..\..\components\tcs_web\include'
    [ -n "$EXTRA_INC" ] && cmd_line="$cmd_line $EXTRA_INC"
    for s in "$@"; do
        case "$s" in
            /*) w="$(cygpath -w "$s")" ;;                 # 绝对路径（如 IDF 的 cJSON.c）
            *)   w="$(cygpath -w "$PWD/$s")" ;;
        esac
        cmd_line="$cmd_line \"$w\""
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
run tmp117pure tests/test_tmp117_pure.c    components/tcs_tmp117/tmp117_pure.c
run tcsproto  tests/test_tcs_proto.c
run control   tests/test_control_esp32.c   components/tcs_core/control.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G e2（X06-F-05 / M12-F-12）：M207 服务内核（m207_core.c）逐拍主机用例——首联、selfloop 九步移植、
# 探测、传感器开路、超温、写路径、离线停写、下行帧黄金样本
run m207core  tests/test_m207_core.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G e2（X03-F-07 / M12-F-08）：安全关断旁路（队列满也下一拍上线路、作废过期的开）、
# 队列满计数、"保存到模块"32 条不丢
run m207queue tests/test_m207_queue.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c

# U 线 W1：网页 WS 契约（st / log 消息，真实 cJSON）
if [ -f "$CJSON_DIR/cJSON.c" ]; then
    EXTRA_INC="/I\"$(cygpath -w "$CJSON_DIR")\""
    run webstatus tests/test_web_status.c components/tcs_web/web_status.c "$CJSON_DIR/cJSON.c"
    EXTRA_INC=""
else
    echo "[webstatus] FAIL: IDF cJSON 源缺失（$CJSON_DIR，可用 IDF_CJSON 覆盖路径）"; FAIL=1
fi

[ $FAIL = 0 ] && echo "esp32-tests: ALL GREEN" || echo "esp32-tests: HAS FAILURES"
exit $FAIL
