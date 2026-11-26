/**
 * link_send.c — 屏（H743）→ 主控（ESP32）参数写入的发送策略。见 link_send.h。
 * 零 HAL 依赖：主机单测 tests/test_link_send.c 直接编译本文件。
 */
#include "link_send.h"
#include "tcs_proto.h"
#include "proto.h"
#include "param_registry.h"   /* 只用 PF_M207 这一个标志位（不调用注册表的任何函数）*/
#include <string.h>

/* 组 "MT+Seq+body" 整帧（与 main.c 的 proto_encode_frame_with_mt 同构）*/
static size_t frame_with_mt(uint8_t mt, uint8_t seq, const uint8_t *body, size_t blen,
                            uint8_t *out, size_t cap)
{
    uint8_t payload[MAX_PAYLOAD];
    if (blen + 2 > MAX_PAYLOAD) return 0;
    payload[0] = mt;
    payload[1] = seq;
    memcpy(&payload[2], body, blen);
    return proto_encode_frame(payload, blen + 2, out, cap);
}

/* X01-F-03：把"这一帧写的是哪个参数"记进跟踪表（定义在下面）*/
static void track_add(uint8_t seq, uint8_t mt, uint16_t id, uint8_t type, float value);

int link_send_set_param(comm_link_t *L, uint16_t id, uint8_t type, float value)
{
    tcs_param_pl_t p = {
        .id = id,
        .type = type,
        .value = (type == PARAM_TYPE_F32) ? tcs_f2b(value) : (uint32_t)value,
    };
    uint8_t body[TCS_PARAM_PL_LEN];
    if (tcs_pl_param(body, sizeof(body), &p) != TCS_PARAM_PL_LEN) return -1;
    uint8_t frame[MAX_FRAME];
    uint8_t seq = comm_next_seq(L);
    size_t n = frame_with_mt(MT_TCS_SET_PARAM, seq, body, sizeof(body),
                             frame, sizeof(frame));
    if (!n) return -1;
    /* 等 ACK：comm_send 登记 pending（500ms 超时 ×3 重传），首传经待发队列由 comm_poll_tx 发出。
     * 成败看返回值（M04-F-04）；序号 255 也是合法序号，不再与"失败"同名。*/
    if (comm_send(L, frame, n, MT_TCS_SET_PARAM, NULL) != 0) return -1;
    /* X01-F-03：记下这次写的是哪个参数，应答回来时按 (seq, mt) 找回。 */
    track_add(seq, MT_TCS_SET_PARAM, id, type, value);
    return 0;
}

int link_send_on_broadcast(comm_link_t *L, uint16_t id, uint8_t type, uint8_t flags, float value)
{
    if (flags & PF_M207) return 0;   /* 已由 on_m207_* 钩子经同一条 0x12 路径发出 */
    return link_send_set_param(L, id, type, value);
}

/* ====== X01-F-03：应答结果 → 参数写入记录 ======
 * 表深 = COMM_MAX_PENDING（同时等 ACK 的上限），所以正常不会因为"表满"丢记录；真满了就覆盖
 * 最老的那条（那条的应答会当成"不是参数写入"忽略，只影响提示，不影响链路）。*/
static struct {
    uint8_t  used, seq, mt;
    uint16_t id;
    uint8_t  type;
    float    value;
    uint32_t stamp;      /* 单调递增，满时替换最老 */
} s_req[COMM_MAX_PENDING];
static uint32_t s_req_stamp;
static link_send_result_cb_t s_result_cb;
static uint32_t s_bad, s_clamp, s_busy;   /* 非 OK 写入按 status 分桶 */

static void track_add(uint8_t seq, uint8_t mt, uint16_t id, uint8_t type, float value)
{
    int slot = -1;
    for (int i = 0; i < COMM_MAX_PENDING; i++) {
        if (!s_req[i].used) { slot = i; break; }
    }
    if (slot < 0) {                        /* 满：替换最老 */
        slot = 0;
        for (int i = 1; i < COMM_MAX_PENDING; i++)
            if (s_req[i].stamp < s_req[slot].stamp) slot = i;
    }
    s_req[slot].used = 1;
    s_req[slot].seq = seq;
    s_req[slot].mt = mt;
    s_req[slot].id = id;
    s_req[slot].type = type;
    s_req[slot].value = value;
    s_req[slot].stamp = ++s_req_stamp;
}

static int track_find(uint8_t seq, uint8_t mt)
{
    for (int i = 0; i < COMM_MAX_PENDING; i++)
        if (s_req[i].used && s_req[i].seq == seq && s_req[i].mt == mt) return i;
    return -1;
}

void link_send_on_result(link_send_result_cb_t cb) { s_result_cb = cb; }

void link_send_on_ack_result(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret)
{
    int i = track_find(acked_seq, (uint8_t)(acked_mt & 0xFFu));
    if (i < 0) return;                     /* 不是参数写入（0x13 / 0x15 的应答）：不归这里 */
    s_req[i].used = 0;
    switch (status) {
    case TCS_ACK_OK:    break;             /* 成功：主控已落表，镜像/快照会带权威值回来 */
    case TCS_ACK_CLAMP: s_clamp++; break;  /* 主控限幅：ret 是实际写入值 */
    case TCS_ACK_BUSY:  s_busy++;  break;
    default:            s_bad++;   break;  /* BAD_ID 及其它未知状态：拒绝 */
    }
    if (s_result_cb) s_result_cb(s_req[i].id, status, ret, s_req[i].value);
}

void link_send_ack_stats(uint32_t *out_bad, uint32_t *out_clamp, uint32_t *out_busy)
{
    if (out_bad) *out_bad = s_bad;
    if (out_clamp) *out_clamp = s_clamp;
    if (out_busy) *out_busy = s_busy;
}

int link_send_inflight(void)
{
    int n = 0;
    for (int i = 0; i < COMM_MAX_PENDING; i++) if (s_req[i].used) n++;
    return n;
}
