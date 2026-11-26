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
static int g_pass = 0;  /* X06-F-06：断言数基线门禁的取数口 */
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("  FAIL: %s (line %d)\n", msg, __LINE__); g_fail++; } else { g_pass++; } \
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

/* ====== 12. 读一拍（M02-F-02 / M02-F-01）：读失败绝不产出样本 =====
 * 总线桩：置了 *_fail 的那次寄存器读"失败"，并且**不动缓冲**——与 HAL 一致
 * （HAL_I2C_Mem_Read 失败时不写 pData），这样"失败后拿没初始化的缓冲去解析"在桩上
 * 表现为：解析出来的坐标/事件标志不是桩里准备的那组，而返回值仍然报成功。 */
typedef struct {
    uint8_t sta_fail, tp_fail;
    uint8_t sta;
    uint8_t tp[6];
    int     reads;
} bus_stub_t;

static int stub_rd(void *ctx, uint8_t reg, uint8_t *buf, uint8_t len)
{
    bus_stub_t *b = (bus_stub_t *)ctx;
    b->reads++;
    if (reg == FT5446_REG_NUM_FINGER && len == 1) {
        if (b->sta_fail) return -1;
        buf[0] = b->sta;
        return 0;
    }
    if (reg == FT5446_TP1_REG && len == 6) {
        if (b->tp_fail) return -1;
        for (uint8_t i = 0; i < 6; i++) buf[i] = b->tp[i];
        return 0;
    }
    return -1;
}

/* 第 1 触点：contact 事件，raw=(0x010, 0x020) → 屏 (0x020, 0x010) */
static void bus_stub_touch(bus_stub_t *b)
{
    memset(b, 0, sizeof(*b));
    b->sta = 1;
    b->tp[0] = 0x80 | 0x00; b->tp[1] = 0x10;     /* X_H X_L */
    b->tp[2] = 0x00;        b->tp[3] = 0x20;     /* Y_H Y_L（id=0）*/
    b->tp[4] = 0x05;        b->tp[5] = 0x06;     /* weight area */
}

static void test_read_ops_fail(void)
{
    printf("[read_ops_fail]\n");
    touch_event_t ev;
    bus_stub_t b;

    /* 触点状态读失败：不得报"合法无触摸"（旧行为：sta 留 0 → n=0 → valid=1 返回 0）*/
    bus_stub_touch(&b); b.sta_fail = 1;
    memset(&ev, 0xA5, sizeof(ev));
    CHECK(ft5446_read_ops(&ev, 0, stub_rd, &b) == -1, "status read fail -> rc=-1");
    CHECK(ev.valid == 0 && ev.touched == 0, "status read fail -> no sample");
    CHECK(ev.contacts == 0 && ev.touch_id == 0 && ev.weight == 0 && ev.area == 0, "status read fail -> fields zero");

    /* 触点状态读成功 n=1，紧跟的 6 字节读失败：不得解析（M02-F-02 幽灵 DOWN 的原型）*/
    bus_stub_touch(&b); b.tp_fail = 1;
    memset(&ev, 0xA5, sizeof(ev));
    CHECK(ft5446_read_ops(&ev, 0, stub_rd, &b) == -1, "tp1 read fail -> rc=-1");
    CHECK(ev.valid == 0 && ev.touched == 0, "tp1 read fail -> no ghost DOWN");
    CHECK(ev.contacts == 0 && ev.touch_id == 0 && ev.weight == 0 && ev.area == 0, "tp1 read fail -> fields zero");
    CHECK(b.reads == 2, "tp1 fail: status then tp1 read attempted");

    /* 正常一拍作对照：同样的数据，读成功就有样本（坐标=映射后）*/
    bus_stub_touch(&b);
    CHECK(ft5446_read_ops(&ev, 0, stub_rd, &b) == 0, "good read rc=0");
    CHECK(ev.valid == 1 && ev.touched == 1, "good read -> touched");
    CHECK(ev.x == 0x020 && ev.y == 0x010, "good read -> mapped (swap x/y)");
    CHECK(ev.contacts == 1 && ev.touch_id == 0 && ev.weight == 5 && ev.area == 6, "good read -> fields");

    /* 无触摸 n=0：合法样本（会话机据此结算抬起）*/
    memset(&b, 0, sizeof(b));
    CHECK(ft5446_read_ops(&ev, 0, stub_rd, &b) == 0 && ev.valid == 1 && ev.touched == 0, "n=0 valid no-touch");
    /* 本屏空闲态 TD_STATUS=0x0F：合法样本、无触摸（M0 实测口径，保持）*/
    b.sta = 0x0F;
    CHECK(ft5446_read_ops(&ev, 0, stub_rd, &b) == 0 && ev.valid == 1 && ev.touched == 0, "n=0x0F idle valid no-touch");
    b.sta = 0xFF;
    CHECK(ft5446_read_ops(&ev, 0, stub_rd, &b) == 0 && ev.valid == 1 && ev.touched == 0, "0xFF idle valid no-touch");
    /* 事件标志不是 contact：丢弃（返回 0 但 valid=0），总线算通 */
    bus_stub_touch(&b); b.tp[0] = 0x40;
    {
        uint32_t bad0 = g_ft5446_badsample_drops;
        CHECK(ft5446_read_ops(&ev, 0, stub_rd, &b) == 0 && ev.valid == 0 && ev.touched == 0, "non-contact dropped");
        CHECK(g_ft5446_badsample_drops == bad0 + 1, "non-contact counted");
    }
    printf("  read_ops_fail done\n");
}

