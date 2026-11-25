/* esp32/tests/host_stubs/freertos/FreeRTOS.h — **主机桩**，不是 FreeRTOS 的头文件。
 * 只为在 PC 上原样编译 components/tcs_link/link_task.c。实现（虚拟时钟）在 test_link_dir.c。*/
#pragma once

#include <stdint.h>

/* 被测文件按"诊断档"编译：link_mute / link_loopback 这一段要进主机用例。*/
#ifndef CONFIG_TCS_DIAG
#define CONFIG_TCS_DIAG 1
#endif
#ifndef CONFIG_TCS_SIM
#define CONFIG_TCS_SIM  0
#endif
#ifndef CONFIG_TCS_DEMO
#define CONFIG_TCS_DEMO 0
#endif

#define configTICK_RATE_HZ 1000u
typedef uint32_t     TickType_t;
typedef int          BaseType_t;
typedef unsigned int UBaseType_t;

#define pdMS_TO_TICKS(ms)  ((TickType_t)(ms))
#define pdTRUE   1
#define pdFALSE  0
#define pdPASS   1

typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
