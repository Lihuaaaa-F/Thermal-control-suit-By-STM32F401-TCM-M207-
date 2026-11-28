/* test_comm_burst.c — 2026-11-28：TX 队列批量排空（消费速率必须追得上生产速率）。
 *
 * 判别性：把 comm_poll_tx_burst 的循环改回"只发一帧"（或 max 传 1），本用例必须变红。
 * 背景：链路任务每 100ms 调一次，原来只出一帧 = 10 帧/s；稳态生产 ~11.3 帧/s
 * （0x20 5/s + 0x21 ≤5/s + 0x11 1.25/s + 0x22 0.3/s）。队列贴满后 ACK 排到队尾，
 * 超 500ms 由屏侧 ARQ 重传、调参被判"被拒"。
 *
 * 用法：由 esp32/tests/run_m207_tests.sh 的 commburst 一行编译运行。
 */
#include <stdio.h>
#include <string.h>
#include "comm.h"

static int g_sent;

static int send_fn(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    g_sent++;
    return 0;
}

static uint32_t now_ms_fn(void) { return 0; }
static void noop(void) { }

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

int main(void)
{
    comm_link_t L;
    uint8_t frame[8] = { 0xAA, 0x55, 0x03, 0x20, 0x00, 0x00, 0x00, 0x00 };

    comm_init(&L, send_fn, now_ms_fn, noop, noop, noop, TCS_SIDE_ESP32);

    /* 1) 一次 burst 最多发 max 帧 */
    for (int i = 0; i < 6; i++) comm_txq_push(&L, frame, sizeof frame);
    CHECK(comm_txq_count(&L) == 6, "6 frames queued");
    comm_poll_tx_burst(&L, 4u);
    CHECK(g_sent == 4, "burst(4) sends exactly 4");
    CHECK(comm_txq_count(&L) == 2, "2 frames left after one burst");

    /* 2) 下一次 burst 把剩余排空，且不过发 */
    comm_poll_tx_burst(&L, 4u);
    CHECK(g_sent == 6, "second burst drains the queue");
    CHECK(comm_txq_count(&L) == 0, "queue empty");

    /* 3) 空队列上再调：不发、不崩 */
    comm_poll_tx_burst(&L, 4u);
    CHECK(g_sent == 6, "empty queue sends nothing");

    /* 4) max=1 退化为"每拍一帧"（对照：修前行为） */
    comm_txq_push(&L, frame, sizeof frame);
    comm_txq_push(&L, frame, sizeof frame);
    comm_poll_tx_burst(&L, 1u);
    CHECK(g_sent == 7 && comm_txq_count(&L) == 1, "max=1 sends one frame (old behavior)");

    printf("test_comm_burst: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