/* 性质：任一次寄存器读失败 ⇒ 返回 -1 且样本无效；返回 0 ⇒ 两次读都成功。
 * 穷举状态字节 × 事件标志高 4 位 × 四种失败组合。 */
static void test_read_ops_property(void)
{
    printf("[read_ops_property]\n");
    touch_event_t ev;
    bus_stub_t b;
    int bad = 0;
    for (int sf = 0; sf < 2; sf++)
    for (int tf = 0; tf < 2; tf++)
    for (int hi = 0; hi < 16; hi++)
    for (int lo = 0; lo < 16; lo++) {
        memset(&b, 0, sizeof(b));
        b.sta_fail = (uint8_t)sf; b.tp_fail = (uint8_t)tf;
        b.sta = (uint8_t)(hi << 4 | lo);
        memset(b.tp, (uint8_t)(hi * 16 + lo), 6);
        b.tp[0] = (uint8_t)(hi << 4 | lo);
        memset(&ev, 0xA5, sizeof(ev));
        int rc = ft5446_read_ops(&ev, 0, stub_rd, &b);
        /* 第 1 触点只在触点数 1..5 时才读；其余情况 tp_fail 根本不会被触发 */
        int tp_needed = (lo >= 1 && lo <= 5);
        int any_fail = sf || (tf && tp_needed);
        if (any_fail) {
            if (!(rc == -1 && ev.valid == 0 && ev.touched == 0)) bad++;
        } else {
            if (!(rc == 0 && (ev.valid == 0 || ev.valid == 1))) bad++;
            if (rc == 0 && ev.valid == 1 && ev.touched == 1) {
                /* 有样本就必须是按桩里的数据解析出来的：contact 事件 + 触点数 1..5 */
                if (!((b.tp[0] & 0xF0) == 0x80 && lo >= 1 && lo <= 5)) bad++;
            }
        }
    }
    CHECK(bad == 0, "read failure never yields a sample (1024 combos)");
    printf("  read_ops_property done\n");
}

/* ====== 13. 在线判定（M02-F-04）：写成功 + 回读一致才算在，版本号不算凭据 ===== */
static void fill_probe(ft5446_probe_t *p)
{
    memset(p, 0, sizeof(*p));
    p->wr_ok = 0x0F;
    p->rd_ok = 0x03;
    p->thgroup = FT5446_THGROUP_VAL;
    p->period = FT5446_PERIOD_VAL;
    p->ver_ok = 1;
    p->ver[0] = 0x00; p->ver[1] = 0x01;      /* 本屏实测版本寄存器 = 0x0001 */
}

