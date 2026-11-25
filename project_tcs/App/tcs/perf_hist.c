/**
 * perf_hist.c — 主循环单拍耗用的窗口直方图。见 perf_hist.h。
 */
#include "perf_hist.h"
#include <string.h>

void perf_hist_init(perf_hist_t *h, uint32_t over_thresh_us)
{
    if (!h) return;
    memset(h, 0, sizeof(*h));
    h->over_thresh = over_thresh_us;
}

uint32_t perf_hist_bucket_of(uint32_t us)
{
    uint32_t b = 0;
    while ((us >> b) > 1u && b < PERF_HIST_BUCKETS - 1u) b++;
    return b;
}

void perf_hist_add(perf_hist_t *h, uint32_t us)
{
    if (!h) return;
    uint32_t b = perf_hist_bucket_of(us);
    if (b < PERF_HIST_BUCKETS) h->cnt[b]++;
    if (h->n != 0xFFFFFFFFu) h->n++;
    if (us > h->max) h->max = us;
    if (h->over_thresh && us >= h->over_thresh && h->over != 0xFFFFFFFFu) h->over++;
}

uint32_t perf_hist_pctl(const perf_hist_t *h, unsigned pct)
{
    if (!h || h->n == 0 || pct > 100u) return 0;
    /* 目标：累计数 ≥ ceil(pct% × n)。pct = 0 时目标是 0，落到第一个非空桶（= 最小样本）。
     * 用 64 位算，避免 n 大时乘法溢出。*/
    uint64_t target = ((uint64_t)pct * (uint64_t)h->n + 99u) / 100u;
    uint64_t cum = 0;
    for (uint32_t b = 0; b < PERF_HIST_BUCKETS; b++) {
        if (h->cnt[b] == 0) continue;
        cum += h->cnt[b];
        if (cum >= target) return 1u << b;      /* b ≤ 31：桶号就是最高置位比特 */
    }
    return 0;
}

void perf_hist_reset_window(perf_hist_t *h)
{
    if (!h) return;
    memset(h->cnt, 0, sizeof(h->cnt));
    h->n = 0;
    h->max = 0;
    h->over = 0;
}
