/* test_m207_queue.c — M207 下行队列：安全关断不被挤掉 / 不静默丢帧（S-G e2：X03-F-07、M12-F-08）
 *
 * X03-F-07：TCSW=0（安全关断）原来和普通参数写同走 16 深队列，队列满的瞬间入队失败、
 *           无计数；已上线路未确认的 TCSW=1 还会在应答超时后被重发，把刚发出去的关断盖掉。
 * M12-F-08：队列满静默丢写且无计数；"保存到模块"一次入队 32 条、队列只有 16 深，
 *           TC2 的 16 条静默丢掉，屏幕仍收到 OK。
 *
 * 测试台见 m207_tb.h。每条断言在未修的内核（提交 22d7323 / A2）上都先跑红过，
 * 红输出见 docs/results/S_G_e2_结果.md。
 */
#include "m207_tb.h"

/* ================= X03-F-07 / M12-F-08（S-G e2）================= */

/* 9. 队列满：普通写被拒并计数；安全关断不进普通队列，照样下一拍上线路 */
static void test_safe_off_bypasses_full_queue(void)
{
    m207_qstats_t st;
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    int okc = 0;
    for (int i = 0; i < 16; i++) okc += (m207_map_set((uint16_t)(0x100 + i), 1.0f + (float)i) == 0);
    TB_CHECK(okc == 16, "16 verified writes fill the queue");
    TB_CHECK(m207_map_set(0x110, 2.0f) == -1, "17th write rejected: queue full");
    m207_stats_queue(&st);
    TB_CHECK(st.qdrop == 1, "M12-F-08: queue-full rejection is counted");

    int n0 = TB.ntx;
    TB_CHECK(m207_tec(1, 0) == 0, "X03-F-07: safe-off accepted while the queue is full");
    tb_tick();
    TB_CHECK(tb_tx_at(n0, "TC1:TCSW=0"), "X03-F-07: safe-off is the very next frame on the wire");
    m207_stats_queue(&st);
    TB_CHECK(st.qdrop == 1, "safe-off never counts as a queue drop");
}

/* 10. 队列里还没发出去的"开"被关断作废 */
static void test_safe_off_cancels_stale_on(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    int n0 = TB.ntx;
    TB_CHECK(m207_tec(1, 1) == 0, "on queued");
    TB_CHECK(m207_tec(1, 0) == 0, "off requested before the on left the queue");
    tb_run(40);
    TB_CHECK(tb_tx_since(n0, "TC1:TCSW=1") < 0, "stale queued ON never reaches the wire");
    TB_CHECK(tb_tx_at(n0, "TC1:TCSW=0"), "off is the next frame");
    int c = 0;                                  /* 自 n0 起数（上电与复联的 TCSW=0 不算）*/
    for (int i = n0; i < TB.ntx; i++) if (!strcmp(TB.tx[i].f, "TC1:TCSW=0")) c++;
    TB_CHECK(c == 1, "off sent exactly once");
}

/* 11. 已上线路、还没确认的"开"：关断之后不再被重发（原来应答超时会把 TCSW=1 再发一遍，
 *     把刚发出去的关断盖掉，TEC 重新打开而系统以为已关）*/
static void test_safe_off_cancels_inflight_on(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    TB.m.drop_pct = 100;                         /* 模块对什么都不应答 */
    int n0 = TB.ntx;
    TB_CHECK(m207_tec(1, 1) == 0, "on queued");
    tb_tick();                                   /* 开已上线路，等不到应答 */
    TB_CHECK(tb_tx_at(n0, "TC1:TCSW=1"), "on on the wire");
    int n1 = TB.ntx;
    TB_CHECK(m207_tec(1, 0) == 0, "off requested while the on is unconfirmed");
    tb_tick();
    TB_CHECK(tb_tx_at(n1, "TC1:TCSW=0"), "off goes out on the next tick");
    tb_run(20);
    TB_CHECK(tb_tx_since(n1 + 1, "TC1:TCSW=1") < 0,
             "X03-F-07: unconfirmed ON is never resent after an off");
}

/* 12. 同一拍内多次关断合成一帧；旁路里没发出去的写以最新值为准 */
static void test_safe_off_coalesces(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    int n0 = TB.ntx;
    for (int i = 0; i < 5; i++) TB_CHECK(m207_tec(1, 0) == 0, "off accepted");
    tb_tick();
    TB_CHECK(TB.ntx == n0 + 1 && tb_tx_at(n0, "TC1:TCSW=0"), "five requests in one tick -> one frame");
    tb_run(10);
    TB_CHECK(tb_tx_since(n0 + 1, "TC1:TCSW=0") < 0, "no duplicate off afterwards");

    /* 50ms 窗内先关后开：最新值（开）上线路，过期的关不发 */
    n0 = TB.ntx;
    TB_CHECK(m207_tec(2, 0) == 0 && m207_tec(2, 1) == 0, "off then on within one tick");
    tb_tick();
    TB_CHECK(tb_tx_at(n0, "TC2:TCSW=1") && tb_tx_since(n0, "TC2:TCSW=0") < 0,
             "latest value wins inside the bypass slot");
}

