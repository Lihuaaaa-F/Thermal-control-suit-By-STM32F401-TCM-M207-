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
    ${CMAKE_CURRENT_SOURCE_DIR}/Core/Src/keypad.c     # 只编译不初始化（PD5 冲突，见 main.c）
    ${CMAKE_CURRENT_SOURCE_DIR}/App/proto/proto.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/comm/comm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/rtt/segger_rtt.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/iwdg/iwdg.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/eeprom/eeprom.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/param_registry.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/m207.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/tmp117.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/control.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/post.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/qr/qrcodegen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/console.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/touch_session.c
    ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs/tcs_ui.c
)

add_executable(tcs_main.elf
    ${STARTUP}
    ${CMSIS_SRC}
    ${HAL_SRC}
    ${APP_SRC}
)

target_compile_definitions(tcs_main.elf PRIVATE
    STM32H743xx
    USE_HAL_DRIVER
    ENABLE_LCD
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

# ====== G4：LVGL 双栈（v12.5 D-L9；=0 零新增编译=产物与 pre-lvgl 逐字节一致 TL-17）======
option(TCS_USE_LVGL "Enable LVGL UI stack (G4)" OFF)
set(TCS_LV_CONF "App/lvgl_port/lv_conf.h" CACHE STRING "lv_conf.h 相对路径（变体构建覆盖，如 DMA2D ON）")
if(TCS_USE_LVGL)
    include(${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port/lvgl_version_check.cmake)  # TL-16 版本锁
    file(GLOB_RECURSE LVGL_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/src/*.c)
    file(GLOB_RECURSE LVGL_DEMO_SRC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/demos/*.c)
    target_sources(tcs_main.elf PRIVATE ${LVGL_SRC} ${LVGL_DEMO_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port/lv_port_disp.c
        ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port/lv_port_indev.c
        ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port/tcs_lvgl.c)
    file(GLOB TUI_SRC ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_ui/*.c
                      ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_ui/fonts/*.c)
    target_sources(tcs_main.elf PRIVATE ${TUI_SRC})
    target_include_directories(tcs_main.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_ui
        ${CMAKE_CURRENT_SOURCE_DIR}/App/tcs)
    target_include_directories(tcs_main.elf PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lvgl/demos
        ${CMAKE_CURRENT_SOURCE_DIR}/App/lvgl_port)
    target_compile_definitions(tcs_main.elf PRIVATE TCS_USE_LVGL=1
        LV_CONF_PATH="${CMAKE_CURRENT_SOURCE_DIR}/${TCS_LV_CONF}")
    message(STATUS "TCS_USE_LVGL=ON: LVGL ${LVGL_PINNED_VERSION} stack on")
endif()

# 转 hex/bin（ASCII 路径可直接 objcopy，仍走 python 统一后处理）
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_command(TARGET tcs_main.elf POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/post_build_objcopy.py $<TARGET_FILE_NAME:tcs_main.elf>
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Generating HEX/BIN + size report"
    BYPRODUCTS tcs_main.hex tcs_main.bin
)
