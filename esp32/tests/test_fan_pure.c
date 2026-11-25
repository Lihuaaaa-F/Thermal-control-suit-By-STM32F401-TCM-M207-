/* test_fan_pure.c — 风扇服务的纯逻辑（主机可测；S-G e2：X04-F-05 ②、M13-F-06）

 * fan_act_classify：执行器侧"命令-回读"——由命令占空比、LEDC 占空比回读、引脚电平采样判定
 *   引脚实际在做什么。能发现 ESP32 这一侧的输出通路故障（通道被改、引脚被占走、该恒高时恒低）；
 *   发现不了风扇本体（硬件上没有转速反馈）。
 * fan_weak_check：M13-F-06 方案② 的弱校验（TEC 持续输出而水温不动 → 可疑），只给判定。
 * 这两段实现是头文件里的 inline 函数，交付固件与主机用的是同一份代码。
 */
#include <stdio.h>
#include <string.h>
#include "fan_service.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static void test_duty_of_pct(void)
{
    CHECK(FAN_DUTY_FULL == 1023u, "LEDC 10-bit full scale is 1023");
    CHECK(fan_duty_of_pct(0) == 1023u, "0% -> duty full (pin constantly high = off)");
    CHECK(fan_duty_of_pct(100) == 0u, "100% -> duty 0 (pin constantly low = on)");
    CHECK(fan_duty_of_pct(50) == 511u, "50% -> 511");
    CHECK(fan_duty_of_pct(1) == 1012u && fan_duty_of_pct(99) == 10u, "1%/99% asymmetric by integer floor");
    CHECK(fan_duty_of_pct(101) == fan_duty_of_pct(100), ">100% clamps to 100%");
    for (int p = 0; p < 100; p++)
        CHECK(fan_duty_of_pct((uint8_t)p) >= fan_duty_of_pct((uint8_t)(p + 1)),
              "duty never increases with pct (low = on)");
}

static void test_act_classify(void)
{
    /* 命令 0%：占空比回读=满、引脚恒高 → 关 */
    CHECK(fan_act_classify(0, 1023u, 1023u, 1, 1) == FAN_ACT_OFF, "0% + high pin -> OFF");
    CHECK(fan_act_classify(0, 1023u, 1023u, 0, 1) == FAN_ACT_MISMATCH, "0% but pin toggling -> MISMATCH");
    CHECK(fan_act_classify(0, 1023u, 1023u, 0, 0) == FAN_ACT_MISMATCH,
          "0% but pin stuck low (fan driven while commanded off) -> MISMATCH");
    CHECK(fan_act_classify(0, 511u, 1023u, 1, 1) == FAN_ACT_MISMATCH,
          "0% but LEDC duty readback differs (channel reconfigured) -> MISMATCH");
    /* 命令 100%：占空比回读=0、引脚恒低 → 开 */
    CHECK(fan_act_classify(100, 0u, 1023u, 0, 0) == FAN_ACT_ON, "100% + low pin -> ON");
    CHECK(fan_act_classify(100, 0u, 1023u, 1, 1) == FAN_ACT_MISMATCH,
          "100% but pin stuck high (optocoupler not driven) -> MISMATCH");
    CHECK(fan_act_classify(100, 0u, 1023u, 0, 1) == FAN_ACT_MISMATCH, "100% but pin toggling -> MISMATCH");
    CHECK(fan_act_classify(100, 1023u, 1023u, 1, 1) == FAN_ACT_MISMATCH, "100% but duty readback = full -> MISMATCH");
    CHECK(fan_act_classify(101, 0u, 1023u, 0, 0) == FAN_ACT_ON, "101% clamps to 100%");
    /* 中间占空比：占空比回读一致即可；引脚在 25kHz 翻转，电平不逐点核对 */
    CHECK(fan_act_classify(50, 511u, 1023u, 0, 1) == FAN_ACT_PWM, "50% + matching duty -> PWM");
    CHECK(fan_act_classify(50, 511u, 1023u, 1, 1) == FAN_ACT_PWM, "50%: level samples all high is fine (PWM)");
    CHECK(fan_act_classify(50, 512u, 1023u, 0, 1) == FAN_ACT_MISMATCH, "50% but duty off by one -> MISMATCH");
    CHECK(fan_act_classify(1, 1012u, 1023u, 1, 1) == FAN_ACT_PWM, "1% + matching duty -> PWM");
    /* 非法回读 */
    CHECK(fan_act_classify(0, 2000u, 1023u, 1, 1) == FAN_ACT_MISMATCH, "duty readback above full scale -> MISMATCH");
    CHECK(fan_act_classify(0, 1023u, 0u, 1, 1) == FAN_ACT_MISMATCH, "zero full scale -> MISMATCH");
    /* 判定不是"永远一致"：命令与回读四种组合里，只有匹配的那种给 OFF */
    CHECK(fan_act_classify(100, 1023u, 1023u, 1, 1) == FAN_ACT_MISMATCH,
          "commanded ON, pin high and duty full: never reported consistent");
    CHECK(fan_act_classify(0, 0u, 1023u, 0, 0) == FAN_ACT_MISMATCH,
          "commanded OFF, pin low and duty 0: never reported consistent");
}

