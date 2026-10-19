/**
 * test_touch_session.c — WP-M3 触摸重写质量门（PC 单测，替代 test_touchflt）
 *
 * 覆盖（任务书 v10.11 M3）：
 *   1. T-13 坐标映射回归断言：GS 验证映射（交换 X/Y 不镜像）逐点断言 + clamp；
 *      raw 级注入（携带 raw 坐标走 map→session 全链）——封死"注入绕过变换层"盲区。
 *   2. 会话状态机纯逻辑：
 *      - 真会话 100% 通过 / 幽灵会话 100% 拦截（M4 双门的 PC 侧前提）
 *      - 边界：按下即抬 / 长按微抖 / 滑动跨界 / 多点并发 /
 *        幽灵会话插在真会话中间（H1 跨会话误杀回归）/ 抬起宽限毛刺 /
 *        翻页与弹窗瞬间点击（T-4：会话层判定=丢弃，严禁错页响应前提）
 *      - 坏包/越界注入：防御性丢弃且不崩（H11 精神）
 *      - 注入豁免：注入样本恒 UP_OK（注入=功能路径，不模拟幽灵）
 *
 * 时间模型：10ms 主循环节拍，feed() 每调用推进 1 拍；
 * 抬手 = 3 个空闲拍（第 1 拍记 t_lift 进宽限，第 2 拍在宽限内，第 3 拍结算）。
 */
#include <stdio.h>
#include <string.h>
#include "touch_session.h"
#include "ft5446.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } \
} while (0)

static uint32_t g_now = 0;

/* 带触点 id 喂入（id 恒 0 = FT5446 单点常见）*/
static void feed_id(ts_state_t *st, uint8_t id, uint8_t touched, int x, int y, ts_out_t *o)
{
    ts_sample_t s;
    memset(&s, 0, sizeof(s));
    s.touched = touched;
    s.valid = 1;
    s.contacts = touched ? 1u : 0u;
    s.touch_id = id;
    s.x = x;
    s.y = y;
    ts_feed(st, &s, g_now, o);
    g_now += 10;
}

static void feed(ts_state_t *st, uint8_t touched, int x, int y, ts_out_t *o)
{
    feed_id(st, 0, touched, x, y, o);
}

/* 抬手三连拍：返回结算事件（第 3 拍）*/
static void lift(ts_state_t *st, ts_out_t *o)
{
    feed(st, 0, 0, 0, o);   /* 进宽限（记 t_lift）*/
    feed(st, 0, 0, 0, o);   /* 宽限内 */
    feed(st, 0, 0, 0, o);   /* 结算 */
}

/* raw 级喂入：先走映射变换链（T-13）*/
static void feed_raw(ts_state_t *st, uint16_t xr, uint16_t yr, uint8_t touched, ts_out_t *o)
{
    int sx, sy;
    ft5446_map_raw(xr, yr, &sx, &sy);
    feed(st, touched, sx, sy, o);
}

/* ===== 1. T-13 坐标映射回归（GS 验证映射：screen=(raw_y, raw_x)）===== */
static void test_map_regression(void)
{
    printf("[map_regression]\n");
    int sx, sy;
    ft5446_map_raw(0, 0, &sx, &sy);       CHECK(sx == 0    && sy == 0,    "raw(0,0)->scr(0,0)");
    ft5446_map_raw(0, 1023, &sx, &sy);    CHECK(sx == 1023 && sy == 0,    "raw(0,1023)->scr(1023,0)");
    ft5446_map_raw(599, 0, &sx, &sy);     CHECK(sx == 0    && sy == 599,  "raw(599,0)->scr(0,599)");
    ft5446_map_raw(599, 1023, &sx, &sy);  CHECK(sx == 1023 && sy == 599,  "raw(599,1023)->scr(1023,599)");
    ft5446_map_raw(300, 512, &sx, &sy);   CHECK(sx == 512 && sy == 300, "center swap");
    ft5446_map_raw(4095, 4095, &sx, &sy); CHECK(sx == 1023 && sy == 599, "clamp max");
    ft5446_map_raw(0xFFFF, 0xFFFF, &sx, &sy); CHECK(sx == 1023 && sy == 599, "clamp u16");
    ft5446_map_raw(500, 100, &sx, &sy);   CHECK(sx == 100 && sy == 500, "no X mirror");
    printf("  map_regression done\n");
}