static void test_probe_online(void)
{
    printf("[probe_online]\n");
    ft5446_probe_t p;

    fill_probe(&p);
    CHECK(ft5446_probe_online(&p) == 1, "real panel: online");

    /* 全 0 读回（数据线常态低 / 假 ACK）：不在线——旧判定把版本 0x0000 当在线 */
    fill_probe(&p);
    p.thgroup = 0x00; p.period = 0x00; p.ver[0] = 0x00; p.ver[1] = 0x00;
    CHECK(ft5446_probe_online(&p) == 0, "all-zero readback -> offline");

    /* 版本寄存器是 0x0000 但回读一致：算在线（版本号只记录，不设门）*/
    fill_probe(&p); p.ver[0] = 0x00; p.ver[1] = 0x00;
    CHECK(ft5446_probe_online(&p) == 1, "version 0x0000 w/ echo -> online (version not a gate)");

    /* 任一配置写失败 → 不在线 */
    for (int bit = 0; bit < 4; bit++) {
        fill_probe(&p);
        p.wr_ok = (uint8_t)(0x0F & ~(1u << bit));
        CHECK(ft5446_probe_online(&p) == 0, "any config write failed -> offline");
    }
    /* 回读失败 / 不一致 → 不在线 */
    fill_probe(&p); p.rd_ok = 0x02;
    CHECK(ft5446_probe_online(&p) == 0, "thgroup readback failed -> offline");
    fill_probe(&p); p.rd_ok = 0x01;
    CHECK(ft5446_probe_online(&p) == 0, "period readback failed -> offline");
    fill_probe(&p); p.thgroup = FT5446_THGROUP_VAL + 1;
    CHECK(ft5446_probe_online(&p) == 0, "thgroup mismatch -> offline");
    fill_probe(&p); p.period = FT5446_PERIOD_VAL + 1;
    CHECK(ft5446_probe_online(&p) == 0, "period mismatch -> offline");
    /* 只有 0 值的两个寄存器回读为 0 不算证据：两个非 0 寄存器必须都对上 */
    fill_probe(&p); p.thgroup = 0;
    CHECK(ft5446_probe_online(&p) == 0, "thgroup 0 is not an echo");
    printf("  probe_online done\n");
}

/* ====== 14. 触摸链路健康/恢复状态机（M02-F-01）===== */

static int diff1(uint32_t a, uint32_t b) { return (a + 1u == b || a == b + 1u || a == b) ? 1 : 0; }

/* 模拟驱动层每拍一次：act = step(prev_ok); prev_ok = 执行(act)。
 * rec 记每种动作执行了几次、单拍最多几个动作、何时回到在线。 */
typedef struct {
    int bus_clear_ok, cfg_ok, verify_ok;
    int act_cnt[8];
    int multi_in_ms;             /* 动作发出后同一毫秒内又发出下一个动作的次数（必须恒 0）*/
    uint32_t online_ms;
    uint32_t last_act_ms[8];     /* 每种动作最近一次的时刻 */
    uint32_t min_gap[8];         /* 相邻两次同类动作的最小间隔 */
} rec_sim_t;

static void rec_reset(rec_sim_t *r, int bus_ok, int cfg_ok, int verify_ok)
{
    memset(r, 0, sizeof(*r));
    r->bus_clear_ok = bus_ok; r->cfg_ok = cfg_ok; r->verify_ok = verify_ok;
    r->online_ms = 0xFFFFFFFFu;
    for (int i = 0; i < 8; i++) r->min_gap[i] = 0xFFFFFFFFu;
}

