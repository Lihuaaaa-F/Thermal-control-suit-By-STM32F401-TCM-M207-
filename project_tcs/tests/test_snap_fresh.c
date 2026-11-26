/* test_snap_fresh.c — X01-F-04（S 线 e1，2026-11-26）：0x20 的 fresh_ms / badge 有人消费。
 *
 * 修复前：H743 的 snap_apply 不写 s->fresh_ms / s->badge，0x21 分支也没有 TCS_D_BADGE；
 * 主控发来的"温度数据龄"与"布防角标"只存在 s_last_snap 里，全仓零消费者
 * （X01 复核 F-04）。本用例把 link_snap.h 里的纯判定按"新鲜 → 陈旧 → 恢复"跑一遍。
 *
 * 关单条件（修复设计 X01-F-04）：断链 2s 后屏上出现"数据陈旧"——由 tcs_link_stale_at()
 * 的 snap_age_ms > 2000 这一条承载（界面层怎么画不在本批次，方案见 results 文档）。
 *
 * 用法：由 tests/run_all.sh 的 snapfresh 一行编译运行。
 */
#include <stdio.h>
#include "link_snap.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

int main(void)
{
    tcs_link_fresh_t f;

    /* ---- 1) 还没收到过快照：陈旧（界面不能显示 0 岁当"新鲜"）---- */
    f.fresh_ms = 123; f.badge = 1; f.have = 0; f.stale = 0;
    CHECK(tcs_link_stale_at(f.fresh_ms, 0, f.have) == 1, "1: 没收到过快照 → 陈旧");
    CHECK(tcs_link_stale_at(100, 100, 1) == 0, "1: 收到过且龄正常 → 新鲜");

    /* ---- 2) 新鲜：主控的温度龄 300ms（500ms/通道轮询的正常值）---- */
    tcs_link_fresh_apply(&f, 300, 1);
    CHECK(f.have == 1 && f.stale == 0 && f.fresh_ms == 300 && f.badge == 1,
          "2: 0x20 新鲜帧落镜像（fresh=300 badge=1 stale=0）");
    CHECK(tcs_link_stale_at(f.fresh_ms, 200, f.have) == 0, "2: 快照 200ms 前到的 + 龄 300ms → 新鲜");

    /* ---- 3) 陈旧：0xFFFF（M207 离线/未知）---- */
    tcs_link_fresh_apply(&f, 0xFFFFu, 1);
    CHECK(f.stale == 1, "3: fresh_ms = 0xFFFF → 陈旧（不能被当成 65.5s 显示）");
    CHECK(f.fresh_ms == 0xFFFFu, "3: 0xFFFF 原样保留（界面据此显示'未知'）");
    CHECK(tcs_link_stale_at(f.fresh_ms, 100, f.have) == 1, "3: 0xFFFF 现算也是陈旧");

    /* ---- 4) 陈旧：龄超过 3 个轮询周期（3000ms 边界：等于不算、超过才算）---- */
    tcs_link_fresh_apply(&f, LINK_SNAP_FRESH_STALE_MS, 1);
    CHECK(f.stale == 0, "4: fresh_ms == 3000 仍算新鲜（边界）");
    tcs_link_fresh_apply(&f, LINK_SNAP_FRESH_STALE_MS + 1u, 1);
    CHECK(f.stale == 1, "4: fresh_ms == 3001 → 陈旧");

    /* ---- 5) 恢复：下一帧新鲜数据 ---- */
    tcs_link_fresh_apply(&f, 200, 1);
    CHECK(f.stale == 0 && f.fresh_ms == 200, "5: 恢复（新鲜 → 陈旧 → 恢复）");

    /* ---- 6) 链路层面：断链 2s 后陈旧（X01 关单条件）---- */
    CHECK(tcs_link_stale_at(200, LINK_SNAP_AGE_STALE_MS, 1) == 0, "6: 快照 2000ms 前到 → 仍新鲜（边界）");
    CHECK(tcs_link_stale_at(200, LINK_SNAP_AGE_STALE_MS + 1u, 1) == 1, "6: 快照 2001ms 没来 → 陈旧");
    CHECK(tcs_link_stale_at(200, 60000u, 1) == 1, "6: 断链 60s → 陈旧");

    /* ---- 7) badge：增量只换角标，不动 fresh；高两位掩掉 ----
     * 2 = 布防后失联报警（协议 post_arm_t 语义）。修前有人把它接到本地 POST 角标的
     * post_badge_state（2 = 已淡出）上，失联报警会显示成"角标淡出"——本用例钉住"原值 2"。*/
    tcs_link_fresh_apply(&f, 250, 0);
    CHECK(f.badge == 0 && f.stale == 0, "7: 未布防（badge 0）");
    tcs_link_fresh_badge(&f, 2);
    CHECK(f.badge == 2 && f.fresh_ms == 250, "7: 增量 badge=2（失联报警）且 fresh_ms 不变");
    tcs_link_fresh_badge(&f, (uint8_t)(0x80u | 1u));
    CHECK(f.badge == 1, "7: badge 高两位被掩掉（0x81 → 1）");
    tcs_link_fresh_apply(&f, 250, 2);
    CHECK(f.badge == 2, "7: 全量帧里的失联报警原样保留");

    printf("test_snap_fresh: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
