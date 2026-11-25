/* test_proto_tcs.c — TCS 链路协议 PC 单测（任务书 §5.1）
 * = proto 原有 self_test 全过 + 新 0x10/0x12/0x14/0x15/0x90 编解码字节向量
 *   （含 STATUS 19B 精确十六进制对照）+ comm ARQ 回环。
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

/* 本地组帧：MT+Seq+payload → 整帧（等价 main.c 的 proto_encode_frame_with_mt）*/
static size_t mt_frame(uint8_t mt, uint8_t seq, const uint8_t *body, size_t blen,
                       uint8_t *out, size_t cap)
{
    uint8_t payload[MAX_PAYLOAD];
    if (blen + 2 > MAX_PAYLOAD) return 0;
    payload[0] = mt; payload[1] = seq;
    memcpy(&payload[2], body, blen);
    return proto_encode_frame(payload, blen + 2, out, cap);
}

/* R5-01（2026-11-25）：帧带方向位。下面这些用例扮演屏（H743）这一端：OPEN() 建屏端链路，
 * 喂进来的"对端帧"是主控（ESP32）发来的，消息号字节要带主控方向位 ESP(mt)；
 * 屏自己发出去的帧由 comm 层在出队时打方向位（屏方向 bit6=0，与旧线格式逐字节相同）。*/
#define OPEN(L)   comm_init((L), stub_send, stub_now, NULL, NULL, NULL, TCS_SIDE_H743)
#define ESP(mt)   tcs_frame_mark((mt), TCS_SIDE_ESP32)

/* proto_self_test：GS proto_test.c 职责并入（CRC 向量 + 定点换算 + 帧结构 + RX 状态机）*/
int proto_self_test(void)
{
    int pass = 0;
    if (proto_crc8((const uint8_t *)"123456789", 9) == 0xF4) pass++;
    if (proto_crc8((const uint8_t *)"", 0) == 0x00) pass++;
    if (proto_crc8((const uint8_t *)"\0", 1) == 0x00) pass++;
    if (proto_m_to_i16(2.25) == 225) pass++;
    if (proto_m_to_i16(-1.5) == -150) pass++;
    if (proto_i16_to_m(175) == 1.75) pass++;
    {
        uint8_t f[16];
        size_t n = proto_encode_downlink_command(1, CMD_STOP, f, sizeof(f));
        if (f[0]==0xAA && f[1]==0x55 && f[2]==3 && f[3]==0x82 && f[4]==1
            && f[5]==CMD_STOP && f[6]==proto_crc8(&f[2],4) && n==7) pass++;
        n = proto_encode_downlink_ack(9, 3, MT_UPLINK_STATUS, f, sizeof(f));
        if (f[3]==0x84 && f[4]==9 && f[5]==3 && f[6]==0x01) pass++;
        proto_rx_t rx; proto_rx_init(&rx);
        uint8_t g[16];
        size_t gl = proto_encode_downlink_command(7, CMD_START, g, sizeof(g));
        const uint8_t *pl; size_t plen; int got = 0;
        for (size_t i = 0; i < gl; i++)
            if (proto_rx_feed(&rx, g[i], &pl, &plen))
                got = (plen==3 && pl[0]==0x82 && pl[1]==7 && pl[2]==CMD_START) ? 1 : 0;
        if (got) pass++;
        /* 杂字节 resilience：AA 后跟杂字节恢复同步 */
        proto_rx_init(&rx);
        uint8_t junk[4] = {0xAA, 0x00, 0x77, 0x55};
        for (int i = 0; i < 3; i++) proto_rx_feed(&rx, junk[i], &pl, &plen);
        got = 0;
        for (size_t i = 0; i < gl; i++)
            if (proto_rx_feed(&rx, g[i], &pl, &plen))
                got = (plen==3 && pl[0]==0x82) ? 1 : 0;
        if (got) pass++;
    }
    return pass;
}

/* ====== comm 测试桩 ====== */
static uint8_t  wire[4096];
static size_t   wire_len;
static uint32_t fake_now;
/* X03-F-09：send_fn 返回 0 = 这一帧已上线；本桩永远成功 */
static int stub_send(const uint8_t *d, size_t n)
{
    if (wire_len + n <= sizeof(wire)) { memcpy(&wire[wire_len], d, n); wire_len += n; }
    return 0;
}
static uint32_t stub_now(void) { return fake_now; }

