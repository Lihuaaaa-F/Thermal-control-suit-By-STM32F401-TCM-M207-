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
extern volatile uint32_t g_stat_touch_us_max;
/* 渲染计数已随旧 UI 栈删除（LVGL 用 TL-8 直方图 g_tl8）*/
extern volatile uint32_t g_stat_tick_ovf;

void console_init(UART_HandleTypeDef *huart);
void console_isr_push(uint8_t b);       /* ISR：RX 单字节入环形缓冲 */
void console_rx_complete(void);          /* RX 完成重启（main 回调转发）*/
void console_uart_error_recover(void);   /* ORE 清标志 + 重置接收 */
void console_poll(void);                 /* 主循环：TX 排空 + RX 行解析（命令执行）*/
/* M03-F-04：阻塞型命令的循环体内调用——每 10ms 回到屏上的急停兜底入口（tcs_lvgl_estop_watch）。
 * 不带屏的构建里是空操作。console.c 里是 pinscan / wdttest / p list，main.c 里是 wave 的采集循环。*/
void console_estop_yield(void);

/* 日志：同时写 RTT 与控制台 TX 环（缓冲满丢弃+计数）。prefix 如 "[APP]"。 */
void tcs_log(const char *prefix, const char *fmt, ...);

/* 复位源解析（RCC CSR）→ 首行日志（main 上电调用）*/
void console_print_reset_source(void);
void console_print_fault_record(void);   /* X04-F-03：打印上次 HardFault 现场（.noinit）*/

#ifdef __cplusplus
}
#endif
#endif /* TCS_CONSOLE_H */
