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

/* 主循环活性圈数（main.c 定义自增；stat 命令显示 + wp_check 活性判定，H2/H8）*/
extern volatile uint32_t g_stat_loop_passes;
/* WP-C.5 DWT 性能计数（main.c 定义；ui_ms_x10 = ms×10 保 1 位小数）*/
extern volatile uint32_t g_stat_work_us_max;
extern volatile uint32_t g_stat_work_us_avg;
extern volatile uint32_t g_stat_ui_ms_x10;
extern volatile uint32_t g_stat_touch_us_max;
/* v9.5 K3 渲染计数（tcs_ui.c 定义）*/
extern volatile uint32_t g_stat_hero_us_max;
extern volatile uint32_t g_stat_full_us_last;
extern volatile uint32_t g_stat_flip_cnt;
extern volatile uint32_t g_stat_patch_cnt;
extern volatile uint32_t g_stat_touch_hz;
extern volatile uint32_t g_stat_tick_ovf;

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