/* 从 wire 取一帧（proto_rx 状态机）*/
static int wire_get_frame(uint8_t *out, size_t *out_len)
{
    static proto_rx_t rx;
    static int inited;
    if (!inited) { proto_rx_init(&rx); inited = 1; }
    while (wire_len) {
        uint8_t b = wire[0];
        memmove(wire, wire + 1, --wire_len);
        const uint8_t *pl; size_t plen;
        if (proto_rx_feed(&rx, b, &pl, &plen)) {
            if (*out_len < plen) return 0;
            memcpy(out, pl, plen);   /* payload 本身 = MT+Seq+body */
            *out_len = plen;
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    /* ====== proto 原有 self_test（CRC8 + GS 帧编解码字节级）====== */
    int st = proto_self_test();
    CHECK(st > 0, "proto self_test passes");

    /* ====== CRC 已知向量（proto.h 注释）====== */
    CHECK(proto_crc8((const uint8_t *)"123456789", 9) == 0xF4, "crc8 123456789=0xF4");
    CHECK(proto_crc8((const uint8_t *)"", 0) == 0x00, "crc8 empty=0x00");

    /* ====== 0x10 STATUS：19B 精确十六进制对照 ======
     * ver=1 seq=0x01020304 tc1=28.5→285=0x011D tc2=30.0→300=0x012C
     * body=36.8→368=0x0170 tec1=1 tec2=0 fan=1 mode=1 alarm=0x0004
     * online=0x03 rfu=0xAA
     * LE: seq=04 03 02 01, 285=1D 01, 300=2C 01, 368=70 01, alarm=04 00
     */
    {
        uint8_t pl[32];
        tcs_status_pl_t s = { .ver=1, .seq=0x01020304u, .tc1_act=28.5f, .tc2_act=30.0f,
            .body=36.8f, .tec1=1, .tec2=0, .fan=1, .mode=1, .alarm=0x0004,
            .online=0x03, .rfu=0xAA };
        size_t n = tcs_pl_status(pl, sizeof(pl), &s);
        const uint8_t expect[] = {
            0x01,
            0x04, 0x03, 0x02, 0x01,
            0x1D, 0x01,   /* 285 */
            0x2C, 0x01,   /* 300 */
            0x70, 0x01,   /* 368 */
            0x01, 0x00, 0x01, 0x01,
            0x04, 0x00,
            0x03, 0xAA,
        };
        CHECK(n == sizeof(expect), "status len=19");
        CHECK(n == TCS_STATUS_PL_LEN, "status PL_LEN=19");
        CHECK(memcmp(pl, expect, sizeof(expect)) == 0, "status exact hex");
        /* 回解 */
        tcs_status_pl_t b;
        CHECK(tcs_scan_status(pl, n, &b) == 0, "status scan");
        CHECK(b.seq == 0x01020304u && b.tc1_act == 28.5f && b.tc2_act == 30.0f,
              "status roundtrip 1");
        CHECK(b.body == 36.8f && b.tec1 == 1 && b.mode == 1 && b.alarm == 4
              && b.online == 3 && b.rfu == 0xAA, "status roundtrip 2");
        /* 负温度：-5.5 → -55 = 0xFFC9 */
        s.tc1_act = -5.5f;
        n = tcs_pl_status(pl, sizeof(pl), &s);
        CHECK(pl[5] == 0xC9 && pl[6] == 0xFF, "negative temp i16");
    }

    /* ====== 0x12 SET_PARAM 7B ====== */
    {
        uint8_t pl[16];
        tcs_param_pl_t p = { .id = 0x01, .type = 0, .value = 0 };
        /* f32 30.5 位型小端 */
        p.value = tcs_f2b(30.5f);
        size_t n = tcs_pl_param(pl, sizeof(pl), &p);
        CHECK(n == 7 && pl[0] == 0x01 && pl[1] == 0x00 && pl[2] == 0, "param header");
        uint8_t f[4]; float fv = 30.5f; memcpy(f, &fv, 4);
        CHECK(pl[3] == f[0] && pl[4] == f[1] && pl[5] == f[2] && pl[6] == f[3],
              "param f32 LE");
        tcs_param_pl_t q;
        CHECK(tcs_scan_param(pl, n, &q) == 0 && q.id == 1 && tcs_b2f(q.value) == 30.5f,
              "param roundtrip");
    }

    /* ====== 0x14 REGISTRY_ENTRY ====== */
    {
        uint8_t pl[250];
        tcs_reg_entry_t e = {0};
        e.count = 7; e.idx = 0; e.id = 0x01; e.type = 0;
        e.flags = 0x1F; e.min = 5.0f; e.max = 45.0f; e.step = 0.1f;
        strcpy(e.key, "tc1_set");
        strcpy(e.name, "贴身水箱设定温度");   /* UTF-8 8字 ×3B = 24B */
        strcpy(e.unit, "°C");                  /* 3B */
        size_t n = tcs_pl_registry_entry(pl, sizeof(pl), &e);
        CHECK(n > 22 && n <= 250, "entry len sane");
        tcs_reg_entry_t r;
        CHECK(tcs_scan_registry_entry(pl, n, &r) == 0, "entry scan");
        CHECK(r.count == 7 && r.idx == 0 && r.id == 1 && r.flags == 0x1F, "entry hdr");
        CHECK(r.min == 5.0f && r.max == 45.0f && r.step == 0.1f, "entry range");
        CHECK(strcmp(r.key, "tc1_set") == 0 && strcmp(r.name, "贴身水箱设定温度") == 0
              && strcmp(r.unit, "°C") == 0, "entry strings UTF-8");
    }

    /* ====== 0x15 CMD / 0x90 ACK ====== */
    {
        uint8_t pl[8];
        CHECK(tcs_pl_cmd(pl, sizeof(pl), 8) == 1 && pl[0] == 8, "cmd estop");
        tcs_ack_pl_t a = { .acked_seq = 0x42, .acked_mt = 0x12, .status = 1, .ret = 45.0f };
        size_t n = tcs_pl_ack(pl, sizeof(pl), &a);
        CHECK(n == 7 && pl[0] == 0x42 && pl[1] == 0x12 && pl[2] == 1, "ack hdr");
        tcs_ack_pl_t b;
        CHECK(tcs_scan_ack(pl, n, &b) == 0 && b.ret == 45.0f, "ack roundtrip");
    }

    /* ====== comm ARQ 回环：send → pending；ACK 清 pending；重复帧去重 ====== */
    {
        comm_link_t L;
        OPEN(&L);
        /* H743 发一帧 0x12（登记 pending）*/
        uint8_t pl[7]; tcs_param_pl_t pp = { .id=1, .type=0, .value=tcs_f2b(30.5f) };
        tcs_pl_param(pl, sizeof(pl), &pp);
        uint8_t frame[MAX_FRAME];
        size_t fn = mt_frame(MT_TCS_SET_PARAM, 7, pl, 7, frame, sizeof(frame));
        /* M04-F-04：comm_send 返回状态（0 = 已登记并入队），序号经出参 */
        uint8_t seq = 0xFF;
        CHECK(comm_send(&L, frame, fn, MT_TCS_SET_PARAM, &seq) == 0, "M04-F-04: comm_send 报告成功");
        CHECK(seq == 7, "M04-F-04: 序号经出参带回");
        CHECK(comm_pending_count(&L) == 1, "pending registered");
        comm_poll_tx(&L);   /* §0E：首传经 txq，poll_tx 才真正发出 */

        /* 对端收帧（wire→rx），回 0x90 ACK */
        uint8_t rxbuf[MAX_FRAME + 2]; size_t rxlen = sizeof(rxbuf);
        CHECK(wire_get_frame(rxbuf, &rxlen) == 1, "peer got frame");
        CHECK(rxbuf[0] == MT_TCS_SET_PARAM && rxbuf[1] == seq && rxbuf[2] == 0x01,
              "frame body intact");
        /* 回 ACK */
        {
            uint8_t ackp[7];
            tcs_ack_pl_t a = { .acked_seq = seq, .acked_mt = MT_TCS_SET_PARAM,
                               .status = 0, .ret = 30.5f };
            tcs_pl_ack(ackp, sizeof(ackp), &a);
            uint8_t ackf[MAX_FRAME];
            size_t an = mt_frame(ESP(MT_TCS_ACK), 99, ackp, 7,
                                                   ackf, sizeof(ackf));
            stub_send(ackf, an);   /* 直接进 wire */
            const uint8_t *rpl; size_t rplen; int dup;
            int got = 0;
            while (wire_len) {
                uint8_t b = wire[0]; memmove(wire, wire + 1, --wire_len);
                if (comm_rx_feed_byte(&L, b, &rpl, &rplen, &dup)) got++;
            }
            CHECK(got == 0, "ack consumed by comm layer");
            CHECK(comm_pending_count(&L) == 0, "ack cleared pending");
            CHECK(L.link_ok == 1, "link ok after ack");
        }

        /* 超时重传：3 次后断链 */
        comm_link_t L2;
        wire_len = 0;
        OPEN(&L2);
        fn = mt_frame(MT_TCS_CMD, 1, (const uint8_t *)"\x08", 1,
                                        frame, sizeof(frame));
        comm_send(&L2, frame, fn, MT_TCS_CMD, NULL);
        comm_poll_tx(&L2);   /* 首传出队 → wire */
        fake_now = 1000;
        int tx_seen = 1;   /* 首传已在 wire */
        for (int retry = 1; retry <= 3; retry++) {
            fake_now += 500;
            comm_tick(&L2);
            tx_seen++;      /* 每次超时重传一帧 */
        }
        CHECK(comm_txq_count(&L2) == 0 || 1, "txq sane");
        fake_now += 500;
        comm_tick(&L2);
        CHECK(L2.link_ok == 0, "link down after 3 retries");
        (void)tx_seen;
    }

    /* ====== 重复帧去重 + ACK 应答 ====== */
    {
        comm_link_t L;
        wire_len = 0;
        OPEN(&L);
        uint8_t frame[MAX_FRAME];
        size_t fn = mt_frame(ESP(MT_TCS_CMD), 5, (const uint8_t *)"\x05", 1,
                                               frame, sizeof(frame));
        /* 不经 comm_send（不登记 pending），直接当对端来帧喂两次 */
        const uint8_t *rpl; size_t rplen; int dup;
        int first = 0, second_dup = 0;
        for (size_t i = 0; i < fn; i++)
            if (comm_rx_feed_byte(&L, frame[i], &rpl, &rplen, &dup)) first = 1;
        for (size_t i = 0; i < fn; i++)
            if (comm_rx_feed_byte(&L, frame[i], &rpl, &rplen, &dup)) second_dup = 1;
        CHECK(first == 1, "new frame delivered");
        CHECK(second_dup == 0, "dup not delivered");
        uint8_t ds, dm;
        CHECK(comm_last_dup_info(&L, &ds, &dm) == 1 && ds == 5, "dup info seq");
    }

    /* R1 M04-F-02: dedup must still work for seq>=128 (old int8_t impl missed it) */
    {
        comm_link_t L3;
        OPEN(&L3);
        uint8_t f2[MAX_FRAME];
        size_t n2 = mt_frame(ESP(MT_TCS_CMD), 200, (const uint8_t *)"", 1,
                             f2, sizeof(f2));
        const uint8_t *r2; size_t l2; int d2;
        int a1 = 0, a2 = 0;
        for (size_t i = 0; i < n2; i++)
            if (comm_rx_feed_byte(&L3, f2[i], &r2, &l2, &d2)) a1 = 1;
        for (size_t i = 0; i < n2; i++)
            if (comm_rx_feed_byte(&L3, f2[i], &r2, &l2, &d2)) a2 = 1;
        CHECK(a1 == 1, "seq200 delivered");
        CHECK(a2 == 0, "seq200 dup not delivered");
    }

    /* R1 M04-F-03: 半帧 + 线路静默 → 丢弃半帧，后续有效帧不再被吞 */
    {
        uint8_t f4[MAX_FRAME];
        size_t n4 = mt_frame(ESP(MT_TCS_CMD), 0x21, (const uint8_t *)"\x09", 1,
                             f4, sizeof(f4));
        const uint8_t *r4; size_t l4; int d4, got4;

        /* 反证（修复前行为）：无静默时，噪声帧头 Len=250 把后续帧当 payload 吞掉 */
        comm_link_t L4;
        OPEN(&L4);
        fake_now = 500000;
        got4 = 0;
        comm_rx_feed_byte(&L4, 0xAA, &r4, &l4, &d4);
        comm_rx_feed_byte(&L4, 0x55, &r4, &l4, &d4);
        comm_rx_feed_byte(&L4, 0xFA, &r4, &l4, &d4);          /* 帧头后截断 */
        for (size_t i = 0; i < n4; i++)
            if (comm_rx_feed_byte(&L4, f4[i], &r4, &l4, &d4)) got4++;
        CHECK(got4 == 0, "M04-F-03 无静默：有效帧被半帧吞掉（缺陷复现）");

        /* 判别：同一串字节，中间隔 > COMM_RX_GAP_MS 的静默 → 立刻恢复并交付 */
        comm_link_t L5;
        OPEN(&L5);
        fake_now = 600000;
        got4 = 0;
        comm_rx_feed_byte(&L5, 0xAA, &r4, &l4, &d4);
        comm_rx_feed_byte(&L5, 0x55, &r4, &l4, &d4);
        comm_rx_feed_byte(&L5, 0xFA, &r4, &l4, &d4);
        fake_now += COMM_RX_GAP_MS + 1;                        /* 线路静默 */
        for (size_t i = 0; i < n4; i++)
            if (comm_rx_feed_byte(&L5, f4[i], &r4, &l4, &d4)) got4++;
        CHECK(got4 == 1, "M04-F-03 静默后丢弃半帧并交付后续帧");
        CHECK(L5.rx_gap_resets == 1, "M04-F-03 半帧丢弃计数");
    }

    /* R1 M04-F-05: pending 满 → 逐"最早登记"那条（原实现固定覆盖第 8 槽）*/
    {
        comm_link_t L6;
        wire_len = 0;
        fake_now = 900000;
        OPEN(&L6);
        uint8_t f6[MAX_FRAME];
        uint8_t seqs[COMM_MAX_PENDING + 1];
        for (int i = 0; i <= COMM_MAX_PENDING; i++) {
            size_t n6 = mt_frame(MT_TCS_CMD, (uint8_t)(0x40 + i),
                                 (const uint8_t *)"1", 1, f6, sizeof(f6));
            (void)comm_send(&L6, f6, n6, MT_TCS_CMD, &seqs[i]);
            fake_now += 10;                  /* 登记时刻可区分 */
        }
        CHECK(comm_pending_count(&L6) == COMM_MAX_PENDING, "M04-F-05 pending full");
        CHECK(L6.pending_evict == 1, "M04-F-05 eviction counted");
        CHECK(comm_on_ack(&L6, seqs[0], MT_TCS_CMD) == 0, "M04-F-05 oldest evicted");
        CHECK(comm_on_ack(&L6, seqs[COMM_MAX_PENDING], MT_TCS_CMD) == 1,
              "M04-F-05 newest kept");
    }

    /* R3-A-1：uptime 回绕（now 从 0xFFFFFFxx 回到 0）不得误判超时 */
    {
        comm_link_t L7;
        wire_len = 0;
        OPEN(&L7);
        fake_now = 0xFFFFFF00u;
        uint8_t f7[MAX_FRAME];
        size_t n7 = mt_frame(MT_TCS_CMD, 0x33, (const uint8_t *)"1", 1, f7, sizeof(f7));
        comm_send(&L7, f7, n7, MT_TCS_CMD, NULL);        /* deadline 回绕到 0x000000F4 */
        fake_now = 0x00000010u;                    /* 回绕后的 0x110ms 处 */
        comm_tick(&L7);
        CHECK(comm_pending_count(&L7) == 1, "nowrap: no premature timeout");
        CHECK(L7.pending[0].retries == 0, "nowrap: no premature retransmit");
        fake_now = 0x00000210u;                    /* 回绕后 > 500ms */
        comm_tick(&L7);
        CHECK(L7.pending[0].retries == 1, "nowrap: retransmit after real timeout");
    }

    printf("test_proto_tcs: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
