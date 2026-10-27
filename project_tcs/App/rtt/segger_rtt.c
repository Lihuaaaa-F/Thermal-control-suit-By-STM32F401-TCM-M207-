/**
 * segger_rtt.c — vendor 聚合薄壳（W4.2，v13.4 §5.2）
 *
 * 自研最小实现（195 行，F4 候选）就此退役——git 历史可回退（w4.1 tag 之前）。
 * 本文件把官方两个编译单元原文链入（CMakeLists 的 APP_SRC 条目名不变，
 * 且 project_tcs/CMakeLists.txt 处于 G 线未提交改动期不可触碰——聚合包含
 * 是零构建系统改动的 vendor 路径；G 线合入后可正常化回独立源条目）。
 * 行为差异对表（模式/锁/格式面）见 docs/results/W4_2_完成.md。
 */
#include "segger_rtt.h"

/* 官方 WriteString/Write 带 BufferIndex 首参；包装恢复历史无首参签名 */
#undef SEGGER_RTT_WriteString
#undef SEGGER_RTT_Write
unsigned SEGGER_RTT_WriteString0(const char *s)
{
    return SEGGER_RTT_WriteString(0u, s);
}

unsigned SEGGER_RTT_Write0(const void *buf, unsigned len)
{
    return SEGGER_RTT_Write(0u, buf, len);
}

/* ====== 官方实现原文链入（vendor/ 文件零改动，SHA256 见 vendor/） ====== */
#include "vendor/SEGGER_RTT.c"
#include "vendor/SEGGER_RTT_printf.c"
