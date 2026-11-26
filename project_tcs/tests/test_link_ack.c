/* test_link_ack.c — X01-F-03（S 线 e1，2026-11-26）：0x90 应答的 status/ret 交给上层。
 *
 * 修复前：两端 comm.c 收到 0x90 只取 payload[2]/[3]（acked_seq / acked_mt）清 pending，
 * payload[4] 的 status 与 payload[5..8] 的 ret 被丢掉；BAD_ID / CLAMP / BUSY 一律当成功
 * （X01 复核 F-03 实测：status=0/2/255 三种 ACK 都使 pending 1→0、link_ok=1）。
 *
 * 本文件编译两份 comm.c 各一次：
 *   - project_tcs/tests/run_all.sh 的 linkack 一行：H743 的 comm.c + link_send.c，
 *     另验"参数写入"这一层的回退/提示；
 *   - esp32/tests/test_link_ack_comm.c：主控那一份 comm.c（只验 comm 层，定义
 *     LINK_ACK_COMM_ONLY 跳过 link_send 那几节）。
 *
 * 两个 comm 实例连成一条线（S=H743，C=ESP32）。C 侧的应用只做一件事：收到 0x12 按
 * g_reply_status / g_reply_ret 回 0x90（等价于 ESP32 的 esp_on_setparam + link_send_ack）。
 */
#include <stdio.h>
#include <string.h>
#include "proto.h"
#include "comm.h"
#include "tcs_proto.h"
#include "param_registry.h"   /* PARAM_TYPE_F32（两端同名同值；这里只用这一个常量）*/
#ifndef LINK_ACK_COMM_ONLY
#include "link_send.h"
#endif

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static uint8_t  g_tx[2][4096];
static size_t   g_tx_n[2];
static uint32_t g_now;
static uint8_t  g_reply_status = TCS_ACK_OK;
static float    g_reply_ret;

static void append(int side, const uint8_t *d, size_t n)
{
    if (g_tx_n[side] + n <= sizeof(g_tx[side])) {
        memcpy(&g_tx[side][g_tx_n[side]], d, n);
        g_tx_n[side] += n;
    }
}
static int send_screen(const uint8_t *d, size_t n) { append(0, d, n); return 0; }
static int send_ctrl(const uint8_t *d, size_t n)   { append(1, d, n); return 0; }
static uint32_t now_fn(void) { return g_now; }

/* —— 应答结果回调记录（comm 层）—— */
static struct {
    int n;
    uint8_t seq, mt, status;
    float ret;
} g_ack_cb;
static void on_ack_cb(uint8_t seq, uint8_t mt, uint8_t status, float ret)
{
    g_ack_cb.n++; g_ack_cb.seq = seq; g_ack_cb.mt = mt;
    g_ack_cb.status = status; g_ack_cb.ret = ret;
}

/* comm 层只有一个应答回调槽；固件里 main.c 注册的是 link_send 的那一个
 * （comm_set_on_ack_result(&g_link, link_send_on_ack_result)）。用例里再串一层，
 * 好同时观察"comm 层收到的原始结果"和"link_send 找回的参数写入"——与固件同一条路。*/
static void on_ack_dispatch(uint8_t seq, uint8_t mt, uint8_t status, float ret)
{
    on_ack_cb(seq, mt, status, ret);
#ifndef LINK_ACK_COMM_ONLY
    link_send_on_ack_result(seq, mt, status, ret);
#endif
}

#ifndef LINK_ACK_COMM_ONLY
/* —— 参数写入结果回调记录（link_send 层）—— */
static struct {
    int n;
    uint16_t id;
    uint8_t status;
    float ret, requested;
} g_wr_cb;
static void on_wr_cb(uint16_t id, uint8_t status, float ret, float requested)
{
    g_wr_cb.n++; g_wr_cb.id = id; g_wr_cb.status = status;
    g_wr_cb.ret = ret; g_wr_cb.requested = requested;
}
#endif

