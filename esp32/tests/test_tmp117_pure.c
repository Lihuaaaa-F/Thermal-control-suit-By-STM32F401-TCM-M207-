/**
 * test_tmp117_pure.c — TMP117 纯逻辑 PC 单测（R1c；L1 非最终验收依据）
 * 覆盖：raw→°C 锚点/负值；健康机 10 错沿/3 成恢复/报警沿计数；均值窗口回绕。
 * 健康机为 Q5 逐字提取件——本测试向量与 project_tcs test_tmp117.c 同源语义。
 */
#include <stdio.h>
#include "tmp117_pure.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { printf("FAIL: %s\n", name); g_fail++; } \
} while (0)

static int g_raise_cnt;
static void raise_stub(void) { g_raise_cnt++; }

int main(void)
{
    /* raw→°C */
    CHECK(tmp117_raw_to_c(0) == 0.0f, "raw 0");
    CHECK(tmp117_raw_to_c(128) > 0.999f && tmp117_raw_to_c(128) < 1.001f, "raw 128 -> 1.0");
    CHECK(tmp117_raw_to_c(-128) < -0.999f && tmp117_raw_to_c(-128) > -1.001f, "raw -128 -> -1.0");
    CHECK(tmp117_raw_to_c(3200) > 24.99f && tmp117_raw_to_c(3200) < 25.01f, "raw 3200 -> 25.0");
    CHECK(tmp117_raw_to_c(32767) > 255.9f, "raw max 255.99");

    /* 健康机：初始 ok；9 错不动；第 10 错掉线沿报警一次 */
    tmp117_health_t h;
    tmp117_health_init(&h);
    CHECK(h.ok == 1, "init ok");
    for (int i = 0; i < 9; i++) tmp117_health_step(&h, 0, raise_stub);
    CHECK(h.ok == 1 && g_raise_cnt == 0, "9 errs no trip");
    tmp117_health_step(&h, 0, raise_stub);
    CHECK(h.ok == 0 && g_raise_cnt == 1, "10th err trips + 1 raise");

    /* 错误中穿插成功：成功清错误计数（间歇接触不误判）*/
    tmp117_health_init(&h);
    g_raise_cnt = 0;
    for (int i = 0; i < 9; i++) tmp117_health_step(&h, 0, raise_stub);
    tmp117_health_step(&h, 1, raise_stub);
    for (int i = 0; i < 9; i++) tmp117_health_step(&h, 0, raise_stub);
    CHECK(h.ok == 1 && g_raise_cnt == 0, "interleaved ok resets err run");

    /* 掉线后 2 成不恢复，第 3 成恢复数据；恢复沿不再 raise（报警粘滞）*/
    tmp117_health_init(&h);
    g_raise_cnt = 0;
    for (int i = 0; i < 10; i++) tmp117_health_step(&h, 0, raise_stub);
    CHECK(g_raise_cnt == 1, "one raise on trip");
    tmp117_health_step(&h, 1, raise_stub);
    tmp117_health_step(&h, 1, raise_stub);
    CHECK(h.ok == 0, "2 oks still down");
    tmp117_health_step(&h, 1, raise_stub);
    CHECK(h.ok == 1 && g_raise_cnt == 1, "3rd ok recovers, no extra raise");

    /* 均值窗口 */
    tmp117_avg_t a;
    tmp117_avg_init(&a);
    CHECK(tmp117_avg_mean(&a) == 0.0f, "empty mean 0");
    for (int i = 1; i <= 8; i++) tmp117_avg_push(&a, (float)i);
    CHECK(tmp117_avg_mean(&a) > 4.499f && tmp117_avg_mean(&a) < 4.501f, "avg 1..8");
    tmp117_avg_push(&a, 100.0f);
    /* 窗口内容 = {100,2,3,4,5,6,7,8} → (100+2+..+8)/8 = 135/8 = 16.875 */
    CHECK(tmp117_avg_mean(&a) > 16.87f && tmp117_avg_mean(&a) < 16.88f, "avg wrap 16.875");

    printf("test_tmp117_pure: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