static void test_weak_check(void)
{
    fan_weak_ctx_t c;

    /* NULL 上下文不下结论 */
    CHECK(fan_weak_check(1, 30.0f, 1000000u, NULL, NULL) == 0, "NULL ctx -> 0");

    /* TEC 没在输出：永远不可疑，并把本轮清零 */
    memset(&c, 0, sizeof c);
    CHECK(fan_weak_check(0, 30.0f, 1000000u, NULL, &c) == 0, "TEC off -> 0");
    CHECK(c.on_since_ms == 0u, "TEC off resets the run");

    /* TEC 连续输出：5 分钟之内不下结论 */
    memset(&c, 0, sizeof c);
    CHECK(fan_weak_check(1, 30.0f, 1000u, NULL, &c) == 0, "first tick: start of run, no verdict");
    CHECK(c.on_since_ms == 1000u && c.temp_at_on_c > 29.99f && c.temp_at_on_c < 30.01f,
          "run start recorded");
    CHECK(fan_weak_check(1, 30.0f, 1000u + 299999u, NULL, &c) == 0, "1ms before 5 min: no verdict");
    /* 满 5 分钟、水温只动了 0.2°C：可疑 */
    CHECK(fan_weak_check(1, 30.2f, 1000u + 300000u, NULL, &c) == 1, "M13-F-06: 5 min of TEC on, +0.2C -> suspect");
    CHECK(fan_weak_check(1, 29.8f, 1000u + 300000u, NULL, &c) == 1, "5 min of TEC on, -0.2C -> suspect");
    /* 水温动了 0.6°C：不可疑（两个方向都算） */
    CHECK(fan_weak_check(1, 30.6f, 1000u + 300000u, NULL, &c) == 0, "5 min of TEC on, +0.6C -> not suspect");
    CHECK(fan_weak_check(1, 29.4f, 1000u + 300000u, NULL, &c) == 0, "5 min of TEC on, -0.6C -> not suspect");

    /* TEC 停一下再开：重新计时（不能把上一轮的时间算进来） */
    CHECK(fan_weak_check(0, 30.0f, 1000u + 300000u, NULL, &c) == 0, "TEC off mid-way");
    CHECK(fan_weak_check(1, 30.0f, 1000u + 300001u, NULL, &c) == 0, "TEC on again: new run");
    CHECK(fan_weak_check(1, 30.0f, 1000u + 300001u + 299999u, NULL, &c) == 0,
          "new run: 5 min counted from its own start");

    /* 时间基准未建立（now_ms=0）：不记起点、不下结论 */
    memset(&c, 0, sizeof c);
    CHECK(fan_weak_check(1, 30.0f, 0u, NULL, &c) == 0, "now_ms=0 at start -> no verdict");
    CHECK(c.on_since_ms == 0u, "now_ms=0: run start not recorded");
    CHECK(fan_weak_check(1, 30.0f, 500u, NULL, &c) == 0, "time base arrives: start of run");
    CHECK(c.on_since_ms == 500u, "run start taken from the first valid timestamp");

    /* 自定义阈值：10 s / 2°C */
    {
        fan_weak_cfg_t cfg = { 10000u, 2.0f };
        memset(&c, 0, sizeof c);
        (void)fan_weak_check(1, 30.0f, 0u, &cfg, &c);
        (void)fan_weak_check(1, 30.0f, 1000u, &cfg, &c);
        CHECK(fan_weak_check(1, 31.5f, 1000u + 10000u, &cfg, &c) == 1, "custom: 10 s, +1.5C -> suspect");
        CHECK(fan_weak_check(1, 32.5f, 1000u + 10000u, &cfg, &c) == 0, "custom: 10 s, +2.5C -> not suspect");
    }
}

int main(void)
{
    test_duty_of_pct();
    test_act_classify();
    test_weak_check();
    printf("test_fan_pure: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
