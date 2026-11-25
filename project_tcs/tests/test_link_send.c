/* test_link_send.c — R5-10：屏→ESP32 的参数写入统一走等 ACK 的 0x12（被测：App/comm/link_send.c）。
 *
 * 两个 comm 实例连成一条线：S 扮演屏（本端身份 H743），C 扮演主控（本端身份 ESP32）。
 * C 侧只放一个最小的"权威注册表 + 回 ACK"（等价于 esp32/main/main.c 的 esp_on_setparam；
 * 主控那一份 comm.c 与这里的 H743 那份只差锁，方向闸门是同一个纯函数）。
 *
 * 关单用例（impl_brief_l1.md）：模拟第一帧丢失，屏侧重传，主控最终落的是正确值。
 * "修复前"的行为（0x11 变更即推、不登记等待确认；M207 参数被广播两次）由变异验证复现：
 * tests/mutation_link_dir.py 的 S1 / S2 两个变异体必须让本用例变红。
 * 用法：由 tests/run_all.sh 的 link_send 一行编译运行。
 */
#include <stdio.h>
#include <string.h>
#include "proto.h"
#include "comm.h"
#include "tcs_proto.h"
#include "param_registry.h"   /* PF_* */
#include "link_send.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static uint8_t  g_tx[2][4096];          /* [0] = 屏发出的字节  [1] = 主控发出的字节 */
static size_t   g_tx_n[2];
static uint32_t g_now;
static int      g_drop_screen;          /* 丢掉屏发出的下一帧（模拟首帧丢失）*/
static int      g_drop_left;            /* 还要丢几帧 */
static int      g_down_cnt;

static void append(int side, const uint8_t *d, size_t n)
{
    if (g_tx_n[side] + n <= sizeof(g_tx[side])) { memcpy(&g_tx[side][g_tx_n[side]], d, n); g_tx_n[side] += n; }
}
/* send_fn（X03-F-09 起返回 int）：0 = 这一帧已上线。这里的"丢帧"是丢在线上——屏这一端
 * 认为发出去了、对端没收到——所以照样返回 0，好让 comm 层走"超时重传"而不是"留在队头重试"。*/
static int send_screen(const uint8_t *d, size_t n)
{
    if (g_drop_screen || g_drop_left > 0) {
        if (g_drop_left > 0) g_drop_left--;
        g_drop_screen = 0;
        return 0;                       /* 丢在线上：对端收不到 */
    }
    append(0, d, n);
    return 0;
}
static int send_ctrl(const uint8_t *d, size_t n) { append(1, d, n); return 0; }
static uint32_t now_fn(void) { return g_now; }
static void on_down(void) { g_down_cnt++; }

/* —— 主控（ESP32）侧的最小应用：一个"权威值" + 回 ACK —— */
static struct { int writes; uint16_t id; float v; int acks; } g_ctrl;
static void ctrl_on_frame(comm_link_t *C, const uint8_t *pl, size_t len)
{
    if (pl[0] != MT_TCS_SET_PARAM || len < 2 + TCS_PARAM_PL_LEN) return;
    tcs_param_pl_t p;
    if (tcs_scan_param(&pl[2], len - 2, &p) != 0) return;
    g_ctrl.writes++; g_ctrl.id = p.id;
    g_ctrl.v = (p.type == PARAM_TYPE_F32) ? tcs_b2f(p.value) : (float)p.value;
    uint8_t a[TCS_ACK_PL_LEN];
    tcs_ack_pl_t ack = { .acked_seq = pl[1], .acked_mt = MT_TCS_SET_PARAM, .status = TCS_ACK_OK, .ret = 0.0f };
    (void)tcs_pl_ack(a, sizeof(a), &ack);
    uint8_t payload[2 + TCS_ACK_PL_LEN];
    payload[0] = MT_TCS_ACK; payload[1] = comm_next_seq(C);
    memcpy(&payload[2], a, TCS_ACK_PL_LEN);
    uint8_t f[MAX_FRAME];
    size_t n = proto_encode_frame(payload, sizeof(payload), f, sizeof(f));
    comm_txq_push(C, f, n);
    g_ctrl.acks++;
}

static comm_link_t S, C;

/* 把两边待发的各一帧送到对端 */
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
    memset(&g_ctrl, 0, sizeof(g_ctrl));
    g_drop_screen = g_drop_left = 0; g_down_cnt = 0;
    g_tx_n[0] = g_tx_n[1] = 0; g_now = 1000;
    comm_init(&S, send_screen, now_fn, on_down, NULL, NULL, TCS_SIDE_H743);
    comm_init(&C, send_ctrl, now_fn, NULL, NULL, NULL, TCS_SIDE_ESP32);
}

