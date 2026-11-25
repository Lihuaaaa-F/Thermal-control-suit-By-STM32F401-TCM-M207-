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
 *   - 已经在安全态时不重复触发；一次断开只触发一次（链路恢复后才重新武装）；
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
    uint8_t tripped;        /* 这次断开已经触发过（或已在安全态跳过）；链路恢复后清零 = 重新武装 */
} link_loss_guard_t;

typedef enum {
    TCS_LINK_LOSS_NONE = 0,        /* 不触发 */
    TCS_LINK_LOSS_ESTOP  = 1,      /* 触发急停（调用方执行 tcs_emergency_stop_src）*/
} link_loss_verdict_t;

/* 每个控制节拍调用一次。
 *   ever_connected = 上电后收到过对端合法帧；
 *   peer_age_ms    = 距最近一帧对端合法帧的毫秒数（ever_connected = 0 时无意义）；
 *   already_safe   = 控制状态机已在安全态。*/
link_loss_verdict_t link_loss_guard_step(link_loss_guard_t *g, int ever_connected,
                                         uint32_t peer_age_ms, int already_safe);

#ifdef __cplusplus
}
#endif
#endif /* TCS_LINK_LOSS_GUARD_H */
