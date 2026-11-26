/**
 * link_loss_guard.c — 屏链路断开 3 秒 = 急停 的判定（m1，用户 2026-11-26 裁决）。口径见头文件。
 */
#include "link_loss_guard.h"

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
    /* 这次断开已经处理过：等链路恢复才再看。否则"链路还断着时在网页上确认解除"会在
     * 下一拍又被判成断链，急停 → 确认 → 急停循环。*/
    if (g->tripped) return TCS_LINK_LOSS_NONE;
    /* 已在安全态（例如屏先按了急停、然后线才断）：输出已经停了，不重复触发；
     * 同样要等链路恢复才重新武装。*/
    if (already_safe) {
        g->tripped = 1;
        return TCS_LINK_LOSS_NONE;
    }
    g->tripped = 1;
    return TCS_LINK_LOSS_ESTOP;
}
