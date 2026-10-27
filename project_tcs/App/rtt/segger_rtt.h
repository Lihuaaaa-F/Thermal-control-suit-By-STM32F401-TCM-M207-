/**
 * segger_rtt.h — SEGGER RTT 工程门面（W4.2 vendor 薄壳，v13.4 §5.2）
 *
 * M1.5 起 RTT 日志入口；W4.2 起实现换官方 vendor（App/rtt/vendor/，上游
 * github.com/SEGGERMicro/RTT@4d8feab，出处/SHA256 见 vendor/UPSTREAM.md）。
 * 本头只做两件事：
 *   ①引入官方 SEGGER_RTT.h（printf 签名与历史用法原生一致：channel 0 首参）；
 *   ②SEGGER_RTT_WriteString 一参兼容壳——官方签名是 (BufferIndex, s)，
 *     历史调用点全是一参，宏转发到 segger_rtt.c 的 *_0 包装（保现封装签名）。
 *
 * 用法不变：SEGGER_RTT_Init() / SEGGER_RTT_printf(0, ...) / SEGGER_RTT_WriteString(s)。
 * ⚠️ 只在主循环调用，绝不在 TIM6 中断里调（格式化耗时影响中断延迟）。
 * ⚠️ 官方 printf 同样不支持浮点——F14/F16 的 snprintf 预格式化纪律长期有效
 *    （门禁：run_all 的 [fmt_gate] %f/%l 双零 + [rtt_count] 调用数对账）。
 */
#ifndef SEGGER_RTT_FACADE_H
#define SEGGER_RTT_FACADE_H

#include "vendor/SEGGER_RTT.h"   /* 官方 API（相对路径直指 vendor/，不依赖 -I；
                                    守卫名避开官方 SEGGER_RTT_H 防互斥跳空） */

/* 一参 WriteString / 两参 Write 兼容壳：调用点零改动（保现封装签名，v13.4 §5.2；
 * 官方签名分别为 (BufferIndex, s) / (BufferIndex, buf, len)，历史调用无首参） */
unsigned SEGGER_RTT_WriteString0(const char *s);
unsigned SEGGER_RTT_Write0(const void *buf, unsigned len);
#define SEGGER_RTT_WriteString(s)      SEGGER_RTT_WriteString0(s)
#define SEGGER_RTT_Write(buf, len)     SEGGER_RTT_Write0(buf, len)

#endif /* SEGGER_RTT_FACADE_H */
