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
#include "tcs_proto.h"   /* R5-01：方向闸门的判定（tcs_rx_verdict 等纯函数，协议唯一源）*/
#include <string.h>

/* 诊断：实际发出的帧数（主循环 comm_poll_tx 唯一计数点；真机取证用）*/
volatile uint32_t g_comm_tx_frames;

void comm_init(comm_link_t *L,
               void (*send_fn)(const uint8_t *data, size_t len),
               uint32_t (*now_ms_fn)(void),
               void (*on_link_down)(void),
               void (*enter_critical)(void),
               void (*exit_critical)(void),
               uint8_t self_side)
{
    memset(L, 0, sizeof(*L));
    L->send_fn = send_fn;
    L->now_ms_fn = now_ms_fn;
    L->on_link_down = on_link_down;
    L->enter_critical = enter_critical;
    L->exit_critical = exit_critical;
    L->link_ok = COMM_LINK_OK_TRUE;
    /* R5-01：本端身份。只认 TCS_SIDE_ESP32 这一个非零值，其余一律按 H743（屏）处理。*/
    L->self_side = (self_side == TCS_SIDE_ESP32) ? TCS_SIDE_ESP32 : TCS_SIDE_H743;
    /* R1：last_seen_seq 用 uint8_t + last_seen_ok 位图表示"是否见过"（全 0 = 都没见过）*/
    for (int i = 0; i < 256; i++) L->last_seen_seq[i] = 0;
    for (int i = 0; i < 32; i++)  L->last_seen_ok[i] = 0;
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
        L->txq_drop++;              /* M11-F-10：溢出可观测（原实现静默丢）*/
    }
    memcpy(L->tx_queue[t], frame, len);
    /* R5-01：出队上线的帧带本端方向位（整帧第 4 字节 = 消息号字节，bit6）。所有发送都经本函数，
     * 发送点不需要、也不能自己打；已带方向位的输入先去掉再按本端身份打。
     * CRC 覆盖"Len + 载荷"，消息号字节改了就必须按改后的字节重算——否则主控发出的帧
     * （bit6=1）在屏端全部过不了 CRC（屏发出的帧 bit6=0，字节不变，CRC 不动）。
     * 顺带自检：方向表不允许本端发的消息号计 tx_wrongdir_cnt（仍照发，由对端按方向表丢弃）。*/
    if (len >= 5u && len <= MAX_FRAME) {
        uint8_t mt = tcs_frame_mt(frame[3]);
        uint8_t mt_wire = tcs_frame_mark(mt, L->self_side);
        if (!tcs_mt_sent_by(mt, L->self_side)) L->tx_wrongdir_cnt++;
        if (mt_wire != frame[3]) {
            L->tx_queue[t][3] = mt_wire;
            L->tx_queue[t][len - 1] = proto_crc8(&L->tx_queue[t][2], len - 3);
        }
    }
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
    g_comm_tx_frames++;

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
        /* M04-F-05：满了就逐"最早登记"的那条（原实现固定覆盖最后一槽，注释却写
         * "丢老的"——真丢的可能是刚登记、还没重传过的急停/命令）。*/
        idx = 0;
        for (int i = 1; i < COMM_MAX_PENDING; i++)
            if ((int32_t)(L->pending[i].enq_ms - L->pending[idx].enq_ms) < 0) idx = i;
        L->pending_evict++;
    }
    comm_pending_t *e = &L->pending[idx];
    memcpy(e->frame, frame_bytes, len);
    e->frame_len = len;
    e->seq = seq;
    e->msg_type = msg_type;
    e->retries = 0;
    e->in_use = 1;
    e->enq_ms = L->now_ms_fn();
    e->deadline_ms = e->enq_ms + COMM_ACK_TIMEOUT_MS;

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
        /* R3-A-1：模 2^32 比较。原 `now < deadline` 在 uptime 跨 49.7 天回绕时
         * 会把「刚发的帧」误判成超时并重传/断链。*/
        if ((int32_t)(now - e->deadline_ms) < 0) continue;  /* 未超时（模 2^32 有符号差）*/
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
        L->retry_cnt++;
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

/* R5-01 方向闸门。判定是 protocol/tcs_proto.h 的纯函数 tcs_rx_verdict（协议唯一源），
 * 这里只负责在"任何处理之前"调用它并分桶计数。
 * ⚠ 两端各有一份 comm.c（ESP32：esp32/components/tcs_link/comm.c）。从下面这行注释起到函数结束，
 * 两份必须逐字相同：python tools/proto_sync.py --check 每次核对，两端各自的主机用例
 * （project_tcs/tests/test_link_dir.c、esp32/tests/test_link_dir.c）与变异验证
 * （tests/mutation_link_dir.py）各自编译自己那一份。*/
