/* test_ui_svc.c — ALM_SERVICE 在屏上有文案（S 线 i3，2026-11-26；P1-12 的另一半）
 *
 * e3a 让 7 个服务里的控制类服务把启动结果汇总出来，有失败就置 ALM_SERVICE（0x0040）——但屏端
 * 对这一位没有任何文案：位亮着，使用者却只看到"某个功能不好使"。现在：
 *   - 横幅："服务启动失败 / 部分功能不可用 · 请断电重启"，红底；
 *   - 点击不出"解除"（这一位确认不掉，要断电重启），只给提示；
 *   - 确认之后还有残留时，原因里说得出是它；
 *   - 优先级：急停 / 超温压过它（安全第一），它压过会自己好的通信类（根因在这儿，重启才好得了）。
 *
 * 纯逻辑用例（ui_model.c 不依赖 LVGL）。用法：由 tests/run_all.sh 末尾的 ui_svc 一行编译运行。
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

/* 主控在线、模块在、体温探头在 */
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
    const char *hint;

    /* 只有 ALM_SERVICE：红底横幅，说清是什么、怎么办 */
    in.alarm = ALM_SERVICE;
    ui_banner_select(&in, &lv, &b);
    CHECK(b.kind == UI_BN_SERVICE, "only ALM_SERVICE: banner kind is SERVICE");
    CHECK(b.red == 1, "only ALM_SERVICE: red banner (needs the user to act)");
    STREQ(b.title, "服务启动失败", "only ALM_SERVICE: title");
    STREQ(b.detail, "部分功能不可用 · 请断电重启", "only ALM_SERVICE: detail says what to do");
    CHECK(b.extra == 0, "only ALM_SERVICE: no 'other alarms'");
    CHECK(b.tap == UI_BN_TAP_HINT, "only ALM_SERVICE: tapping gives a hint, not an ack (ack cannot clear it)");
    hint = ui_banner_tap_hint(&b);
    CHECK(hint && strstr(hint, "断电重启"), "only ALM_SERVICE: the tap hint says to power-cycle");

    /* 没有别的报警位时这一位不再被忽略（修前：位亮着，横幅却是"无"）*/
    CHECK(in.alarm != 0 && b.kind != UI_BN_NONE, "ALM_SERVICE alone must produce a banner");

    /* 急停 / 超温压过它 */
    in.alarm = (uint16_t)(ALM_ESTOP | ALM_SERVICE);
    ui_banner_select(&in, &lv, &b);
    CHECK(b.kind == UI_BN_ESTOP_PENDING && b.extra == 1, "estop outranks the service banner");
    in.alarm = (uint16_t)(ALM_OVERTEMP | ALM_SERVICE);
    ui_banner_select(&in, &lv, &b);
    CHECK(b.kind == UI_BN_OVERTEMP && b.extra == 1, "overtemp outranks the service banner");

    /* 它压过会自己好的通信类：根因是服务没起来，重启才好得了 */
    in.alarm = (uint16_t)(ALM_M207 | ALM_SERVICE);
    ui_banner_select(&in, &lv, &b);
    CHECK(b.kind == UI_BN_SERVICE && b.extra == 1, "the service banner outranks self-recovering comm alarms");
    in.alarm = (uint16_t)(ALM_EEPROM | ALM_SERVICE);
    ui_banner_select(&in, &lv, &b);
    CHECK(b.kind == UI_BN_SERVICE && b.extra == 1, "the service banner outranks the informational banner");

    /* 主控不在：链路中断照旧压过一切（屏上的报警位是陈旧的）*/
    in.alarm = ALM_SERVICE;
    in.snap_age_ms = 9000u;
    lv = ui_link_view(&in);
    ui_banner_select(&in, &lv, &b);
    CHECK(b.kind == UI_BN_LINK_LOST, "controller away: link-lost banner still wins");

    /* 确认之后还有残留：原因里说得出是它 */
    {
        ui_in_t up = in_up(1000u, 1u);
        lv = ui_link_view(&up);
    }
    STREQ(ui_ack_reason(ALM_SERVICE, &lv), "有后台服务没能启动，请断电重启", "ack reason names the service failure");
    CHECK(ui_ack_reason(0, &lv) == NULL, "no residual bits: no reason");

    printf("test_ui_svc: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
