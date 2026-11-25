/**
 * perf_hist.h — 主循环单拍耗用的窗口直方图（纯逻辑，零 HAL，主机可测）
 *
 * M01-F-09（2026-11-26，S-G l1）：perf 原来只有"终身最大值"和一条指数平均——看得出历史上
 * 有没有超预算，看不出**最近**超得勤不勤（一个几小时前的尖峰会一直占着 max）。这里按固定的
 * 窗口（调用方定，主循环用 1 秒）统计：样本数、P50 / P99（按下界报）、窗口最大值、超预算的拍数。
 *
 * 桶：32 个对数桶（样本是 uint32_t，最高的置位比特就是第 31 位），桶 b 覆盖 [2^b, 2^(b+1)) 微秒
 * （b = 0 覆盖 0 和 1）。审查方案写的是"64 桶、桶边界按 4 的幂"，这里把每个 4 的幂的区间再对半分
 * （即边界取 2 的幂），粒度细一倍：8ms 预算落在 [4.096, 8.192)ms 这一桶的顶部——P99 报 4096 就是
 * "99% 的拍不到 8.2ms"。桶边不落在预算线上不要紧：**是否超预算是精确比较**，看 over 计数。
 */
#ifndef TCS_PERF_HIST_H
#define TCS_PERF_HIST_H

#include <stdint.h>
#include <stddef.h>

#define PERF_HIST_BUCKETS   32u

typedef struct {
    uint32_t cnt[PERF_HIST_BUCKETS];
    uint32_t n;               /* 窗口内的样本数 */
    uint32_t max;             /* 窗口内的最大值（µs）*/
    uint32_t over;            /* 窗口内 ≥ over_thresh 的样本数 */
    uint32_t over_thresh;     /* 预算（µs）；init 设定，reset_window 不动它 */
} perf_hist_t;

/* 设定预算并清空。over_thresh_us = 0 表示不统计超预算。*/
void perf_hist_init(perf_hist_t *h, uint32_t over_thresh_us);

/* 一个样本落在哪个桶：最高的置位比特（0 和 1 → 0；2、3 → 1；4..7 → 2 …）。*/
uint32_t perf_hist_bucket_of(uint32_t us);

/* 记一个样本。h 为空时什么都不做。*/
void perf_hist_add(perf_hist_t *h, uint32_t us);

/* 窗口的百分位下界（µs）：累计样本数第一次达到 pct% 的那个桶的下界（2^b）。
 * pct = 50 / 99 …；pct = 0 返回最小样本所在桶的下界。窗口为空或参数非法返回 0。*/
uint32_t perf_hist_pctl(const perf_hist_t *h, unsigned pct);

/* 清空窗口（cnt / n / max / over），预算不动。调用方在每个窗口结束时先把要留的读走。*/
void perf_hist_reset_window(perf_hist_t *h);

/* 由 Core/Src/main.c 实现：最近一个**已经结束**的窗口（主循环每 1 秒结束一个）。perf 命令读它。
 * 本模块（纯逻辑）不实现也不调用它；主机单测不需要给定义。*/
const perf_hist_t *tcs_perf_last_window(void);

#endif /* TCS_PERF_HIST_H */