/* 从 t0 起跑 n_ticks 拍（每拍 tick_ms）；回到在线时停并返回 1。prev 指针跨调用保存上一步结果。*/
static int rec_run(tp_link_t *st, rec_sim_t *r, int *prev_ok, uint32_t t0, int n_ticks, uint32_t tick_ms)
{
    for (int k = 0; k < n_ticks; k++) {
        uint32_t now = t0 + (uint32_t)k * tick_ms;
        tp_link_act_t act = ts_tp_link_step(st, *prev_ok, now);
        if (act != TP_ACT_NONE) {
            r->act_cnt[act]++;
            uint32_t gap = now - r->last_act_ms[act];
            if (r->last_act_ms[act] != 0 && gap < r->min_gap[act]) r->min_gap[act] = gap;
            r->last_act_ms[act] = now;
            switch (act) {
            case TP_ACT_BUS_CLEAR:  *prev_ok = r->bus_clear_ok; break;
            case TP_ACT_CFG:        *prev_ok = r->cfg_ok; break;
            case TP_ACT_VERIFY:     *prev_ok = r->verify_ok; break;
            default: *prev_ok = 1; break;
            }
            /* 动作执行完，同一毫秒里再问一次：不得连发下一步（"每拍只做一小步"由状态机自己保证）*/
            if (ts_tp_link_step(st, *prev_ok, now) != TP_ACT_NONE) r->multi_in_ms++;
        }
        if (st->state == TP_LINK_ONLINE) { r->online_ms = now; return 1; }
    }
    return 0;
}

static void test_tp_link_poll(void)
{
    printf("[tp_link_poll]\n");
    tp_link_t st;
    /* 一直成功：永不离线，连续失败 0 */
    ts_tp_link_init(&st, 1, 0);
    for (int i = 0; i < 10000; i++) CHECK(ts_tp_link_poll(&st, 1, (uint32_t)i * 10u) == 0, "ok poll never offline");
    CHECK(st.state == TP_LINK_ONLINE && st.streak == 0 && st.fail_total == 0, "all ok: streak 0");
    /* 1~2 次失败不离线；成功一次就清零（偶发 NAK 不该打断使用中的触摸）*/
    CHECK(ts_tp_link_poll(&st, 0, 100000) == 0 && st.streak == 1, "fail#1 stays online");
    CHECK(ts_tp_link_poll(&st, 0, 100010) == 0 && st.streak == 2, "fail#2 stays online");
    CHECK(ts_tp_link_poll(&st, 1, 100020) == 0 && st.streak == 0, "ok resets streak");
    CHECK(st.fail_total == 2, "fail_total counts every failure");
    /* 连续第 3 次失败：本次调用判离线 */
    CHECK(ts_tp_link_poll(&st, 0, 100030) == 0, "fail#1 again");
    CHECK(ts_tp_link_poll(&st, 0, 100040) == 0, "fail#2 again");
    CHECK(ts_tp_link_poll(&st, 0, 100050) == 1, "fail#3 -> offline now");
    CHECK(st.state == TP_LINK_WAIT && st.streak == 3 && st.t_offline_ms == 100050, "offline state");
    /* 离线期间的读失败只记账，不重复触发 */
    CHECK(ts_tp_link_poll(&st, 0, 100060) == 0 && st.state == TP_LINK_WAIT, "fail while offline no re-trigger");
    CHECK(ts_tp_link_poll(&st, 1, 100070) == 0 && st.state == TP_LINK_WAIT, "ok poll while offline stays offline");
    /* 饱和：streak 不回绕成 0（否则"连续失败 256 次"会被读成 0）*/
    for (int i = 0; i < 1000; i++) (void)ts_tp_link_poll(&st, 0, 100100u + (uint32_t)i);
    CHECK(st.streak == 255, "streak saturates at 255");
    printf("  tp_link_poll done\n");
}

