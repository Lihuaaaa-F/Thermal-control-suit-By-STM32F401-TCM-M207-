/**
 * blackbox.h — T1 IWDG 复位黑匣子（临时诊断件，结案后随调试页脚一并移除）
 *
 * 介质：RTC 备份寄存器 BKP0R..BKP31R（D3 域，跨系统复位保持；IWDG 复位不清，
 * 仅整板断电清零）。观察臂：复位源/心跳/圈数/故障现场持久化；
 * 实验臂 B：SysTick ISR 每约 1s 额外喂狗一次（与主循环喂狗并存）。
 */
#ifndef TCS_BLACKBOX_H
#define TCS_BLACKBOX_H

#include <stdint.h>

/* 启动时最先调用（须在 console 清 RSR 之前） */
void blackbox_boot(void);

/* 主循环每圈 +1（RAM）；heartbeat 100ms 节流写 BKP */
void blackbox_loop_tick(void);
void blackbox_heartbeat(uint32_t now_ms);

/* SysTick ISR 调用：喂狗 + 计数（实验臂 B） */
void blackbox_isr_feed(void);

/* 故障现场持久化：marker 0xFA17=HardFault 0xE110=Error_Handler */
void blackbox_fault(uint32_t marker, uint32_t pc, uint32_t lr,
                    uint32_t cfsr, uint32_t hfsr);

/* 调试页脚文本：B=启动数 U=运行秒 L=圈数 I=ISR喂狗 F=上轮故障 R=上轮复位源 */
const char *blackbox_footer_text(uint32_t now_ms);

#endif /* TCS_BLACKBOX_H */
