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
EXTRA_DEF=""        # run 可按用例追加预处理定义（见下方 tmp117sim）

cl_run() {  # cl_run <out.exe> <src...> — bat 内 cd 到 OUT，产物/对象全落 OUT
    local exe="$1"; shift
    local cmd_line='cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /I..\..\components\tcs_m207\include /I..\..\components\tcs_tmp117\include /I..\..\components\tcs_fan\include /I..\..\components\tcs_core /I..\..\components\tcs_link\include /I..\..\components\tcs_link /I..\..\components\tcs_web\include /I..\..\components\tcs_core\include'
    [ -n "$EXTRA_INC" ] && cmd_line="$cmd_line $EXTRA_INC"
    [ -n "$EXTRA_DEF" ] && cmd_line="$cmd_line $EXTRA_DEF"
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
# S-G e2（M12-F-12）：H743 的万条模糊测试源原样拿来，对 ESP32 这一份协议副本再跑一遍
# （tests/shim_h7/m207.h 把 "m207.h" 引到 m207_proto.h）。两份副本逐字节一致另有门禁。
EXTRA_INC="/I..\..\tests\shim_h7"
run fuzz_esp32 ../project_tcs/tests/fuzz_m207.c components/tcs_m207/m207_proto.c
EXTRA_INC=""
run m207golden tests/test_m207_golden.c components/tcs_m207/m207_proto.c
run tmp117pure tests/test_tmp117_pure.c    components/tcs_tmp117/tmp117_pure.c
# S-G e2（X04-F-05 ② / M13-F-06）：风扇服务的纯逻辑（头文件 inline）——命令-回读判定、弱校验
run fanpure   tests/test_fan_pure.c
# S-G e2：TMP117 采样内核（tmp117_core.c）逐拍主机用例——假器件记录每一次寄存器读写
run tmp117core tests/test_tmp117_core.c components/tcs_tmp117/tmp117_core.c components/tcs_tmp117/tmp117_pure.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# 同一套用例按注入构建（CONFIG_TCS_SIM=y）再编一遍：注入实现体在交付档不编入，
# 但它在的那一档必须编得过、跑得对
EXTRA_DEF="/DCONFIG_TCS_SIM=1"
run tmp117sim  tests/test_tmp117_core.c components/tcs_tmp117/tmp117_core.c components/tcs_tmp117/tmp117_pure.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
EXTRA_DEF=""
run tcsproto  tests/test_tcs_proto.c
run control   tests/test_control_esp32.c   components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# m1（模式页，2026-11-26）：手动两路 TEC 输出映射 / 只在变化时下发 / 写前校验（非手动·安全态·越界）/
# 进安全态与切自动清零 / 风扇联动与恢复 / 屏链路断开 3 秒 = 急停（纯判定 + 急停路径 + 恢复后保持到确认）
run ctlmanual tests/test_control_manual.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/link_loss_guard.c components/tcs_core/post_pure.c components/tcs_link/proto.c

# S-G e2（X06-F-05 / M12-F-12）：M207 服务内核（m207_core.c）逐拍主机用例——首联、selfloop 九步移植、
# 探测、传感器开路、超温、写路径、离线停写、下行帧黄金样本
run m207core  tests/test_m207_core.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G e2（X03-F-07 / M12-F-08）：安全关断旁路（队列满也下一拍上线路、作废过期的开）、
# 队列满计数、"保存到模块"32 条不丢
run m207queue tests/test_m207_queue.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G e2（M12-F-03 后半）：探测期 / 开机首联的等待窗里，任意合法 CMD:REPLY 都是链路活证据——
# 两路传感器同时开路时链路仍回到 online，ALM_M207 可确认
run m207link  tests/test_m207_link.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G e2（M12-F-13 / M12-F-15）：上电回环自检的帧表与判定（主机可测）——过线帧只能是查询，
# 不许带写命令；去抖时延由常量推算并钉住
run m207lb tests/test_m207_lb.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G e2（X04-F-05 ①）：模块自带保护状态的生产路径轮询与取值——如实可取、不报警不进 SAFE
run m207status tests/test_m207_status.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G m1（模式页，2026-11-26）：TCx:TCACTVOL? / TCx:TCACTCUR? 电学量轮询——正常值 / 负电压 / 负零、
# 温度轮询节奏一步不让、模块不认（回码）或不应答三次即停、数据龄、复联重试、与别的帧交错不误归属
run m207elec tests/test_m207_elec.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G i3（P1-09 的另一半，2026-11-26）：安全关断专用入口 m207_tec_force——模块离线也受理（记保持位）、
# 复联第一帧补发、复联判据读命令意图不读回显；"控制 + 服务内核"按 main.c 的装配联调
run m207force tests/test_m207_force.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G i3（M09-F-09 的注入档口径，2026-11-26）：台架水温注入（只在 CONFIG_TCS_SIM 档编译）——注入值每拍
# 覆盖对外读数、生效状态给 control 的卡死豁免用。这一行同时验证 SIM 档的实现体编得过、跑得对。
EXTRA_DEF="/DCONFIG_TCS_SIM=1"
run m207sim tests/test_m207_sim.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
# S-G i3（条目 8）：诊断 / 仿真档的只读导出（读数"逐位不变"的最长连续时长）按 SIM 档再编一遍同一个用例，
# 让那段 #if 里的实现体与用例都在让它生效的档上跑（交付档不编译它）。
run ctlstat tests/test_control_esp32.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
EXTRA_DEF=""

# S-G l1（2026-11-25）：R5-01 / R5-10 的 ESP32 侧用例——components/tcs_link/link_task.c 原样编译
# （IDF / FreeRTOS 用 tests/host_stubs 里的桩，按诊断档），在虚拟时钟上跑一条时间线：
# 自己发出的帧被回送的三种情形、屏写参数紧跟本端发送之后到达、ACK 回声、方向表、linkloop 20s。
EXTRA_INC="/I..\..\tests\host_stubs"
run linkdir tests/test_link_dir.c components/tcs_link/link_task.c components/tcs_link/link_dispatch.c components/tcs_link/link_registry.c components/tcs_link/link_demo.c components/tcs_link/link_selftest.c components/tcs_link/link_send.c components/tcs_link/link_status.c components/tcs_link/comm.c components/tcs_link/proto.c
EXTRA_INC=""
# m1（模式页）：对端帧时刻只在方向闸门之后刷新——"屏链路断开 3 秒 = 急停"的计时起点（comm.c 原样编译）
run linkpeer tests/test_link_peer.c components/tcs_link/comm.c components/tcs_link/proto.c
# 2026-11-28：TX 队列批量排空（消费 10 帧/s < 生产 ~11.3 帧/s 的回归用例）
run commburst tests/test_comm_burst.c components/tcs_link/comm.c components/tcs_link/proto.c

# S-G e3a：tcs_core 里 control.c 之外的纯逻辑（上次运行记录、安全记录 v2，以及后面本批次补的参数用例）
run tcscore   tests/test_tcs_core.c       components/tcs_core/boot_record.c components/tcs_core/nvs_store.c components/tcs_core/svc_status.c components/tcs_core/param_registry.c components/tcs_link/proto.c


# S-G e3b（X03-F-12 / M08-F-12）：OTA 自拷贝的纯逻辑（main/ota_pure.c）——单实例闸门、镜像长度。
# C11 原子操作在 MSVC 上要 /experimental:c11atomics（14.42 实测；cl_run 把 EXTRA_DEF 追加在编译命令末尾）
EXTRA_INC="/I..\..\main"; EXTRA_DEF="/experimental:c11atomics"
run otapure  tests/test_ota_pure.c main/ota_pure.c
EXTRA_INC=""; EXTRA_DEF=""

# U 线 W1：网页 WS 契约（st / log 消息，真实 cJSON）
if [ -f "$CJSON_DIR/cJSON.c" ]; then
    EXTRA_INC="/I\"$(cygpath -w "$CJSON_DIR")\""
    run webstatus tests/test_web_status.c components/tcs_web/web_status.c "$CJSON_DIR/cJSON.c"
    # S-G e3b（M14-F-13）：/api/ota 的 m 取值判定（web_status.c 的纯逻辑）
    run webhard   tests/test_web_hardening.c components/tcs_web/web_status.c "$CJSON_DIR/cJSON.c"
    EXTRA_INC=""
else
    echo "[webstatus] FAIL: IDF cJSON 源缺失（$CJSON_DIR，可用 IDF_CJSON 覆盖路径）"; FAIL=1
fi

# S-G e2：源码门禁——落在与硬件绑定的代码里的修复（上电回环自检、控制台 perf 行、UART 安装），
# 主机单测够不着，用读源码逐字核对来把关。只依赖标准库，任何 Python 都行。
PYGATE="${TCS_PYBIN:-python}"
command -v "$PYGATE" >/dev/null 2>&1 || PYGATE=python3
if command -v "$PYGATE" >/dev/null 2>&1 && "$PYGATE" -I tests/test_m207_gates.py; then :;
else echo "[m207gates] FAIL"; FAIL=1; fi
# m1（模式页）：屏链路断开 3 秒 = 急停在 control_service.c 里的接线（RTOS 文件，主机编译不了）
if command -v "$PYGATE" >/dev/null 2>&1 && "$PYGATE" -I tests/test_m1_gates.py; then :;
else echo "[m1gates] FAIL"; FAIL=1; fi

# S-G e3b：网页控制台 / OTA 加固的源码门禁（X03-F-12、M08-F-12、M14-F-13、M14-F-11、M14-F-12、M10-F-06）。
# 这些修复落在与 IDF 绑定的代码里，主机单测够不着；同样只依赖标准库。
if command -v "$PYGATE" >/dev/null 2>&1 && "$PYGATE" -I tests/test_web_gates.py; then :;
else echo "[webgates] FAIL"; FAIL=1; fi

# S-G e2（X06-F-05）：对端模型直驱固件内核。主机桥 = 交付固件用的同一份 m207_core.c；
# 对端 = project_tcs/tools/m207_sim.py 的 M207Core。五行注入矩阵 + 黄金语料三方一致 + 50ms 铁律。
run m207host tests/m207_core_host.c components/tcs_m207/m207_core.c components/tcs_m207/m207_proto.c components/tcs_core/control.c components/tcs_core/control_manual.c components/tcs_core/param_registry.c components/tcs_core/post_pure.c components/tcs_link/proto.c
if [ -x build/tests_m207/tests_m207host.exe ] && "$PYGATE" -I tests/test_m207_fw_adv.py \
        build/tests_m207/tests_m207host.exe; then :; else echo "[m207fwadv] FAIL"; FAIL=1; fi

# S-G e3a：分区表布局与现场故障证据配置的源码门禁（X04-F-03：coredump 分区 / 配置 / 现有偏移不变）
python tests/check_layout.py || FAIL=1
# S-G e3a：版本单一源（X06-F-09）——头文件可解析、CMake / 注册表都从它推导、没有第二处写死的版本号
python ../tools/version_bump.py --check || FAIL=1

# ---- S-G e1（2026-11-26）------------------------------------------------------------------------
# X01-F-05：待发队列深度进协议契约。同一个源文件在屏端 run_all.sh 里编屏的那一份 comm.h，
# 这里编主控这一份——两端的值都必须等于 protocol/tcs_proto.h 的 TCS_MAX_TXQ（_Static_assert
# 在头文件里，值漂移连编译都过不去；用例再断言一次运行期看到的数）。
run linkcontract ../project_tcs/tests/test_link_contract.c
# M11-F-13 / M08-F-07：快照任务的纯时间算术——修复前的 int32 承接 2^31 微秒就跳变
# （用例里逐字抄了旧写法做对照），修复后跨 2^31 / 2^32 / 50 天连续；节拍迟到跨 tick 回绕、
# 极大差值钳位。main/snap_time.c 零 IDF 依赖。
EXTRA_INC="/I..\..\main"
run snaptime tests/test_snap_time.c main/snap_time.c
EXTRA_INC=""
# X01-F-03：0x90 应答的 status/ret 交给上层——主控这一份 comm.c 跑同一组 comm 层断言
# （用例本体在 project_tcs/tests/test_link_ack.c，这里用 LINK_ACK_COMM_ONLY 跳过 link_send 那几节）。
run linkack tests/test_link_ack_comm.c components/tcs_link/comm.c components/tcs_link/proto.c

[ $FAIL = 0 ] && echo "esp32-tests: ALL GREEN" || echo "esp32-tests: HAS FAILURES"
exit $FAIL
