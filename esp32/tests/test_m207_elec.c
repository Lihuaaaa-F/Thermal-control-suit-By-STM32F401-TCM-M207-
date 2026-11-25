/* test_m207_elec.c — M207 电学量轮询（m1"模式"页，用户 2026-11-26 定的新功能）
 *
 * 需求：模式页显示每路 TEC 的功率 = |实际输出电压| × 实际输出电流。模块认这两条只读状态
 *（厂商参数字典 TCx:TCACTVOL / TCx:TCACTCUR；真机 2026-11-26 实测：
 *   TC1:TCACTVOL=0.023、TC1:TCACTCUR=0.000、TC2:TCACTVOL=0.020、TC2:TCACTCUR=-0.000）。
 * 实现（m207_core.c）：每秒把四条查询各问一遍，只借温度轮询的空档；应答窗必须早于下一次
 * 温度轮询结束；电学查询的超时 / 被拒不计链路失败；连续 3 次拿不到就判"不支持"并停发。
 *
 * 判别性：这些用例在加轮询之前的 m207_core.c 上编译不过（m207_elec_get /
 * m207_elec_supported 不存在）；用例 8 专抓"把别的帧的回码记到电学查询头上"。
 */
#include <math.h>
#include "m207_tb.h"

/* 带格式化说明的断言（测试台自带的 TB_CHECK 只收两个参数）*/
#define CHECK(cond, ...) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: "); printf(__VA_ARGS__); printf(" (line %d)\n", __LINE__); fflush(stdout); } \
} while (0)

#define E0 "TC1:TCACTVOL?"
#define E1 "TC1:TCACTCUR?"
#define E2 "TC2:TCACTVOL?"
#define E3 "TC2:TCACTCUR?"

static int is_elec(const char *f)
{
    return !strcmp(f, E0) || !strcmp(f, E1) || !strcmp(f, E2) || !strcmp(f, E3);
}
static int is_temp_poll(const char *f)
{
    return strlen(f) == 14 && f[3] == ':' && !strcmp(f + 4, "TCACTTEMP?");
}
static int elec_count_since(int idx)
{
    int c = 0;
    for (int i = idx; i < TB.ntx; i++) if (is_elec(TB.tx[i].f)) c++;
    return c;
}

/* 1. 正常值、负电压、负零；取值接口；坏参数 */
static void test_elec_values(void)
{
    m207_elec_t e;
    tb_init();
    TB.m.volt[1] = 8.234f; TB.m.cur[1] = 3.104f;
    TB.m.volt[2] = -0.023f;
    TB.m.cur[2] = -0.0f;                         /* 真机：TC2:TCACTCUR=-0.000 */
    TB_CHECK(tb_wait_online(120), "online");
    CHECK(m207_elec_supported() == 1, "not yet decided counts as supported");
    CHECK(m207_elec_get(1, &e) == 0 && e.valid == 0 && e.fresh == 0, "unknown before the first reply");
    CHECK(e.age_ms == 0xFFFFFFFFu, "age unknown before the first reply");
    tb_run(60);                                  /* 3 s */
    CHECK(m207_elec_get(1, &e) == 0 && e.valid == 1, "TC1 voltage + current valid");
    CHECK(fabsf(e.volt - 8.234f) < 2e-3f, "TC1 voltage 8.234 V (got %f)", (double)e.volt);
    CHECK(fabsf(e.cur - 3.104f) < 2e-3f, "TC1 current 3.104 A (got %f)", (double)e.cur);
    CHECK(e.age_ms < 2000u, "TC1 data age under 2 s (refreshed every second; got %lu)", (unsigned long)e.age_ms);
    CHECK(fabsf(fabsf(e.volt) * e.cur - 25.54f) < 0.05f, "TC1 power = |V| x I = 25.5 W");
    CHECK(m207_elec_get(2, &e) == 0 && e.valid == 1, "TC2 valid");
    CHECK(fabsf(e.volt + 0.023f) < 2e-3f, "TC2 negative voltage kept (got %f)", (double)e.volt);
    CHECK(e.cur == 0.0f && !signbit(e.cur), "TC2 '-0.000' -> +0.0 (negative zero eaten)");
    CHECK(fabsf(e.volt * e.cur) == 0.0f, "TC2 power 0.0 W (no '-0.0' anywhere)");
    CHECK(m207_elec_get(0, &e) == -1 && m207_elec_get(3, &e) == -1, "bad channel rejected");
    CHECK(m207_elec_get(1, NULL) == -1, "NULL out rejected");
    CHECK(m207_elec_supported() == 1, "still supported with an answering module");
}

