/* esp32/tests/host_stubs/freertos/task.h — **主机桩**（见 FreeRTOS.h 的说明）。*/
#pragma once

#include "FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t  xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_depth,
                        void *arg, UBaseType_t prio, TaskHandle_t *handle);
BaseType_t  xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack_depth,
                                    void *arg, UBaseType_t prio, TaskHandle_t *handle,
                                    BaseType_t core);
void         vTaskDelay(TickType_t ticks);
void         vTaskDelayUntil(TickType_t *last_wake, TickType_t increment);
TickType_t   xTaskGetTickCount(void);

/* 被测代码只在本核（单线程主机用例）上进临界区：空操作。*/
#define taskENTER_CRITICAL(mux) do { (void)(mux); } while (0)
#define taskEXIT_CRITICAL(mux)  do { (void)(mux); } while (0)