/* 13. 关断经回读确认；模块"关不动"时重发一次后 NAK，如实可见 */
static void test_safe_off_is_verified(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    uint16_t vok0 = m207_stats_verify_ok();
    int n0 = TB.ntx;
    TB_CHECK(m207_tec(2, 0) == 0, "off accepted");
    tb_run(10);
    TB_CHECK(tb_tx_since(n0, "TC2:TCSW?") >= 0, "off is read back");
    TB_CHECK(m207_stats_verify_ok() == vok0 + 1, "off confirmed by read-back");

    uint16_t vok1 = m207_stats_verify_ok(), nak0 = m207_stats_nak(), rs0 = m207_stats_resend();
    TB.m.sw_stuck[2] = 1; TB.m.sw[2] = 1;        /* 设定照回 REPLY=1，回读仍是 1 */
    n0 = TB.ntx;
    TB_CHECK(m207_tec(2, 0) == 0, "second off accepted");
    tb_run(12);
    int c = 0;
    for (int i = n0; i < TB.ntx; i++) if (!strcmp(TB.tx[i].f, "TC2:TCSW=0")) c++;
    TB_CHECK(c == 2, "stuck ON: off sent twice (initial + one resend)");
    TB_CHECK(m207_stats_resend() == rs0 + 1 && m207_stats_nak() == nak0 + 1,
             "stuck ON: resend once then NAK");
    TB_CHECK(m207_stats_verify_ok() == vok1, "stuck ON: no false confirmation");
}

/* 14. M12-F-08：保存到模块 32 条帧全部上线路、各一次、确认计数如实
 *     （原来一次入队 32 条、队列只有 16 深：TC2 的 16 条静默丢掉，屏幕仍收到 OK）*/
static void test_save_all_is_lossless(void)
{
    m207_qstats_t st;
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    int n0 = TB.ntx;
    int n = m207_save_all();
    TB_CHECK(n == 32, "save_all reports 32 frames");
    TB_CHECK(m207_save_busy(), "save sequence is busy");
    TB_CHECK(m207_save_all() == -1, "second press while saving -> BUSY (-1)");
    int guard = 80;
    while (m207_save_busy() && guard-- > 0) tb_tick();
    TB_CHECK(!m207_save_busy(), "save sequence finished");
    int nsave = 0;
    for (int i = n0; i < TB.ntx; i++) if (strchr(TB.tx[i].f, '!')) nsave++;
    TB_CHECK(nsave == 32, "all 32 save frames on the wire (TC1 x16 + TC2 x16)");
    TB_CHECK(tb_tx_count("TC1:TCPIDP!") == 1 && tb_tx_count("TC1:TCSTEPTEMP!") == 1,
             "TC1 saves exactly once");
    TB_CHECK(tb_tx_count("TC2:TCPIDP!") == 1 && tb_tx_count("TC2:TCSTEPTEMP!") == 1,
             "TC2 saves exactly once (used to be dropped silently)");
    tb_run(10);
    m207_stats_queue(&st);
    TB_CHECK(st.save_ok == 32 && st.save_rej == 0, "32 save frames acknowledged with CMD:REPLY=8");
}

/* 15. 保存序列进行中掉线：序列中止；离线时按保存返回 -2 */
static void test_save_all_offline(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    TB.m.silent = 1;
    int guard = 200;
    while (m207_stats_offline() == 0 && guard-- > 0) tb_tick();
    TB_CHECK(m207_save_all() == -2, "offline -> rejected (-2)");

    TB.m.silent = 0;
    guard = 150;
    while (!m207_online() && guard-- > 0) tb_tick();
    TB_CHECK(m207_online(), "back online");
    TB_CHECK(m207_save_all() == 32, "save started");
    tb_run(3);
    TB_CHECK(m207_save_busy(), "still saving");
    TB.m.silent = 1;
    guard = 400;
    while (m207_stats_offline() < 2 && guard-- > 0) tb_tick();
    TB_CHECK(m207_stats_offline() == 2, "offline again");
    TB_CHECK(!m207_save_busy(), "save sequence aborted at offline");
}

int main(void)
{
    test_safe_off_bypasses_full_queue();
    test_safe_off_cancels_stale_on();
    test_safe_off_cancels_inflight_on();
    test_safe_off_coalesces();
    test_safe_off_is_verified();
    test_save_all_is_lossless();
    test_save_all_offline();
    return tb_done("test_m207_queue");
}
