/* test_hist_body.c — 曲线页历史环多一条体温（m1"模式"页，用户 2026-11-26 追加的需求）
 *
 * 需求：体温卡点进去也要有曲线 → H743 的历史环（control.c 的 s_hist_*，每 30 秒一格、60 格）
 * 增加体温采样。体温探头没接 / 失联、链路不在的那一格记无效（NaN，图上断开，不画成 0）。
 * 原来两条水温线的采样与读出不变。
 *
 * 历史环只在 TCS_USE_LVGL 下编译；这里按交付档语义编（TCS_PROFILE=0：链路不在的格子记 NaN）。
 * 用法：由 tests/run_all.sh 末尾的 histbody 一段编译运行。
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "control.h"
#include "param_registry.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); fflush(stdout); } \
} while (0)

/* param_registry.c 的目标机桩（主机用例不碰 EEPROM 镜像）*/
int eeprom_ready(void) { return 0; }
int eeprom_read_image(uint8_t *img) { (void)img; return -1; }

int main(void)
{
    static float h1[60], h2[60], hb[60];
    uint32_t n = 0, nb = 0, i;

    /* 上电第一格（now=0 不算时间戳，格子照常推进）*/
    g_tcs.esp_link_ok = 1; g_tcs.body_sensor_ok = 1;
    g_tcs.tc1_act = 20.0f; g_tcs.tc2_act = 21.0f; g_tcs.body_temp = 36.6f;
    tcs_hist_tick(1000u);
    tcs_hist_snapshot_body(hb, &nb);
    CHECK(nb == 1 && fabsf(hb[0] - 36.6f) < 1e-4f, "H1 first cell: body temperature 36.6");
    tcs_hist_snapshot(h1, h2, &n);
    CHECK(n == 1 && fabsf(h1[0] - 20.0f) < 1e-4f && fabsf(h2[0] - 21.0f) < 1e-4f, "H1 water lines unaffected");

    /* 30 秒之内不再采样 */
    tcs_hist_tick(10000u);
    tcs_hist_snapshot_body(hb, &nb);
    CHECK(nb == 1, "H2 no new cell before 30 s");

    /* 体温探头没接：这一格无效（NaN），不是 0 */
    g_tcs.body_sensor_ok = 0; g_tcs.body_temp = 0.0f;
    tcs_hist_tick(31000u);
    tcs_hist_snapshot_body(hb, &nb);
    CHECK(nb == 2 && isnan(hb[1]), "H3 probe not connected -> NaN (not 0)");

    /* 链路不在：这一格无效；两路水温和体温一样 */
    g_tcs.body_sensor_ok = 1; g_tcs.body_temp = 36.7f;
    g_tcs.esp_link_ok = 0;
    tcs_hist_tick(61000u);
    tcs_hist_snapshot_body(hb, &nb);
    tcs_hist_snapshot(h1, h2, &n);
    CHECK(nb == 3 && isnan(hb[2]) && n == 3 && isnan(h1[2]) && isnan(h2[2]),
          "H4 link down -> NaN on all three lines (gap keeps its 30 s slot)");

    /* 链路回来：继续采样，缺口前后的点仍各占一格 */
    g_tcs.esp_link_ok = 1;
    tcs_hist_tick(91000u);
    tcs_hist_snapshot_body(hb, &nb);
    CHECK(nb == 4 && fabsf(hb[3] - 36.7f) < 1e-4f && isnan(hb[1]) && isnan(hb[2]),
          "H5 samples before and after the gap keep their slots");
    CHECK(tcs_hist_count() == 4u, "H5 cell counter");

    /* 攒满 60 格再继续：最老的掉出去，顺序仍是旧→新 */
    g_tcs.body_temp = 36.0f;
    for (i = 0; i < 70u; i++) {
        g_tcs.body_temp += 0.01f;
        tcs_hist_tick(91000u + 30000u * (i + 1u));
    }
    tcs_hist_snapshot_body(hb, &nb);
    CHECK(nb == 60u, "H6 ring holds 60 cells");
    CHECK(fabsf(hb[59] - (36.0f + 0.70f)) < 2e-3f, "H6 newest is last");
    for (i = 0; i < 60u; i++) CHECK(!isnan(hb[i]) && hb[i] >= 36.0f && hb[i] <= 36.71f, "H6 old cells dropped, monotonic order");
    tcs_hist_snapshot(h1, h2, &n);
    CHECK(n == 60u, "H6 water snapshot length unchanged");

    printf("test_hist_body: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