/* —— C（主控）侧最小应用：0x12 → 回 0x90（status/ret 取 g_reply_*）—— */
static int g_writes;
static void ctrl_on_frame(comm_link_t *C, const uint8_t *pl, size_t len)
{
    if (pl[0] != MT_TCS_SET_PARAM || len < 2 + TCS_PARAM_PL_LEN) return;
    g_writes++;
    uint8_t a[TCS_ACK_PL_LEN];
    tcs_ack_pl_t ack = { .acked_seq = pl[1], .acked_mt = MT_TCS_SET_PARAM,
                         .status = g_reply_status, .ret = g_reply_ret };
    (void)tcs_pl_ack(a, sizeof(a), &ack);
    uint8_t payload[2 + TCS_ACK_PL_LEN];
    payload[0] = MT_TCS_ACK;
    payload[1] = comm_next_seq(C);
    memcpy(&payload[2], a, TCS_ACK_PL_LEN);
    uint8_t f[MAX_FRAME];
    size_t n = proto_encode_frame(payload, sizeof(payload), f, sizeof(f));
    comm_txq_push(C, f, n);
}

static comm_link_t S, C;

static void pump(void)
{
    const uint8_t *pl; size_t len; int dup;
    comm_poll_tx(&S);
    for (size_t i = 0; i < g_tx_n[0]; i++)
        if (comm_rx_feed_byte(&C, g_tx[0][i], &pl, &len, &dup)) ctrl_on_frame(&C, pl, len);
    g_tx_n[0] = 0;
    comm_poll_tx(&C);
    for (size_t i = 0; i < g_tx_n[1]; i++)
        comm_rx_feed_byte(&S, g_tx[1][i], &pl, &len, &dup);
    g_tx_n[1] = 0;
}

static void open_links(void)
{
    g_tx_n[0] = g_tx_n[1] = 0; g_now = 1000;
    g_reply_status = TCS_ACK_OK; g_reply_ret = 0.0f;
    g_writes = 0;
    memset(&g_ack_cb, 0, sizeof(g_ack_cb));
#ifndef LINK_ACK_COMM_ONLY
    memset(&g_wr_cb, 0, sizeof(g_wr_cb));
#endif
    comm_init(&S, send_screen, now_fn, NULL, NULL, NULL, TCS_SIDE_H743);
    comm_init(&C, send_ctrl, now_fn, NULL, NULL, NULL, TCS_SIDE_ESP32);
    comm_set_on_ack_result(&S, on_ack_dispatch);
#ifndef LINK_ACK_COMM_ONLY
    link_send_on_result(on_wr_cb);      /* 固件里 main.c 的对应注册 */
#endif
}

/* 直接经 comm 层发一帧 0x12（不经过 link_send），并走完整条线；*out_seq 可为 NULL */
static int raw_set_param(uint16_t id, float v, uint8_t *out_seq)
{
    uint8_t payload[2 + TCS_PARAM_PL_LEN];
    tcs_param_pl_t p = { .id = id, .type = PARAM_TYPE_F32, .value = tcs_f2b(v) };
    payload[0] = MT_TCS_SET_PARAM;
    payload[1] = comm_next_seq(&S);
    (void)tcs_pl_param(&payload[2], TCS_PARAM_PL_LEN, &p);
    uint8_t f[MAX_FRAME];
    size_t n = proto_encode_frame(payload, sizeof(payload), f, sizeof(f));
    int rv = comm_send(&S, f, n, MT_TCS_SET_PARAM, out_seq);
    pump();
    return rv;
}

/* 直接从 C 发一条 0x90（acked_seq/acked_mt/status/ret 任选）——用于"没匹配到 pending"的应答 */
static void peer_ack(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret)
{
    uint8_t a[TCS_ACK_PL_LEN];
    tcs_ack_pl_t ack = { .acked_seq = acked_seq, .acked_mt = acked_mt,
                         .status = status, .ret = ret };
    (void)tcs_pl_ack(a, sizeof(a), &ack);
    uint8_t payload[2 + TCS_ACK_PL_LEN];
    payload[0] = MT_TCS_ACK;
    payload[1] = comm_next_seq(&C);
    memcpy(&payload[2], a, TCS_ACK_PL_LEN);
    uint8_t f[MAX_FRAME];
    size_t n = proto_encode_frame(payload, sizeof(payload), f, sizeof(f));
    comm_txq_push(&C, f, n);
    pump();
}

