/**
 * comm.c — 可靠链路层实现。详见 comm.h。
 *
 * 与 Python comm_protocol.ReliableLink 行为对齐。零 HAL 依赖。
 *
 * §0E 中断重入修复（M1.5）：所有 TX 经环形待发队列，主循环 comm_poll_tx
 * 唯一消费点串行 HAL_UART_Transmit。中断里 comm_tick 只判超时+重传入队。
 * 急停=常规尾插（队头插队函数已按 W3-A/终审 MF-8 删除：双端 0 调用死码）。
 * tx_head/tx_tail/tx_count 的读改写用 enter/exit_critical 短临界区保护。
 */
#include "comm.h"
#include <string.h>

void comm_init(comm_link_t *L,
               void (*send_fn)(const uint8_t *data, size_t len),
               uint32_t (*now_ms_fn)(void),
               void (*on_link_down)(void),
               void (*enter_critical)(void),
               void (*exit_critical)(void))
{
    memset(L, 0, sizeof(*L));
    L->send_fn = send_fn;
    L->now_ms_fn = now_ms_fn;
    L->on_link_down = on_link_down;
    L->enter_critical = enter_critical;
    L->exit_critical = exit_critical;
    L->link_ok = COMM_LINK_OK_TRUE;
    /* last_seen_seq 全 -1（没见过） */
    for (int i = 0; i < 256; i++) L->last_seen_seq[i] = -1;
    proto_rx_init(&L->rx);
}

uint8_t comm_next_seq(comm_link_t *L)
{
    uint8_t s = L->next_seq;
    L->next_seq = (uint8_t)((L->next_seq + 1) & 0xFF);
    return s;
}

int comm_pending_count(const comm_link_t *L)
{
    int n = 0;
    for (int i = 0; i < COMM_MAX_PENDING; i++)
        if (L->pending[i].in_use) n++;
    return n;
}

int comm_txq_count(const comm_link_t *L)
{
    return (int)L->tx_count;
}

/* 找空 pending 槽，没有返回 -1 */
static int find_free_slot(const comm_link_t *L)
{
    for (int i = 0; i < COMM_MAX_PENDING; i++)
        if (!L->pending[i].in_use) return i;
    return -1;
}

/* 按 (seq, mt) 找 pending，没有返回 -1 */
static int find_pending(const comm_link_t *L, uint8_t seq, uint8_t mt)
{
    for (int i = 0; i < COMM_MAX_PENDING; i++)
        if (L->pending[i].in_use &&
            L->pending[i].seq == seq &&
            L->pending[i].msg_type == mt)
            return i;
    return -1;
}

/* —— §0E 待发队列（环形，临界区保护索引）—— */

/* 内部：入队尾部（不保护，调用方包临界区）。满则丢最老。*/
static void txq_push_unlocked(comm_link_t *L, const uint8_t *frame, size_t len)
{
    if (len > MAX_FRAME) len = MAX_FRAME;
    uint8_t t = L->tx_tail;
    if (L->tx_count >= MAX_TXQ) {   /* 满：丢最老，head 前移 */
        L->tx_head = (uint8_t)((L->tx_head + 1) % MAX_TXQ);
        L->tx_count--;
    }
    memcpy(L->tx_queue[t], frame, len);
    L->tx_len[t] = (uint16_t)len;
    L->tx_tail = (uint8_t)((t + 1) % MAX_TXQ);
    L->tx_count++;
}

void comm_txq_push(comm_link_t *L, const uint8_t *frame, size_t len)
{
    if (L->enter_critical) L->enter_critical();
    txq_push_unlocked(L, frame, len);
    if (L->exit_critical) L->exit_critical();
}

void comm_poll_tx(comm_link_t *L)
{
    /* 无锁读 tx_count：容忍一拍误差（急停场景可接受）。
     * 即使 ISR 在此刻 push，下次主循环会处理。主循环只读不改 tail。*/
    if (L->tx_count == 0) return;
    if (!L->send_fn) return;

    /* 取队头：head 是主循环独占消费索引，ISR 只 push 改 tail 不碰 head
     * （push_front 已按 W3-A/终审 MF-8 删除）；故此处 head 无竞态。*/
    uint8_t h = L->tx_head;
    L->send_fn(L->tx_queue[h], L->tx_len[h]);

    /* 消费后 head 前移，tx_count-- 。tx_count 被 ISR 的 push 读，故这里要
     * 临界区保护 tx_count 的写（防 ISR 同时读到一个中间态）。
     * head 的写仅主循环，不冲突。*/
    if (L->enter_critical) L->enter_critical();
    L->tx_head = (uint8_t)((h + 1) % MAX_TXQ);
    L->tx_count--;
    if (L->exit_critical) L->exit_critical();
}

