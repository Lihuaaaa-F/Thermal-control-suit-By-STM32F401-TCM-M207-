/* test_link_dir.c — R5-01（P1，协议分不清方向）的 H743 侧主机用例：屏这一端的 comm 层方向闸门。
 *
 * 被测对象：App/comm/comm.c（H743 那一份）+ protocol/tcs_proto.h 的纯函数。
 * ESP32 那一份 comm.c 由 esp32/tests/test_link_dir.c 以同一组规则测；两份 comm.c 的闸门函数
 * 必须逐字相同（tools/proto_sync.py --check 核对）。
 *
 * 验收判据（impl_brief_l1.md）：
 *   (d) 本端发出的每一种帧被回送，都不得触及任何处理（含 ACK）；
 *   (e) 对端方向来的每一种帧照常处理，没有被误丢；
 *   外加方向表本身（屏方向的写入只认 0x12；对端不会发的消息号按方向表丢弃并计数）。
 * 修复前（"等长 + 发送窗"的 f2571e7，屏端没有任何防护）这些用例必须变红——修复前后的实际输出
 * （未修 comm.c PASS=83 FAIL=26 → 修复后全过）记在 docs/results/S_G_l1_结果.md。
 * 用法：由 tests/run_all.sh 的 linkdir 一行编译运行（gcc 或 MSVC cl）。
 */
#include <stdio.h>
#include <string.h>
#include "proto.h"
#include "comm.h"
#include "tcs_proto.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static uint8_t  g_wire[8192];
static size_t   g_wire_n;
static uint32_t g_now;
static int      g_send_fail_left;     /* X03-F-09：接下来几次 send_fn 报"没有上线"*/

/* send_fn：返回 0 = 这一帧已上线；非 0 = 没有上线（由 g_send_fail_left 控制）*/
static int stub_send(const uint8_t *d, size_t n)
{
    if (g_send_fail_left > 0) { g_send_fail_left--; return -1; }
    if (g_wire_n + n <= sizeof(g_wire)) { memcpy(&g_wire[g_wire_n], d, n); g_wire_n += n; }
    return 0;
}
static uint32_t stub_now(void) { return g_now; }

static void link_open(comm_link_t *L, uint8_t side)
{
    comm_init(L, stub_send, stub_now, NULL, NULL, NULL, side);
}

/* 组一整帧：mt_wire = 线上消息号字节（消息号 | 方向位）*/
static size_t wire_frame(uint8_t mt_wire, uint8_t seq, const uint8_t *body, size_t blen,
                         uint8_t *out)
{
    uint8_t payload[MAX_PAYLOAD];
    payload[0] = mt_wire; payload[1] = seq;
    memcpy(&payload[2], body, blen);
    return proto_encode_frame(payload, blen + 2, out, MAX_FRAME);
}
static size_t ack_frame(uint8_t mt_wire, uint8_t seq, uint8_t acked_seq, uint8_t acked_mt, uint8_t *out)
{
    tcs_ack_pl_t a = { .acked_seq = acked_seq, .acked_mt = acked_mt, .status = TCS_ACK_OK, .ret = 0.0f };
    uint8_t body[TCS_ACK_PL_LEN];
    (void)tcs_pl_ack(body, sizeof(body), &a);
    return wire_frame(mt_wire, seq, body, sizeof(body), out);
}
static void param_body(uint8_t *out, uint16_t id, float v)
{
    tcs_param_pl_t p = { .id = id, .type = 0 /* PARAM_TYPE_F32 */, .value = tcs_f2b(v) };
    (void)tcs_pl_param(out, TCS_PARAM_PL_LEN, &p);
}

/* 把一帧喂给链路：返回被交付给上层的次数；*dup = 是否被判为重复帧 */
static int feed(comm_link_t *L, const uint8_t *frame, size_t n, int *dup, uint8_t *first_mt)
{
    const uint8_t *pl; size_t plen; int d;
    int got = 0;
    if (dup) *dup = 0;
    for (size_t i = 0; i < n; i++) {
        if (comm_rx_feed_byte(L, frame[i], &pl, &plen, &d)) {
            got++;
            if (first_mt && got == 1) *first_mt = pl[0];
        } else if (d && dup) *dup = 1;
    }
    return got;
}

static void tx_drain(comm_link_t *L) { g_wire_n = 0; comm_poll_tx(L); }

/* comm_send + 断言成功，返回序号。M04-F-04：comm_send 本身返回 0 / -1，序号经出参；
 * 这里把"报告成功"也作为断言，再让各用例照旧核对序号。*/
