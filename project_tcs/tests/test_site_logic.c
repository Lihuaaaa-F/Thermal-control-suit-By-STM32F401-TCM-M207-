/* test_site_logic.c — S 线（现场收尾）抽出的纯逻辑单测
 * 1) 急停屏入口判定 estop_gate_step（estop_gate.c）
 *
 * 页面重建判据不在这里：U 线（2026-11-24）把"就地更新/重建判据"下沉到 App/lvgl_ui/ui_model.c，
 * 自带 tests/test_ui_model.c 与变异证明，本文件原来的 ui_page_state 部分随之删除。
 *
 * 规则见 estop_gate.h（A 首页卡死 / B 首页长按 / C 其它页面"界面没在处理这次按压"时长按）。
 * 判别性：把实现逐个改坏，这些用例必须变红——变异清单与运行在 tests/mutation_estop_gate.py
 * （run_all.sh 每次都跑），结果记录在 docs/evidence/S_线/。
 */
#include <stdio.h>
#include <string.h>
#include "estop_gate.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* ====== 1. 急停屏入口判定 ====== */
static estop_gate_state_t g;
static uint32_t g_now, g_beat, g_reads;

/* 一次新按压：状态清零，时基定在 t0，心跳定在 1000，读次数定在 5000。*/
static void press_at(uint32_t t0)
{
    memset(&g, 0, sizeof g);
    g_now = t0;
    g_beat = 1000u;
    g_reads = 5000u;
}

/* 模拟主循环一拍：先过 dt 毫秒。
 * alive = 本拍 LVGL 真正跑了 handler（心跳 +1）
 * seen  = 本拍 LVGL 的触摸读回调报告了按下（读次数 +1）
 * dlg   = 0 没有弹窗；1 "确认急停"弹窗；2 别的弹窗 */
static estop_gate_fire_t tick(uint8_t pressed, uint8_t alive, uint8_t seen, uint8_t dlg, uint8_t home, uint32_t dt)
{
    g_now += dt;
    if (alive) g_beat++;
    if (seen)  g_reads++;
    return estop_gate_step(&g, pressed, (uint8_t)(dlg == 1u), home, g_now, g_beat, g_reads);
}

/* 按 step_ms 一拍一拍地按住 total_ms，返回这段时间里触发的次数。*/
static int hold_for(uint8_t alive, uint8_t seen, uint8_t dlg, uint8_t home, uint32_t step_ms, uint32_t total_ms)
{
    int fires = 0;
    for (uint32_t t = 0; t < total_ms; t += step_ms) {
        if (tick(1, alive, seen, dlg, home, step_ms) != ESTOP_GATE_IDLE) fires++;
    }
    return fires;
}

static void test_no_press_is_idle(void)
{
    press_at(1000u);
    for (int i = 0; i < 20; i++) CHECK(tick(0, 1, 0, 0, 1, 10) == ESTOP_GATE_IDLE, "no press idle");
    CHECK(g.in_zone == 0, "no press not in zone");
}

/* B：首页、界面正常、按住 600ms → 直接触发（用户点名：按住 0.6 秒直接急停）*/
static void test_hold_fires_on_home(void)
{
    press_at(1000u);
    CHECK(tick(1, 1, 1, 0, 1, 0) == ESTOP_GATE_IDLE, "press registered idle");
    CHECK(g.in_zone == 1, "press in zone");
    for (int i = 0; i < 5; i++) {                       /* 100..500ms */
        CHECK(tick(1, 1, 1, 0, 1, 100) == ESTOP_GATE_IDLE, "hold<600 idle");
    }
    CHECK(tick(1, 1, 1, 0, 1, 100) == ESTOP_GATE_FIRE_HOLD, "hold=600 fires on home");
    for (int i = 0; i < 20; i++) CHECK(tick(1, 1, 1, 0, 1, 100) == ESTOP_GATE_IDLE, "after fire idle");
}

static void test_hold_threshold_boundary(void)
{
    press_at(0u);
    tick(1, 1, 1, 0, 1, 0);
    CHECK(hold_for(1, 1, 0, 1, 599, 599) == 0, "hold 599 no fire");
    press_at(0u);
    tick(1, 1, 1, 0, 1, 0);
    CHECK(hold_for(1, 1, 0, 1, 600, 600) == 1, "hold 600 fire once");
}

/* C：其它页面、界面在正常处理这次按压 → 绝不触发（调参页的"+"就在热区里）*/
static void test_other_page_handled_never_fires(void)
{
    press_at(0u);
    tick(1, 1, 1, 0, 0, 0);
    CHECK(hold_for(1, 1, 0, 0, 100, 5000) == 0, "other page, handled: hold 5s no fire");
    CHECK(tick(0, 1, 0, 0, 0, 10) == ESTOP_GATE_IDLE, "release on other page");
}

