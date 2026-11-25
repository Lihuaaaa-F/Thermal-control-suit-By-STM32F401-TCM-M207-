/* esp32/tests/host_stubs/esp_timer.h — **主机桩**：返回用例的虚拟时钟（微秒）。*/
#pragma once

#include <stdint.h>

int64_t esp_timer_get_time(void);