static uint8_t xsend(comm_link_t *L, const uint8_t *f, size_t n, uint8_t mt)
{
    uint8_t s = 0xFF;
    CHECK(comm_send(L, f, n, mt, &s) == 0, "comm_send 报告成功（M04-F-04）");
    return s;
}

/* ======================= P：方向表与判定函数（纯函数）======================= */
static void test_pure(void)
{
    struct { uint8_t mt; uint8_t dir; const char *name; } tbl[] = {
        { MT_TCS_STATUS,         TCS_DIR_H743_ONLY,  "0x10 STATUS" },
        { MT_TCS_PARAM,          TCS_DIR_ESP32_ONLY, "0x11 PARAM(镜像)" },
        { MT_TCS_SET_PARAM,      TCS_DIR_H743_ONLY,  "0x12 SET_PARAM" },
        { MT_TCS_GET_REGISTRY,   TCS_DIR_ESP32_ONLY, "0x13 GET_REGISTRY" },
        { MT_TCS_REGISTRY_ENTRY, TCS_DIR_H743_ONLY,  "0x14 REGISTRY_ENTRY" },
        { MT_TCS_CMD,            TCS_DIR_BOTH,       "0x15 CMD" },
        { MT_TCS_SNAPSHOT,       TCS_DIR_ESP32_ONLY, "0x20 SNAPSHOT" },
        { MT_TCS_DELTA,          TCS_DIR_ESP32_ONLY, "0x21 DELTA" },
        { MT_TCS_INFO,           TCS_DIR_ESP32_ONLY, "0x22 INFO" },
        { MT_TCS_ACK,            TCS_DIR_BOTH,       "0x90 ACK" },
    };
    CHECK(sizeof(tbl) / sizeof(tbl[0]) == 10, "P: 方向表 10 个消息号");
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        uint8_t mt = tbl[i].mt, dir = tbl[i].dir;
        int ok = (tcs_mt_dir(mt) == dir) &&
                 (tcs_mt_sent_by(mt, TCS_SIDE_H743) == ((dir & TCS_DIR_H743_ONLY) != 0)) &&
                 (tcs_mt_sent_by(mt, TCS_SIDE_ESP32) == ((dir & TCS_DIR_ESP32_ONLY) != 0));
        if (!ok) printf("  方向表不符：%s\n", tbl[i].name);
        CHECK(ok, tbl[i].name);
        /* 消息号 / 方向位 ⇄ 线上字节 的往返 */
        CHECK(tcs_frame_mt(tcs_frame_mark(mt, TCS_SIDE_ESP32)) == mt &&
              tcs_frame_sender(tcs_frame_mark(mt, TCS_SIDE_ESP32)) == TCS_SIDE_ESP32 &&
              tcs_frame_sender(tcs_frame_mark(mt, TCS_SIDE_H743)) == TCS_SIDE_H743, "P: 标方向/取方向往返");
    }
    /* 未登记的消息号：两个方向都放行（前向兼容），bit7 不是方向位 */
    CHECK(tcs_mt_dir(0x30) == 0 && tcs_mt_sent_by(0x30, TCS_SIDE_H743) && tcs_mt_sent_by(0x30, TCS_SIDE_ESP32),
          "P: 未登记消息号两个方向都放行");
    CHECK(tcs_frame_mt(0x90) == 0x90 && tcs_frame_mt(0xD0) == 0x90, "P: ACK 的 bit7 属于消息号，不是方向位");

    /* tcs_rx_verdict：两个身份 × 每个消息号 × 两个方向 */
    uint8_t body[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    for (size_t i = 0; i < sizeof(tbl) / sizeof(tbl[0]); i++) {
        uint8_t mt = tbl[i].mt, dir = tbl[i].dir;
        uint8_t pl[4] = { mt, 0x11, 0, 0 };
        /* 屏这一端：屏发的 = 回声；主控发的 = 方向表说了算 */
        pl[0] = tcs_frame_mark(mt, TCS_SIDE_H743);
        CHECK(tcs_rx_verdict(pl[0], TCS_SIDE_H743, pl, 4) == TCS_RX_ECHO, "P: 屏端收屏方向=回声");
        pl[0] = tcs_frame_mark(mt, TCS_SIDE_ESP32);
        CHECK(tcs_rx_verdict(pl[0], TCS_SIDE_H743, pl, 4) ==
              ((dir & TCS_DIR_ESP32_ONLY) ? TCS_RX_ACCEPT : TCS_RX_WRONG_DIR), "P: 屏端收主控方向按方向表");
        /* 主控那一端对称 */
        pl[0] = tcs_frame_mark(mt, TCS_SIDE_ESP32);
        CHECK(tcs_rx_verdict(pl[0], TCS_SIDE_ESP32, pl, 4) == TCS_RX_ECHO, "P: 主控端收主控方向=回声");
        pl[0] = tcs_frame_mark(mt, TCS_SIDE_H743);
        CHECK(tcs_rx_verdict(pl[0], TCS_SIDE_ESP32, pl, 4) ==
              ((dir & TCS_DIR_H743_ONLY) ? TCS_RX_ACCEPT : TCS_RX_WRONG_DIR), "P: 主控端收屏方向按方向表");
        (void)body;
    }
    /* ACK 的被确认消息号：必须是收到这帧 ACK 的一端会发出去的消息号 */
    {
        uint8_t pl[4];
        pl[0] = tcs_frame_mark(MT_TCS_ACK, TCS_SIDE_ESP32);
        pl[2] = 0x40;
        pl[3] = MT_TCS_SET_PARAM;      /* 主控确认收到屏的 0x12：屏会发 0x12 → 合理 */
        CHECK(tcs_rx_verdict(pl[0], TCS_SIDE_H743, pl, 4) == TCS_RX_ACCEPT, "P: ACK 确认了本端会发的 0x12");
        pl[3] = MT_TCS_GET_REGISTRY;   /* 主控确认收到屏的 0x13：屏从不发 0x13 → 不合理 */
        CHECK(tcs_rx_verdict(pl[0], TCS_SIDE_H743, pl, 4) == TCS_RX_WRONG_DIR, "P: ACK 确认了本端不会发的 0x13");
        pl[3] = (uint8_t)(MT_TCS_SET_PARAM | 0x40);   /* 被确认消息号若带了方向位，按去掉方向位解释 */
        CHECK(tcs_rx_verdict(pl[0], TCS_SIDE_H743, pl, 4) == TCS_RX_ACCEPT, "P: 被确认消息号带方向位也能认");
        CHECK(tcs_rx_verdict(pl[0], TCS_SIDE_H743, pl, 3) == TCS_RX_ACCEPT, "P: ACK 载荷不足 4 字节交给原逻辑丢弃");
    }
}