/* C：其它页面、LVGL 活着但触摸读回调一直没报告按下（indev 半死）→ 按住 600ms 触发，且只触发一次 */
static void test_other_page_indev_half_dead(void)
{
    press_at(0u);
    tick(1, 1, 0, 0, 0, 0);
    CHECK(hold_for(1, 0, 0, 0, 100, 500) == 0, "other page, unseen: hold 500 no fire");
    CHECK(tick(1, 0, 0, 0, 0, 100) == ESTOP_GATE_FIRE_HOLD_UNHANDLED, "other page, unseen: hold 600 fires");
    CHECK(hold_for(1, 0, 0, 0, 100, 3000) == 0, "other page, unseen: once only");
}

/* C：其它页面、LVGL 卡死 → 不是一按就停，而是按住 600ms 才停（一下误碰不停机）*/
static void test_other_page_dead_needs_hold(void)
{
    press_at(0u);
    CHECK(tick(1, 0, 0, 0, 0, 0) == ESTOP_GATE_IDLE, "other page dead: registered");
    CHECK(tick(1, 0, 0, 0, 0, 5) == ESTOP_GATE_IDLE, "other page dead: held 5 no fire (not immediate)");
    CHECK(tick(1, 0, 0, 0, 0, 95) == ESTOP_GATE_IDLE, "other page dead: held 100 idle");
    for (int i = 0; i < 4; i++) CHECK(tick(1, 0, 0, 0, 0, 100) == ESTOP_GATE_IDLE, "other page dead: 200..500 idle");
    CHECK(tick(1, 0, 0, 0, 0, 100) == ESTOP_GATE_FIRE_HOLD_UNHANDLED, "other page dead: held 600 fires");
    CHECK(hold_for(0, 0, 0, 0, 100, 2000) == 0, "other page dead: once");
}

static void test_other_page_threshold_boundary(void)
{
    press_at(0u);
    tick(1, 0, 0, 0, 0, 0);
    CHECK(hold_for(0, 0, 0, 0, 599, 599) == 0, "other page: hold 599 no fire");
    press_at(0u);
    tick(1, 0, 0, 0, 0, 0);
    CHECK(hold_for(0, 0, 0, 0, 600, 600) == 1, "other page: hold 600 fire once");
}

/* C 的判据不能被"读次数在登记之前就已经加过"骗过：
 * 登记之前 LVGL 读过 7 次按下，登记之后读回调死了、再也不变 → 仍然是"没在处理"，600ms 触发。*/
static void test_reads_changed_before_registration_only(void)
{
    press_at(0u);
    g_reads += 7u;
    tick(1, 1, 0, 0, 0, 0);
    CHECK(g.reads0 == 5007u, "reads0 captured after the earlier reads");
    CHECK(hold_for(1, 0, 0, 0, 100, 500) == 0, "reads stopped at registration: hold 500 no fire");
    CHECK(tick(1, 0, 0, 0, 0, 100) == ESTOP_GATE_FIRE_HOLD_UNHANDLED, "reads stopped at registration: hold 600 fires");
}

/* C：其它页面，按住期间读次数变过一次就算界面在处理 → 不触发（哪怕是最后 100ms 才变）*/
static void test_other_page_one_read_is_enough(void)
{
    press_at(0u);
    tick(1, 1, 0, 0, 0, 0);
    CHECK(hold_for(1, 0, 0, 0, 100, 500) == 0, "no reads yet: hold 500 no fire");
    CHECK(tick(1, 1, 1, 0, 0, 100) == ESTOP_GATE_IDLE, "one read at 600: handled, no fire");
    CHECK(hold_for(1, 0, 0, 0, 100, 5000) == 0, "after one read: hold 5s no fire");
}

/* C：其它页面，心跳没推进就算没在处理——哪怕读次数在变（互相矛盾的输入，卡死优先）*/
static void test_other_page_dead_wins_over_reads(void)
{
    press_at(0u);
    tick(1, 0, 1, 0, 0, 0);
    CHECK(hold_for(0, 1, 0, 0, 100, 500) == 0, "other page dead + reads changing: hold 500 no fire");
    CHECK(tick(1, 0, 1, 0, 0, 100) == ESTOP_GATE_FIRE_HOLD_UNHANDLED,
          "other page dead + reads changing: hold 600 fires");
}

/* 读次数 32 位回绕后"不等于"仍然成立 → 不会因为回绕被误判成"没在处理"*/
static void test_reads_wraparound(void)
{
    press_at(0u);
    g_reads = 0xFFFFFFFFu;
    tick(1, 1, 0, 0, 0, 0);
    CHECK(hold_for(1, 1, 0, 0, 100, 5000) == 0, "reads wrap: still handled, no fire");
}

