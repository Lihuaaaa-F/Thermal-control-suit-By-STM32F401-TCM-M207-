# CMakeLists.txt — 调温服 STM32H743IIT6（project_tcs，arm-none-eabi-gcc + Ninja）
#
# 用法：
#   cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake
#   cmake --build build
#   probe-rs download build/tcs_main.elf --chip STM32H743II
#
cmake_minimum_required(VERSION 3.20)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/arm-none-eabi-gcc.cmake CACHE PATH "toolchain")

project(tcs_main C ASM)

# Cortex-M7 + FPv5-D16 硬浮点
set(MCU_FLAGS "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb")
# TL-17 可复现构建：源路径映射归一（跨目录/跨机 __FILE__ 等路径串不进产物）
set(MCU_FLAGS "${MCU_FLAGS} -ffile-prefix-map=${CMAKE_CURRENT_SOURCE_DIR}=.")

set(CMAKE_C_FLAGS   "${CMAKE_C_FLAGS} ${MCU_FLAGS} -Wall -Wextra -Os -ffunction-sections -fdata-sections -g3")
set(CMAKE_ASM_FLAGS "${CMAKE_ASM_FLAGS} ${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T${CMAKE_CURRENT_SOURCE_DIR}/gcc/stm32h743xx_flash.ld \
    -Wl,--gc-sections -Wl,-Map=${CMAKE_BINARY_DIR}/tcs_main.map \
    -Wl,--print-memory-usage -u _printf_float")

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ====== 包含路径 ======
set(INC_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Inc/Legacy
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Include
    ${CMAKE_CURRENT_SOURCE_DIR}/App/proto
    ${CMAKE_CURRENT_SOURCE_DIR}/App/comm
    ${CMAKE_CURRENT_SOURCE_DIR}/App/rtt
    ${CMAKE_CURRENT_SOURCE_DIR}/App/iwdg
    ${CMAKE_CURRENT_SOURCE_DIR}/App/eeprom
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs
)
include_directories(${INC_DIRS})

