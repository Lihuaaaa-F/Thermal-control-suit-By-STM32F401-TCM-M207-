/**
 * link_loss_guard.c — 屏链路断开 3 秒 = 急停 的判定（m1，用户 2026-11-26 裁决）。口径见头文件。
 */
#include "link_loss_guard.h"
#include "control.h"      /* SAFETY_BIT_ESTOP：只给 link_loss_already_safe 用 */

/* r7f（独立复核 R7-01，2026-11-26）：already_safe 只认“人工确认才能解除”的安全态。
 * R8-04（独立复核 r8f，2026-11-26）：再把范围收窄到**急停锁存**。超温同样“只有确认能解除”，
 * 但它在网页上确认一次就没了——若把它也算成“这次断开已经处理过”，确认之后屏仍断着、设备就再也
 * 不会判急停（R8 主机实测：peer_age=8.2 s、tec1_cmd=1、estops=0）。断链在超温安全态里照常把急停位
 * 补上（tcs_emergency_stop_src 对已停的输出幂等），急停位锁上之后才不重复触发。
 * 主机用例：tests/test_control_manual.c 的 L01b / L04 / L05；变异：B07–B11。*/
uint8_t link_loss_already_safe(uint8_t safety_latched)
{
    return (uint8_t)((safety_latched & (uint8_t)SAFETY_BIT_ESTOP) != 0u);
}

link_loss_verdict_t link_loss_guard_step(link_loss_guard_t *g, int ever_connected,
                                         uint32_t peer_age_ms, int already_safe)
{
    if (!g) return TCS_LINK_LOSS_NONE;
    /* 上电后屏从没连上过：不触发（台架上只接网页、不接屏是正常用法）*/
    if (!ever_connected) {
        g->tripped = 0;
        return TCS_LINK_LOSS_NONE;
    }
    /* 链路在（3 秒防抖窗内有过对端帧）：重新武装 */
    if (peer_age_ms < TCS_LINK_LOST_ESTOP_MS) {
        g->tripped = 0;
        return TCS_LINK_LOSS_NONE;
    }
    /* 急停位已经在锁存里（例如屏先按了急停、然后线才断）：这次断开处理过了，不重复触发，
     * 等链路恢复才重新武装。否则“链路还断着时在网页上确认解除”会在下一拍又被判成断链，
     * 急停 → 确认 → 急停 循环。*/
    if (already_safe) {
        g->tripped = 1;
        return TCS_LINK_LOSS_NONE;
    }
    /* R8-04：锁存里**没有**急停位（空锁存 / 只有超温或会自愈的位）→ 这一拍就要把急停锁上。
     * 不能因为“这次断开已经处理过”（tripped）就沉默：超温可以在网页上确认掉，确认之后屏仍断着，
     * 设备必须有急停位留着。tripped 只作为“这次断开触发过”的记录（诊断与用例读它）。*/
    g->tripped = 1;
    return TCS_LINK_LOSS_ESTOP;
}