/* 2. 节奏：温度轮询一步不让；每个电学量每秒至少刷新一次；电学查询不挤进温度轮询前的应答窗 */
static void test_elec_cadence(void)
{
    static const char *const q[4] = { E0, E1, E2, E3 };
    int cnt[4] = { 0, 0, 0, 0 }, lastsame[4] = { -1, -1, -1, -1 };
    int t = 0, last = -1, maxgap = 0, nelec = 0, tight = 0, maxsame = 0, n0, i, j;
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(60);                                  /* 复联查询与模块状态查询发完 */
    n0 = TB.ntx;
    tb_run(200);                                 /* 10 s */
    for (i = n0; i < TB.ntx; i++) {
        const char *f = TB.tx[i].f;
        int tk = (int)TB.tx[i].tick;
        if (is_temp_poll(f)) {
            t++;
            if (last >= 0 && tk - last > maxgap) maxgap = tk - last;
            last = tk;
        } else if (is_elec(f)) {
            int kn = -1;
            nelec++;
            /* 它的应答截止（发出后 6 拍）必须早于下一次温度轮询 */
            for (int k = i + 1; k < TB.ntx; k++) if (is_temp_poll(TB.tx[k].f)) { kn = (int)TB.tx[k].tick; break; }
            if (kn >= 0 && kn - tk < 7) tight++;
            for (j = 0; j < 4; j++)
                if (!strcmp(f, q[j])) {
                    cnt[j]++;
                    if (lastsame[j] >= 0 && tk - lastsame[j] > maxsame) maxsame = tk - lastsame[j];
                    lastsame[j] = tk;
                }
        }
    }
    CHECK(t == 20, "temperature polling cadence unchanged: 20 polls in 10 s (got %d)", t);
    CHECK(maxgap == 10, "temperature polls stay 500 ms apart (largest gap %d ticks)", maxgap);
    CHECK(cnt[0] >= 14 && cnt[1] >= 14 && cnt[2] >= 14 && cnt[3] >= 14,
          "all four electrical queries polled regularly (10 s: %d/%d/%d/%d)", cnt[0], cnt[1], cnt[2], cnt[3]);
    CHECK(cnt[0] <= 16 && cnt[1] <= 16 && cnt[2] <= 16 && cnt[3] <= 16,
          "electrical polling keeps to about the 1 s rate (10 s: %d/%d/%d/%d)", cnt[0], cnt[1], cnt[2], cnt[3]);
    CHECK(nelec > 0 && tight == 0, "no electrical query inside the 300 ms window before a temperature poll (%d)", tight);
    CHECK(maxsame <= 20, "each quantity refreshed at least once per second (largest gap %d ticks)", maxsame);
}

/* 3. 模块不认（回 CMD:REPLY=2）：三次后停发；不算链路失败；=2 不得被当成传感器开路 */
static void test_elec_rejected(void)
{
    m207_elec_t e;
    tb_init();
    TB.m.elec_mode = 1;
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(60);
    CHECK(elec_count_since(0) == 3, "module rejects the electrical queries: exactly 3 attempts (got %d)",
          elec_count_since(0));
    CHECK(m207_elec_supported() == 0, "marked unsupported");
    tb_run(400);                                 /* 再过 20 s */
    CHECK(elec_count_since(0) == 3, "no further attempts (no per-second retry / log spam)");
    CHECK(m207_online() == 1 && m207_stats_offline() == 0, "rejections are not a comm failure");
    CHECK(g_tcs.m207_sensor_open == 0, "CMD:REPLY=2 for an electrical query is NOT taken as sensor-open");
    CHECK(m207_elec_get(1, &e) == 0 && e.valid == 0 && e.fresh == 0, "nothing to show -> unknown");
    CHECK(m207_last_temp(1) > 0.0f, "temperature still sampled normally");
}

/* 4. 模块不应答（超时）：三次后停发；温度轮询一步不让 */
static void test_elec_timeout(void)
{
    int t = 0, last = -1, maxgap = 0, n0;
    tb_init();
    TB.m.elec_mode = 2;
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(60);
    n0 = TB.ntx;
    CHECK(elec_count_since(0) == 3, "no reply: exactly 3 attempts (got %d)", elec_count_since(0));
    CHECK(m207_elec_supported() == 0, "marked unsupported after timeouts");
    tb_run(200);                                 /* 10 s */
    CHECK(elec_count_since(0) == 3, "stopped after 3 timeouts");
    for (int i = n0; i < TB.ntx; i++)
        if (is_temp_poll(TB.tx[i].f)) {
            t++;
            if (last >= 0 && (int)TB.tx[i].tick - last > maxgap) maxgap = (int)TB.tx[i].tick - last;
            last = (int)TB.tx[i].tick;
        }
    CHECK(t == 20 && maxgap == 10, "temperature polling never waits for an electrical reply (%d polls, gap %d)",
          t, maxgap);
    CHECK(m207_online() == 1 && m207_stats_offline() == 0, "electrical timeouts are not a comm failure");
    CHECK(g_tcs.m207_sensor_open == 0, "no false sensor-open from electrical timeouts");
}