static void test_tp_link_timeline(void)
{
    printf("[tp_link_timeline]\n");
    tp_link_t st;
    rec_sim_t r;
    int prev_ok = 0;

    /* 上电没起来（t=1000）：先退避 100ms，再开始第一轮 */
    ts_tp_link_init(&st, 0, 1000);
    CHECK(st.state == TP_LINK_WAIT && st.t_offline_ms == 1000, "init offline -> WAIT");
    rec_reset(&r, 1, 1, 1);
    CHECK(ts_tp_link_step(&st, prev_ok, 1099) == TP_ACT_NONE, "99ms: still backing off");
    CHECK(ts_tp_link_step(&st, prev_ok, 1100) == TP_ACT_BUS_CLEAR, "100ms: bus clear");
    CHECK(st.state == TP_LINK_BUS_CLEAR && st.recover_tries == 1, "round 1 started");
    CHECK(ts_tp_link_step(&st, 1, 1100) == TP_ACT_NONE, "same ms: result not consumed (one step per ms)");
    CHECK(ts_tp_link_step(&st, 1, 1101) == TP_ACT_PERIPH_INIT, "bus idle -> periph reinit (next ms)");
    prev_ok = 1;
    CHECK(ts_tp_link_step(&st, prev_ok, 1112) == TP_ACT_RST_LOW, "periph ok -> RST low");
    for (uint32_t t = 1113; t < 1132; t++)
        CHECK(ts_tp_link_step(&st, 1, t) == TP_ACT_NONE, "RST low: waiting 20ms (non-blocking)");
    CHECK(ts_tp_link_step(&st, 1, 1132) == TP_ACT_RST_HIGH, "20ms: RST release");
    for (uint32_t t = 1133; t < 1432; t++)
        CHECK(ts_tp_link_step(&st, 1, t) == TP_ACT_NONE, "ROM boot: waiting 300ms (non-blocking)");
    CHECK(ts_tp_link_step(&st, 1, 1431) == TP_ACT_NONE, "299ms: still waiting");
    CHECK(ts_tp_link_step(&st, 1, 1432) == TP_ACT_CFG, "300ms: config writes");
    prev_ok = 1;
    CHECK(ts_tp_link_step(&st, prev_ok, 1432) == TP_ACT_NONE, "same ms: cfg result not consumed");
    CHECK(ts_tp_link_step(&st, prev_ok, 1433) == TP_ACT_VERIFY, "config ok -> verify");
    prev_ok = 1;
    CHECK(ts_tp_link_step(&st, prev_ok, 1433) == TP_ACT_NONE, "same ms: verify result not consumed");
    CHECK(ts_tp_link_step(&st, prev_ok, 1434) == TP_ACT_NONE, "verify ok -> nothing more");
    CHECK(st.state == TP_LINK_ONLINE, "back online");
    CHECK(st.recoveries == 1 && st.recover_tries == 1, "recovery counted");
    CHECK(st.offline_last_ms == 434 && st.offline_max_ms == 434, "offline duration 1000..1434");
    CHECK(st.streak == 0, "online: streak cleared");
    /* 在线之后 step 恒为 NONE */
    CHECK(ts_tp_link_step(&st, 1, 1470) == TP_ACT_NONE && ts_tp_link_step(&st, 0, 1480) == TP_ACT_NONE,
          "online: step idle");

    /* 整段按 10ms 节拍跑一遍：一拍最多一个动作；排线插回后 ≤ 退避 + 复位延时 + 几拍就回到在线 */
    ts_tp_link_init(&st, 0, 0);
    rec_reset(&r, 1, 1, 1);
    prev_ok = 0;
    CHECK(rec_run(&st, &r, &prev_ok, 0, 1000, 10) == 1, "recovers in 1000 ticks");
    CHECK(r.multi_in_ms == 0, "never a second action in the same ms");
    CHECK(r.act_cnt[TP_ACT_BUS_CLEAR] == 1 && r.act_cnt[TP_ACT_PERIPH_INIT] == 1, "one round only");
    CHECK(r.act_cnt[TP_ACT_RST_LOW] == 1 && r.act_cnt[TP_ACT_RST_HIGH] == 1, "RST pulse once");
    CHECK(r.act_cnt[TP_ACT_CFG] == 1 && r.act_cnt[TP_ACT_VERIFY] == 1, "cfg+verify once");
    CHECK(r.online_ms >= 100u + 20u + 300u, "cannot be faster than reset delays");
    CHECK(r.online_ms <= 100u + 20u + 300u + 5u * 10u, "recovery latency bounded");
    printf("  tp_link_timeline done\n");
    printf("    online at %u ms\n", (unsigned)r.online_ms);
}

