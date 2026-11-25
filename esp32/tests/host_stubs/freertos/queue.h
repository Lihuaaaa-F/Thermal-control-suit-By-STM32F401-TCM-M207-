/* esp32/tests/host_stubs/freertos/queue.h — **主机桩**（见 FreeRTOS.h 的说明）。*/
#pragma once

#include "FreeRTOS.h"

typedef void *QueueHandle_t;

BaseType_t xQueueReceive(QueueHandle_t q, void *buf, TickType_t timeout);
