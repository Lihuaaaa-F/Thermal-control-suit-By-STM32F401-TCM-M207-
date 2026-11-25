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
    size_t n = frame_with_mt(MT_TCS_SET_PARAM, comm_next_seq(L), body, sizeof(body),
                             frame, sizeof(frame));
    if (!n) return -1;
    /* 等 ACK：comm_send 登记 pending（500ms 超时 ×3 重传），首传经待发队列由 comm_poll_tx 发出。*/
    return (comm_send(L, frame, n, MT_TCS_SET_PARAM) != 0xFFu) ? 0 : -1;
}

int link_send_on_broadcast(comm_link_t *L, uint16_t id, uint8_t type, uint8_t flags, float value)
{
    if (flags & PF_M207) return 0;   /* 已由 on_m207_* 钩子经同一条 0x12 路径发出 */
    return link_send_set_param(L, id, type, value);
}
