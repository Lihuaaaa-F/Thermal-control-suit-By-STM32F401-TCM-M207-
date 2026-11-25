/* esp32/tests/host_stubs/esp_task_wdt.h — **主机桩**。esp_task_wdt_reset 是被测任务
 * 每圈必调的函数，用例借它推进虚拟时间并在到点时跳出任务的 while(1)。*/
#pragma once

#include "esp_err.h"
#include "freertos/task.h"

esp_err_t esp_task_wdt_add(TaskHandle_t handle);
esp_err_t esp_task_wdt_reset(void);