/* ======================= T：屏发出的帧带屏方向，消息号在屏的方向表里 ======================= */
static void test_tx(void)
{
    comm_link_t L;
    uint8_t body[TCS_PARAM_PL_LEN]; param_body(body, 0x01, 30.0f);
    uint8_t f[MAX_FRAME]; size_t n;
    g_wire_n = 0;
    link_open(&L, TCS_SIDE_H743);

    /* 等 ACK 的帧（0x12）与免 ARQ 的帧（0x90 ACK）出线时消息号字节 = 消息号本身（屏方向 bit6=0）*/
    n = wire_frame(MT_TCS_SET_PARAM, 7, body, sizeof(body), f);
    CHECK(xsend(&L, f, n, MT_TCS_SET_PARAM) == 7, "T: 0x12 已登记");
    tx_drain(&L);
    CHECK(g_wire_n == n && g_wire[0] == 0xAA && g_wire[3] == MT_TCS_SET_PARAM, "T: 0x12 出线带屏方向位（bit6=0）");
    n = ack_frame(MT_TCS_ACK, 8, 7, MT_TCS_GET_REGISTRY, f);
    comm_txq_push(&L, f, n);
    tx_drain(&L);
    CHECK(g_wire_n == n && g_wire[3] == MT_TCS_ACK, "T: 0x90 出线带屏方向位（bit6=0）");

    /* 已带方向位的输入：先去掉再按本端身份打（屏发出去的帧不可能是主控方向）。
     * 消息号字节在 CRC 覆盖范围内：字节改了 CRC 必须重算，否则对端过不了校验。*/
    n = wire_frame((uint8_t)(MT_TCS_GET_REGISTRY | 0x40), 9, NULL, 0, f);
    comm_txq_push(&L, f, n);
    tx_drain(&L);
    CHECK(g_wire_n == n && g_wire[3] == MT_TCS_GET_REGISTRY, "T: 输入带错方向位时按本端身份重打");
    CHECK(proto_crc8(&g_wire[2], g_wire_n - 3) == g_wire[g_wire_n - 1], "T: 重打方向位之后 CRC 是重算过的");
    /* 用主控身份发一帧（bit6=1）：CRC 必须按带方向位的字节算——2026-11-25 实现第一版就是在这里
     * 漏了重算，主控发出的帧在屏端全部过不了 CRC（esp32/tests/test_link_dir.c 抓到）。*/
    {
        comm_link_t E;
        size_t save = g_wire_n;
        link_open(&E, TCS_SIDE_ESP32);
        comm_txq_push(&E, f, n);
        g_wire_n = 0;
        comm_poll_tx(&E);
        CHECK(g_wire_n == n && g_wire[3] == (uint8_t)(MT_TCS_GET_REGISTRY | 0x40), "T: 主控身份出线带主控方向位（bit6=1）");
        CHECK(proto_crc8(&g_wire[2], g_wire_n - 3) == g_wire[g_wire_n - 1], "T: 带方向位的帧 CRC 正确");
        CHECK(E.tx_wrongdir_cnt == 0, "T: 主控发 0x13 是方向表允许的");
        g_wire_n = save;
    }

    /* 重传的也是带方向位的同一帧 */
    g_wire_n = 0;
    g_now += COMM_ACK_TIMEOUT_MS + 1;
    comm_tick(&L);
    tx_drain(&L);
    CHECK(g_wire_n > 0 && g_wire[3] == MT_TCS_SET_PARAM && g_wire[4] == 7, "T: 重传帧同样带屏方向位");

    CHECK(L.tx_wrongdir_cnt == 1, "T: 自检——屏发出主控专用的 0x13 被计了 tx_wrongdir（重打那一帧）");
}

