/* test_ui_mismatch.c — 协议版本不配对时屏上的表现（S 线 i3，条目 11）
 *
 * 主控能经网页 OTA 升级，屏只能经探针烧录。协议版本一旦不同，屏按 ver 不符丢弃全部 0x20 快照，
 * 屏看到的是"主控连接中断"——行为安全（执行器/模式显示未知、按钮不可点、没有急停），但提示不贴切。
 * 现在屏端把"被版本校验拒掉的快照"计数（stat 的 vr=），链路不新鲜且计数在涨时横幅换成
 * "主控固件与屏不匹配 / 请同步升级后再用"；急停没送到时仍然急停优先。
 *
 * 纯逻辑用例（ui_model.c 不依赖 LVGL）。用法：由 tests/run_all.sh 末尾的 ui_mismatch 一行编译运行。
 */
#include <stdio.h>
#include <string.h>
#include "ui_model.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); fflush(stdout); } \
} while (0)
#define STREQ(a, b, name) do { \
    if (strcmp((a), (b)) == 0) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d): 得到 \"%s\" 期望 \"%s\"\n", name, __LINE__, (a), (b)); fflush(stdout); } \
} while (0)

static ui_in_t in_up(uint32_t now, uint32_t seq)
{
    ui_in_t in;
    memset(&in, 0, sizeof in);
    in.now_ms = now;
    in.snap_age_ms = 100u;
    in.snap_seq = seq;
    in.m207_online = 1;
    in.body_ok = 1;
    in.tec_known = 0x03u;
    return in;
}

int main(void)
{
    ui_in_t in = in_up(1000u, 1u);
    ui_link_view_t lv = ui_link_view(&in);
    ui_banner_t b;

    ui_banner_select(&in, &lv, &b);
    CHECK(b.kind == UI_BN_NONE, "link fresh: no banner");

    /* 链路不新鲜 + 版本拒收计数在涨：文案换成版本不配对；行为仍是断链那一套 */
    in.snap_age_ms = 1200u;
    in.ver_mismatch = 1u;
    lv = ui_link_view(&in);
    ui_banner_select(&in, &lv, &b);
    CHECK(lv.link == UI_LINK_DOWN, "mismatch: the link is still counted as down (same safety behaviour)");
    CHECK(b.kind == UI_BN_VER_MISMATCH && b.red == 1, "mismatch banner: red");
    STREQ(b.title, "主控固件与屏不匹配", "mismatch banner title");
    STREQ(b.detail, "请同步升级后再用", "mismatch banner detail");
    CHECK(b.tap == UI_BN_TAP_HINT, "mismatch banner: hint, not an ack");
    CHECK(ui_banner_tap_hint(&b) != NULL, "mismatch banner has a tap hint");

    /* 急停还没送到主控：安全优先，压过版本提示 */
    in.estop_local = 1;
    ui_banner_select(&in, &lv, &b);
    CHECK(b.kind == UI_BN_ESTOP_UNSENT, "an unsent estop still outranks the version hint");
    in.estop_local = 0;

    /* 计数不涨（ver_mismatch=0）：还是"主控连接中断" */
    in.ver_mismatch = 0;
    ui_banner_select(&in, &lv, &b);
    CHECK(b.kind == UI_BN_LINK_LOST, "no version rejects -> the plain link-lost banner");
    STREQ(b.title, "主控连接中断", "plain banner title unchanged");

    /* ui_vr_mismatch：首拍建基准；计数涨且在 2 s 窗口内 → 1；超窗 → 0；计数为 0 永不触发 */
    {
        ui_vr_t v;
        memset(&v, 0, sizeof v);
        CHECK(ui_vr_mismatch(&v, 5u, 1000u) == 0, "first call only establishes a baseline");
        CHECK(ui_vr_mismatch(&v, 5u, 1500u) == 0, "no change -> 0");
        CHECK(ui_vr_mismatch(&v, 6u, 2000u) == 1, "a new reject just now -> 1");
        CHECK(ui_vr_mismatch(&v, 6u, 3000u) == 1, "still inside the 2 s window");
        CHECK(ui_vr_mismatch(&v, 6u, 4100u) == 0, "nothing for > 2 s -> 0 (the link really broke)");
        CHECK(ui_vr_mismatch(&v, 7u, 5000u) == 1, "it grows again -> 1");
        {
            ui_vr_t z;
            memset(&z, 0, sizeof z);
            CHECK(ui_vr_mismatch(&z, 0u, 100u) == 0 && ui_vr_mismatch(&z, 0u, 200u) == 0,
                  "a zero counter never raises the flag");
        }
        CHECK(ui_vr_mismatch(NULL, 1u, 1u) == 0, "NULL tracker -> 0");
    }

    printf("test_ui_mismatch: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