static void test_tp_link_stuck_bus(void)
{
    printf("[tp_link_stuck_bus]\n");
    tp_link_t st;
    rec_sim_t r;
    int prev_ok = 0;

    /* 两线被拉死（短路 / 器件死拉）：清总线过不了 → 退避重来。
     * 整个过程里除清总线外不得发出任何要碰 I2C 外设 / 触摸 IC 的动作
     * （卡死总线上每次 HAL 事务都要等 25ms 超时）。*/
    ts_tp_link_init(&st, 0, 0);
    rec_reset(&r, 0, 1, 1);
    prev_ok = 0;
    CHECK(rec_run(&st, &r, &prev_ok, 0, 20000, 10) == 0, "stuck bus never comes online");
    CHECK(st.state != TP_LINK_ONLINE && st.recoveries == 0, "no false online");
    CHECK(r.act_cnt[TP_ACT_BUS_CLEAR] > 100, "keeps trying bus clear");
    CHECK(r.act_cnt[TP_ACT_PERIPH_INIT] == 0 && r.act_cnt[TP_ACT_RST_LOW] == 0 &&
          r.act_cnt[TP_ACT_RST_HIGH] == 0 && r.act_cnt[TP_ACT_CFG] == 0 && r.act_cnt[TP_ACT_VERIFY] == 0,
          "stuck bus: no periph/IC/config traffic");
    CHECK(r.min_gap[TP_ACT_BUS_CLEAR] >= TP_RETRY_DELAY_MS, "retry cadence >= backoff");
    CHECK(r.multi_in_ms == 0, "one action per ms");
    CHECK(st.recover_tries == (uint32_t)r.act_cnt[TP_ACT_BUS_CLEAR], "tries counted per round");

    /* 排线没插（器件不应答）：每轮走到写配置就失败退避；永远不回到在线 */
    ts_tp_link_init(&st, 0, 0);
    rec_reset(&r, 1, 0, 1);
    prev_ok = 0;
    CHECK(rec_run(&st, &r, &prev_ok, 0, 20000, 10) == 0, "absent device never online");
    CHECK(st.recoveries == 0, "absent: no recovery counted");
    /* 每一轮都走到写配置才失败（模拟结束时可能有半轮在进行：差 1 以内）*/
    CHECK(diff1(r.act_cnt[TP_ACT_BUS_CLEAR], r.act_cnt[TP_ACT_CFG]), "every round reaches CFG and fails there");
    CHECK(r.act_cnt[TP_ACT_VERIFY] == 0, "cfg failed -> no verify");
    CHECK(r.min_gap[TP_ACT_BUS_CLEAR] >= TP_RETRY_DELAY_MS + TP_RST_LOW_MS + TP_RST_HIGH_MS,
          "round length = backoff + reset delays");

    /* 器件应答但回读不一致（假 ACK / 数据相位坏）：每轮在核对处失败 */
    ts_tp_link_init(&st, 0, 0);
    rec_reset(&r, 1, 1, 0);
    prev_ok = 0;
    CHECK(rec_run(&st, &r, &prev_ok, 0, 20000, 10) == 0, "bad echo never online");
    CHECK(diff1(r.act_cnt[TP_ACT_CFG], r.act_cnt[TP_ACT_VERIFY]) && r.act_cnt[TP_ACT_VERIFY] > 100,
          "every round reaches VERIFY and fails there");

    /* 外设重配失败（HAL 返回错）：退避，不去碰触摸 IC */
    ts_tp_link_init(&st, 0, 0);
    rec_reset(&r, 1, 1, 1);
    prev_ok = 0;
    CHECK(ts_tp_link_step(&st, prev_ok, 0) == TP_ACT_NONE, "backoff");
    CHECK(ts_tp_link_step(&st, prev_ok, 100) == TP_ACT_BUS_CLEAR, "bus clear");
    CHECK(ts_tp_link_step(&st, 1, 110) == TP_ACT_PERIPH_INIT, "periph");
    CHECK(ts_tp_link_step(&st, 0, 120) == TP_ACT_NONE, "periph failed -> backoff");
    CHECK(st.state == TP_LINK_WAIT, "periph failed -> WAIT");
    CHECK(ts_tp_link_step(&st, 0, 219) == TP_ACT_NONE && ts_tp_link_step(&st, 0, 220) == TP_ACT_BUS_CLEAR,
          "retry after backoff");
    printf("  tp_link_stuck_bus done\n");
}