/* ======================= X03-F-09：发送失败不出队 =======================
 * send_fn（屏端 = 阻塞的 HAL_UART_Transmit）返回失败 = 这一帧没有上线。
 * 原来 send_fn 是 void、发完就前移队头：HAL_BUSY 的那一帧被消费掉了却从没上过线。*/
extern volatile uint32_t g_comm_tx_frames;
static void test_tx_fail(void)
{
    comm_link_t L;
    uint8_t body[TCS_PARAM_PL_LEN], f[MAX_FRAME], f2[MAX_FRAME]; size_t n, n2;
    uint32_t sent0;
    int pend;
    param_body(body, 0x01, 30.0f);
    g_wire_n = 0;
    link_open(&L, TCS_SIDE_H743);

    /* 队头那帧第一次没上线（HAL_BUSY）：留在队头，下一拍重试，成功后只上线一次 */
    n = wire_frame(MT_TCS_SET_PARAM, 0x41, body, sizeof(body), f);
    (void)xsend(&L, f, n, MT_TCS_SET_PARAM);
    sent0 = g_comm_tx_frames;
    g_send_fail_left = 1;
    comm_poll_tx(&L);                          /* 失败：不出队 */
    CHECK(comm_txq_count(&L) == 1 && g_wire_n == 0, "X: 发送失败的那一帧留在队里");
    CHECK(L.tx_fail_cnt == 1 && L.txq_drop == 0, "X: 记一次发送失败，没有丢帧");
    CHECK(comm_pending_count(&L) == 1, "X: 等待确认的登记还在");
    CHECK(g_comm_tx_frames == sent0, "X: 失败的发送不计入\"实际发出\"的帧数");
    comm_poll_tx(&L);                          /* 重试成功 */
    CHECK(comm_txq_count(&L) == 0 && g_wire_n == n && g_wire[4] == 0x41, "X: 下一拍重试上线，且只上线一次");
    CHECK(g_comm_tx_frames == sent0 + 1, "X: 成功那一次计入\"实际发出\"");
    CHECK(L.tx_fail_streak == 0, "X: 成功后失败连计清零");
    g_wire_n = 0;

    /* 队头那一帧一直发不出去（句柄卡死）：连续 COMM_TX_MAX_FAIL 次失败后丢弃它，
     * 不能把后面排着的帧堵死——后面排着的是急停命令 */
    n = wire_frame(MT_TCS_SET_PARAM, 0x42, body, sizeof(body), f);
    n2 = wire_frame(MT_TCS_CMD, 0x43, (const uint8_t *)"\x08", 1, f2);
    (void)xsend(&L, f, n, MT_TCS_SET_PARAM);
    comm_txq_push(&L, f2, n2);
    pend = comm_pending_count(&L);            /* 0x41 与 0x42 两帧都还在等确认 */
    g_send_fail_left = COMM_TX_MAX_FAIL;
    for (int i = 0; i < COMM_TX_MAX_FAIL - 1; i++) {
        comm_poll_tx(&L);
        CHECK(comm_txq_count(&L) == 2 && g_wire_n == 0, "X: 失败次数没到上限之前不出队");
    }
    comm_poll_tx(&L);                          /* 第 COMM_TX_MAX_FAIL 次失败：丢弃队头 */
    CHECK(comm_txq_count(&L) == 1 && L.txq_drop == 1, "X: 连续失败的队头帧被丢弃并计数");
    CHECK(comm_pending_count(&L) == pend, "X: 丢弃出队帧不动等待确认的登记（之后由 comm_tick 超时重传）");
    comm_poll_tx(&L);                          /* 下一帧：急停命令 */
    CHECK(comm_txq_count(&L) == 0 && g_wire_n == n2 && g_wire[3] == MT_TCS_CMD,
          "X: 队头丢弃之后，后面排着的急停命令发了出去");
}

