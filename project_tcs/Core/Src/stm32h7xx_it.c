/* stm32h7xx_it.c — 中断处理（TCS：三路 UART + HardFault dump + SysTick）
 *
 * 铁律（§3.2）：ISR 只做 UART RX 单字节入环形缓冲/置标志，禁止 HAL_UART_Transmit/
 * 协议解析/参数写。HardFault dump 沿用 GS §0B.2（g_fault_* 全局 + RTT，probe-rs 可读）。
 */
#include "stm32h7xx_it.h"
#include "main.h"
#include "stm32h7xx_hal.h"
#include "segger_rtt.h"
#include "console.h"
#include "m207.h"

extern UART_HandleTypeDef huart_console;
extern UART_HandleTypeDef huart_link;
extern UART_HandleTypeDef huart_m207;

/* 链路 RX 单字节落点；主循环经 link_rx_pop 取字节喂 comm（铁律：协议处理在主循环）*/
static uint8_t s_link_rx_byte;
void link_rx_isr_byte(uint8_t b);        /* main.c：入 256B 环形缓冲 */
void link_rx_rearm(void);                /* main.c：重启单字节接收 */
void link_uart_error_recover(void);      /* main.c：ORE 清标志+重置 */

/* ====== §0B.2 全局 fault 信息（probe-rs 可读）====== */
volatile uint32_t g_fault_cfsr, g_fault_hfsr, g_fault_bfar, g_fault_pc, g_fault_lr, g_fault_sp;

#include "blackbox.h"   /* T1：故障现场持久化到备份寄存器 */

void NMI_Handler(void)        { while (1) {} }
void MemManage_Handler(void)  { while (1) {} }
void BusFault_Handler(void)   { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void SVC_Handler(void)        {}
void DebugMon_Handler(void)   {}
void PendSV_Handler(void)     {}

void HardFault_Handler(void)
{
    uint32_t lr;
    __asm volatile("mov %0, lr" : "=r"(lr));
    g_fault_lr = lr;
    uint32_t *frame;
    if (lr & (1u << 2)) {
        __asm volatile("mrs %0, psp" : "=r"(frame));
    } else {
        __asm volatile("mrs %0, msp" : "=r"(frame));
    }
    g_fault_sp = (uint32_t)frame;
    g_fault_pc = frame[6];
    g_fault_cfsr = SCB->CFSR;
    g_fault_hfsr = SCB->HFSR;
    g_fault_bfar = SCB->BFAR;

    SEGGER_RTT_printf(0, "FAULT CFSR=%08x HFSR=%08x BFAR=%08x PC=%08x LR=%08x SP=%08x\n",
                      g_fault_cfsr, g_fault_hfsr, g_fault_bfar,
                      g_fault_pc, g_fault_lr, g_fault_sp);
    blackbox_fault(0xFA17u, g_fault_pc, g_fault_lr, g_fault_cfsr, g_fault_hfsr);

    /* 不喂 IWDG（2s）→ 自动复位。闪 LED2 指示。*/
    while (1) {
        HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
        for (volatile int i = 0; i < 1000000; i++) {}
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
    /* T1 实验臂 B：SysTick 上下文每约 1s 额外喂狗（主循环喂狗保持不变） */
    static uint16_t s_feed_div = 0;
    if (++s_feed_div >= 1024u) { s_feed_div = 0; blackbox_isr_feed(); }
}

void WWDG_IRQHandler(void) { while (1) {} }

/* —— USART1 控制台 —— */
void USART1_IRQHandler(void) { HAL_UART_IRQHandler(&huart_console); }

/* —— USART2 ESP32 链路：单字节入 RX 暂存，主循环喂 comm —— */
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart_link); }

/* —— USART3 M207 —— */
void USART3_IRQHandler(void) { HAL_UART_IRQHandler(&huart_m207); }

/* ====== RX 完成回调（三路分发；只入缓冲/重启接收）====== */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        console_rx_complete();
    } else if (huart->Instance == USART2) {
        link_rx_isr_byte(s_link_rx_byte);
        link_rx_rearm();
    } else if (huart->Instance == USART3) {
        m207_rx_complete();
    }
}

/* ====== 错误回调：ORE 先清标志再重置接收（F401 踩坑防御保留）====== */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)      console_uart_error_recover();
    else if (huart->Instance == USART2) link_uart_error_recover();
    else if (huart->Instance == USART3) m207_uart_error_recover();
}