int main(void)
{
    /* ---- 1) 非 M207 参数（模式）：广播钩子发一帧 0x12，登记等待确认，主控落值并回 ACK ---- */
    open_links();
    CHECK(link_send_on_broadcast(&S, 0x06, PARAM_TYPE_F32, PF_RW, 1.0f) == 0, "1: 广播钩子返回成功");
    CHECK(comm_pending_count(&S) == 1 && comm_txq_count(&S) == 1, "1: 登记了等待确认（走 ARQ），不是变更即推");
    pump();
    CHECK(g_ctrl.writes == 1 && g_ctrl.id == 0x06 && g_ctrl.v == 1.0f, "1: 主控落的是屏写的值");
    CHECK(comm_pending_count(&S) == 0, "1: 主控的 ACK 清掉等待确认");

    /* ---- 2) M207 参数：广播钩子不重复发；设定值本身经 link_send_set_param 走同一条路 ---- */
    CHECK(link_send_on_broadcast(&S, 0x01, PARAM_TYPE_F32, PF_RW | PF_M207, 30.0f) == 0, "2: M207 广播钩子返回成功");
    CHECK(comm_pending_count(&S) == 0 && comm_txq_count(&S) == 0, "2: 广播钩子没有为 M207 参数再发一帧");
    CHECK(link_send_set_param(&S, 0x01, PARAM_TYPE_F32, 30.0f) == 0, "2: 设定值经链路写入成功");
    pump();
    CHECK(g_ctrl.writes == 2 && g_ctrl.id == 0x01 && g_ctrl.v == 30.0f, "2: 主控收到设定值（一帧）");
    CHECK(comm_pending_count(&S) == 0, "2: 等待确认已清");

    /* ---- 3) u8 型参数按整数编码 ---- */
    CHECK(link_send_set_param(&S, 0x08, PARAM_TYPE_U8, 1.0f) == 0, "3: u8 参数入队");
    pump();
    CHECK(g_ctrl.id == 0x08 && g_ctrl.v == 1.0f, "3: u8 参数值到达主控");

    /* ---- 4) 关单用例：第一帧丢失 → 屏侧 500ms 后重传 → 主控最终落的是正确值 ---- */
    open_links();
    g_drop_screen = 1;
    CHECK(link_send_on_broadcast(&S, 0x08, PARAM_TYPE_U8, PF_RW, 1.0f) == 0, "4: 写入入队");
    pump();
    CHECK(g_ctrl.writes == 0, "4: 第一帧丢在线上，主控什么都没收到");
    CHECK(comm_pending_count(&S) == 1, "4: 屏侧仍在等确认（丢了有人知道）");
    for (int t = 0; t < 4; t++) {          /* 499ms 内不得重传 */
        g_now += 124; comm_tick(&S); pump();
    }
    CHECK(g_ctrl.writes == 0 && comm_txq_count(&S) == 0, "4: 超时之前不重传");
    g_now += 10;                            /* 506ms */
    comm_tick(&S);
    pump();
    CHECK(g_ctrl.writes == 1 && g_ctrl.id == 0x08 && g_ctrl.v == 1.0f, "4: 重传到达，主控最终落的是正确值");
    CHECK(comm_pending_count(&S) == 0, "4: 主控的 ACK 清掉等待确认");
    for (int t = 0; t < 20; t++) {          /* 此后不再重传 */
        g_now += 200; comm_tick(&S); pump();
    }
    CHECK(g_ctrl.writes == 1 && g_ctrl.acks == 1, "4: 确认之后不再重传");

    /* ---- 5) 四份全丢：三次重传后判链路断（可见），不会悄悄当成已送达 ---- */
    open_links();
    g_drop_left = 10;
    CHECK(link_send_set_param(&S, 0x06, PARAM_TYPE_F32, 0.0f) == 0, "5: 写入入队");
    for (int t = 0; t < 25; t++) {
        g_now += 200; comm_tick(&S); pump();
    }
    CHECK(g_ctrl.writes == 0, "5: 主控没收到");
    CHECK(g_down_cnt == 1, "5: 三次重传耗尽 → 报链路断（丢了要可见）");
    CHECK(comm_pending_count(&S) == 0, "5: 等待确认已清");
    CHECK(S.retry_cnt == 3, "5: 重传计数 = 3");

    /* ---- 6) M04-F-04：序号 255 是合法序号，屏写到第 256 帧不得被报成失败 ----
     * 旧实现 comm_send 用返回值 0xFF 同时表示"组帧失败"和"序号 255"：这一帧其实已登记、已发出，
     * 调用方却当成失败（m207_redirect_send 因此回落到本地 M207 写）。*/
    open_links();
    S.next_seq = 255;
    CHECK(link_send_set_param(&S, 0x06, PARAM_TYPE_F32, 1.0f) == 0, "6: 序号 255 的写入返回成功（旧实现被当成失败）");
    CHECK(comm_pending_count(&S) == 1, "6: 已登记等待确认");
    pump();
    CHECK(g_ctrl.writes == 1 && g_ctrl.id == 0x06, "6: 主控收到了这一帧");
    CHECK(comm_pending_count(&S) == 0, "6: 主控的 ACK 清掉等待确认");
    CHECK(S.next_seq == 0, "6: 序号回绕到 0");
    /* 连发 300 帧（跨过 255）：每一帧都报成功 */
    {
        int bad = 0;
        for (int i = 0; i < 300; i++) {
            if (link_send_set_param(&S, 0x03, PARAM_TYPE_F32, 30.0f) != 0) bad++;
            pump();
        }
        CHECK(bad == 0, "6: 连发 300 帧没有一帧被报成失败");
        CHECK(g_ctrl.writes == 301, "6: 300 帧全部到达主控");
    }

    printf("test_link_send: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
