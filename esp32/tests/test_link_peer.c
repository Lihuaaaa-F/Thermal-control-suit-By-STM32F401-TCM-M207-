/* test_link_peer.c — m1（模式页，2026-11-26）："屏链路断开 3 秒 = 急停"的计时起点
 * （comm_peer_last_ms / comm_link_t.peer_ms）的 ESP32 侧主机用例。
 *
 * 被测对象 = components/tcs_link/comm.c **本身**（原样编译）。要点：
 *   - 只有**对端方向**的合法帧才刷新时刻：本端回送帧（方向位 = ESP32）、方向表不允许的帧、
 *     坏 CRC、杂字节都不算（它们在方向闸门 / 帧层就被丢了，到不了 comm_note_peer）；
 *   - 对端的 ACK、重复帧、新帧都算（与 M04-F-06 的判活口径一致）；
 *   - 上电后从未收到 → 返回 0（"只对连上过之后断开生效"靠它）。
 * 判别性：加这两条字段之前 comm_peer_last_ms 根本不存在（编译失败）。
 * 用法：由 esp32/tests/run_m207_tests.sh 的 linkpeer 一行编译运行（MSVC cl /W4 /std:c11）。
 */
#include <stdio.h>
#include <string.h>
#include "comm.h"
#include "proto.h"
#include "tcs_proto.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); fflush(stdout); } \
} while (0)

static uint32_t g_now_ms;
static uint32_t now_fn(void) { return g_now_ms; }
static int send_fn(const uint8_t *d, size_t n) { (void)d; (void)n; return 0; }

static comm_link_t L;

/* 组一帧：mt_wire = 消息号 | 方向位（0x40 = ESP32 发的；不带 = H743 发的）*/
static size_t frame(uint8_t mt_wire, uint8_t seq, const uint8_t *body, size_t blen,
                    uint8_t *out, size_t cap)
{
    uint8_t payload[MAX_PAYLOAD];
    if (blen + 2u > sizeof(payload)) return 0;
    payload[0] = mt_wire;
    payload[1] = seq;
    if (blen) memcpy(&payload[2], body, blen);
    return proto_encode_frame(payload, blen + 2u, out, cap);
}

/* 逐字节喂进去；返回交到上层的次数 */
static int feed(const uint8_t *f, size_t n)
{
    int delivered = 0;
    for (size_t i = 0; i < n; i++) {
        const uint8_t *pl; size_t plen; int dup;
        if (comm_rx_feed_byte(&L, f[i], &pl, &plen, &dup)) delivered++;
    }
    return delivered;
}

static int seen_at(uint32_t expect_ms)
{
    uint32_t ms = 0xDEADBEEFu;
    int seen = comm_peer_last_ms(&L, &ms);
    return seen == 1 && ms == expect_ms;
}