uint8_t comm_send(comm_link_t *L, const uint8_t *frame_bytes, size_t len, uint8_t msg_type)
{
    if (len < 5 || len > MAX_FRAME) return 0xFF;
    uint8_t seq = frame_bytes[4];  /* 起始符2+Length1+MsgType1+SeqNo1 */

    /* 已有同 (seq,mt) pending → 覆盖重置（理论上递增不重复） */
    int idx = find_pending(L, seq, msg_type);
    if (idx < 0) idx = find_free_slot(L);
    if (idx < 0) {
        /* pending 满，丢老的（LIFO）腾位 */
        idx = COMM_MAX_PENDING - 1;
    }
    comm_pending_t *e = &L->pending[idx];
    memcpy(e->frame, frame_bytes, len);
    e->frame_len = len;
    e->seq = seq;
    e->msg_type = msg_type;
    e->retries = 0;
    e->in_use = 1;
    e->deadline_ms = L->now_ms_fn() + COMM_ACK_TIMEOUT_MS;

    /* §0E：首传入队（不直接 send_fn），主循环 comm_poll_tx 串行消费 */
    comm_txq_push(L, frame_bytes, len);
    return seq;
}

void comm_tick(comm_link_t *L)
{
    uint32_t now = L->now_ms_fn();
    for (int i = 0; i < COMM_MAX_PENDING; i++) {
        comm_pending_t *e = &L->pending[i];
        if (!e->in_use) continue;
        if (now < e->deadline_ms) continue;  /* 未超时 */
        if (e->retries >= COMM_MAX_RETRIES) {
            /* 超重传上限：清该 pending，判链路断 */
            e->in_use = 0;
            if (comm_pending_count(L) == 0) {
                /* 所有 pending 都失败 → link_ok=False */
                L->link_ok = COMM_LINK_OK_FALSE;
                if (L->on_link_down) L->on_link_down();
            }
            continue;
        }
        /* 超时未达上限 → 重传入队（§0E，不 send_fn） */
        e->retries++;
        e->deadline_ms = now + COMM_ACK_TIMEOUT_MS;
        comm_txq_push(L, e->frame, e->frame_len);
    }
}

int comm_on_ack(comm_link_t *L, uint8_t acked_seq, uint8_t acked_mt)
{
    int idx = find_pending(L, acked_seq, acked_mt);
    if (idx < 0) return 0;
    L->pending[idx].in_use = 0;
    L->link_ok = COMM_LINK_OK_TRUE;  /* 任一 ACK 到即链路恢复 */
    return 1;
}

/* 记最近重复帧信息（去重命中时上层回 ACK 用） */
static uint8_t s_dup_seq;
static uint8_t s_dup_mt;
static int s_dup_valid = 0;

int comm_rx_feed_byte(comm_link_t *L, uint8_t b,
                      const uint8_t **out_payload, size_t *out_len, int *out_is_dup)
{
    if (out_is_dup) *out_is_dup = 0;
    const uint8_t *payload; size_t plen;
    if (!proto_rx_feed(&L->rx, b, &payload, &plen)) {
        /* 还在收/CRC 错/杂字节，不交付 */
        return 0;
    }
    if (plen < 2) return 0;  /* 至少 MsgType+SeqNo */
    uint8_t mt = payload[0];
    uint8_t seq = payload[1];

    /* ACK 帧：交给发送侧清 pending，不交上层 */
    /* TCS 扩展（任务书 §4.2）：0x90 = TCS_ACK，布局与 GS ACK 兼容（[2]acked_seq [3]acked_mt）*/
    if (mt == MT_UPLINK_ACK || mt == MT_DOWNLINK_ACK || mt == 0x90 /* MT_TCS_ACK */) {
        if (plen < 4) return 0;
        comm_on_ack(L, payload[2], payload[3]);
        return 0;
    }

    /* 去重：同 msg_type 同 seq = 重复 */
    int8_t last = L->last_seen_seq[mt];
    int is_dup = (last >= 0 && (uint8_t)last == seq);
    if (!is_dup) L->last_seen_seq[mt] = (int8_t)seq;

    if (is_dup) {
        /* 重复帧：记信息供上层回 ACK，但不交上层处理 */
        s_dup_seq = seq;
        s_dup_mt = mt;
        s_dup_valid = 1;
        if (out_is_dup) *out_is_dup = 1;
        return 0;
    }

    /* 新帧：交上层 */
    if (out_payload) *out_payload = payload;
    if (out_len) *out_len = plen;
    return 1;
}

int comm_last_dup_info(comm_link_t *L, uint8_t *out_seq, uint8_t *out_mt)
{
    (void)L;
    if (!s_dup_valid) return 0;
    if (out_seq) *out_seq = s_dup_seq;
    if (out_mt) *out_mt = s_dup_mt;
    s_dup_valid = 0;  /* 取一次清一次 */
    return 1;
}