/* A：首页、LVGL 卡死 → 过了宽限期就触发，不等 600ms；开着弹窗也一样（弹窗这条路已经不可用）*/
static void test_dead_fires_after_grace_on_home(void)
{
    press_at(0u);
    CHECK(tick(1, 0, 0, 0, 1, 0) == ESTOP_GATE_IDLE, "home dead: press registered");
    CHECK(tick(1, 0, 0, 0, 1, 4) == ESTOP_GATE_IDLE, "home dead: held 4 < grace idle");
    CHECK(tick(1, 0, 0, 0, 1, 1) == ESTOP_GATE_FIRE_DEAD, "home dead: held 5 fires");
    CHECK(tick(1, 0, 0, 0, 1, 10) == ESTOP_GATE_IDLE, "home dead: once");

    press_at(0u);
    CHECK(tick(1, 0, 0, 1, 1, 0) == ESTOP_GATE_IDLE, "home dead + estop dialog: registered");
    CHECK(tick(1, 0, 0, 1, 1, 5) == ESTOP_GATE_FIRE_DEAD, "home dead + estop dialog: fires");

    press_at(0u);
    CHECK(tick(1, 0, 0, 2, 1, 0) == ESTOP_GATE_IDLE, "home dead + other dialog: registered");
    CHECK(tick(1, 0, 0, 2, 1, 5) == ESTOP_GATE_FIRE_DEAD, "home dead + other dialog: fires");
}

/* B："确认急停"弹窗开着 → 长按让位（它本身就是确认入口）*/
static void test_estop_dialog_suppresses_hold(void)
{
    press_at(0u);
    tick(1, 1, 1, 1, 1, 0);
    CHECK(hold_for(1, 1, 1, 1, 100, 5000) == 0, "estop dialog open: hold 5s no fire");
    /* 弹窗关掉后，接着按住 → 立刻满足条件就触发（已按住的时长继续算）*/
    CHECK(tick(1, 1, 1, 0, 1, 10) == ESTOP_GATE_FIRE_HOLD, "estop dialog closed while held: fires");
}

/* B 的边界：首页、"确认急停"弹窗开着、界面活着、但触摸读回调不再报告按下（弹窗之后 indev 才死的）
 * → 仍然让位给弹窗，不能因为"界面没在处理这次按压"就走其它页面那条路（变异验证 G7 抓出来的盲区）*/
static void test_home_estop_dialog_wins_even_if_indev_dead(void)
{
    press_at(0u);
    tick(1, 1, 1, 1, 1, 0);                         /* 弹窗开着，这一拍读回调还正常 */
    CHECK(hold_for(1, 0, 1, 1, 100, 5000) == 0, "home + estop dialog + indev half-dead: hold 5s no fire");
    CHECK(g.fired == 0, "home + estop dialog + indev half-dead: not latched");
}

/* B：别的弹窗开着 → 不让位（急停不该被任何弹窗挡住）*/
static void test_other_dialog_does_not_block(void)
{
    press_at(0u);
    tick(1, 1, 1, 2, 1, 0);
    CHECK(hold_for(1, 1, 2, 1, 100, 500) == 0, "other dialog: hold 500 no fire");
    CHECK(tick(1, 1, 1, 2, 1, 100) == ESTOP_GATE_FIRE_HOLD, "other dialog: hold 600 fires");
}

static void test_grace_gives_lvgl_one_tick(void)
{
    /* watch 在 poll 之前调用：刚按下那一拍心跳还没来得及 +1，不能据此判死。*/
    press_at(0u);
    tick(1, 0, 0, 0, 1, 0);                                /* 登记时心跳 1000 */
    CHECK(g.beat0 == 1000u, "beat0 captured");
    /* 下一拍 poll 跑过了（心跳 1001），只过了 2ms：仍未到宽限，不下结论 */
    CHECK(tick(1, 1, 0, 0, 1, 2) == ESTOP_GATE_IDLE, "alive within grace idle");
    CHECK(hold_for(1, 0, 0, 1, 100, 500) == 0, "alive: held 502 no fire");
    /* 走到长按门限时必须是"长按"而不是"判死" */
    CHECK(tick(1, 1, 0, 0, 1, 100) == ESTOP_GATE_FIRE_HOLD, "alive: held 602 fires HOLD not DEAD");
}

