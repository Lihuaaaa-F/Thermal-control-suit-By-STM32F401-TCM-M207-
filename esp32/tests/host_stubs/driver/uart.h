/* esp32/tests/host_stubs/driver/uart.h — **主机桩**，不是 IDF driver/uart.h。
 * 名字与类型按 IDF v5.5.5 的声明抄（只抄 link_task.c 用到的那几项）；
 * 行为在 test_link_dir.c：TX 进记录缓冲、可回环进 RX；RX 是用例注入的字节队列；
 * "线时"按 230400 8N1 计入虚拟时钟。*/
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef int uart_port_t;

enum {
    UART_DATA_8_BITS      = 0,
    UART_PARITY_DISABLE   = 0,
    UART_STOP_BITS_1      = 0,
    UART_HW_FLOWCTRL_DISABLE = 0,
    UART_SCLK_DEFAULT     = 0,
};

typedef struct {
    int baud_rate;
    int data_bits;
    int parity;
    int stop_bits;
    int flow_ctrl;
    int source_clk;
} uart_config_t;

#define UART_NUM_1          1
#define UART_PIN_NO_CHANGE  (-1)

typedef enum {
    UART_DATA = 0,
    UART_FIFO_OVF,
    UART_BUFFER_FULL,
} uart_event_type_t;

typedef struct {
    uart_event_type_t type;
    size_t            size;
} uart_event_t;

esp_err_t uart_driver_install(uart_port_t num, int rx_buf_size, int tx_buf_size,
                              int queue_size, QueueHandle_t *uart_queue, int intr_flags);
esp_err_t uart_driver_delete(uart_port_t num);
esp_err_t uart_param_config(uart_port_t num, const uart_config_t *cfg);
esp_err_t uart_set_pin(uart_port_t num, int tx, int rx, int rts, int cts);
esp_err_t uart_set_loop_back(uart_port_t num, bool on);
esp_err_t uart_flush_input(uart_port_t num);
esp_err_t uart_get_tx_buffer_free_size(uart_port_t num, size_t *size);
int      uart_read_bytes(uart_port_t num, void *buf, uint32_t length, TickType_t ticks);
int      uart_write_bytes(uart_port_t num, const void *src, size_t size);