static void test_tp_link_replug(void)
{
    printf("[tp_link_replug]\n");
    tp_link_t st;
    rec_sim_t r;
    int prev_ok = 0;

    /* 在线 → 连续 3 次失败掉线 → 器件一直不在 → 1.5s 后"插回" → 一轮内回到在线 */
    ts_tp_link_init(&st, 1, 0);
    for (int i = 0; i < 3; i++) (void)ts_tp_link_poll(&st, 0, 10u * (uint32_t)i);
    CHECK(st.state == TP_LINK_WAIT && st.t_offline_ms == 20, "went offline at 20ms");
    rec_reset(&r, 1, 0, 0);       /* 器件不在 */
    CHECK(rec_run(&st, &r, &prev_ok, 30, 150, 10) == 0, "still offline before replug");
    uint32_t t_replug = 30u + 150u * 10u;
    uint32_t tries0 = st.recover_tries;
    r.cfg_ok = 1; r.verify_ok = 1;   /* 插回 */
    CHECK(rec_run(&st, &r, &prev_ok, t_replug, 200, 10) == 1, "online after replug");
    /* 插回时可能正有一轮做到一半（复位已经发过）：只约束上界，不约束"必须重新复位一次" */
    CHECK(r.online_ms - t_replug <= TP_RETRY_DELAY_MS + TP_RST_LOW_MS + TP_RST_HIGH_MS + 5u * 10u,
          "after replug: bounded latency");
    CHECK(st.recoveries == 1 && st.recover_tries >= tries0, "recovery counted once");
    CHECK(st.offline_last_ms == r.online_ms - 20u && st.offline_max_ms == st.offline_last_ms,
          "offline duration = replug recovery");

    /* 再掉一次再恢复：计数累加，最长离线时长取大者 */
    for (int i = 0; i < 3; i++) (void)ts_tp_link_poll(&st, 0, 5000u + 10u * (uint32_t)i);
    rec_reset(&r, 1, 1, 1);
    prev_ok = 0;
    CHECK(rec_run(&st, &r, &prev_ok, 5100, 200, 10) == 1, "second recovery");
    CHECK(st.recoveries == 2, "recoveries accumulate");
    CHECK(st.offline_max_ms >= st.offline_last_ms, "max offline duration kept");

    /* 毫秒计数回绕：在 0xFFFFFF00 附近掉线，跨回绕恢复 */
    ts_tp_link_init(&st, 0, 0xFFFFFF00u);
    rec_reset(&r, 1, 1, 1);
    prev_ok = 0;
    CHECK(rec_run(&st, &r, &prev_ok, 0xFFFFFF00u, 100, 10) == 1, "recovers across tick wrap");
    CHECK(st.offline_last_ms == r.online_ms - 0xFFFFFF00u, "wrap: duration by difference");
    CHECK(st.offline_last_ms >= 420u && st.offline_last_ms <= 480u, "wrap: duration is sane");

    /* 台架钩子：在线时强制走一遍恢复流程，不记失败 */
    ts_tp_link_init(&st, 1, 9000);
    CHECK(ts_tp_link_step(&st, 1, 9000) == TP_ACT_NONE, "online idle");
    ts_tp_link_force_offline(&st, 9000);
    CHECK(st.state == TP_LINK_WAIT && st.t_offline_ms == 9000, "forced offline");
    CHECK(st.streak == 0 && st.fail_total == 0, "forced offline is not a failure");
    ts_tp_link_force_offline(&st, 9001);
    CHECK(st.t_offline_ms == 9000, "force while offline is a no-op");
    rec_reset(&r, 1, 1, 1);
    prev_ok = 0;
    CHECK(rec_run(&st, &r, &prev_ok, 9010, 200, 10) == 1 && st.recoveries == 1, "forced offline recovers");
    printf("  tp_link_replug done\n");
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

    /* S-G h1a（审查 M02-F-01/02/04）：读失败不出样本、在线判定、链路健康/恢复 */
    test_read_ops_fail();
    test_read_ops_property();
    test_probe_online();
    test_tp_link_poll();
    test_tp_link_timeline();
    test_tp_link_stuck_bus();
    test_tp_link_replug();

    printf("===================\n");
    if (g_fail == 0) { printf("test_touch_session: PASS=%d FAIL=0\n", g_pass); return 0; }
    printf("test_touch_session: FAIL (%d)\n", g_fail);
    return 1;
}
