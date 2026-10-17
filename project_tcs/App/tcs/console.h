/**
 * console.h — USART1@115200 控制台（COM 口 CH340，任务书 §3.8 / D7）
 */
#ifndef TCS_CONSOLE_H
#define TCS_CONSOLE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

void console_init(UART_HandleTypeDef *huart);
void console_isr_push(uint8_t b);       /* ISR：RX 单字节入环形缓冲 */
void console_rx_complete(void);          /* RX 完成重启（main 回调转发）*/
void console_uart_error_recover(void);   /* ORE 清标志 + 重置接收 */
void console_poll(void);                 /* 主循环：TX 排空 + RX 行解析（命令执行）*/

/* 日志：同时写 RTT 与控制台 TX 环（缓冲满丢弃+计数）。prefix 如 "[APP]"。 */
void tcs_log(const char *prefix, const char *fmt, ...);

/* 复位源解析（RCC CSR）→ 首行日志（main 上电调用）*/
void console_print_reset_source(void);

#ifdef __cplusplus
}
#endif
#endif /* TCS_CONSOLE_H */
