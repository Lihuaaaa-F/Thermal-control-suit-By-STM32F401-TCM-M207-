/* test_ui_known.c — TEC 开关状态"未知"时画未知，不画已关（S 线 i3，2026-11-26；P1-09 收口）
 *
 * 主控在、M207 离线：快照里的 tec1 / tec2 只是主控的命令意图（多半是 0），执行器实际在做什么
 * 无从得知。原来屏拿它当"实际状态"画成"停止 / 暂停"——把"不知道"画成了"已关"。
 * 现在 0x20 快照末尾带 tec_known（bit0 = TEC1 bit1 = TEC2，1 = 可信）；ui_model.c 据此换文案、
 * 转灰，版面不变。默认（没填）= 0 = 未知：宁可少显示。
 *
 * 纯逻辑用例（ui_model.c 不依赖 LVGL）。变异见 tests/mutation_ui_model.py 的 M30–M34。
 * 用法：由 tests/run_all.sh 末尾的 ui_known 一行编译运行。
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

/* 主控在线、模块在、两路开关状态可信 */
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
    ui_in_t in;
    ui_link_view_t lv;
    ui_home_t h0, h1;
    ui_mode_state_t m;

    /* ====== 首页水箱卡的两枚 TEC 胶囊 ====== */
    in = in_up(1000u, 1u);
    in.m207_online = 0;                       /* M207 离线（主控还在）*/
    in.tec1 = 0; in.tec2 = 0;                 /* 主控的命令意图：两路都是关 */
    in.tec_known = 0;                         /* 开关状态：两路都无从得知 */
    lv = ui_link_view(&in);
    CHECK(lv.link == UI_LINK_UP && lv.module == UI_SRC_LOST, "precondition: controller up, module lost");
    ui_home_state(&in, &lv, &h0);
    CHECK(h0.tec1 == 0 && h0.tec2 == 0, "home: the command values are still passed through (0 = commanded off)");
    CHECK(h0.tec_known == 0, "home: both channels are UNKNOWN while the module is away (must not be painted 'stopped')");
    CHECK(h0.tec_known == 0 || h0.tec_known == 3 || h0.tec_known == 1 || h0.tec_known == 2,
          "home: only the two channel bits");   /* tui_p0.c 按位取，高位不得漏进来 */
    in.tec_known = 0x03u;                     /* 模块回来了 */
    ui_home_state(&in, &lv, &h1);
    CHECK(h1.tec_known == 3, "home: module back -> known");
    CHECK((ui_home_diff(&h0, &h1) & UI_H_TECK) != 0, "home: a change of known-ness repaints the TEC pills");
    CHECK(ui_home_diff(&h1, &h1) == 0, "home: nothing changed -> nothing repainted");
    in.tec_known = 0x02u;                     /* 只有 TEC2 可信 */
    ui_home_state(&in, &lv, &h1);
    CHECK(h1.tec_known == 2, "home: per channel (only TEC2 known)");
    memset(&in, 0, sizeof in);                /* 没填 tec_known 的输入（全 0）= 两路未知 */
    in.now_ms = 1000u; in.snap_age_ms = 100u; in.snap_seq = 1u; in.m207_online = 1; in.body_ok = 1;
    in.tec1 = 0; in.tec2 = 0;
    lv = ui_link_view(&in);
    ui_home_state(&in, &lv, &h1);
    CHECK(lv.link == UI_LINK_UP && h1.tec_known == 0,
          "home: an input that never filled tec_known is UNKNOWN, not 'known' (fail-safe default)");
    /* 主控不在：照旧全 0（页面本来一律画未知）*/
    in.snap_age_ms = 9000u; in.tec_known = 0x03u;
    lv = ui_link_view(&in);
    ui_home_state(&in, &lv, &h1);
    CHECK(h1.tec_known == 0, "home: controller away -> nothing is known regardless of the last frame");

    /* ====== 模式页两行的状态文字 ====== */
    in = in_up(1000u, 1u);
    in.mode = 0; in.dir1 = TCS_TEC_DIR_HEAT; in.dir2 = TCS_TEC_DIR_COOL;
    in.tec_known = 0x02u;                     /* TEC1 无从得知、TEC2 可信 */
    lv = ui_link_view(&in);
    ui_mode_state(&in, &lv, &m);
    CHECK(m.link == UI_LINK_UP && m.manual == 1, "mode: precondition manual, controller up");
    CHECK(m.dir[0] == UI_TD_UNKNOWN && m.dir[1] == UI_TD_COOL,
          "mode: an unknown channel shows UNKNOWN; a known one keeps its direction");
    STREQ(ui_mode_dir_text(m.dir[0]), "未知", "mode: text for an unknown channel is '未知', not '暂停'");
    STREQ(ui_mode_dir_text(m.dir[1]), "制冷中", "mode: the known channel still reads its direction");
    CHECK(ui_mode_btn_style(&m, 0, UI_TD_HEAT) == UI_MD_BTN_NORMAL,
          "mode: nothing is highlighted for an unknown channel (the last direction is not 'current')");
    in.tec_known = 0x01u;
    ui_mode_state(&in, &lv, &m);
    CHECK(m.dir[0] == UI_TD_HEAT && m.dir[1] == UI_TD_UNKNOWN, "mode: the other channel unknown");
    in.tec_known = 0x03u;
    ui_mode_state(&in, &lv, &m);
    CHECK(m.dir[0] == UI_TD_HEAT && m.dir[1] == UI_TD_COOL, "mode: both known -> both directions");
    CHECK(m.keys_on == 1, "mode: layout unchanged (keys stay usable when not in SAFE)");
    /* 主控不在：一律未知，tec_known 说什么都不算 */
    in.tec_known = 0x03u; in.snap_age_ms = 9000u;
    lv = ui_link_view(&in);
    ui_mode_state(&in, &lv, &m);
    CHECK(m.dir[0] == UI_TD_UNKNOWN && m.dir[1] == UI_TD_UNKNOWN,
          "mode: controller away -> unknown regardless of tec_known");

    printf("test_ui_known: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
