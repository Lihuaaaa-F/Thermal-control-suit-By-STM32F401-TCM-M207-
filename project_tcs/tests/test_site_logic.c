/* test_site_logic.c — S 线（现场收尾）抽出的纯逻辑单测
 * 1) 急停屏入口兜底判定 estop_gate_step（estop_gate.c）
 *
 * 页面重建判据不在这里：U 线（2026-11-24）把"就地更新/重建判据"下沉到 App/lvgl_ui/ui_model.c，
 * 自带 tests/test_ui_model.c 与变异证明，本文件原来的 ui_page_state 部分随之删除。
 *
 * 判别性（把实现改坏，下面这些用例必须变红——2026-11-24 逐个实际跑过，记录在
 * docs/evidence/S_线/B03_变异验证.txt）：
 *  M1 去掉 on_home 条件        → test_hold_only_on_home 红
 *  M2 去掉 !dialog_open 条件   → test_hold_suppressed_by_dialog 红
 *  M3 去掉 fired 自锁          → test_fire_once_per_press 红
 *  M4 宽限期改成 0             → test_registration_tick_never_fires / test_dead_fires_after_grace 红
 *  M5 宽限期改成 50            → test_dead_fires_after_grace / test_grace_gives_lvgl_one_tick 红
 *  M6 held >= 600 改成 > 600   → test_hold_threshold_boundary 红
 *  M7 lvgl_alive 取反          → test_dead_fires_after_grace / test_hold_fires_on_home 红
 *  M8 离开热区不复位           → test_leave_zone_resets 红
 */
#include <stdio.h>
#include <string.h>
#include "estop_gate.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* ====== 1. 急停兜底判定 ====== */
static estop_gate_state_t g;
static uint32_t g_now, g_beat;

/* 一次新按压：状态清零，时基定在 t0，心跳定在 1000。*/
static void press_at(uint32_t t0)
{
    memset(&g, 0, sizeof g);
    g_now = t0;
    g_beat = 1000u;
}

/* 模拟主循环一拍：先过 dt 毫秒；alive=本拍 LVGL 有没有真正跑 handler（跑则心跳 +1）。*/
static estop_gate_fire_t tick(uint8_t pressed, uint8_t alive, uint8_t dialog, uint8_t home, uint32_t dt)
{
    g_now += dt;
    if (alive) g_beat++;
    return estop_gate_step(&g, pressed, dialog, home, g_now, g_beat);
}

/* 按 step_ms 一拍一拍地按住 total_ms，返回这段时间里触发的次数。*/
static int hold_for(uint8_t alive, uint8_t dialog, uint8_t home, uint32_t step_ms, uint32_t total_ms)
{
    int fires = 0;
    for (uint32_t t = 0; t < total_ms; t += step_ms) {
        estop_gate_fire_t r = tick(1, alive, dialog, home, step_ms);
        if (r != ESTOP_GATE_IDLE) fires++;
    }
    return fires;
}

static void test_no_press_is_idle(void)
{
    press_at(1000u);
    for (int i = 0; i < 20; i++) CHECK(tick(0, 1, 0, 1, 10) == ESTOP_GATE_IDLE, "no press idle");
    CHECK(g.in_zone == 0, "no press not in zone");
}

static void test_hold_fires_on_home(void)
{
    press_at(1000u);
    /* 第一拍登记按压（此时心跳已被上一拍 poll 推进过）*/
    CHECK(tick(1, 1, 0, 1, 0) == ESTOP_GATE_IDLE, "press registered idle");
    CHECK(g.in_zone == 1, "press in zone");
    for (int i = 0; i < 5; i++) {                       /* 100..500ms */
        estop_gate_fire_t r = tick(1, 1, 0, 1, 100);
        CHECK(r == ESTOP_GATE_IDLE, "hold<600 idle");
    }
    CHECK(tick(1, 1, 0, 1, 100) == ESTOP_GATE_FIRE_HOLD, "hold=600 fires on home");   /* 600ms */
    for (int i = 0; i < 20; i++) CHECK(tick(1, 1, 0, 1, 100) == ESTOP_GATE_IDLE, "after fire idle");
}

static void test_hold_threshold_boundary(void)
{
    press_at(0u);
    tick(1, 1, 0, 1, 0);
    CHECK(hold_for(1, 0, 1, 599, 599) == 0, "hold 599 no fire");
    press_at(0u);
    tick(1, 1, 0, 1, 0);
    CHECK(hold_for(1, 0, 1, 600, 600) == 1, "hold 600 fire once");
}

static void test_hold_only_on_home(void)
{
    /* 2026-11-24 现场复查：调参页的"+"就在热区里，长按不能急停。*/
    press_at(0u);
    tick(1, 1, 0, 0, 0);
    CHECK(hold_for(1, 0, 0, 100, 5000) == 0, "non-home hold 5s no fire");
    CHECK(tick(0, 1, 0, 0, 10) == ESTOP_GATE_IDLE, "release on other page");
}

static void test_hold_suppressed_by_dialog(void)
{
    press_at(0u);
    tick(1, 1, 1, 1, 0);
    CHECK(hold_for(1, 1, 1, 100, 5000) == 0, "dialog open hold 5s no fire");
}