# ====== HAL（按 stm32h7xx_hal_conf.h 开启模块；排除 *_template.c）======
file(GLOB HAL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/STM32H7xx_HAL_Driver/Src/*.c)
foreach(f ${HAL_SRC})
    if(f MATCHES "template\\.c$")
        list(REMOVE_ITEM HAL_SRC ${f})
    endif()
endforeach()

set(CMSIS_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Source/Templates/system_stm32h7xx.c
)
set(STARTUP
    ${CMAKE_CURRENT_SOURCE_DIR}/Drivers/CMSIS/Device/ST/STM32H7xx/Source/Templates/gcc/startup_stm32h743xx.s
)

# ====== 应用层 ======
set(APP_SRC
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/stm32h7xx_it.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/stm32h7xx_hal_msp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/syscalls_stub.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ltdc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/ft5446.c
    # keypad.c 已删（2026-11-25 S 线，审查条目 M02-F-07 / X05-F-08）：矩阵键盘的 KP_COL2=PD5 与 USART2_TX 冲突，
    # 全工程零调用，此前"只编译不初始化"——公开的 keypad_init() 一旦有人调用就会把屏链发送脚改成输入。
    ${CMAKE_CURRENT_SOURCE_DIR}/App/proto/proto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/comm/comm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/comm/link_send.c     # 屏→主控 参数写入的发送策略（等 ACK 的 0x12，R5-10；主机可测）
    ${CMAKE_CURRENT_SOURCE_DIR}/App/comm/link_info.c   # 主控设备信息（0x22 INFO）保存与查询
    ${CMAKE_CURRENT_SOURCE_DIR}/App/rtt/segger_rtt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/iwdg/iwdg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/eeprom/eeprom.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/param_registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/m207.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/m207_txq.c    # M207 下发指令队列（纯逻辑，主机可测；过长指令不截断，M07-F-10）
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/tmp117.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/control.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/estop_gate.c      # 急停屏入口兜底判定（纯函数）
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/post.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/qr/qrcodegen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/console.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/audit_ring.c    # 关键控制台命令的审计留痕环（纯逻辑，主机可测；M07-F-08）
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/touch_session.c
)

add_executable(tcs_main.elf
    ${STARTUP}
    ${CMSIS_SRC}
    ${HAL_SRC}
    ${APP_SRC}
)

# ====== 构建档位（R1 修复 P0-03 / X05-F-03；AD-5）======
# PROD=0（默认，正式交付）、BENCH=1（台架：引脚探针/波形/普查/手动直控/看门狗试验）、
# SIM=2（+板内仿真应答器）。诊断能力的"声明/实现/调用点"三者同时进 #if（S10）。
set(TCS_PROFILE "PROD" CACHE STRING "build profile: PROD | BENCH | SIM")
string(TOUPPER "${TCS_PROFILE}" TCS_PROFILE_U)
if(TCS_PROFILE_U STREQUAL "PROD")
    set(TCS_PROFILE_NUM 0)
elseif(TCS_PROFILE_U STREQUAL "BENCH")
    set(TCS_PROFILE_NUM 1)
elseif(TCS_PROFILE_U STREQUAL "SIM")
    set(TCS_PROFILE_NUM 2)
else()
    message(FATAL_ERROR "TCS_PROFILE must be PROD | BENCH | SIM (got '${TCS_PROFILE}')")
endif()
message(STATUS "TCS_PROFILE=${TCS_PROFILE_U} (=${TCS_PROFILE_NUM})")
# v2/AD-1：PROD = 纯显示终端（停跑本地 M207/TMP117/control/post 闭环）。
# 台架要本地闭环请用 BENCH/SIM（本开关自动为 0）。改档位后必须重新 configure。
if(TCS_PROFILE_U STREQUAL "PROD")
    set(TCS_H743_LINK_ONLY 1)
else()
    set(TCS_H743_LINK_ONLY 0)
endif()
message(STATUS "TCS_H743_LINK_ONLY=${TCS_H743_LINK_ONLY}")

target_compile_definitions(tcs_main.elf PRIVATE
    STM32H743xx
    USE_HAL_DRIVER
    ENABLE_LCD
    TCS_PROFILE=${TCS_PROFILE_NUM}
    TCS_H743_LINK_ONLY=${TCS_H743_LINK_ONLY}
    # 无 SCREEN_TEST：TCS 直接出正式版（任务书 §3.1）
)

# ====== WP-G 板内仿真构建（默认关；独立目录构建，不入正式发布）======
# 用法：cmake -S . -B build-sim -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake -DTCS_SIM=ON
set(TCS_SIM OFF CACHE BOOL "board-in simulation build (M207/TMP117 sim + touch inject)")
if(TCS_SIM)
    target_compile_definitions(tcs_main.elf PRIVATE TCS_SIM)
    target_link_libraries(tcs_main.elf PRIVATE m)   # sim 应答器 sinf
    message(STATUS "TCS_SIM=ON: simulation build (M207 responder / TMP117 inject / touch inject)")
endif()

# ====== 构建标识：可复现构建（2026-11-24 S 线，审查条目 X05-F-06）======
# 版本串里的构建时间此前用编译器宏 __DATE__/__TIME__，同一份源码两次构建的固件必然不同。
# 现在：时间优先取环境变量 SOURCE_DATE_EPOCH；没给就取 HEAD 提交的时间——同一提交两次从零构建产物相同
# （tools/build_matrix.sh --twice 核对）。提交号取 git 短号，已跟踪文件有未提交改动时加 -dirty。
# 都是 configure 时定下的：提交之后要重新 configure 才会更新（发布构建一律从零）。
set(TCS_BUILD_GIT "n/a")
# 直接试 git，不看"目录里有没有 .git"：本工程在仓库的子目录里，.git 在上一级；
# 链接工作树（git worktree）里它还是一个文件而不是目录。（2026-11-24 第一版就是这么写错的，结果 git=n/a。）
execute_process(COMMAND git rev-parse --short=9 HEAD
                WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE _tcs_git_rev OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _tcs_git_rc ERROR_QUIET)
if(_tcs_git_rc EQUAL 0 AND _tcs_git_rev)
    # 只看已跟踪文件：未跟踪的证据截图、日志不影响固件内容，不该让版本串带上 -dirty
    # （与 git describe --dirty 的口径一致。2026-11-25 第一次从干净提交上构建时就因为旁边多了几张
    #   未跟踪的 PNG 被标成了 dirty。）
    execute_process(COMMAND git status --porcelain --untracked-files=no
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE _tcs_git_dirty ERROR_QUIET)
    if(NOT "${_tcs_git_dirty}" STREQUAL "")
        set(TCS_BUILD_GIT "${_tcs_git_rev}-dirty")
    else()
        set(TCS_BUILD_GIT "${_tcs_git_rev}")
    endif()
    if(NOT DEFINED ENV{SOURCE_DATE_EPOCH})
        execute_process(COMMAND git log -1 --format=%ct
                        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                        OUTPUT_VARIABLE _tcs_git_ct OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_tcs_git_ct MATCHES "^[0-9]+$")
            set(ENV{SOURCE_DATE_EPOCH} "${_tcs_git_ct}")    # string(TIMESTAMP) 认这个环境变量
        endif()
    endif()
endif()
# 时间是提交时间（UTC），标明时区，免得被当成按下编译键的本地时间。
string(TIMESTAMP TCS_BUILD_STAMP "%Y-%m-%d %H:%M UTC" UTC)
message(STATUS "build id: git=${TCS_BUILD_GIT} stamp=${TCS_BUILD_STAMP}")
configure_file(App/tcs/build_info.h.in "${CMAKE_BINARY_DIR}/generated/build_info.h" @ONLY)
target_include_directories(tcs_main.elf PRIVATE "${CMAKE_BINARY_DIR}/generated")

# ====== UI 栈：LVGL 为唯一交付栈（2026-11-15 R1：旧 tcs_ui 栈已删除）======
# 历史：v12.5 曾用 TCS_USE_LVGL=OFF/ON 双栈共存；审查 M03/X05 证明 ON 产物仍链接旧栈
# 63KB（"一套半活"）。现改为单一栈：不再提供 OFF 构建，回滚走 tag `pre-ui-switch`。
set(TCS_LV_CONF "App/lvgl_port/lv_conf.h" CACHE STRING "lv_conf.h 相对路径（变体构建覆盖，如 DMA2D ON）")
include(${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port/lvgl_version_check.cmake)  # TL-16 版本锁
file(GLOB_RECURSE LVGL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/src/*.c)
# LVGL demos 只在显式要求调试场景时编译（2026-11-24 S 线 C01）。
# 此前四档无条件编译 demos（84 个目标文件），但没有任何档位调用它们——唯一调用点在
# TCS_LVGL_SCENE_BENCH 变体里，而那个宏从来不是由构建系统定义的；靠 --gc-sections 才没进镜像。
# 白白多编译一遍，还给每档带来 lv_demo_benchmark.c 的 1 条 #warning（审查 X05-F-08"编译不链接"）。
# 重跑 G4② 的 LVGL 基准场景：cmake -DTCS_PROFILE=BENCH -DTCS_LVGL_SCENE=BENCH ...
set(TCS_LVGL_SCENE "" CACHE STRING "LVGL 调试场景变体：空=不编译 demos（默认）| BENCH | HELLO")
if(CMAKE_C_FLAGS MATCHES "TCS_LVGL_SCENE_(BENCH|HELLO)")
    message(FATAL_ERROR "LVGL 场景变体请用 -DTCS_LVGL_SCENE=BENCH 或 HELLO 指定"
                        "（写进 CMAKE_C_FLAGS 的话 demos 不会被编译，链接会失败）")
endif()
if(TCS_LVGL_SCENE STREQUAL "BENCH")
    file(GLOB_RECURSE LVGL_DEMO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/demos/*.c)
    list(APPEND LVGL_SRC ${LVGL_DEMO_SRC})
elseif(TCS_LVGL_SCENE STREQUAL "HELLO")
    # 字体矩阵场景只用工程自己的字体，不需要 demos
elseif(NOT TCS_LVGL_SCENE STREQUAL "")
    message(FATAL_ERROR "TCS_LVGL_SCENE 只能是空、BENCH 或 HELLO（收到 '${TCS_LVGL_SCENE}'）")
endif()
if(NOT TCS_LVGL_SCENE STREQUAL "")
    if(TCS_PROFILE_U STREQUAL "PROD")
        message(FATAL_ERROR "LVGL 调试场景不能配交付档：加 -DTCS_PROFILE=BENCH 或 SIM")
    endif()
    target_compile_definitions(tcs_main.elf PRIVATE TCS_LVGL_SCENE_${TCS_LVGL_SCENE})
endif()
target_sources(tcs_main.elf PRIVATE ${LVGL_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port/lv_port_disp.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port/lv_port_indev.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port/tcs_lvgl.c)
file(GLOB TUI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_ui/*.c
                  ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_ui/fonts/*.c)
target_sources(tcs_main.elf PRIVATE ${TUI_SRC})
target_include_directories(tcs_main.elf PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_ui
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/demos
    ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port)
target_compile_definitions(tcs_main.elf PRIVATE TCS_USE_LVGL=1
    LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/${TCS_LV_CONF}")
message(STATUS "UI stack: LVGL ${LVGL_PINNED_VERSION} (single stack, tcs_ui removed)")

# 转 hex/bin（ASCII 路径可直接 objcopy，仍走 python 统一后处理）
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# 字体门禁：新增上屏文案未进字库 → 构建直接失败。
# 2026-10-29 教训：字体补齐靠手工流程时，改完文案忘重跑 → 上屏豆腐块。
add_custom_target(lvgl_font_gate ALL
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/font_audit_lvgl.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "LVGL 字体门禁（font_audit_lvgl.py）")
add_dependencies(tcs_main.elf lvgl_font_gate)
add_custom_command(TARGET tcs_main.elf POST_BUILD
    # objcopy / size 用工具链文件找到的同一套工具链（与编译器同目录），不再各找各的
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/post_build_objcopy.py $<TARGET_FILE_NAME:tcs_main.elf>
            "${GCC_BIN}/${TOOLCHAIN_PREFIX}objcopy.exe" "${GCC_BIN}/${TOOLCHAIN_PREFIX}size.exe"
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating HEX/BIN + size report"
    BYPRODUCTS tcs_main.hex tcs_main.bin
)
