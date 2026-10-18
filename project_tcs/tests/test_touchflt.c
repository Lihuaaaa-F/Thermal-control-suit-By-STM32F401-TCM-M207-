/* test_touchflt.c — H1 幽灵触点过滤 PC 单测（>200px/帧 丢弃；首点恒收）*/
#include <stdio.h>
#include "ft5446.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

int main(void)
{
    /* 首点（无基准）恒可信 */
    CHECK(ft5446_ghost_jump_ok(0, 0, 0, 500, 300) == 1, "first point always ok");

    /* 阈值边沿：位移 =200px 可信（≤），=201 丢弃（>）*/
    CHECK(ft5446_ghost_jump_ok(1, 100, 100, 300, 100) == 1, "dx=200 ok");
    CHECK(ft5446_ghost_jump_ok(1, 100, 100, 100, 300) == 1, "dy=200 ok");
    CHECK(ft5446_ghost_jump_ok(1, 100, 100, 301, 100) == 0, "dx=201 drop");
    CHECK(ft5446_ghost_jump_ok(1, 100, 100, 100, 301) == 0, "dy=201 drop");
    /* 负方向同判 */
    CHECK(ft5446_ghost_jump_ok(1, 100, 100, -101, 100) == 0, "dx=-201 drop");
    CHECK(ft5446_ghost_jump_ok(1, 100, 100, 100, -101) == 0, "dy=-201 drop");
    /* 正常手指移动（<200px/帧 @20Hz≈4000px/s）可信 */
    CHECK(ft5446_ghost_jump_ok(1, 512, 300, 520, 295) == 1, "normal drag ok");
    /* 对角合成超限（单轴不超、双轴和超）不判幽灵（轴独立判定口径）*/
    CHECK(ft5446_ghost_jump_ok(1, 100, 100, 200, 200) == 1, "per-axis policy ok");

    printf("test_touchflt: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
