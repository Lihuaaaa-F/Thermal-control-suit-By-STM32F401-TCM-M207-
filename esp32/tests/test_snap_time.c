/**
 * test_snap_time.c — M11-F-13 / M08-F-07 的主机用例（S 线 e1，2026-11-26）。
 *
 * 判别性来自第 1 节：把修复前的写法（先截 int32 再除 / int32 相减）逐字抄进来，
 * 同一组时刻上它报错，新实现报对。第 2~4 节是边界：2^31 微秒、32 位 ms 回绕、
 * 71.6 分钟、tick 回绕、迟到钳位。
 *
 * 编译：esp32/tests/run_m207_tests.sh 的 snaptime 一行（main/snap_time.c + 本文件）。
 */
#include <stdio.h>
#include <stdint.h>
#include "snap_time.h"

static int fails;
static int checks;

#define CHECK(cond, fmt, ...)                                              \
    do {                                                                   \
        checks++;                                                          \
        if (cond) {                                                        \
            printf("  ok   " fmt "\n", ##__VA_ARGS__);                     \
        } else {                                                           \
            printf("  FAIL " fmt "\n", ##__VA_ARGS__);                     \
            fails++;                                                       \
        }                                                                  \
    } while (0)

/* ---- 修复前的实现（原样，仅供对照；不得在生产代码里出现）---- */
static uint32_t old_ms_from_us(int64_t us)
{
    int32_t t0 = (int32_t)us;                 /* 截成 32 位 */
    return (uint32_t)(t0 / 1000);
}
static int32_t old_elapsed_us(int64_t t0_us, int64_t now_us)
{
    int32_t t0 = (int32_t)t0_us;
    return (int32_t)now_us - t0;              /* int32 相减 */
}

static void test_old_vs_new(void)
{
    printf("[1] 修复前后对照\n");
    /* 1a. 35.8 分钟 = 2^31 微秒 */
    const int64_t t_2p31 = (int64_t)1 << 31;
    uint32_t nu = snap_ms_from_us(t_2p31);
    uint32_t ou = old_ms_from_us(t_2p31);
    printf("      ms @2^31us: new=%lu old=%lu\n", (unsigned long)nu, (unsigned long)ou);
    CHECK(ou > 4000000000u, "修复前的 ms 在 2^31 处跳到约 4.29e9（红）");
    CHECK(nu == 2147483u, "修复后的 ms @2^31us = %lu（2147483，连续）", (unsigned long)nu);
    CHECK(snap_ms_from_us(t_2p31 - 1000) + 1u == nu, "跨 2^31 前后 ms 连续（差 1ms）");

    /* 1b. 间隔超过 35.8 分钟：修复前的 int32 相减得到负值（有符号溢出/负值） */
    int64_t a = 1000;
    int64_t b = t_2p31 + 5000;
    CHECK(old_elapsed_us(a, b) < 0, "修复前的 int32 差为负（红）：%ld", (long)old_elapsed_us(a, b));
    CHECK(snap_elapsed_us(a, b) == (uint32_t)(t_2p31 + 4000),
          "修复后的间隔 = %lu us（2^31+4000）",
          (unsigned long)snap_elapsed_us(a, b));
}

static void test_ms_boundaries(void)
{
    printf("[2] ms 字段：49.7 天回绕正常，35.8 分钟 / 71.6 分钟不跳变\n");
    CHECK(snap_ms_from_us(0) == 0u, "ms(0) = 0");
    CHECK(snap_ms_from_us(199999) == 199u, "ms(199999us) = %lu（截断）",
          (unsigned long)snap_ms_from_us(199999));
    /* 2148 s（修复前报告里的验证点）*/
    CHECK(snap_ms_from_us(2148LL * 1000000) == 2148000u, "ms(2148s) = %lu",
          (unsigned long)snap_ms_from_us(2148LL * 1000000));
    /* 71.6 分钟 = 2^32 微秒：int32 微秒计数器回绕处。修复前 (int32)2^32 = 0 → ms 跳回 0，
     * 修复后 ms 仍然是 4294967（连续）。 */
    {
        int64_t t_2p32 = (int64_t)1 << 32;
        uint32_t old_at = old_ms_from_us(t_2p32);
        uint32_t new_at = snap_ms_from_us(t_2p32);
        printf("      ms @2^32us: new=%lu old=%lu\n", (unsigned long)new_at, (unsigned long)old_at);
        CHECK(old_at == 0u, "修复前的 ms 在 71.6 分钟处跳回 0（红）");
        CHECK(new_at == 4294967u, "修复后的 ms @2^32us = %lu（4294967，连续）",
              (unsigned long)new_at);
        CHECK(snap_ms_from_us(t_2p32 - 1000000) + 1000u == new_at,
              "跨 71.6 分钟前后 ms 连续（差 1000ms）");
    }
    /* 49.7 天 = 2^32 毫秒：这才是 32 位 ms 字段本身的物理回绕（回到 0 是正确的） */
    {
        int64_t t_ms_wrap = (int64_t)4294967296LL * 1000LL;
        CHECK(snap_ms_from_us(t_ms_wrap) == 0u, "ms @49.7 天回绕到 0（32 位字段本身的回绕）");
    }
    /* 50 天：与"微秒 / 1000 再取模 2^32"一致 */
    {
        int64_t days50 = (int64_t)50 * 24 * 3600 * 1000000;
        CHECK(snap_ms_from_us(days50) == (uint32_t)(((uint64_t)days50 / 1000u) & 0xFFFFFFFFu),
              "ms(50 days) 与模 2^32 的结果一致");
    }
}

static void test_elapsed(void)
{
    printf("[3] 间隔计算（无符号语义）\n");
    CHECK(snap_elapsed_us(1000, 4500) == 3500u, "普通间隔 = %lu", (unsigned long)snap_elapsed_us(1000, 4500));
    CHECK(snap_elapsed_us(4500, 1000) == (uint32_t)(0u - 3500u),
          "now < t0 时按模 2^32 解释（%lu）", (unsigned long)snap_elapsed_us(4500, 1000));
    /* 一拍最长允许的编码时间远小于 1s；跨 2^32 微秒（71.6min）也不出错 */
    int64_t t0 = ((int64_t)1 << 32) - 200;
    int64_t now = ((int64_t)1 << 32) + 300;
    CHECK(snap_elapsed_us(t0, now) == 500u, "跨 2^32 微秒的间隔 = %lu us",
          (unsigned long)snap_elapsed_us(t0, now));
}

static void test_late(void)
{
    printf("[4] 节拍迟到（M08-F-07）\n");
    CHECK(snap_late_ms(100, 100, 10) == 0u, "准时醒来 late = 0ms");
    CHECK(snap_late_ms(103, 100, 10) == 30u, "晚 3 个 tick = %lu ms（30）",
          (unsigned long)snap_late_ms(103, 100, 10));
    CHECK(snap_late_ms(120, 100, 10) == 200u, "晚一个完整周期 = 200ms");
    /* tick 回绕：2^32-2 → 3，中间 5 个 tick */
    CHECK(snap_late_ms(3u, 0xFFFFFFFEu, 10) == 50u, "跨 uint32 tick 回绕 = %lu ms（50）",
          (unsigned long)snap_late_ms(3u, 0xFFFFFFFEu, 10));
    /* 钳位：不会回绕成小数 */
    CHECK(snap_late_ms(0xFFFFFFFFu, 0u, 10) == 0xFFFFFFFFu, "极大差值钳到 UINT32_MAX");
}

int main(void)
{
    printf("test_snap_time (M11-F-13 / M08-F-07)\n");
    test_old_vs_new();
    test_ms_boundaries();
    test_elapsed();
    test_late();
    printf("test_snap_time: PASS=%d FAIL=%d\n", checks - fails, fails);
    return fails ? 1 : 0;
}
