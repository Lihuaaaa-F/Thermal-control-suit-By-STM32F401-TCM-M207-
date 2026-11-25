/* esp32/tests/host_stubs/esp_log.h — **主机桩**：日志行整条交给用例（test_link_dir.c 里
 * host_log 落进缓冲区，用例按原文断言，例如"回环期间不得出现 PARAM id="）。
 * 经第二层宏转发是为了让没有可变参数的调用（ESP_LOGI(TAG, "...")）在 MSVC 传统预处理器
 * 下也不留悬空逗号。*/
#pragma once

void host_log(char lvl, const char *tag, const char *fmt, ...);

#define HOST_LOG_FWD(lvl, tag, ...) host_log(lvl, tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) HOST_LOG_FWD('I', tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) HOST_LOG_FWD('W', tag, __VA_ARGS__)
#define ESP_LOGE(tag, ...) HOST_LOG_FWD('E', tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) HOST_LOG_FWD('D', tag, __VA_ARGS__)