int main(void)
{
    uint8_t f[MAX_FRAME];
    uint8_t body[8];
    size_t n;

    comm_init(&L, send_fn, now_fn, NULL, NULL, NULL, TCS_SIDE_ESP32);

    /* 上电后从未收到对端帧 */
    g_now_ms = 500u;
    CHECK(comm_peer_last_ms(&L, NULL) == 0, "P01 never seen -> 0");
    CHECK(!seen_at(0u), "P01 never seen is not reported as seen");

    /* 本端回送帧（方向位 = ESP32）：到不了 comm_note_peer */
    n = frame((uint8_t)(MT_TCS_SNAPSHOT | TCS_MT_DIR_ESP32), 7, body, 4, f, sizeof(f));
    g_now_ms = 1000u;
    CHECK(n && feed(f, n) == 0, "P02 (setup) own echo is dropped");
    CHECK(!seen_at(1000u), "P02 own echo does not refresh the peer timestamp");

    /* 坏 CRC 帧（方向位说是 H743）*/
    n = frame(MT_TCS_STATUS, 1, body, 19, f, sizeof(f));
    g_now_ms = 1100u;
    if (n) { f[n - 1] ^= 0xFFu; }
    CHECK(n && feed(f, n) == 0, "P03 (setup) bad CRC dropped");
    CHECK(!seen_at(1100u), "P03 bad CRC frame does not count");

    /* 杂字节 */
    g_now_ms = 1150u;
    {
        const uint8_t junk[9] = { 0xAA, 0x00, 0x55, 0x12, 0x34, 0xAA, 0x55, 0x00, 0x00 };
        CHECK(feed(junk, sizeof(junk)) == 0, "P04 (setup) junk dropped");
        CHECK(!seen_at(1150u), "P04 junk does not count");
    }

    /* 方向表不允许：H743 不会发 0x20 快照 */
    n = frame(MT_TCS_SNAPSHOT, 2, body, 8, f, sizeof(f));
    g_now_ms = 1180u;
    CHECK(n && feed(f, n) == 0, "P05 (setup) wrong-direction frame dropped");
    CHECK(!seen_at(1180u), "P05 wrong-direction frame does not count");

    /* 对端（H743）发来的 STATUS：第一次连上 */
    memset(body, 0, sizeof(body));
    n = frame(MT_TCS_STATUS, 3, body, 19, f, sizeof(f));
    g_now_ms = 1200u;
    CHECK(n && feed(f, n) == 1, "P06 (setup) peer STATUS delivered");
    CHECK(seen_at(1200u), "P06 first peer frame -> seen at 1200");

    /* 重复帧（同消息号同序号）：不交付，但算对端活着 */
    g_now_ms = 1300u;
    CHECK(feed(f, n) == 0, "P07 (setup) duplicate not delivered");
    CHECK(seen_at(1300u), "P07 duplicate still refreshes the peer timestamp");

    /* 对端的 ACK（确认本端发过的 0x13）：comm 层消费，算对端活着 */
    {
        uint8_t ack[TCS_ACK_PL_LEN];
        tcs_ack_pl_t a = { 0x21, MT_TCS_GET_REGISTRY, TCS_ACK_OK, 0.0f };
        size_t an = tcs_pl_ack(ack, sizeof(ack), &a);
        n = frame(MT_TCS_ACK, 9, ack, an, f, sizeof(f));
        g_now_ms = 1400u;
        CHECK(n && feed(f, n) == 0, "P08 (setup) peer ACK consumed by comm layer");
        CHECK(seen_at(1400u), "P08 peer ACK refreshes the peer timestamp");
    }

    /* 本端发出的 ACK 被回送（方向位 = ESP32）：闸门第一关就丢，到不了 comm_note_peer */
    {
        uint8_t ack[TCS_ACK_PL_LEN];
        tcs_ack_pl_t a = { 0x21, MT_TCS_SET_PARAM, TCS_ACK_OK, 0.0f };
        size_t an = tcs_pl_ack(ack, sizeof(ack), &a);
        n = frame((uint8_t)(MT_TCS_ACK | TCS_MT_DIR_ESP32), 10, ack, an, f, sizeof(f));
        g_now_ms = 1500u;
        CHECK(n && feed(f, n) == 0, "P09 (setup) own ACK echo dropped");
        CHECK(seen_at(1400u), "P09 own ACK echo does not refresh (still 1400)");
    }

    /* 方向位说是对端、但它确认的是只有对端才会发的消息号（0x12：本端不会发，也就收不到对端的
     * ACK）：这是本端发出的 ACK 被回送、方向位被改坏的情形——坏帧，不认（R5-01） */
    {
        uint8_t ack[TCS_ACK_PL_LEN];
        tcs_ack_pl_t a = { 0x21, MT_TCS_SET_PARAM, TCS_ACK_OK, 0.0f };
        size_t an = tcs_pl_ack(ack, sizeof(ack), &a);
        n = frame(MT_TCS_ACK, 11, ack, an, f, sizeof(f));
        g_now_ms = 1550u;
        CHECK(n && feed(f, n) == 0, "P09b (setup) ACK for a message the peer never receives is dropped");
        CHECK(seen_at(1400u), "P09b wrong-direction ACK does not refresh (still 1400)");
    }

    /* 之后对端不再发帧：时刻停在最后一次（断链计时从这里起算）*/
    g_now_ms = 1500u + 2900u;
    CHECK(seen_at(1400u), "P10 timestamp frozen at last peer frame while the link is down");

    /* 没有锁函数（NULL）时整组读也成立 */
    CHECK(comm_peer_last_ms(&L, NULL) == 1, "P11 seen flag readable without out pointer");

    printf("test_link_peer: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
