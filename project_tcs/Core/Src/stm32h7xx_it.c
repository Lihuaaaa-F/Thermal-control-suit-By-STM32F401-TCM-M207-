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
void link_rx_cplt(void);                 /* main.c：推 Receive_IT 落点 + 重启接收 */
void link_uart_error_isr(void);          /* main.c：X03-F-09——ISR 只置标志，恢复在主循环做 */

/* ====== §0B.2 全局 fault 信息（probe-rs 可读）====== */
volatile uint32_t g_fault_cfsr, g_fault_hfsr, g_fault_bfar, g_fault_pc, g_fault_lr, g_fault_sp;

/* X04-F-03（2026-11-23）：复位后仍可读的故障记录（.noinit，掉电才丢）。
 * 现场无 RTT 时靠它回答「上次为什么死/复位」。magic 区分「未发生」与「清零后的 0」。*/
#define FAULT_REC_MAGIC 0x54434641u   /* 'AFCT' */
/* 前 6 个字的顺序不变（工具按偏移读）。2026-11-26 追加后面 7 个字（S 线 C06）：三次受控触发读出的 PC 都是 0，
 * 光有 PC / CFSR 判不出记录器哪一步不对——把栈指针、栈里的返回地址与 xPSR、异常状态一并留下。
 * seq：同一次上电内第几次 HardFault（magic 无效时从 1 起）。掉电仍会丢：H743 没有不掉电的存储。*/
typedef struct {
    uint32_t magic, pc, lr, cfsr, hfsr, bfar;
    uint32_t sp, stk_lr, xpsr, icsr, shcsr, fpccr, seq;
} fault_rec_t;
volatile fault_rec_t g_fault_rec __attribute__((section(".noinit")));

void NMI_Handler(void)        { while (1) {} }
void MemManage_Handler(void)  { while (1) {} }
void BusFault_Handler(void)   { while (1) {} }
void UsageFault_Handler(void) { while (1) {} }
void SVC_Handler(void)        {}
void DebugMon_Handler(void)   {}
void PendSV_Handler(void)     {}

/* 取栈帧必须发生在任何压栈之前（2026-11-26，S 线 C06）：本工程没有 RTOS，主循环（线程模式）用的就是 MSP；
 * 异常进入时硬件把被打断的现场压在 MSP 上，而普通的 C 函数一进来就先 push 寄存器，之后再去读 MSP，
 * 读到的已经是压过寄存器的位置——读出来的是处理函数自己压进去的东西。真机上受控触发三次，
 * 记录里的 PC 全是 0、栈里的 LR / xPSR 三次一模一样，就是这个原因。
 * 所以入口做成 naked 汇编：先取 SP 与 EXC_RETURN，再尾调用 C 函数（尾调用不再往 MSP 上压任何东西）。
 * EXC_RETURN 的 bit2：1 = 用 PSP，0 = 用 MSP。*/
void HardFault_Handler(void) __attribute__((naked));
void HardFault_Handler(void)
{
    __asm volatile(
        "tst lr, #4            \n"
        "ite eq                \n"
        "mrseq r0, msp         \n"
        "mrsne r0, psp         \n"
        "mov r1, lr            \n"
        "b hardfault_c         \n"
    );
}

void hardfault_c(uint32_t *frame, uint32_t lr)
{
    g_fault_lr = lr;
    g_fault_sp = (uint32_t)frame;
    g_fault_pc = frame[6];
    g_fault_cfsr = SCB->CFSR;
    g_fault_hfsr = SCB->HFSR;
    g_fault_bfar = SCB->BFAR;

    /* 先落盘（纯内存写，几拍完成），再打印/停机。
     * 序号要在覆盖 magic 之前算：上一次的记录有效（magic 对得上）就在它的序号上加 1，否则这是上电后第 1 次。*/
    const uint32_t prev_seq = (g_fault_rec.magic == FAULT_REC_MAGIC) ? g_fault_rec.seq : 0u;
    g_fault_rec.magic = FAULT_REC_MAGIC;
    g_fault_rec.pc = g_fault_pc;
    g_fault_rec.lr = g_fault_lr;
    g_fault_rec.cfsr = g_fault_cfsr;
    g_fault_rec.hfsr = g_fault_hfsr;
    g_fault_rec.bfar = g_fault_bfar;
    g_fault_rec.sp = (uint32_t)frame;
    g_fault_rec.stk_lr = frame[5];
    g_fault_rec.xpsr = frame[7];
    g_fault_rec.icsr = SCB->ICSR;
    g_fault_rec.shcsr = SCB->SHCSR;
    g_fault_rec.fpccr = FPU->FPCCR;
    g_fault_rec.seq = (prev_seq < 0xFFFFFFFFu) ? prev_seq + 1u : 1u;

    SEGGER_RTT_printf(0, "FAULT CFSR=%08x HFSR=%08x BFAR=%08x PC=%08x LR=%08x SP=%08x\n",
                      (unsigned)g_fault_cfsr, (unsigned)g_fault_hfsr, (unsigned)g_fault_bfar,
                      (unsigned)g_fault_pc, (unsigned)g_fault_lr, (unsigned)g_fault_sp);

    /* 不喂 IWDG（2s，WP-C.1）→ 自动复位。闪 LED2 指示。*/
    while (1) {
        HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
        for (volatile int i = 0; i < 1000000; i++) {}
    }
}

void SysTick_Handler(void)
{
    HAL_IncTick();
    /* WP-C：T1 实验臂 B（ISR 喂狗）已随 blackbox 移除——2s IWDG 必须能抓主循环假死 */
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
        link_rx_cplt();          /* 推 Receive_IT 落点（原推未写入的静态字节=恒 0x00）*/
    } else if (huart->Instance == USART3) {
        m207_rx_complete();
    }
}

/* ====== 错误回调：ORE 先清标志再重置接收（F401 踩坑防御保留）======
 * X03-F-09（2026-11-25）：USART2 这一路不再在 ISR 里动句柄——只置标志，
 * AbortReceive + 重挂由主循环 link_rx_drain() 做（否则与主循环的 100ms 自愈并发操作同一句柄）。*/
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)      console_uart_error_recover();
    else if (huart->Instance == USART2) link_uart_error_isr();
    else if (huart->Instance == USART3) m207_uart_error_recover();
}