/* ======================= (d) 本端发出的每一种帧被回送 ======================= */
static void test_echo(void)
{
    comm_link_t L;
    uint8_t f[MAX_FRAME], body[16], spl[TCS_SNAP_PL_LEN];
    size_t n; int dup = 0, got; uint8_t mt0 = 0;
    g_wire_n = 0;
    link_open(&L, TCS_SIDE_H743);

    /* 先挂一笔等待确认的出站帧：seq=0x21 的 0x12、seq=0x22 的 0x15 */
    param_body(body, 0x01, 30.0f);
    n = wire_frame(MT_TCS_SET_PARAM, 0x21, body, TCS_PARAM_PL_LEN, f);
    CHECK(xsend(&L, f, n, MT_TCS_SET_PARAM) == 0x21, "D: 0x12 已登记");
    n = wire_frame(MT_TCS_CMD, 0x22, (const uint8_t *)"\x08", 1, f);
    (void)xsend(&L, f, n, MT_TCS_CMD);
    tx_drain(&L);
    CHECK(comm_pending_count(&L) == 2, "D: 两帧在等确认");
    g_wire_n = 0;

    /* 自己的 0x90 应答回送，且 (seq, mt) 恰好等于等待确认的 0x12：不得清等待状态 */
    n = ack_frame(MT_TCS_ACK, 0x30, 0x21, MT_TCS_SET_PARAM, f);
    got = feed(&L, f, n, &dup, NULL);
    CHECK(got == 0 && dup == 0, "D: 自己的 ACK 回声不交付、不算重复");
    CHECK(comm_pending_count(&L) == 2, "D: ACK 回声没有清掉等待确认状态");
    /* 自己的急停命令帧回送：不得再执行一次 */
    n = wire_frame(MT_TCS_CMD, 0x22, (const uint8_t *)"\x08", 1, f);
    got = feed(&L, f, n, &dup, &mt0);
    CHECK(got == 0 && dup == 0, "D: 自己的 0x15 回声不交付、不算重复");
    CHECK(comm_pending_count(&L) == 2, "D: 0x15 回声没有清掉等待确认状态");
    /* 自己的 0x12 / 0x10 / 0x14 回送 */
    n = wire_frame(MT_TCS_SET_PARAM, 0x21, body, TCS_PARAM_PL_LEN, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && dup == 0, "D: 自己的 0x12 回声不交付");
    tcs_status_pl_t st; memset(&st, 0, sizeof(st)); st.ver = TCS_PROTO_VER;
    (void)tcs_pl_status(spl, sizeof(spl), &st);
    n = wire_frame(MT_TCS_STATUS, 0x40, spl, TCS_STATUS_PL_LEN, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && dup == 0, "D: 自己的 0x10 回声不交付");
    tcs_reg_entry_t e; memset(&e, 0, sizeof(e)); e.count = 51; e.idx = 0; e.id = 0x01;
    strcpy(e.key, "tc1_set"); strcpy(e.name, "x"); strcpy(e.unit, "C");
    uint8_t rp[200]; n = tcs_pl_registry_entry(rp, sizeof(rp), &e);
    n = wire_frame(MT_TCS_REGISTRY_ENTRY, 0x41, rp, n, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && dup == 0, "D: 自己的 0x14 回声不交付");
    /* 回声晚到 / 改了长度：判定不看时刻、不看长度 */
    g_now += 60000u;
    n = wire_frame(MT_TCS_CMD, 0x22, (const uint8_t *)"\x08", 1, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && dup == 0, "D: 60 秒后同一回声再到仍不交付");
    n = wire_frame(MT_TCS_SET_PARAM, 0x21, body, TCS_PARAM_PL_LEN + 1, f);   /* 载荷多 1 字节 */
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && dup == 0, "D: 长度被改的回声不交付");

    /* 回声不得把 link_ok 置真 */
    L.link_ok = COMM_LINK_OK_FALSE;
    n = ack_frame(MT_TCS_ACK, 0x31, 0x21, MT_TCS_SET_PARAM, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0, "D: 链路已断时 ACK 回声不交付");
    CHECK(L.link_ok == COMM_LINK_OK_FALSE, "D: ACK 回声没有把 link_ok 置真");
    CHECK(g_wire_n == 0, "D: 回声不引发任何发送");
    CHECK(comm_pending_count(&L) == 2, "D: 等待确认的两帧始终没被动过");
    CHECK(L.echo_cnt == 8, "D: 八帧回声全部计入 echo");
    CHECK(L.echo_by_mt[MT_TCS_ACK] == 2 && L.echo_by_mt[MT_TCS_CMD] == 2 &&
          L.echo_by_mt[MT_TCS_SET_PARAM] == 2 && L.echo_by_mt[MT_TCS_STATUS] == 1 &&
          L.echo_by_mt[MT_TCS_REGISTRY_ENTRY] == 1, "D: echo 按消息号分计");
    CHECK(L.wrongdir_cnt == 0, "D: 回声不进 wrongdir");
    {
        uint8_t ds, dm;
        CHECK(comm_last_dup_info(&L, &ds, &dm) == 0, "D: 回声不进\"重复帧\"记录（不会为它回 ACK）");
    }
}

