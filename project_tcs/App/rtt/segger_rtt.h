/**
 * segger_rtt.h — SEGGER RTT 最小实现（probe-rs 兼容）
 *
 * M1.5 自主调试基础：MCU 往 RAM 里的环形缓冲写日志，probe-rs attach 实时收。
 * 零外部依赖，不拉 newlib printf（自带简化格式化，省 Flash）。
 *
 * 控制块 _SEGGER_RTT 放 .bss（DTCMRAM，不经 D-Cache，probe-rs 直接读）。
 * 只用 1 个上行 buffer（MCU→host），下行不用（agent 不向板子发命令）。
 *
 * 用法：
 *   SEGGER_RTT_Init();                 // main 开头一次
 *   SEGGER_RTT_printf(0, "boot HCLK=%d\n", 240);
 *   SEGGER_RTT_WriteString("LINK DOWN\n");
 *
 * ⚠️ 只在主循环调用，绝不在 TIM6 中断里调（写 buffer 虽不阻塞，但格式化耗时影响中断延迟）。
 */
#ifndef SEGGER_RTT_H
#define SEGGER_RTT_H

#include <stdint.h>
#include <stddef.h>

/* 初始化 RTT 控制块（main 开头调一次）。重复调用安全。*/
void SEGGER_RTT_Init(void);

/* 写 0 结尾字符串到 up buffer 0。返回写入字节数。*/
unsigned SEGGER_RTT_WriteString(const char *s);

/* 写任意字节。返回写入字节数。*/
unsigned SEGGER_RTT_Write(const char *buf, unsigned len);

/* 简化 printf：支持 %d %u %x %X %s %c，可选最小宽度 %Nd（右对齐空格填充）
 * 和 %0Nd（左补零）。不支持浮点/长 long/精度。channel 固定 0（忽略首参）。*/
void SEGGER_RTT_printf(unsigned channel, const char *fmt, ...);

#endif /* SEGGER_RTT_H */
