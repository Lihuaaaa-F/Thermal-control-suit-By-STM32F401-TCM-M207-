/**
 * ota_pure.c — OTA 自拷贝的纯逻辑（说明见 ota_pure.h）
 */
#include "ota_pure.h"
#include <stdbool.h>
#include <stdatomic.h>

/* ====== 单实例闸门 ===== */
static atomic_bool s_ota_busy;          /* 静态存储，上电即 false（ATOMIC_VAR_INIT 在 C17 已弃用，主机编译器也不一定有）*/

/* 比较并交换：只有把 false 换成 true 的那一例算拿到，其余的看到 true、直接回绝。
 * 与 IDF 自带组件的写法相同（components/esp_adc/adc_filter.c）；IDF 为 xtensa 提供了
 * <stdatomic.h> 的实现（components/newlib/src/stdatomic.c），不需要额外链接。 */
int ota_gate_try_enter(void)
{
    bool expected = false;
    return atomic_compare_exchange_strong(&s_ota_busy, &expected, true) ? 1 : 0;
}

void ota_gate_leave(void)
{
    atomic_store(&s_ota_busy, false);
}

int ota_gate_busy(void)
{
    return atomic_load(&s_ota_busy) ? 1 : 0;
}