/* 老式 4 字节 ACK（GS 遗留 0x04，没有 status/ret）*/
static void peer_legacy_ack(uint8_t acked_seq, uint8_t acked_mt)
{
    uint8_t payload[4] = { MT_UPLINK_ACK, comm_next_seq(&C), acked_seq, acked_mt };
    uint8_t f[MAX_FRAME];
    size_t n = proto_encode_frame(payload, sizeof(payload), f, sizeof(f));
    comm_txq_push(&C, f, n);
    pump();
}

int main(void)
{
    uint8_t mt; uint8_t st; float ret;

    /* ---- 1) comm 层：CLAMP 的 status/ret 交给上层 ---- */
    open_links();
    g_reply_status = TCS_ACK_CLAMP; g_reply_ret = 44.5f;
    CHECK(raw_set_param(0x01, 50.0f, NULL) == 0, "1: 参数写入入队");
    CHECK(g_writes == 1, "1: 主控收到 0x12");
    CHECK(comm_pending_count(&S) == 0, "1: 应答清掉 pending（行为不变）");
    CHECK(S.ack_nonok_cnt == 1, "1: 非 OK 应答计数 +1");
    CHECK(g_ack_cb.n == 1 && g_ack_cb.mt == MT_TCS_SET_PARAM &&
          g_ack_cb.status == TCS_ACK_CLAMP && g_ack_cb.ret == 44.5f,
          "1: 完成回调带 status/ret（修复前 ret 恒 0、status 不参与判定）");
    CHECK(comm_last_ack(&S, &mt, &st, &ret) == 1 && mt == MT_TCS_SET_PARAM &&
          st == TCS_ACK_CLAMP && ret == 44.5f, "1: comm_last_ack 可取到最近一条应答的结果");
    CHECK(comm_last_ack(&S, &mt, &st, &ret) == 0, "1: comm_last_ack 读后清标记");

    /* ---- 2) comm 层：OK 不计非 OK，完成回调照样通知 ---- */
    open_links();
    CHECK(raw_set_param(0x01, 30.0f, NULL) == 0, "2: 写入入队");
    CHECK(S.ack_nonok_cnt == 0, "2: OK 不计入非 OK");
    CHECK(g_ack_cb.n == 1 && g_ack_cb.status == TCS_ACK_OK, "2: OK 也调完成回调");

    /* ---- 3) comm 层：status 高位被掩掉（l1 的方向位在消息号字节；将来若有人往 status
     *         高位塞标志，解析方不能把它当成未知状态码）---- */
    open_links();
    g_reply_status = (uint8_t)(0x80u | TCS_ACK_BUSY);
    CHECK(raw_set_param(0x02, 26.0f, NULL) == 0, "3: 写入入队");
    CHECK(g_ack_cb.status == TCS_ACK_BUSY, "3: 0x80|BUSY 按 BUSY 解释（掩码 0x0F）");

    /* ---- 4) comm 层：没匹配到 pending 的非 OK 应答也要计数 ---- */
    open_links();
    peer_ack(99, MT_TCS_SET_PARAM, TCS_ACK_BAD_ID, 0.0f);
    CHECK(S.ack_nonok_cnt == 1, "4: 未匹配的非 OK 应答也计数");
    CHECK(g_ack_cb.n == 0, "4: 未匹配不调完成回调");

    /* ---- 5) comm 层：老式 4 字节 ACK（GS 遗留 0x04）仍能清 pending ---- */
    open_links();
    {
        uint8_t seq = 0;
        CHECK(raw_set_param(0x01, 31.0f, &seq) == 0, "5: 写入入队");
        /* raw_set_param 内部已 pump，C 已回过一条 OK；再造一条 pending 再回老式 ACK */
        CHECK(comm_pending_count(&S) == 0, "5: 第一条已被 0x90 清掉");
        /* 重新登记一条（不 pump，避免 C 立刻应答）*/
        {
            uint8_t payload[2 + TCS_PARAM_PL_LEN];
            tcs_param_pl_t p = { .id = 0x01, .type = PARAM_TYPE_F32, .value = tcs_f2b(31.0f) };
            payload[0] = MT_TCS_SET_PARAM; payload[1] = 0x33;
            (void)tcs_pl_param(&payload[2], TCS_PARAM_PL_LEN, &p);
            uint8_t f[MAX_FRAME];
            size_t n = proto_encode_frame(payload, sizeof(payload), f, sizeof(f));
            CHECK(comm_send(&S, f, n, MT_TCS_SET_PARAM, NULL) == 0, "5: 第二条登记");
        }
        CHECK(comm_pending_count(&S) == 1, "5: 第二条在等应答");
        peer_legacy_ack(0x33, MT_TCS_SET_PARAM);
        CHECK(comm_pending_count(&S) == 0, "5: 老式 4 字节 ACK 仍能清 pending");
        CHECK(S.ack_nonok_cnt == 0, "5: 老式 ACK 没有 status，不计非 OK");
    }

#ifndef LINK_ACK_COMM_ONLY
    /* ---- 6) link_send：CLAMP → 上报"实际写入值 vs 请求值" ---- */
    open_links();
    g_reply_status = TCS_ACK_CLAMP; g_reply_ret = 44.5f;
    CHECK(link_send_set_param(&S, 0x01, PARAM_TYPE_F32, 50.0f) == 0, "6: 写入入队");
    pump();
    CHECK(g_wr_cb.n == 1 && g_wr_cb.id == 0x01 && g_wr_cb.status == TCS_ACK_CLAMP &&
          g_wr_cb.ret == 44.5f && g_wr_cb.requested == 50.0f,
          "6: link_send 按 (seq,mt) 找回参数 id 并上报表决（CLAMP: 实际 44.5 / 请求 50）");
    CHECK(link_send_inflight() == 0, "6: 跟踪表项已释放");

    /* ---- 7) link_send：BAD_ID → 上层拿到"哪一条写入被拒" ---- */
    open_links();
    g_reply_status = TCS_ACK_BAD_ID;
    CHECK(link_send_set_param(&S, 0x7F, PARAM_TYPE_F32, 9.0f) == 0, "7: 写未知 id 入队");
    pump();
    CHECK(g_wr_cb.n == 1 && g_wr_cb.id == 0x7F && g_wr_cb.status == TCS_ACK_BAD_ID &&
          g_wr_cb.requested == 9.0f, "7: 上层能提示/回退被拒的那一条写入");
    CHECK(link_send_inflight() == 0, "7: 跟踪表项已释放");

    /* ---- 8) link_send：BUSY ---- */
    open_links();
    g_reply_status = TCS_ACK_BUSY;
    CHECK(link_send_set_param(&S, 0x02, PARAM_TYPE_F32, 25.0f) == 0, "8: 写入入队");
    pump();
    CHECK(g_wr_cb.n == 1 && g_wr_cb.status == TCS_ACK_BUSY, "8: BUSY 也交给上层");

    /* ---- 9) link_send：连续多次写入，跟踪表不泄漏 ---- */
    open_links();
    for (int i = 0; i < 6; i++)
        CHECK(link_send_set_param(&S, (uint16_t)(0x01 + i), PARAM_TYPE_F32,
                                  25.0f + (float)i) == 0, "9: 连续写入入队");
    for (int k = 0; k < 10; k++) pump();     /* comm_poll_tx 一拍只发一帧 */
    CHECK(link_send_inflight() == 0, "9: 全部应答后无残留表项");
    CHECK(g_wr_cb.n == 6, "9: 每次写入都有一次完成回调");
#endif

    printf("test_link_ack: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