/* ======================= (e) 对端方向来的每一种帧照常处理 ======================= */
static void test_peer(void)
{
    comm_link_t L;
    uint8_t f[MAX_FRAME], body[16]; size_t n; int dup = 0; uint8_t mt0 = 0;
    g_wire_n = 0;
    link_open(&L, TCS_SIDE_H743);
    param_body(body, 0x01, 30.0f);
    n = wire_frame(MT_TCS_SET_PARAM, 0x51, body, TCS_PARAM_PL_LEN, f);
    CHECK(xsend(&L, f, n, MT_TCS_SET_PARAM) == 0x51, "E: 0x12 已登记");
    L.link_ok = COMM_LINK_OK_FALSE;

    /* 镜像 0x11：交付，且交给上层的是去掉方向位的消息号 */
    n = wire_frame((uint8_t)(MT_TCS_PARAM | 0x40), 0x60, body, TCS_PARAM_PL_LEN, f);
    CHECK(feed(&L, f, n, &dup, &mt0) == 1 && mt0 == MT_TCS_PARAM && !dup, "E: 0x11 镜像交付（上层看到 0x11）");
    /* 快照 0x20 / 增量 0x21 / 设备信息 0x22 / 请求注册表 0x13 / 主控命令 0x15 */
    uint8_t spl[TCS_SNAP_PL_LEN]; tcs_snap_pl_t s; memset(&s, 0, sizeof(s)); s.ver = TCS_PROTO_VER;
    (void)tcs_pl_snapshot(spl, sizeof(spl), &s);
    n = wire_frame((uint8_t)(MT_TCS_SNAPSHOT | 0x40), 0x61, spl, sizeof(spl), f);
    CHECK(feed(&L, f, n, &dup, &mt0) == 1 && mt0 == MT_TCS_SNAPSHOT, "E: 0x20 快照交付");
    tcs_delta_src_t d; memset(&d, 0, sizeof(d)); d.fan = 1;
    uint8_t dp[8]; size_t dn = tcs_pl_delta(dp, sizeof(dp), TCS_D_FAN, &d);
    n = wire_frame((uint8_t)(MT_TCS_DELTA | 0x40), 0x62, dp, dn, f);
    CHECK(feed(&L, f, n, &dup, &mt0) == 1 && mt0 == MT_TCS_DELTA, "E: 0x21 增量交付");
    n = wire_frame((uint8_t)(MT_TCS_INFO | 0x40), 0x63, (const uint8_t *)"ssid=x", 6, f);
    CHECK(feed(&L, f, n, &dup, &mt0) == 1 && mt0 == MT_TCS_INFO, "E: 0x22 设备信息交付");
    n = wire_frame((uint8_t)(MT_TCS_GET_REGISTRY | 0x40), 0x64, NULL, 0, f);
    CHECK(feed(&L, f, n, &dup, &mt0) == 1 && mt0 == MT_TCS_GET_REGISTRY, "E: 0x13 请求注册表交付");
    n = wire_frame((uint8_t)(MT_TCS_CMD | 0x40), 0x65, (const uint8_t *)"\x01", 1, f);
    CHECK(feed(&L, f, n, &dup, &mt0) == 1 && mt0 == MT_TCS_CMD, "E: 0x15 主控命令交付");
    /* 未登记的消息号：照常交付（由分发的 default 丢弃），前向兼容 */
    n = wire_frame(0x70, 0x66, (const uint8_t *)"zz", 2, f);
    CHECK(feed(&L, f, n, &dup, &mt0) == 1 && mt0 == 0x30, "E: 未登记消息号也交付（前向兼容）");
    /* 主控方向的 ACK：确认 (0x51, 0x12)，必须照常清掉等待确认状态并把 link_ok 置真 */
    n = ack_frame((uint8_t)(MT_TCS_ACK | 0x40), 0x67, 0x51, MT_TCS_SET_PARAM, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && !dup, "E: 对端 ACK 由 comm 层消费");
    CHECK(comm_pending_count(&L) == 0, "E: 对端 ACK 清掉等待确认状态");
    CHECK(L.link_ok == COMM_LINK_OK_TRUE, "E: 对端 ACK 把 link_ok 置真");
    /* 对端帧再来一次 = 重复帧（去重照常工作，上层会为它回 ACK）*/
    n = wire_frame((uint8_t)(MT_TCS_GET_REGISTRY | 0x40), 0x64, NULL, 0, f);
    CHECK(feed(&L, f, n, &dup, &mt0) == 0 && dup, "E: 对端重复帧照常判重复");
    {
        uint8_t ds, dm;
        CHECK(comm_last_dup_info(&L, &ds, &dm) == 1 && ds == 0x64 && dm == MT_TCS_GET_REGISTRY,
              "E: 重复帧记录里是去掉方向位的消息号");
    }
    CHECK(L.echo_cnt == 0 && L.wrongdir_cnt == 0, "E: 对端帧不进 echo / wrongdir");
}