/* ===== 2. 真会话 100% 通过 / 幽灵 100% 拦截 ===== */
static void test_real_vs_ghost(void)
{
    printf("[real_vs_ghost]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;
    uint32_t ghost0 = g_ts_ghost_sessions;

    /* 真击：按下 + 2 拍移动（位移 3px）+ 抬起 → UP_OK */
    feed(&st, 1, 100, 200, &o);   CHECK(o.evt == TS_EVT_DOWN, "real DOWN");
    feed(&st, 1, 101, 200, &o);   CHECK(o.evt == TS_EVT_MOVE, "real MOVE");
    feed(&st, 1, 103, 201, &o);   CHECK(o.evt == TS_EVT_MOVE, "real MOVE2");
    lift(&st, &o);                CHECK(o.evt == TS_EVT_UP_OK, "real UP_OK");
    CHECK(o.dur_ms >= 30 && o.max_disp >= 1, "dur/disp reported");

    /* 幽灵：按下即抬（10ms，零位移）→ DOWN+UP_GHOST */
    feed(&st, 1, 700, 300, &o);   CHECK(o.evt == TS_EVT_DOWN, "ghost DOWN");
    lift(&st, &o);                CHECK(o.evt == TS_EVT_UP_GHOST, "ghost UP_GHOST");
    CHECK(g_ts_ghost_sessions == ghost0 + 1, "ghost counted");

    /* 长按微抖（40 拍，±1px）：非幽灵 → UP_OK */
    feed(&st, 1, 400, 400, &o);   CHECK(o.evt == TS_EVT_DOWN, "hold DOWN");
    int ok = 1;
    for (int i = 0; i < 40; i++) {
        feed(&st, 1, 400 + (i & 1), 400, &o);
        if (o.evt != TS_EVT_MOVE) ok = 0;
    }
    CHECK(ok, "hold MOVEs");
    lift(&st, &o);                CHECK(o.evt == TS_EVT_UP_OK, "hold UP_OK");
    printf("  real_vs_ghost done\n");
}

/* ===== 3. H1 跨会话误杀回归：幽灵夹在两真会话之间，真会话零惩罚 ===== */
static void test_ghost_between_real(void)
{
    printf("[ghost_between_real]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;

    /* 真会话 1（有位移+时长）*/
    feed(&st, 1, 100, 100, &o);  CHECK(o.evt == TS_EVT_DOWN, "r1 DOWN");
    feed(&st, 1, 105, 102, &o);  CHECK(o.evt == TS_EVT_MOVE, "r1 MOVE");
    feed(&st, 1, 108, 104, &o);  CHECK(o.evt == TS_EVT_MOVE, "r1 MOVE2");
    feed(&st, 1, 110, 105, &o);  CHECK(o.evt == TS_EVT_MOVE, "r1 MOVE3");
    lift(&st, &o);               CHECK(o.evt == TS_EVT_UP_OK, "r1 UP_OK");
    /* 幽灵（远端随机位置，零位移短会话）*/
    feed(&st, 1, 900, 500, &o);  CHECK(o.evt == TS_EVT_DOWN, "g DOWN");
    lift(&st, &o);               CHECK(o.evt == TS_EVT_UP_GHOST, "g UP_GHOST");
    /* 真会话 2：首帧与幽灵位置相距 >200px——旧 H1 在此丢真击，会话机必须放行 */
    feed(&st, 1, 150, 120, &o);  CHECK(o.evt == TS_EVT_DOWN, "r2 DOWN (no cross-session penalty)");
    feed(&st, 1, 152, 121, &o);  CHECK(o.evt == TS_EVT_MOVE, "r2 MOVE");
    lift(&st, &o);               CHECK(o.evt == TS_EVT_UP_OK, "r2 UP_OK");
    printf("  ghost_between_real done\n");
}

/* ===== 4. T-4 翻页/弹窗瞬间点击 + 幽灵判定边界（40ms vs 50ms）===== */
static void test_flip_instant_click(void)
{
    printf("[flip_instant]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;
    /* 翻页瞬间 1 拍点击 → 幽灵（UI 层不得响应）*/
    feed(&st, 1, 512, 550, &o);  CHECK(o.evt == TS_EVT_DOWN, "flip click DOWN");
    lift(&st, &o);               CHECK(o.evt == TS_EVT_UP_GHOST, "flip click swallowed");
    /* 边界：3 拍按下（30ms 抬起）零位移 → 幽灵；4 拍（40ms）零位移仍 <50 → 幽灵；
     * 6 拍（50ms）零位移 → UP_OK（≥50ms 判真）*/
    feed(&st, 1, 512, 550, &o);  CHECK(o.evt == TS_EVT_DOWN, "b1 DOWN");
    for (int i = 0; i < 3; i++) feed(&st, 1, 512, 550, &o);
    lift(&st, &o);               CHECK(o.evt == TS_EVT_UP_GHOST, "30ms zero-disp ghost");
    feed(&st, 1, 512, 550, &o);  CHECK(o.evt == TS_EVT_DOWN, "b2 DOWN");
    for (int i = 0; i < 5; i++) feed(&st, 1, 512, 550, &o);
    lift(&st, &o);               CHECK(o.evt == TS_EVT_UP_OK, "50ms zero-disp real");
    printf("  flip_instant done\n");
}

/* ===== 5. 多点并发（T-3）：第二接触忽略+计数；单次失配不拆会话 ===== */
static void test_multipoint(void)
{
    printf("[multipoint]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;
    uint32_t mp0 = g_ts_multipoint_drops;

    feed(&st, 1, 100, 100, &o);  CHECK(o.evt == TS_EVT_DOWN, "s1 DOWN");
    /* 第二接触（不同 id）：忽略+计数 */
    {
        ts_sample_t s;
        memset(&s, 0, sizeof(s));
        s.touched = 1; s.valid = 1; s.contacts = 2; s.touch_id = 1;
        s.x = 800; s.y = 400;
        ts_feed(&st, &s, g_now, &o);
        g_now += 10;
    }
    CHECK(o.evt == TS_EVT_NONE, "2nd contact ignored");
    CHECK(g_ts_multipoint_drops == mp0 + 1, "multipoint counted");
    /* 原会话继续正常（失配计数被同 id 样本清零）*/
    feed(&st, 1, 100, 102, &o);  CHECK(o.evt == TS_EVT_MOVE, "orig session alive");
    lift(&st, &o);               CHECK(o.evt == TS_EVT_UP_OK, "orig UP_OK (multipoint≠ghost)");
    printf("  multipoint done\n");
}

/* ===== 5b. 触点被顶替：连续 3 拍失配 → 结算旧会话+链式接续 ===== */
static void test_contact_takeover(void)
{
    printf("[takeover]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;

    feed_id(&st, 0, 1, 100, 100, &o);  CHECK(o.evt == TS_EVT_DOWN, "s0 DOWN");
    feed_id(&st, 1, 1, 800, 400, &o);  CHECK(o.evt == TS_EVT_NONE, "mismatch1 ignored");
    feed_id(&st, 1, 1, 800, 400, &o);  CHECK(o.evt == TS_EVT_NONE, "mismatch2 ignored");
    feed_id(&st, 1, 1, 800, 400, &o);  CHECK(o.evt == TS_EVT_UP_GHOST, "old settled (short ghost)");
    feed_id(&st, 1, 1, 800, 400, &o);  CHECK(o.evt == TS_EVT_MOVE, "chained session alive");
    lift(&st, &o);                     CHECK(o.evt == TS_EVT_UP_OK, "chained UP_OK (never ghost)");
    printf("  takeover done\n");
}

/* ===== 6. 抬起宽限：单帧 n==0 毛刺不拆会话 ===== */
static void test_lift_grace(void)
{
    printf("[lift_grace]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;
    uint32_t gr0 = g_ts_glitch_resume;

    feed(&st, 1, 200, 200, &o);  CHECK(o.evt == TS_EVT_DOWN, "g DOWN");
    feed(&st, 1, 200, 202, &o);  CHECK(o.evt == TS_EVT_MOVE, "g MOVE");
    feed(&st, 0, 0, 0, &o);      CHECK(o.evt == TS_EVT_NONE, "grace arm (no event)");
    feed(&st, 1, 200, 203, &o);  CHECK(o.evt == TS_EVT_MOVE, "glitch resumed -> MOVE");
    CHECK(g_ts_glitch_resume == gr0 + 1, "glitch_resume counted");
    lift(&st, &o);               CHECK(o.evt == TS_EVT_UP_OK, "settle UP_OK");
    printf("  lift_grace done\n");
}

/* ===== 7. 坏包/越界注入（H11）：防御丢弃不崩、不扰会话 ===== */
static void test_badsample(void)
{
    printf("[badsample]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;
    uint32_t bad0 = g_ts_badsample_drops;

    feed(&st, 1, 100, 100, &o);  CHECK(o.evt == TS_EVT_DOWN, "DOWN");
    {
        ts_sample_t s;
        memset(&s, 0, sizeof(s));
        s.touched = 1; s.valid = 0;   /* 坏包 */
        s.x = -32000; s.y = 32000;
        ts_feed(&st, &s, g_now, &o);
        g_now += 10;
    }
    CHECK(o.evt == TS_EVT_NONE, "bad dropped");
    CHECK(g_ts_badsample_drops == bad0 + 1, "bad counted");
    feed(&st, 1, 100, 101, &o);  CHECK(o.evt == TS_EVT_MOVE, "session intact");
    lift(&st, &o);               CHECK(o.evt == TS_EVT_UP_OK, "UP_OK");
    printf("  badsample done\n");
}

/* ===== 8. 会话内瞬移：样本丢弃、会话继续 ===== */
static void test_insession_jump(void)
{
    printf("[insession_jump]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;
    uint32_t j0 = g_ts_jump_drops;

    feed(&st, 1, 100, 100, &o);   CHECK(o.evt == TS_EVT_DOWN, "DOWN");
    feed(&st, 1, 500, 500, &o);   CHECK(o.evt == TS_EVT_NONE, "teleport dropped");
    CHECK(g_ts_jump_drops == j0 + 1, "jump counted");
    feed(&st, 1, 102, 100, &o);   CHECK(o.evt == TS_EVT_MOVE, "session alive");
    lift(&st, &o);                CHECK(o.evt == TS_EVT_UP_OK, "UP_OK");
    printf("  insession_jump done\n");
}

/* ===== 9. 注入豁免：注入样本恒 UP_OK（功能路径）===== */
static void test_inject_exempt(void)
{
    printf("[inject_exempt]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;
    uint32_t ghost0 = g_ts_ghost_sessions;

    {
        ts_sample_t s;
        memset(&s, 0, sizeof(s));
        s.touched = 1; s.valid = 1; s.injected = 1; s.contacts = 1;
        s.x = 512; s.y = 300;
        ts_feed(&st, &s, g_now, &o);
        g_now += 10;
    }
    CHECK(o.evt == TS_EVT_DOWN, "inj DOWN");
    {
        ts_sample_t s;
        memset(&s, 0, sizeof(s));
        s.touched = 0; s.valid = 1; s.injected = 1;
        ts_feed(&st, &s, g_now, &o);
        g_now += 10;
        ts_feed(&st, &s, g_now, &o);
        g_now += 10;
        ts_feed(&st, &s, g_now, &o);
        g_now += 10;
    }
    CHECK(o.evt == TS_EVT_UP_OK, "inj 10ms session still UP_OK");
    CHECK(g_ts_ghost_sessions == ghost0, "no ghost count for injected");
    printf("  inject_exempt done\n");
}

/* ===== 10. 链式会话（ENDING 期间换人）：旧结算 + 新会话恒非幽灵 ===== */
static void test_chained(void)
{
    printf("[chained]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;

    feed_id(&st, 0, 1, 100, 100, &o);  CHECK(o.evt == TS_EVT_DOWN, "s1 DOWN");
    feed_id(&st, 0, 0, 0, 0, &o);      CHECK(o.evt == TS_EVT_NONE, "grace arm");
    {
        /* 宽限内换 id：旧会话结算 + 链式新会话（本拍只出 UP）*/
        ts_sample_t s;
        memset(&s, 0, sizeof(s));
        s.touched = 1; s.valid = 1; s.contacts = 1; s.touch_id = 1;
        s.x = 300; s.y = 300;
        ts_feed(&st, &s, g_now, &o);
        g_now += 10;
    }
    CHECK(o.evt == TS_EVT_UP_GHOST, "old short session settled ghost");
    feed_id(&st, 1, 1, 300, 300, &o);  CHECK(o.evt == TS_EVT_MOVE, "chained session alive");
    lift(&st, &o);                     CHECK(o.evt == TS_EVT_UP_OK, "chained UP_OK (never ghost)");
    printf("  chained done\n");
}

/* ===== 11. raw 级注入走全链（T-13）：raw→map→session ===== */
static void test_raw_inject_chain(void)
{
    printf("[raw_inject_chain]\n");
    ts_state_t st;
    ts_init(&st);
    ts_out_t o;
    /* raw(100,200) → 屏 (200,100)；raw(110,210) → 屏 (210,110) */
    feed_raw(&st, 100, 200, 1, &o);  CHECK(o.evt == TS_EVT_DOWN, "raw DOWN");
    CHECK(o.x == 200 && o.y == 100, "raw mapped through chain");
    feed_raw(&st, 110, 210, 1, &o);  CHECK(o.evt == TS_EVT_MOVE, "raw MOVE");
    CHECK(o.x == 210 && o.y == 110, "raw mapped move");
    lift(&st, &o);                   CHECK(o.evt == TS_EVT_UP_OK, "raw UP_OK");
    printf("  raw_inject_chain done\n");
}

int main(void)
{
    test_map_regression();
    test_real_vs_ghost();
    test_ghost_between_real();
    test_flip_instant_click();
    test_multipoint();
    test_contact_takeover();
    test_lift_grace();
    test_badsample();
    test_insession_jump();
    test_inject_exempt();
    test_chained();
    test_raw_inject_chain();

    printf("===================\n");
    if (g_fail == 0) { printf("test_touch_session: PASS\n"); return 0; }
    printf("test_touch_session: FAIL (%d)\n", g_fail);
    return 1;
}
