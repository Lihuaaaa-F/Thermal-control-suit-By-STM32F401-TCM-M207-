/**
 * link_status.c — 链路状态缓存、临界区、判活、统计汇总（M11-F-15 拆分，S 线 e1）
 *
 * 从 link_task.c 拆出的第 1 块：原先 s_status / s_mux / s_real_status_ms / s_cb 与
 * 一堆 getter 混在任务文件里，演示模式、收帧分发、注册表都要伸手进来改。
 * 现在写入口只有两个：link_status_buf()（持锁）+ link_status_note_alive()；
 * 读出口是公开的 link_status_copy / link_alive / link_peer_age_ms / link_stats_get。
 */
#include "link_internal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_log.h"

static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static link_status_t s_status;
static volatile uint32_t s_real_status_ms;  /* M11-F-12：int64 拆两半读会撕裂，改 32 位毫秒（单写者+32 位对齐=原子）*/
static link_event_cb_t s_cb;

void link_internal_lock(void)   { taskENTER_CRITICAL(&s_mux); }
void link_internal_unlock(void) { taskEXIT_CRITICAL(&s_mux); }

/* comm_init 的临界区钩子：与内部 lock/unlock 是同一把锁 */
void link_crit_enter(void) { link_internal_lock(); }
void link_crit_exit(void)  { link_internal_unlock(); }

link_status_t *link_status_buf(void) { return &s_status; }

void link_status_note_alive(void)
{
    link_internal_lock();
    s_real_status_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
    link_internal_unlock();
}

void link_notify_event(void)
{
    /* s_cb 只在初始化时注册一次（link_on_event），读一次局部变量再调，避免回调里再注册时自锁 */
    link_event_cb_t cb = s_cb;
    if (cb) cb();
}

void link_on_event(link_event_cb_t cb) { s_cb = cb; }

bool link_alive(void)
{
    /* 只看真链路心跳（demo_step 不更新 s_real_status_ms，防演示模式自锁）
     * R9-06（独立复核 R9，2026-11-26，只记录不改行为）：e1 拆分时这里的边界从旧 link_task.c 的
     * `< 3000` 变成 `<= 3000`——恰好 3000 ms 时由"断"变"连"，1 ms 的差，无实际影响。
     * 保持现状：断链急停的判定走 link_peer_age_ms + `peer_age >= TCS_LINK_LOST_ESTOP_MS`（不是
     * link_alive），两者本来就不必逐毫秒一致；把这里改回 `<` 只会与"活着 = 3 秒内有过帧"的读法相悖。*/
    uint32_t last = s_real_status_ms;
    return last && (uint32_t)((uint32_t)(esp_timer_get_time() / 1000LL) - last) <= 3000u;
}

void link_status_copy(link_status_t *out)
{
    if (!out) return;
    link_internal_lock();
    *out = s_status;
    link_internal_unlock();
}

uint32_t link_rx_bytes(void) { return link_core_rx_bytes(); }
uint32_t link_rx_frames(void) { return link_core_rx_frames(); }

void link_stats_get(link_stats_t *out)
{
    if (!out) return;
    comm_link_t *L = link_ctx();
    out->rx_bytes = link_core_rx_bytes();
    out->rx_frames = link_core_rx_frames();
    out->crc_err = L->rx.n_crc_err;
    out->len_err = L->rx.n_len_err;
    out->gap_resets = L->rx_gap_resets;
    out->dup = L->dup_cnt;
    out->retry = L->retry_cnt;
    out->txq_drop = L->txq_drop;
    out->uart_txdrop = link_core_txdrop();
    /* R5-01：回声 = 本端发出的帧被回送、按方向位丢弃的帧数（comm 层方向闸门）；
     * wdir = 方向位说是对端、但方向表不允许的消息号（不是回声：坏帧或版本不匹配的对端）。*/
    out->echo_drop = L->echo_cnt;
    out->wrongdir = L->wrongdir_cnt;
    out->tx_wrongdir = L->tx_wrongdir_cnt;
    out->tx_fail = L->tx_fail_cnt;          /* X03-F-09：交给了驱动却没能上线的帧数 */
    out->arq_ok = (uint8_t)(L->link_ok ? 1u : 0u);
    out->echo_param = L->echo_by_mt[MT_TCS_PARAM];
    out->echo_snap  = (uint32_t)L->echo_by_mt[MT_TCS_SNAPSHOT] + L->echo_by_mt[MT_TCS_DELTA];
    out->echo_info  = L->echo_by_mt[MT_TCS_INFO];
    out->echo_ack   = L->echo_by_mt[MT_TCS_ACK];
    out->echo_getreg = L->echo_by_mt[MT_TCS_GET_REGISTRY];
    out->pending_evict = L->pending_evict;
    out->proto_mismatch = link_dispatch_proto_mismatch();
    out->ack_bad = L->ack_nonok_cnt;        /* X01-F-03：非 OK 应答（status/ret 不再被丢弃）*/
    out->peer_ver = link_dispatch_peer_ver(); /* M11-F-09：实际收到的对端版本 */
}

/* M11-F-09：版本不符提示位（组 0x20/0x21 时按位或进 alarm 字段）与累计计数。*/
uint16_t link_alarm_hint(void)
{
    return link_dispatch_alarm_hint();
}

uint32_t link_proto_mismatch(void)
{
    return link_dispatch_proto_mismatch();
}