/* ======================= 方向表：对端不会发的消息号 ======================= */
static void test_wrongdir(void)
{
    comm_link_t L;
    uint8_t f[MAX_FRAME], body[TCS_PARAM_PL_LEN], spl[TCS_SNAP_PL_LEN]; size_t n; int dup = 0;
    g_wire_n = 0;
    link_open(&L, TCS_SIDE_H743);
    param_body(body, 0x01, 5.0f);
    /* 屏自己挂一笔 (0x71, 0x12) 等确认 */
    n = wire_frame(MT_TCS_SET_PARAM, 0x71, body, TCS_PARAM_PL_LEN, f);
    CHECK(xsend(&L, f, n, MT_TCS_SET_PARAM) == 0x71, "W: 0x12 已登记");
    tx_drain(&L);
    g_wire_n = 0;

    /* 主控方向的 0x12：主控从不写参数，这不是"主控让我改参数" */
    n = wire_frame((uint8_t)(MT_TCS_SET_PARAM | 0x40), 0x80, body, TCS_PARAM_PL_LEN, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && !dup, "W: 主控方向的 0x12 不交付");
    /* 主控方向的 0x10 / 0x14 */
    tcs_status_pl_t st; memset(&st, 0, sizeof(st)); st.ver = TCS_PROTO_VER;
    (void)tcs_pl_status(spl, sizeof(spl), &st);
    n = wire_frame((uint8_t)(MT_TCS_STATUS | 0x40), 0x81, spl, TCS_STATUS_PL_LEN, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && !dup, "W: 主控方向的 0x10 不交付");
    tcs_reg_entry_t e; memset(&e, 0, sizeof(e)); e.count = 2; e.idx = 0; e.id = 0x01;
    strcpy(e.key, "k"); strcpy(e.name, "n"); strcpy(e.unit, "u");
    uint8_t rp[64]; n = tcs_pl_registry_entry(rp, sizeof(rp), &e);
    n = wire_frame((uint8_t)(MT_TCS_REGISTRY_ENTRY | 0x40), 0x82, rp, n, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && !dup, "W: 主控方向的 0x14 不交付");
    /* "主控"发来的 ACK 确认了一个本端不会发的消息号 0x13——seq 故意取成本端在等的那一帧 */
    n = ack_frame((uint8_t)(MT_TCS_ACK | 0x40), 0x83, 0x71, MT_TCS_GET_REGISTRY, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && !dup, "W: ACK 确认了本端不会发的 0x13：不处理");
    CHECK(comm_pending_count(&L) == 1, "W: 等待确认的 0x12 没有被它清掉");
    CHECK(g_wire_n == 0, "W: 没有引发任何发送");

    /* 自己发出的 ACK 回声，方向位被改坏（读成主控方向）且 CRC 正确：它确认的是主控发来的
     * 0x13——本端不发 0x13，过不了方向表 */
    n = ack_frame((uint8_t)(MT_TCS_ACK | 0x40), 0x71, 0x71, MT_TCS_GET_REGISTRY, f);
    CHECK(feed(&L, f, n, &dup, NULL) == 0 && !dup, "W: 方向位被改坏的 ACK 回声不处理");
    CHECK(comm_pending_count(&L) == 1, "W: 等待确认的 0x12 仍在");
    CHECK(L.wrongdir_cnt == 5, "W: 五帧全部按\"方向表不允许\"计数");
    CHECK(L.echo_cnt == 0, "W: 它们不是回声，不进 echo");
}