/* 5. 坏值不入缓存：负电流、超量程 */
static void test_elec_bad_values(void)
{
    m207_elec_t e;
    tb_init();
    TB.m.volt[1] = 8.0f; TB.m.cur[1] = 3.0f;
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(60);
    CHECK(m207_elec_get(1, &e) == 0 && e.valid == 1, "(setup) good values cached");
    TB.m.cur[1] = -0.5f;                         /* 电流恒为正：负值是坏值 */
    tb_run(60);
    CHECK(m207_elec_get(1, &e) == 0 && fabsf(e.cur - 3.0f) < 1e-3f,
          "negative current ignored, last good value kept (got %f)", (double)e.cur);
    TB.m.volt[1] = 1.0e9f;                       /* 明显超量程 */
    tb_run(60);
    CHECK(m207_elec_get(1, &e) == 0 && fabsf(e.volt - 8.0f) < 1e-3f,
          "out-of-range voltage ignored, last good value kept (got %f)", (double)e.volt);
    CHECK(m207_elec_supported() == 1 && m207_online() == 1, "bad values do not count as failures");
}

/* 6. 数据龄：停更后龄继续增长，太旧按未知（旧值仍可读、fresh 保持）*/
static void test_elec_stale(void)
{
    m207_elec_t e;
    tb_init();
    TB.m.volt[1] = 8.0f; TB.m.cur[1] = 3.0f;
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(40);
    CHECK(m207_elec_get(1, &e) == 0 && e.valid == 1, "(setup) valid");
    TB.m.elec_mode = 2;                          /* 之后不应答 */
    tb_run(30);                                  /* 1.5 s */
    CHECK(m207_elec_get(1, &e) == 0 && e.valid == 1 && e.fresh == 1, "still valid at 1.5 s");
    tb_run(50);                                  /* 再 2.5 s：已超过 3 s */
    CHECK(m207_elec_get(1, &e) == 0 && e.valid == 0 && e.fresh == 1, "older than 3 s -> unknown");
    CHECK(e.age_ms > 3000u, "age keeps growing (%lu)", (unsigned long)e.age_ms);
    CHECK(e.volt == 8.0f && e.cur == 3.0f, "last values still readable for diagnostics");
}

/* 7. 复联后重新试 */
static void test_elec_retry_after_reconnect(void)
{
    m207_elec_t e;
    int n0, w;
    tb_init();
    TB.m.elec_mode = 1;
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(60);
    CHECK(m207_elec_supported() == 0, "(setup) unsupported");
    n0 = elec_count_since(0);
    TB.m.silent = 1;
    w = 200;
    while (m207_stats_offline() == 0 && w-- > 0) tb_tick();
    TB.m.silent = 0;
    TB.m.elec_mode = 0;                          /* 换了一块认这两条的模块 */
    w = 150;
    while (!m207_online() && w-- > 0) tb_tick();
    TB_CHECK(m207_online(), "back online");
    tb_run(60);
    CHECK(elec_count_since(0) > n0, "electrical polling resumes after reconnect");
    CHECK(m207_elec_supported() == 1, "unsupported flag cleared on reconnect");
    CHECK(m207_elec_get(2, &e) == 0 && e.valid == 1, "values read after reconnect");
}

/* 8. 与别的帧交错：电学查询在途时，本拍的发送名额给了别的帧（这里是被模块拒绝的控制台帧），
 *    那一帧的回码不得记到电学查询头上——在途的电学等待作废，回码照旧按"无查询在途的回码"留痕。*/
static void test_elec_not_blamed_for_others(void)
{
    int u0, found, guard;
    tb_init();
    TB.m.elec_mode = 2;                          /* 电学查询本身不应答（走超时）*/
    TB_CHECK(tb_wait_online(120), "online");
    /* 不先跑 60 拍：不应答时三次超时在头 3 秒内就会判"不支持"。上电后立刻开始交错。*/
    CHECK(m207_elec_supported() == 1, "(setup) still trying");
    u0 = (int)m207_stats_unexpected();
    found = 0;
    for (guard = 0; guard < 600 && found < 5; guard++) {
        tb_tick();
        if (TB.ntx && is_elec(TB.tx[TB.ntx - 1].f) && TB.tx[TB.ntx - 1].tick == TB.tick) {
            (void)m207_send_raw("ABC");          /* 下一拍上线路：那一拍的名额被它占走；模块回 CMD:REPLY=6 */
            found++;
        }
    }
    tb_run(8);
    CHECK(found == 5, "(setup) interleaved %d rejected console frames with in-flight electrical queries", found);
    CHECK((int)m207_stats_unexpected() - u0 == 5,
          "each rejected console frame is accounted as unexpected, not blamed on the electrical query (got %d)",
          (int)m207_stats_unexpected() - u0);
    CHECK(m207_elec_supported() == 1, "a frame rejected for other reasons is not an electrical failure");
    CHECK(m207_online() == 1 && g_tcs.m207_sensor_open == 0, "interleaving causes no false sensor-open / offline");
}

int main(void)
{
    test_elec_values();
    test_elec_cadence();
    test_elec_rejected();
    test_elec_timeout();
    test_elec_bad_values();
    test_elec_stale();
    test_elec_retry_after_reconnect();
    test_elec_not_blamed_for_others();
    return tb_done("test_m207_elec");
}
