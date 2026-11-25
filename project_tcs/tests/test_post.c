/* test_post.c — H6 PC 单测：合理性滤波 + 布防三态机 + POST 聚合（向量含边沿）
 * 向量口径（任务书 v7.3 §2 H6/H6.7）：
 *   体温 25~45°C、|Δ|>2°C/s 不可信；M207 |Δ|>10°C/s 不可信；
 *   三态 UNARMED_ABSENT ⇄ ARMED_ONLINE ⇄ LOST_ALARM；上线补入=连续 10 次健康采样+人工清。
 */
#include <stdio.h>
#include "post.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* ====== 1. 合理性滤波 ====== */
static void test_body_sanity(void)
{
    /* 范围边沿：25.0/45.0 过，24.9/45.1 不过 */
    CHECK(post_body_sanity_ok(0, 25.0f, 0) == 1, "body 25.0 in range");
    CHECK(post_body_sanity_ok(0, 45.0f, 0) == 1, "body 45.0 in range");
    CHECK(post_body_sanity_ok(0, 24.9f, 0) == 0, "body 24.9 out of range");
    CHECK(post_body_sanity_ok(0, 45.1f, 0) == 0, "body 45.1 out of range");
    /* 首样本（dt=0）只查范围 */
    CHECK(post_body_sanity_ok(0, 36.8f, 0) == 1, "body first sample range only");
    /* 速率边沿：|Δ|=2.0°C/s 过（≤），2.1°C/s 不过 */
    CHECK(post_body_sanity_ok(36.0f, 38.0f, 1.0f) == 1, "body +2.0C/s pass");
    CHECK(post_body_sanity_ok(36.0f, 34.0f, 1.0f) == 1, "body -2.0C/s pass");
    CHECK(post_body_sanity_ok(36.0f, 38.1f, 1.0f) == 0, "body +2.1C/s drop");
    CHECK(post_body_sanity_ok(36.0f, 33.9f, 1.0f) == 0, "body -2.1C/s drop");
    /* dt 折算：0.1s 内跳 0.25°C（2.5°C/s）不过；跳 0.19°C（1.9°C/s）过 */
    CHECK(post_body_sanity_ok(36.0f, 36.25f, 0.1f) == 0, "body 2.5C/s drop");
    CHECK(post_body_sanity_ok(36.0f, 36.19f, 0.1f) == 1, "body 1.9C/s pass");
}

static void test_m207_sanity(void)
{
    /* 首样本放行 */
    CHECK(post_m207_sanity_ok(0, 80.0f, 0) == 1, "m207 first sample pass");
    /* |Δ|=10°C/s 过，10.1 不过（同通道轮询周期 1s 口径）*/
    CHECK(post_m207_sanity_ok(25.0f, 35.0f, 1.0f) == 1, "m207 +10C/s pass");
    CHECK(post_m207_sanity_ok(25.0f, 14.9f, 1.0f) == 0, "m207 -10.1C/s drop");
    CHECK(post_m207_sanity_ok(25.0f, 35.1f, 1.0f) == 0, "m207 +10.1C/s drop");
}

