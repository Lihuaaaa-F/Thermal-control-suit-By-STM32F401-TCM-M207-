/**
 * link_loss_guard.h — 屏链路断开 3 秒 = 急停（新功能批次 m1，用户 2026-11-26 裁决）
 *
 * 用户原话："断链时必须安全优先，等于发送急停。" 取代 2026-11 的旧裁决"断链只报告、不进安全态"。
 *
 * 纯逻辑、零 HAL、主机可编译（esp32/tests/test_control_manual.c）：给它三个事实，
 * 它回答"这一拍要不要触发急停"。真正的急停由调用方（control_service.c 的节拍）执行，
 * 走和急停命令同一条路径（tcs_emergency_stop_src：两路 TEC 全停、风扇全开、锁存、要确认才解除）。
 *
 * 口径：
 *   - "断开" = 自上一帧**对端方向**的合法帧起满 TCS_LINK_LOST_ESTOP_MS（方向闸门之后计数，
 *     本端回送帧不算）；
 *   - 只对"上电后连上过、然后断开"生效；上电后屏从没连上过不触发；
 *   - 锁存里已有急停位时不重复触发（调用方用 link_loss_already_safe 判，r7f / R7-01；
 *     R8-04 把它收窄到急停位）；一次断开只触发一次（链路恢复后才重新武装）；
 *   - R8-04：锁存里**没有**急停位（空锁存 / 只有超温或会自愈的位）时，断链照常触发急停——
 *     超温可以在网页上确认掉，确认之后屏仍断着，设备必须留着急停锁存；
 *   - 链路恢复后保持安全态，直到屏上确认（急停锁存本来就只有确认能清）。
 */
#ifndef TCS_LINK_LOSS_GUARD_H
#define TCS_LINK_LOSS_GUARD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TCS_LINK_LOST_ESTOP_MS   3000u    /* 断链防抖：满 3 秒才判 */

typedef struct {
    uint8_t tripped;        /* 这次断开触发过（诊断 / 用例读它）；链路恢复后清零。是否重复触发看
                             * already_safe（急停位在不在锁存里），不看这个标志（R8-04）*/} link_loss_guard_t;

typedef enum {
    TCS_LINK_LOSS_NONE = 0,        /* 不触发 */
    TCS_LINK_LOSS_ESTOP  = 1,      /* 触发急停（调用方执行 tcs_emergency_stop_src）*/
} link_loss_verdict_t;

/* 每个控制节拍调用一次。
 *   ever_connected = 上电后收到过对端合法帧；
 *   peer_age_ms    = 距最近一帧对端合法帧的毫秒数（ever_connected = 0 时无意义）；
 *   already_safe   = 锁存里已有急停位（用 link_loss_already_safe 算）；0 = 这一拍要触发 / 重新锁上。*/
link_loss_verdict_t link_loss_guard_step(link_loss_guard_t *g, int ever_connected,
                                         uint32_t peer_age_ms, int already_safe);

/* r7f（独立复核 R7-01，2026-11-26）：link_loss_guard_step 的 already_safe 该怎么算——原先只认
 * “急停 / 超温锁存着”的安全态。safety_latched 传 control_safety_latched()。
 * 为什么不再按“状态机在安全态”算：急停 / 超温只有人工确认能解除，输出已经停了，断链不必再触发一次；
 * 而模块失联 / 体温失联 / 读数卡死的安全态会在源恢复 2 秒后**自愈**——原先把它们也算成“这次断开
 * 处理过了”，屏断链期间故障一自愈，设备就在没有人确认的情况下自己把 TEC 开了回来。
 * R8-04（独立复核 r8f，2026-11-26）：超温也不够。超温可以被人在**网页**上确认掉（不需要屏），确认
 * 之后锁存一空、屏仍断着 → 旧口径下 tripped 一直为 1，再也不会判急停，状态机还按体温把 TEC 开回来。
 * 现在只认急停位：超温安全态里断链照常补一个急停位（幂等），确认掉超温也出不去；急停位锁上之后
 * 才按“这次断开处理过了”保持安静。*/
uint8_t link_loss_already_safe(uint8_t safety_latched);

#ifdef __cplusplus
}
#endif
#endif /* TCS_LINK_LOSS_GUARD_H */
