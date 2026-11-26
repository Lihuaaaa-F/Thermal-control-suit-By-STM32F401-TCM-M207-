/**
 * link_send.c — 上行发送（M11-F-15 拆分，S 线 e1）
 *
 * 从 link_task.c 拆出的第 5 块：组帧 + 六种上行帧的发送。免 ARQ / 等 ACK 的选择按
 * protocol/tcs_proto.h 的方向表（0x11/0x20/0x21/0x22 免 ARQ；0x13/0x15 等 ACK；0x90 直接入队）。
 */
#include "link_internal.h"
#include <string.h>

/* —— 组帧（MT+Seq+body → 整帧）—— */
size_t link_frame_build(uint8_t mt, uint8_t seq, const uint8_t *body, size_t blen,
                        uint8_t *out, size_t cap)
{
    uint8_t payload[MAX_PAYLOAD];
    if (blen + 2 > MAX_PAYLOAD) return 0;
    payload[0] = mt;
    payload[1] = seq;
    memcpy(&payload[2], body, blen);
    return proto_encode_frame(payload, blen + 2, out, cap);
}

/* R2：回 ACK（H743 侧 comm 层按 (seq,mt) 匹配 pending；fire-and-forget 入队）*/
void link_send_ack(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret)
{
    uint8_t pl[TCS_ACK_PL_LEN];
    tcs_ack_pl_t a = { acked_seq, acked_mt, status, ret };
    size_t n = tcs_pl_ack(pl, sizeof(pl), &a);
    uint8_t frame[MAX_FRAME];
    size_t fn = link_frame_build(MT_TCS_ACK, comm_next_seq(link_ctx()), pl, n,
                                 frame, sizeof(frame));
    if (fn) comm_txq_push(link_ctx(), frame, fn);
}

/* —— 上行 API —— */

/* 返回值（M08-F-06）：0=已入队，1=演示模式抑制（无真链路，属正常），-1=组帧失败 */
int link_send_frame_mt(uint8_t mt, const uint8_t *pl, size_t n)
{
#if CONFIG_TCS_DEMO
    if (link_demo_suppressed()) return 1;          /* 演示模式不发（本地闭环）*/
#endif
    uint8_t frame[MAX_FRAME];
    size_t fn = link_frame_build(mt, comm_next_seq(link_ctx()), pl, n, frame, sizeof(frame));
    if (!fn) return -1;
    /* R1 P1-02：0x20/0x21 免 ARQ。屏端本来就不回 ACK，走 ARQ 会让 8 槽 pending
     * 被快照长期占满、每帧重传 4 次，并把参数写/命令帧挤掉。*/
    if (mt == MT_TCS_SNAPSHOT || mt == MT_TCS_DELTA) {
        comm_send_noarq(link_ctx(), frame, fn);
    } else {
        comm_send(link_ctx(), frame, fn, mt, NULL);
    }
    return 0;
}

/* v2 补口（2026-11-16）：ESP32→H743 参数镜像。R5-01（2026-11-25）起 0x11 只在这个方向使用：
 * 屏写参数一律走 0x12（等 ACK），ESP32 收到 0x11 一律按方向表丢弃，不存在"把 0x11 当成屏的
 * 写入"的代码路径——2026-11-23 现场那条 tc1_set 40.1→5.0 的路（本端镜像回声冒充屏的写入）由此关掉。
 * 背景：网页/控制台改的非设定值参数（body_hot/body_cold/hyst/tc_set_confirm/0x1xx…）需要
 * 有通道回屏，否则屏上显示旧值，用户在屏上再按 +/- 就会用旧值覆盖网页的新值（静默数据丢失）。
 * H743 侧 param_mirror_apply 静默落表、不回推，故不会成环。*/
int link_send_param(uint16_t id, float v)
{
#if CONFIG_TCS_DEMO
    if (link_demo_suppressed()) return 1;  /* 演示模式无真链路（与 link_send_frame_mt 同规）*/
#endif
    uint8_t pl[TCS_PARAM_PL_LEN];
    tcs_param_pl_t p = { .id = id, .type = PARAM_TYPE_F32, .value = tcs_f2b(v) };
    size_t n = tcs_pl_param(pl, sizeof(pl), &p);
    if (!n) return -1;
    uint8_t frame[MAX_FRAME];
    size_t fn = link_frame_build(MT_TCS_PARAM, comm_next_seq(link_ctx()), pl, n, frame, sizeof(frame));
    if (!fn) return -1;
    /* 首发就用 no-ARQ：0x11 是对端"变更即推"的单向帧，H743 侧收到不回的 ACK。
     * 用 comm_send 会登记 pending 却永远等不到确认 → 4 次重传后把链路判成断开。*/
    comm_send_noarq(link_ctx(), frame, fn);
    return 0;
}

/* U 线 W3：0x22 设备信息（热点名/密码/网址/固件版本）→ 屏的连接页。
 * 与 link_send_param 同规：单向周期帧，免 ARQ（屏侧不回 ACK）。载荷由调用方组好。*/
int link_send_info(const uint8_t *pl, size_t n)
{
#if CONFIG_TCS_DEMO
    if (link_demo_suppressed()) return 1;
#endif
    if (!pl || !n || n > TCS_INFO_MAX_LEN) return -1;
    uint8_t frame[MAX_FRAME];
    size_t fn = link_frame_build(MT_TCS_INFO, comm_next_seq(link_ctx()), pl, n, frame, sizeof(frame));
    if (!fn) return -1;
    comm_send_noarq(link_ctx(), frame, fn);
    return 0;
}

int link_send_cmd(uint8_t cmd)
{
#if CONFIG_TCS_DEMO
    if (link_demo_suppressed()) return link_demo_cmd(cmd);   /* WP-F.2：演示模式本地闭环 */
#endif
    uint8_t frame[MAX_FRAME];
    size_t fn = link_frame_build(MT_TCS_CMD, comm_next_seq(link_ctx()), &cmd, 1,
                                 frame, sizeof(frame));
    if (!fn) return -1;
    comm_send(link_ctx(), frame, fn, MT_TCS_CMD, NULL);
    return 0;
}