/* ====== 2. 布防三态机 ====== */
static void test_arm_fsm(void)
{
    post_arm_ctx_t c;

    /* auto（0x08=0）：POST 未过/源不健康 → 停在 UNARMED（未接线安静，附录A-3）*/
    post_arm_init(&c);
    CHECK(c.state == POST_ARM_UNARMED_ABSENT, "boot unarmed");
    CHECK(post_arm_step(&c, 0, 0, 0, 0) == POST_ARM_UNARMED_ABSENT, "auto no-post stays unarmed");
    CHECK(post_arm_step(&c, 0, 1, 0, 1) == POST_ARM_UNARMED_ABSENT, "auto unhealthy stays unarmed");

    /* auto：POST 过 + 源健康 → ARMED_ONLINE */
    CHECK(post_arm_step(&c, 0, 1, 1, 1) == POST_ARM_ARMED_ONLINE, "auto post+healthy -> ARMED");

    /* 失联沿：ARMED → LOST_ALARM（源掉线一拍即迁移）*/
    CHECK(post_arm_step(&c, 0, 1, 0, 1) == POST_ARM_LOST_ALARM, "ARMED -> LOST on loss");

    /* LOST：健康采样计数（未到 10 不回；未人工清不回）*/
    for (int i = 0; i < POST_ARM_RECOVER_RUN - 1; i++)
        CHECK(post_arm_step(&c, 0, 1, 1, 0) == POST_ARM_LOST_ALARM, "LOST run<10 no recovery");
    /* 中途再掉线 → 计数清零（边沿）*/
    CHECK(post_arm_step(&c, 0, 1, 0, 0) == POST_ARM_LOST_ALARM, "LOST relink resets run");
    for (int i = 0; i < 3; i++)
        (void)post_arm_step(&c, 0, 1, 1, 0);
    CHECK(post_arm_step(&c, 0, 1, 0, 0) == POST_ARM_LOST_ALARM, "run cleared again");
    /* 满 10 次健康但未 ack → 仍 LOST */
    for (int i = 0; i < POST_ARM_RECOVER_RUN; i++)
        (void)post_arm_step(&c, 0, 1, 1, 0);
    CHECK(post_arm_step(&c, 0, 1, 1, 0) == POST_ARM_LOST_ALARM, "healthy 10 no-ack stays LOST");
    /* 重新计数（上一步未清）→ ack + 10 次健康 → ARMED（上线补入）*/
    for (int i = 0; i < POST_ARM_RECOVER_RUN; i++)
        CHECK(post_arm_step(&c, 0, 1, 1, 1) >= 0, "counting");
    CHECK(c.state == POST_ARM_ARMED_ONLINE, "ack + 10 healthy -> ARMED");

    /* 强制布防（0x08=1）：无视 POST/健康直接 ARMED；掉线仍进 LOST（FMEA 恒开）*/
    post_arm_init(&c);
    CHECK(post_arm_step(&c, 1, 0, 0, 0) == POST_ARM_ARMED_ONLINE, "force arm w/o post");
    CHECK(post_arm_step(&c, 1, 0, 0, 0) == POST_ARM_LOST_ALARM, "force arm loss -> LOST");
    /* 强制布防下：健康+ack 立即恢复（run 满）*/
    for (int i = 0; i < POST_ARM_RECOVER_RUN; i++) (void)post_arm_step(&c, 1, 0, 1, 1);
    CHECK(c.state == POST_ARM_ARMED_ONLINE, "force arm recover");

    /* 强制不布防（0x08=2）：任何态 → UNARMED（安静）*/
    post_arm_init(&c);
    (void)post_arm_step(&c, 0, 1, 1, 1);
    CHECK(c.state == POST_ARM_ARMED_ONLINE, "pre check armed");
    CHECK(post_arm_step(&c, 2, 1, 1, 1) == POST_ARM_UNARMED_ABSENT, "mode08=2 -> UNARMED");
    (void)post_arm_step(&c, 0, 1, 1, 1);
    (void)post_arm_step(&c, 0, 1, 0, 0);   /* LOST */
    CHECK(post_arm_step(&c, 2, 1, 1, 1) == POST_ARM_UNARMED_ABSENT, "mode08=2 from LOST -> UNARMED");

    /* post_armed()/commit 映射 */
    post_arm_commit(POST_ARM_UNARMED_ABSENT);
    CHECK(post_armed() == 0, "commit unarmed -> post_armed=0");
    post_arm_commit(POST_ARM_ARMED_ONLINE);
    CHECK(post_armed() == 1, "commit armed -> post_armed=1");
    post_arm_commit(POST_ARM_LOST_ALARM);
    CHECK(post_armed() == 1, "commit LOST -> post_armed=1 (FMEA stays on)");
    post_arm_commit(POST_ARM_UNARMED_ABSENT);
}

/* ====== 3. POST 结果聚合 ====== */
static void test_aggregate(void)
{
    post_reset_results();
    CHECK(post_all_pass() == 0, "undecided != pass");
    for (int i = 0; i < POST_ITEM_N - 1; i++) post_set_result((uint8_t)i, 1);
    CHECK(post_all_pass() == 0, "5/6 decided != pass");
    post_set_result(POST_ITEM_N - 1, 1);
    CHECK(post_all_pass() == 1, "6/6 pass");
    post_set_result(POST_ITEM_M207, 0);
    CHECK(post_all_pass() == 0, "any fail != pass");
    CHECK(post_item_ok(POST_ITEM_M207) == 0 && post_item_decided(POST_ITEM_M207) == 1,
          "item state visible");
    CHECK(post_item_decided(99) == 0 && post_item_ok(99) == 0, "bounds safe");
    post_reset_results();
    CHECK(post_item_decided(POST_ITEM_M207) == 0, "reset clears");
}

int main(void)
{
    test_body_sanity();
    test_m207_sanity();
    test_arm_fsm();
    test_aggregate();
    printf("test_post: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