static int comm_rx_gate(comm_link_t *L, const uint8_t *payload, size_t plen)
{
    int vd = tcs_rx_verdict(payload[0], L->self_side, payload, plen);
    if (vd == TCS_RX_ACCEPT) return 0;
    uint8_t mt = tcs_frame_mt(payload[0]);
    if (vd == TCS_RX_ECHO) {
        L->echo_cnt++;
        if (L->echo_by_mt[mt] != 0xFFFFu) L->echo_by_mt[mt]++;
    } else {
        L->wrongdir_cnt++;
    }
    return 1;
}

int comm_rx_feed_byte(comm_link_t *L, uint8_t b,
                      const uint8_t **out_payload, size_t *out_len, int *out_is_dup)
{
    if (out_is_dup) *out_is_dup = 0;

    /* M04-F-03：半帧 + 线路静默 → 丢弃半帧回"找帧头"。
     * 阈值取 50ms 而非 3.5 字符时间（230400 下 ≈152µs）：本层只能测"处理时间"，
     * 任务/主循环调度抖动会把正常突发误判成帧结束；50ms 远大于一帧线时（最满帧 250B≈11ms），
     * 又远小于快照周期（200ms），只用于从"帧头后噪声/截断 + 静默"中恢复——
     * 否则后续有效帧会被当成这半帧的 payload 吞掉（Len 被噪声放大时最多吞 250B）。*/
    if (L->now_ms_fn) {
        uint32_t now = L->now_ms_fn();
        if (L->rx_gap_seen && (uint32_t)(now - L->rx_last_ms) > COMM_RX_GAP_MS) {
            proto_rx_reset(&L->rx);
            L->rx_gap_resets++;
        }
        L->rx_last_ms = now;
        L->rx_gap_seen = 1;
    }

    const uint8_t *payload; size_t plen;
    if (!proto_rx_feed(&L->rx, b, &payload, &plen)) {
        /* 还在收/CRC 错/杂字节，不交付 */
        return 0;
    }
    if (plen < 2) return 0;  /* 至少 MsgType+SeqNo */
    /* R5-01：方向闸门——本端发出的帧被回送 / 对端不可能发来的消息号，在 ACK 处理、去重、
     * 交付之前丢弃。只看字节里的方向信息，不看长度、不看时刻。*/
    if (comm_rx_gate(L, payload, plen)) return 0;
    uint8_t mt = tcs_frame_mt(payload[0]);
    uint8_t seq = payload[1];

    /* ACK 帧：交给发送侧清 pending，不交上层 */
    /* TCS 扩展（任务书 §4.2）：0x90 = TCS_ACK，布局与 GS ACK 兼容（[2]acked_seq [3]acked_mt）
     * R5-01：载荷里的被确认消息号按"去掉方向位"解释（comm 层往上只认去掉方向位的消息号）。*/
    if (mt == MT_UPLINK_ACK || mt == MT_DOWNLINK_ACK || mt == MT_TCS_ACK) {
        if (plen < 4) return 0;
        comm_on_ack(L, payload[2], tcs_frame_mt(payload[3]));
        return 0;
    }

    /* 去重：同 msg_type 同 seq = 重复。
     * R1 修复 P1（M04-F-02）：原用 int8_t 存 0..255，seq≥128 变负数 → 判定恒假、去重失效。
     * 改为 uint8_t 值 + 32B 位图（该 msg_type 是否见过），两者共同构成判据。*/
    int is_dup;
    {
        uint8_t bit = (uint8_t)(1u << (mt & 7u));
        uint8_t *bm = &L->last_seen_ok[(mt >> 3) & 31u];
        is_dup = ((*bm & bit) != 0) && (L->last_seen_seq[mt] == seq);
        if (!is_dup) { *bm |= bit; L->last_seen_seq[mt] = seq; }
    }

    if (is_dup) {
        L->dup_cnt++;               /* M11-F-10：重复帧分桶 */
        /* 重复帧：记信息供上层回 ACK，但不交上层处理 */
        s_dup_seq = seq;
        s_dup_mt = mt;
        s_dup_valid = 1;
        if (out_is_dup) *out_is_dup = 1;
        return 0;
    }

    /* 新帧：交上层。消息号字节去掉方向位再交（上层只认 protocol/tcs_proto.h 里的消息号）。
     * proto_rx_feed 交付的就是 L->rx.payload 本身：原地改第 0 字节，有效期不变（到下一帧）。*/
    uint8_t *out = L->rx.payload;
    if (payload != out) return 0;   /* 帧不在本链路的接收缓冲里：不交付（正常不会发生）*/
    out[0] = mt;
    if (out_payload) *out_payload = out;
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

