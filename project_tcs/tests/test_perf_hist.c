/* test_perf_hist.c — M01-F-09（2026-11-26，S-G l1）：主循环单拍耗用的 1 秒窗口直方图
 * （被测：App/tcs/perf_hist.c，纯逻辑）。
 *
 * 审查方案的关单条件"perf 输出 p50 / p99 / max 三个数"里的三个数由本模块算；这里验证：
 * 分桶、百分位下界、"一个尖峰不会挪动 P99、但会出现在窗口 max 与超预算计数里"、
 * "两个尖峰就会把 P99 带过去"、窗口清空、大 n 不溢出。
 */
#include <stdio.h>
#include <string.h>
#include "perf_hist.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static void add_n(perf_hist_t *h, uint32_t us, unsigned n)
{
    for (unsigned i = 0; i < n; i++) perf_hist_add(h, us);
}

int main(void)
{
    perf_hist_t h;

    /* ---- 1) 分桶：桶 b 覆盖 [2^b, 2^(b+1)) ---- */
    CHECK(perf_hist_bucket_of(0) == 0 && perf_hist_bucket_of(1) == 0, "0 和 1 在桶 0");
    CHECK(perf_hist_bucket_of(2) == 1 && perf_hist_bucket_of(3) == 1, "2、3 在桶 1");
    CHECK(perf_hist_bucket_of(4) == 2 && perf_hist_bucket_of(7) == 2 && perf_hist_bucket_of(8) == 3, "4..7 在桶 2，8 在桶 3");
    CHECK(perf_hist_bucket_of(1000u) == 9 && perf_hist_bucket_of(1024u) == 10, "1000 在桶 9（512..1023），1024 在桶 10");
    CHECK(perf_hist_bucket_of(7999u) == 12 && perf_hist_bucket_of(8000u) == 12 && perf_hist_bucket_of(8192u) == 13,
          "预算 8ms 落在 [4.096,8.192)ms 桶的顶部：7999、8000 同桶，8192 进 [8.192,16.384)ms 桶");
    CHECK(perf_hist_bucket_of(0x80000000u) == 31 && perf_hist_bucket_of(0xFFFFFFFFu) == 31, "32 位最大值在桶 31，不越界");

    /* ---- 2) 空窗口 ---- */
    perf_hist_init(&h, 8000u);
    CHECK(h.n == 0 && h.max == 0 && h.over == 0 && h.over_thresh == 8000u, "init：空，预算 8000us");
    CHECK(perf_hist_pctl(&h, 50u) == 0 && perf_hist_pctl(&h, 99u) == 0, "空窗口的百分位是 0");
    CHECK(perf_hist_pctl(NULL, 50u) == 0 && perf_hist_pctl(&h, 101u) == 0, "空指针 / 百分位越界：0");
    perf_hist_add(NULL, 5u);                       /* 不崩 */
    /* X06-F-06：原来 CHECK(1, ...)——只要进程还在就过。空指针路径真正该成立的是"什么都没写"：
     * 同一个窗口的样本数仍是 0、预算没被改。*/
    CHECK(h.n == 0 && h.over_thresh == 8000u, "对空指针加样本不崩，且不写任何窗口状态");

    /* ---- 3) 100 拍，99 拍 1000us、1 拍 9000us：P99 不动，尖峰进 max 与超预算计数 ---- */
    perf_hist_init(&h, 8000u);
    add_n(&h, 1000u, 99);
    perf_hist_add(&h, 9000u);
    CHECK(h.n == 100 && h.max == 9000u && h.over == 1, "n=100，窗口 max=9000，超预算 1 拍");
    CHECK(perf_hist_pctl(&h, 50u) == 512u, "P50 落在 [512,1024)us 桶，报下界 512");
    CHECK(perf_hist_pctl(&h, 99u) == 512u, "一个尖峰（1%）不挪动 P99");
    CHECK(perf_hist_pctl(&h, 100u) == 8192u, "P100 是最大样本所在的桶：9000 在 [8192,16384)");
    CHECK(perf_hist_pctl(&h, 0u) == 512u, "P0 = 最小样本所在的桶");

    /* ---- 4) 两个尖峰：P99 被带过去 ---- */
    perf_hist_init(&h, 8000u);
    add_n(&h, 1000u, 98);
    add_n(&h, 9000u, 2);
    CHECK(h.n == 100 && h.over == 2, "n=100，超预算 2 拍");
    CHECK(perf_hist_pctl(&h, 99u) == 8192u, "两个尖峰（2%）：P99 进了尖峰所在的桶 [8192,16384)");
    CHECK(perf_hist_pctl(&h, 50u) == 512u && perf_hist_pctl(&h, 98u) == 512u,
          "P50、P98 还在主体里（升序第 98 个样本仍是 1000us）");
    /* 向上取整要紧的地方：pct × n 除不尽的时候 */
    perf_hist_init(&h, 8000u);
    add_n(&h, 1000u, 50);
    add_n(&h, 9000u, 51);                           /* 101 拍，大样本 51 拍 */
    CHECK(perf_hist_pctl(&h, 50u) == 8192u,
          "101 拍里大样本过半：中位数在大样本桶（目标 ceil(50.5) = 51 > 50）");

    /* ---- 5) 预算统计的边界：等于预算算超 ---- */
    perf_hist_init(&h, 8000u);
    perf_hist_add(&h, 7999u);
    CHECK(h.over == 0, "7999us < 预算：不算超");
    perf_hist_add(&h, 8000u);
    CHECK(h.over == 1, "8000us = 预算：算超");
    perf_hist_init(&h, 0u);                        /* 预算 0 = 不统计 */
    perf_hist_add(&h, 100000u);
    CHECK(h.over == 0 && h.max == 100000u, "预算为 0：不统计超预算，max 照记");

    /* ---- 6) 窗口清空：cnt / n / max / over 清，预算不动 ---- */
    perf_hist_init(&h, 8000u);
    add_n(&h, 1000u, 10);
    perf_hist_add(&h, 9000u);
    perf_hist_reset_window(&h);
    CHECK(h.n == 0 && h.max == 0 && h.over == 0 && h.over_thresh == 8000u, "reset_window：清空但预算不动");
    {
        unsigned nonzero = 0;
        for (unsigned b = 0; b < PERF_HIST_BUCKETS; b++) nonzero += h.cnt[b] != 0;
        CHECK(nonzero == 0, "所有桶清零");
    }
    CHECK(perf_hist_pctl(&h, 99u) == 0, "清空后百分位是 0");
    /* 调用方的"窗口切换"就是：读走 → reset → 继续采 */
    {
        perf_hist_t last;
        add_n(&h, 3000u, 50);
        last = h;                                  /* 窗口结束：整块拷走 */
        perf_hist_reset_window(&h);
        add_n(&h, 100u, 7);
        CHECK(last.n == 50 && perf_hist_pctl(&last, 99u) == 2048u && h.n == 7 && perf_hist_pctl(&h, 99u) == 64u,
              "上一个窗口保持 50 拍 / P99=2048，新窗口 7 拍 / P99=64");
    }

    /* ---- 7) 大 n：百分位的乘法要用 64 位（99 × n 超过 32 位能表示的范围）---- */
    perf_hist_init(&h, 8000u);
    h.n = 0xFFFFFF00u;                             /* 直接把 n 抬到接近 32 位上限：约 42.9 亿拍 */
    h.cnt[9] = 0xFFFFFF00u - 2u;                  /* 主体：[512,1024)us */
    h.cnt[20] = 2u;                               /* 两个大样本：[1s, 2s) */
    h.max = 2000000u;
    CHECK(perf_hist_pctl(&h, 99u) == 512u, "大 n：99% 仍在主体桶（64 位乘法）");
    CHECK(perf_hist_pctl(&h, 100u) == 1048576u, "大 n：P100 到最大样本所在的桶");
    perf_hist_init(&h, 8000u);
    h.n = 0xFFFFFFFFu;                             /* n 饱和：再加样本不回绕 */
    perf_hist_add(&h, 1000u);
    CHECK(h.n == 0xFFFFFFFFu, "样本数饱和在 2^32−1，不回绕");

    printf("test_perf_hist: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
