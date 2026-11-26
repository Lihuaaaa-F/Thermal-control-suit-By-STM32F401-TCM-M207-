/**
 * snap_time.h — 快照任务的纯时间计算（S 线 e1：M11-F-13 / M08-F-07）
 *
 * 为什么要单独一个文件：tcs_snap.c 依赖 IDF（FreeRTOS 任务、esp_timer、esp_netif），
 * 主机上编不了；而"71.6 分钟回绕"和"节拍迟到"这两件事都只是算术。把它们抽成
 * 不依赖 IDF 的纯函数，主机用例（esp32/tests/test_snap_time.c）就能把时钟拨过 2^31 微秒、
 * 把 tick 拨到回绕点，直接验证。
 *
 * 零依赖：只用 <stdint.h>。
 */
#ifndef TCS_SNAP_TIME_H
#define TCS_SNAP_TIME_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * M11-F-13：esp_timer 的 64 位微秒 → 0x20 快照的 ms 字段（线上 32 位）。
 * 用无符号截断：每 49.7 天回绕一次是 32 位字段本身的物理事实，但**不会**在 35.8 分钟
 * （2^31 微秒）处跳变。修复前的写法是先把 esp_timer_get_time() 截成 int32 再除 1000：
 * 过了 2^31 微秒就是负数，转 uint32 后 ms 从约 2.15e6 跳到约 4.29e9。
 */
uint32_t snap_ms_from_us(int64_t us);

/**
 * M11-F-13：两个 esp_timer 时刻之差（微秒），无符号语义。
 * 允许 now 比 t0 小（32 位截断区间内），返回值仍然是"真实经过的微秒数"（模 2^32）。
 * 修复前是 int32 相减：差超过 2^31 微秒或截断点附近的行为未定义/为负。
 */
uint32_t snap_elapsed_us(int64_t t0, int64_t now);

/**
 * M08-F-07：200ms 节拍迟到多少毫秒。
 *   now_ticks    = xTaskGetTickCount()
 *   wake_ticks   = vTaskDelayUntil 的 last_wake（= 本次应该醒来的 tick）
 *   tick_ms      = portTICK_PERIOD_MS
 * 返回 (now_ticks - wake_ticks) * tick_ms，按无符号减法处理 tick 回绕；超过
 * UINT32_MAX 时钳到 UINT32_MAX（不会出现负值/回绕成小数）。
 * 准时醒来时差值为 0～1 个 tick（调度粒度），不是错误。
 */
uint32_t snap_late_ms(uint32_t now_ticks, uint32_t wake_ticks, uint32_t tick_ms);

#ifdef __cplusplus
}
#endif
#endif /* TCS_SNAP_TIME_H */