static void test_dead_fires_after_grace(void)
{
    press_at(0u);
    CHECK(tick(1, 0, 0, 0, 0) == ESTOP_GATE_IDLE, "dead: press registered");   /* 非首页也算 */
    CHECK(tick(1, 0, 0, 0, 4) == ESTOP_GATE_IDLE, "dead: held 4 < grace idle");
    CHECK(tick(1, 0, 0, 0, 1) == ESTOP_GATE_FIRE_DEAD, "dead: held 5 fires");
    CHECK(tick(1, 0, 0, 0, 10) == ESTOP_GATE_IDLE, "dead: once");
}

static void test_grace_gives_lvgl_one_tick(void)
{
    /* watch 在 poll 之前调用：刚按下那一拍心跳还没来得及 +1，不能据此判死。*/
    press_at(0u);
    tick(1, 0, 0, 1, 0);                                /* 登记时心跳 1000 */
    CHECK(g.beat0 == 1000u, "beat0 captured");
    /* 下一拍 poll 跑过了（心跳 1001），只过了 2ms：仍未到宽限，不下结论 */
    CHECK(tick(1, 1, 0, 1, 2) == ESTOP_GATE_IDLE, "alive within grace idle");
    CHECK(hold_for(1, 0, 1, 100, 500) == 0, "alive: held 502 no fire");
    /* 走到长按门限时必须是"长按"而不是"判死" */
    CHECK(tick(1, 1, 0, 1, 100) == ESTOP_GATE_FIRE_HOLD, "alive: held 602 fires HOLD not DEAD");
}

/* 登记那一拍本身绝不能触发：此时 beat0 就是当前心跳，宽限期就是为了不在这一拍下结论。*/
static void test_registration_tick_never_fires(void)
{
    for (uint32_t t0 = 0; t0 < 3; t0++) {
        press_at(500u);
        CHECK(tick(1, 0, 0, 1, 0) == ESTOP_GATE_IDLE, "registration tick idle (dead-looking)");
        press_at(500u);
        CHECK(tick(1, 1, 0, 1, 0) == ESTOP_GATE_IDLE, "registration tick idle (alive)");
    }
}

static void test_alive_not_misjudged_dead(void)
{
    /* 心跳每拍都在走：按住到触发长按之前，一次都不能因"判死"触发。*/
    press_at(0u);
    tick(1, 1, 0, 1, 0);
    int dead = 0;
    for (int i = 0; i < 5; i++) if (tick(1, 1, 0, 1, 100) == ESTOP_GATE_FIRE_DEAD) dead++;
    CHECK(dead == 0, "alive never fires DEAD");
}

static void test_fire_once_per_press(void)
{
    press_at(0u);
    tick(1, 1, 0, 1, 0);
    CHECK(hold_for(1, 0, 1, 50, 5000) == 1, "5s hold fires exactly once");
    /* 松开再按：可以再次触发 */
    CHECK(tick(0, 1, 0, 1, 10) == ESTOP_GATE_IDLE, "release");
    CHECK(hold_for(1, 0, 1, 100, 700) == 1, "re-press fires again");
}

static void test_leave_zone_resets(void)
{
    press_at(0u);
    tick(1, 1, 0, 1, 0);
    CHECK(hold_for(1, 0, 1, 100, 300) == 0, "300ms no fire");
    CHECK(tick(0, 1, 0, 1, 10) == ESTOP_GATE_IDLE, "leave zone");     /* 松手 / 滑出 / 读失败 */
    CHECK(g.in_zone == 0, "zone state cleared");
    CHECK(hold_for(1, 0, 1, 100, 300) == 0, "re-press 300ms no fire (not cumulative)");
    CHECK(hold_for(1, 0, 1, 100, 400) == 1, "re-press reaches 600 fires");   /* 300/400/500/600 */
}

static void test_wraparound(void)
{
    press_at(0xFFFFFF00u);
    tick(1, 1, 0, 1, 0);
    int fires = 0;
    for (int i = 0; i < 5; i++) fires += (tick(1, 1, 0, 1, 100) != ESTOP_GATE_IDLE);
    CHECK(fires == 0, "wrap: held 500 no fire");
    CHECK(tick(1, 1, 0, 1, 100) == ESTOP_GATE_FIRE_HOLD, "wrap: held 600 across wrap fires");

    press_at(0xFFFFFFFFu);
    CHECK(tick(1, 0, 0, 1, 0) == ESTOP_GATE_IDLE, "wrap dead: registered");
    CHECK(tick(1, 0, 0, 1, 5) == ESTOP_GATE_FIRE_DEAD, "wrap dead: held 5 across wrap fires");
}

static void test_page_switch_while_holding(void)
{
    press_at(0u);
    tick(1, 1, 0, 0, 0);
    CHECK(hold_for(1, 0, 0, 100, 1000) == 0, "holding on other page");
    CHECK(tick(1, 1, 0, 1, 10) == ESTOP_GATE_FIRE_HOLD, "arrive home while held>=600 fires");
}

int main(void)
{
    test_no_press_is_idle();
    test_hold_fires_on_home();
    test_hold_threshold_boundary();
    test_hold_only_on_home();
    test_hold_suppressed_by_dialog();
    test_dead_fires_after_grace();
    test_registration_tick_never_fires();
    test_grace_gives_lvgl_one_tick();
    test_alive_not_misjudged_dead();
    test_fire_once_per_press();
    test_leave_zone_resets();
    test_wraparound();
    test_page_switch_while_holding();
    printf("test_site_logic: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