/* ======================= M04-F-06：判活单一口径 =======================
 * link_ok 置假之后，对端发来的任何一帧合法帧（不只 ACK）都把它置回真，并报一次恢复。 */
static int g_up_cnt;
static void on_up(void) { g_up_cnt++; }
static void test_link_ok(void)
{
    comm_link_t L;
    uint8_t f[MAX_FRAME], spl[TCS_SNAP_PL_LEN]; size_t n; int dup = 0;
    g_wire_n = 0;
    link_open(&L, TCS_SIDE_H743);
    comm_set_on_link_up(&L, on_up);
    g_up_cnt = 0;
    tcs_snap_pl_t s; memset(&s, 0, sizeof(s)); s.ver = TCS_PROTO_VER;
    (void)tcs_pl_snapshot(spl, sizeof(spl), &s);
    L.link_ok = COMM_LINK_OK_FALSE;                 /* 一帧等 ACK 的命令重传耗尽之后 */

    /* 对端的快照（免 ARQ，永远不会有对应的 ACK）到达：链路明明是通的 */
    n = wire_frame((uint8_t)(MT_TCS_SNAPSHOT | 0x40), 0x90, spl, sizeof(spl), f);
    CHECK(feed(&L, f, n, &dup, NULL) == 1, "K: 快照交付");
    CHECK(L.link_ok == COMM_LINK_OK_TRUE, "K: 对端的非 ACK 帧把 link_ok 置回真（旧实现只能被 ACK 复位）");
    CHECK(g_up_cnt == 1, "K: 由假转真报了一次恢复");
    s.seq = 7;
    (void)tcs_pl_snapshot(spl, sizeof(spl), &s);
    n = wire_frame((uint8_t)(MT_TCS_SNAPSHOT | 0x40), 0x91, spl, sizeof(spl), f);
    feed(&L, f, n, &dup, NULL);
    CHECK(g_up_cnt == 1, "K: 链路本来就通时不重复报恢复");
    /* 重复帧也算对端活着 */
    L.link_ok = COMM_LINK_OK_FALSE;
    feed(&L, f, n, &dup, NULL);
    CHECK(L.link_ok == COMM_LINK_OK_TRUE && g_up_cnt == 2, "K: 重复帧同样把 link_ok 置回真");
    /* ACK 照旧 */
    L.link_ok = COMM_LINK_OK_FALSE;
    n = wire_frame(MT_TCS_SET_PARAM, 0x92, (const uint8_t *)"\x01\x00\x00\x00\x00\x00\x00", 7, f);
    CHECK(xsend(&L, f, n, MT_TCS_SET_PARAM) == 0x92, "K: 前置——登记一帧");
    n = ack_frame((uint8_t)(MT_TCS_ACK | 0x40), 0x93, 0x92, MT_TCS_SET_PARAM, f);
    feed(&L, f, n, &dup, NULL);
    CHECK(L.link_ok == COMM_LINK_OK_TRUE && g_up_cnt == 3 && comm_pending_count(&L) == 0, "K: ACK 把 link_ok 置回真");
}

int main(void)
{
    test_pure();
    test_tx();
    test_tx_fail();
    test_echo();
    test_peer();
    test_wrongdir();
    test_link_ok();
    printf("test_link_dir(H743): PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
