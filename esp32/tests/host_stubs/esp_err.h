/* esp32/tests/host_stubs/esp_err.h — **主机桩**，不是 ESP-IDF 的头文件。
 * 只为在 PC 上原样编译 components/tcs_link/link_task.c（esp32/tests/test_link_dir.c）。
 * 固件源码不得包含本目录下的任何文件；IDF 的真头文件在 D:\Espressif 里。*/
#pragma once

typedef int esp_err_t;

#define ESP_OK    0
#define ESP_FAIL  -1

void        host_stub_abort(const char *why);
const char *esp_err_to_name(esp_err_t err);

#define ESP_ERROR_CHECK(x) do { if ((x) != ESP_OK) host_stub_abort("ESP_ERROR_CHECK"); } while (0)
