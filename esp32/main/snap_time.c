/**
 * snap_time.c — 快照任务的纯时间计算（S 线 e1：M11-F-13 / M08-F-07）。见 snap_time.h。
 * 零依赖：不引用 esp_timer / FreeRTOS 的任何符号（时刻与 tick 由调用方传入），
 * 所以主机用例 esp32/tests/test_snap_time.c 直接编译本文件。
 */
#include "snap_time.h"

uint32_t snap_ms_from_us(int64_t us)
{
    /* 64 位整数先除后截：负数（时钟不该出现，但防御）取绝对值区间同样是 32 位模运算。 */
    return (uint32_t)(us / 1000);
}

uint32_t snap_elapsed_us(int64_t t0, int64_t now)
{
    /* 先算 64 位差再截 32 位：差在 (0, 2^32) 内时结果就是真实经过的微秒数。
     * 修复前的 (int32_t)now - (int32_t)t0 在 2^31 处越界（有符号溢出是 UB）。*/
    return (uint32_t)(uint64_t)(now - t0);
}

uint32_t snap_late_ms(uint32_t now_ticks, uint32_t wake_ticks, uint32_t tick_ms)
{
    uint32_t d = (uint32_t)(now_ticks - wake_ticks);   /* 无符号减法：tick 回绕安全 */
    uint64_t ms = (uint64_t)d * (uint64_t)tick_ms;
    return (ms > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)ms;
}
