# arm-none-eabi-gcc.cmake — 交叉编译工具链（含 Cortex-M7 + 硬浮点 flags）
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_PREFIX arm-none-eabi-)
set(GCC_BIN "C:/Program Files (x86)/Arm GNU Toolchain arm-none-eabi/13.2 Rel1/bin")

set(CMAKE_C_COMPILER   ${GCC_BIN}/${TOOLCHAIN_PREFIX}gcc.exe)
set(CMAKE_ASM_COMPILER ${GCC_BIN}/${TOOLCHAIN_PREFIX}gcc.exe)
set(CMAKE_CXX_COMPILER ${GCC_BIN}/${TOOLCHAIN_PREFIX}g++.exe)
set(CMAKE_OBJCOPY      ${GCC_BIN}/${TOOLCHAIN_PREFIX}objcopy.exe CACHE FILEPATH "")
set(CMAKE_OBJDUMP      ${GCC_BIN}/${TOOLCHAIN_PREFIX}objdump.exe CACHE FILEPATH "")
set(CMAKE_SIZE         ${GCC_BIN}/${TOOLCHAIN_PREFIX}size.exe CACHE FILEPATH "")
set(CMAKE_AR           ${GCC_BIN}/${TOOLCHAIN_PREFIX}ar.exe CACHE FILEPATH "")
set(CMAKE_RANLIB       ${GCC_BIN}/${TOOLCHAIN_PREFIX}ranlib.exe CACHE FILEPATH "")

# Cortex-M7 + FPv5-D16 硬浮点 + Thumb 指令集（必须在 project() 前设，否则测试编译用空 flags）
set(MCU_FLAGS "-mcpu=cortex-m7 -mfpu=fpv5-d16 -mfloat-abi=hard -mthumb")
set(CMAKE_C_FLAGS_INIT   "${MCU_FLAGS} -Wall -Wextra -Os -ffunction-sections -fdata-sections -g3")
set(CMAKE_ASM_FLAGS_INIT "${MCU_FLAGS} -x assembler-with-cpp")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${MCU_FLAGS} -Wl,--gc-sections -Wl,--print-memory-usage -u _printf_float")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