/* 登记那一拍本身绝不能触发：此时 beat0 就是当前心跳，宽限期就是为了不在这一拍下结论。*/
static void test_registration_tick_never_fires(void)
{
    for (uint32_t t0 = 0; t0 < 3; t0++) {
        press_at(500u);
        CHECK(tick(1, 0, 0, 0, 1, 0) == ESTOP_GATE_IDLE, "registration tick idle (dead-looking, home)");
        press_at(500u);
        CHECK(tick(1, 0, 0, 0, 0, 0) == ESTOP_GATE_IDLE, "registration tick idle (dead-looking, other page)");
        press_at(500u);
        CHECK(tick(1, 1, 0, 0, 1, 0) == ESTOP_GATE_IDLE, "registration tick idle (alive)");
    }
}

static void test_alive_not_misjudged_dead(void)
{
    /* 心跳每拍都在走：按住到触发长按之前，一次都不能因"判死"触发。*/
    press_at(0u);
    tick(1, 1, 1, 0, 1, 0);
    int dead = 0;
    for (int i = 0; i < 5; i++) if (tick(1, 1, 1, 0, 1, 100) == ESTOP_GATE_FIRE_DEAD) dead++;
    CHECK(dead == 0, "alive never fires DEAD");
}

static void test_fire_once_per_press(void)
{
    press_at(0u);
    tick(1, 1, 1, 0, 1, 0);
    CHECK(hold_for(1, 1, 0, 1, 50, 5000) == 1, "5s hold fires exactly once");
    /* 松开再按：可以再次触发 */
    CHECK(tick(0, 1, 0, 0, 1, 10) == ESTOP_GATE_IDLE, "release");
    CHECK(hold_for(1, 1, 0, 1, 100, 700) == 1, "re-press fires again");
}

static void test_leave_zone_resets(void)
{
    press_at(0u);
    tick(1, 1, 1, 0, 1, 0);
    CHECK(hold_for(1, 1, 0, 1, 100, 300) == 0, "300ms no fire");
    CHECK(tick(0, 1, 0, 0, 1, 10) == ESTOP_GATE_IDLE, "leave zone");     /* 松手 / 滑出 / 读失败 */
    CHECK(g.in_zone == 0, "zone state cleared");
    CHECK(hold_for(1, 1, 0, 1, 100, 300) == 0, "re-press 300ms no fire (not cumulative)");
    CHECK(hold_for(1, 1, 0, 1, 100, 400) == 1, "re-press reaches 600 fires");
}

static void test_wraparound(void)
{
    press_at(0xFFFFFF00u);
    tick(1, 1, 1, 0, 1, 0);
    int fires = 0;
    for (int i = 0; i < 5; i++) fires += (tick(1, 1, 1, 0, 1, 100) != ESTOP_GATE_IDLE);
    CHECK(fires == 0, "wrap: held 500 no fire");
    CHECK(tick(1, 1, 1, 0, 1, 100) == ESTOP_GATE_FIRE_HOLD, "wrap: held 600 across wrap fires");

    press_at(0xFFFFFFFFu);
    CHECK(tick(1, 0, 0, 0, 1, 0) == ESTOP_GATE_IDLE, "wrap dead: registered");
    CHECK(tick(1, 0, 0, 0, 1, 5) == ESTOP_GATE_FIRE_DEAD, "wrap dead: held 5 across wrap fires");

    press_at(0xFFFFFF00u);
    tick(1, 0, 0, 0, 0, 0);
    CHECK(hold_for(0, 0, 0, 0, 100, 600) == 1, "wrap: other page dead, held 600 across wrap fires once");
}

static void test_page_switch_while_holding(void)
{
    press_at(0u);
    tick(1, 1, 1, 0, 0, 0);
    CHECK(hold_for(1, 1, 0, 0, 100, 1000) == 0, "holding on other page (handled): no fire");
    CHECK(tick(1, 1, 0, 0, 1, 10) == ESTOP_GATE_FIRE_HOLD, "arrive home while held>=600 fires");
}

int main(void)
{
    test_no_press_is_idle();
    test_hold_fires_on_home();
    test_hold_threshold_boundary();
    test_other_page_handled_never_fires();
    test_other_page_indev_half_dead();
    test_other_page_dead_needs_hold();
    test_other_page_threshold_boundary();
    test_reads_changed_before_registration_only();
    test_other_page_one_read_is_enough();
    test_other_page_dead_wins_over_reads();
    test_reads_wraparound();
    test_dead_fires_after_grace_on_home();
    test_estop_dialog_suppresses_hold();
    test_home_estop_dialog_wins_even_if_indev_dead();
    test_other_dialog_does_not_block();
    test_grace_gives_lvgl_one_tick();
    test_registration_tick_never_fires();
    test_alive_not_misjudged_dead();
    test_fire_once_per_press();
    test_leave_zone_resets();
    test_wraparound();
    test_page_switch_while_holding();
    printf("test_site_logic: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
