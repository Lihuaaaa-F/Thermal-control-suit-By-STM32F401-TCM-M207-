/* test_ui_model.c — 界面纯逻辑单测（U 线 S2；源 = App/lvgl_ui/ui_model.c，零 LVGL）
 *
 * 这些判定原先散在 LVGL 页面代码里、只能上板用眼睛看，现在逐条变成断言：
 *   连接视图（主控断了不能拿陈旧值冒充"在线"）、状态胶囊文案与灯色、
 *   横幅选型（自动恢复的不写"点击确认"）、点确认后的反馈、主页差分位。
 * 判别性见 tests/mutation_ui_model.py（旧式实现与变异必须变红）。
 */
#include <stdio.h>
#include <string.h>
#include "ui_model.h"

static int g_pass, g_fail;
/* 失败行立刻落盘：后面某个断言把进程带崩时，前面的失败清单也不丢（输出重定向时 stdout 是全缓冲的）*/
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
    in.sensor_open = 0;
    return in;
}

int main(void)
{
    char text[32];
    ui_led_t led;

    /* ====== A. 连接视图 ====== */
    {
        ui_in_t in;
        memset(&in, 0, sizeof in);
        in.now_ms = 1234u;
        in.snap_age_ms = UI_LINK_NEVER;
        ui_link_view_t v = ui_link_view(&in);
        CHECK(v.link == UI_LINK_NEVER_SEEN, "A 上电从未收到快照");
        CHECK(v.module == UI_SRC_UNKNOWN && v.body == UI_SRC_UNKNOWN, "A 从未收到：三个源一律未知");
        CHECK(v.stale == 1 && v.down_ms == 1234u, "A 从未收到：标记陈旧，断开时长=开机时长");

        in = in_up(2000u, 7u);
        v = ui_link_view(&in);
        CHECK(v.link == UI_LINK_UP && v.module == UI_SRC_OK && v.body == UI_SRC_OK, "A 快照新鲜：全在线");
        CHECK(v.stale == 0 && v.down_ms == 0, "A 在线时不算陈旧");

        in.snap_age_ms = UI_LINK_STALE_MS;
        CHECK(ui_link_view(&in).link == UI_LINK_UP, "A 500ms 整仍算在线");
        in.snap_age_ms = UI_LINK_STALE_MS + 1u;
        v = ui_link_view(&in);
        CHECK(v.link == UI_LINK_DOWN, "A 501ms 判主控断开");
        CHECK(v.module == UI_SRC_UNKNOWN && v.body == UI_SRC_UNKNOWN,
              "A 主控断开：M207/体温显示未知，而不是沿用最后一帧的\"在线\"");
        CHECK(v.stale == 1 && v.down_ms == UI_LINK_STALE_MS + 1u, "A 断开时长 = 快照龄");

        /* 旧实现的毛病：断链后 m207_online 沿用最后一帧 → 这里必须不受它影响 */
        in = in_up(3000u, 8u);
        in.snap_age_ms = 900u;
        in.m207_online = 1;               /* 陈旧帧里写着"在线" */
        CHECK(ui_link_view(&in).module == UI_SRC_UNKNOWN, "A 陈旧的\"在线\"不得冒充真值");

        in = in_up(3000u, 8u);
        in.m207_online = 0;
        CHECK(ui_link_view(&in).module == UI_SRC_LOST, "A 模块离线 → LOST");
        in = in_up(3000u, 8u);
        in.sensor_open = 1;
        CHECK(ui_link_view(&in).module == UI_SRC_LOST, "A 探头断线 → 模块按 LOST");
        in = in_up(3000u, 8u);
        in.body_ok = 0;
        CHECK(ui_link_view(&in).body == UI_SRC_LOST && ui_link_view(&in).module == UI_SRC_OK,
              "A 体温失联只影响体温源");
    }

    /* ====== B. 状态胶囊 ====== */
    {
        ui_in_t in = in_up(1000u, 1u);
        ui_link_view_t v = ui_link_view(&in);
        ui_status_ctrl(&v, text, sizeof text, &led);
        STREQ(text, "主控已连接", "B 主控在线文案");
        CHECK(led == UI_LED_GREEN, "B 主控在线绿灯");
        ui_status_module(&v, text, sizeof text, &led);
        STREQ(text, "模块在线", "B 模块在线文案");
        CHECK(led == UI_LED_GREEN, "B 模块在线绿灯");

        in.snap_age_ms = 3000u;
        v = ui_link_view(&in);
        ui_status_ctrl(&v, text, sizeof text, &led);
        STREQ(text, "主控断开 3s", "B 主控断开带秒数");
        CHECK(led == UI_LED_RED, "B 主控断开红灯");
        ui_status_module(&v, text, sizeof text, &led);
        STREQ(text, "模块未知", "B 主控断开时模块未知");
        CHECK(led == UI_LED_GRAY, "B 未知灰灯");

        in.snap_age_ms = 100u * 1000u + 500u;
        v = ui_link_view(&in); ui_status_ctrl(&v, text, sizeof text, &led);
        STREQ(text, "主控断开 1分", "B 断开满 100s 改按分钟");

        in.snap_age_ms = UI_LINK_NEVER;
        v = ui_link_view(&in); ui_status_ctrl(&v, text, sizeof text, &led);
        STREQ(text, "主控未连接", "B 从未连上");

        in = in_up(1000u, 1u);
        in.m207_online = 0;
        v = ui_link_view(&in); ui_status_module(&v, text, sizeof text, &led);
        STREQ(text, "模块失联", "B 模块失联文案");
        CHECK(led == UI_LED_YELLOW, "B 模块失联黄灯（数据仍实时，不是红灯）");

        /* 文案不得溢出缓冲：恰好放得下/放不下都不越界 */
        char small[4];
        ui_status_ctrl(&v, small, sizeof small, &led);
        CHECK(small[sizeof small - 1] == '\0', "B 文案缓冲太小也以 NUL 结尾");
    }

    /* ====== C. 横幅选型 ====== */
    {
        ui_in_t in;
        ui_link_view_t v;
        ui_banner_t b;

        /* 链路断开压过一切：哪怕陈旧报警位里写着急停 */
        in = in_up(1000u, 3u);
        in.snap_age_ms = 2000u;
        in.alarm = ALM_ESTOP | ALM_BODY_SENSOR;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_LINK_LOST && b.red == 1, "C 链路断开：红色横幅，优先于陈旧报警位");
        STREQ(b.title, "主控连接中断", "C 链路断开标题");
        CHECK(strstr(b.detail, "2s 前") != NULL, "C 链路断开带\"数据停在哪一刻\"");
        CHECK(b.tap == UI_BN_TAP_HINT, "C 链路断开：点击只给提示");
        CHECK(ui_banner_tap_hint(&b) != NULL, "C 链路断开：有提示语");

        in.snap_age_ms = UI_LINK_NEVER;
        v = ui_link_view(&in); ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_LINK_LOST && strstr(b.detail, "等待主控") != NULL,
              "C 从未连上：提示正在等待");

        in = in_up(1000u, 3u);
        in.alarm = ALM_ESTOP;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_ESTOP && b.red == 1 && b.tap == UI_BN_TAP_ACK, "C 急停：红、需点击解除");
        STREQ(b.title, "急停已触发", "C 急停标题");
        CHECK(strstr(b.detail, "点击解除") != NULL, "C 急停第二行写明点击解除");
        CHECK(b.extra == 0, "C 只有一条报警时 extra=0");
        CHECK(ui_banner_tap_hint(&b) == NULL, "C 需点击解除的横幅没有\"自动恢复\"提示");

        in.alarm = ALM_ESTOP | ALM_M207 | ALM_EEPROM;
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_ESTOP && b.extra == 2, "C 急停优先，另计还有 2 条");

        in.alarm = ALM_OVERTEMP;
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_OVERTEMP && b.red == 1 && b.tap == UI_BN_TAP_ACK, "C 超温：红、需点击解除");
        STREQ(b.title, "水温过高", "C 超温标题");

        in.alarm = ALM_M207;
        ui_banner_select(&in, &v, &b);
        STREQ(b.title, "温控模块通信中断", "C 模块失联标题");
        in.alarm = ALM_SENSOR_OPEN;
        ui_banner_select(&in, &v, &b);
        STREQ(b.title, "水温探头断线", "C 探头断线与模块失联是两句话");
        in.alarm = ALM_BODY_SENSOR | ALM_SENSOR_OPEN | ALM_M207;
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_AUTO && b.red == 0, "C 通信类：黄色（会自己好，不是红）");
        STREQ(b.title, "体温传感器失联", "C 三种通信类同时存在：体温优先");
        CHECK(b.extra == 2, "C 通信类同时存在时也计\"还有几条\"");
        CHECK(b.tap == UI_BN_TAP_HINT, "C 通信类：点击只给提示，不发确认");
        CHECK(strstr(b.detail, "自动恢复") != NULL, "C 通信类第二行写\"自动恢复\"");
        CHECK(strstr(b.detail, "点击") == NULL, "C 通信类不再写\"点击确认\"（旧实现的矛盾）");
        {   /* 提示语可能为空（比如点击行为被改错）：先判空再用，别让测试靠崩溃变红 */
            const char *hint = ui_banner_tap_hint(&b);
            CHECK(hint != NULL && strstr(hint, "自动恢复") != NULL, "C 通信类点击提示");
        }

        /* 未布防：没有报警位，但源确实不在 → 提示级 */
        in.alarm = 0;
        in.body_ok = 0;
        in.mode = 1;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_INFO && b.red == 0 && b.tap == UI_BN_TAP_HINT, "C 未布防体温失联：提示级");
        STREQ(b.title, "体温传感器未连接", "C 未布防体温失联标题");
        CHECK(strstr(b.detail, "自动开始自动温控") != NULL, "C 自动模式下说明接好后自动开始");
        in.mode = 0;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(strstr(b.detail, "自动开始") == NULL, "C 手动模式不提自动温控");
        in.body_ok = 1;
        in.m207_online = 0;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        STREQ(b.title, "温控模块未连接", "C 未布防模块失联：提示级");
        CHECK(strstr(b.detail, "已停止") == NULL, "C 未布防不说\"已停止\"（未布防并不停机）");
        in.m207_online = 1;
        in.sensor_open = 1;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        STREQ(b.title, "水温探头未连接", "C 未布防探头断线");

        in.sensor_open = 0;
        in.alarm = ALM_EEPROM;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_INFO && b.tap == UI_BN_TAP_ACK, "C 参数校验失败：提示级，可点击确认");

        in.alarm = 0;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_NONE, "C 一切正常：没有横幅");
    }

    /* ====== D. 点"解除 / 恢复"后的反馈 ====== */
    {
        ui_in_t in = in_up(1000u, 10u);
        in.alarm = ALM_ESTOP;
        ui_ack_t a;
        memset(&a, 0, sizeof a);
        uint16_t still = 0xFFFF;

        ui_ack_start(&a, &in);
        CHECK(a.waiting == 1, "D 发出后进入等待");
        in.now_ms += 100u;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_WAIT, "D 没来新快照：继续等");
        in.now_ms = 1000u + UI_ACK_TIMEOUT_MS - 1u;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_WAIT, "D 差 1ms 到超时仍等");
        in.now_ms = 1000u + UI_ACK_TIMEOUT_MS;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_NO_REPLY, "D 超时没有新快照：主控没回话");
        CHECK(a.waiting == 0, "D 出结果后不再等待");
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_WAIT, "D 空闲时 poll 不重复出结果");

        /* 静置期：点击后头 450ms 内，哪怕来了新快照也不下结论——那多半是命令还在路上时
         * 发出的旧帧（2026-11-24 真机：据此误报"急停仍未解除"）。*/
        in.alarm = ALM_ESTOP;
        in.snap_seq = 10u;
        in.now_ms = 2000u;
        ui_ack_start(&a, &in);
        in.snap_seq = 11u;                       /* 新快照立刻到，报警位还是旧的 */
        in.now_ms += 100u;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_WAIT, "D 静置期内来了旧报警位的新快照：不下结论");
        in.now_ms = 2000u + UI_ACK_SETTLE_MS - 1u;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_WAIT, "D 差 1ms 出静置期仍等");
        CHECK(a.waiting == 1, "D 静置期内仍在等待");

        /* 出静置期后主控已清零 → 全部解除 */
        in.alarm = 0;
        in.now_ms = 2000u + UI_ACK_SETTLE_MS;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_ALL_CLEAR, "D 静置后报警清零 → 全部解除");

        /* 出静置期后仍有残留 → 部分解除，带残留位 */
        in.alarm = ALM_ESTOP;
        ui_ack_start(&a, &in);
        in.snap_seq = 12u;
        in.alarm = ALM_ESTOP | ALM_BODY_SENSOR;
        in.now_ms += UI_ACK_SETTLE_MS;
        still = 0;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_PARTIAL && still == (ALM_ESTOP | ALM_BODY_SENSOR),
              "D 静置后仍有残留 → 部分解除并带残留位");

        /* 静置期内主控就断了：不出"没回话"，等到超时才报 */
        ui_ack_start(&a, &in);
        in.now_ms += 300u;
        in.snap_age_ms = 900u;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_WAIT, "D 静置期内断链不提前报没回话");

        /* 主控断开时点击：等不到新快照 → 没回话（不能假装成功）*/
        in = in_up(5000u, 20u);
        in.snap_age_ms = 900u;
        ui_ack_start(&a, &in);
        in.now_ms += UI_ACK_TIMEOUT_MS;
        in.snap_age_ms += UI_ACK_TIMEOUT_MS;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_NO_REPLY, "D 主控断开时点击：如实报没回话");

        /* 残留原因 */
        ui_in_t one = in_up(1u, 1u);
        ui_link_view_t v = ui_link_view(&one);
        STREQ(ui_ack_reason(ALM_BODY_SENSOR, &v), "体温传感器仍失联", "D 原因：体温");
        STREQ(ui_ack_reason(ALM_SENSOR_OPEN, &v), "水温探头仍断线", "D 原因：探头");
        STREQ(ui_ack_reason(ALM_M207, &v), "温控模块仍失联", "D 原因：模块");
        STREQ(ui_ack_reason(ALM_OVERTEMP, &v), "水温仍高于 48°C", "D 原因：超温");
        STREQ(ui_ack_reason(ALM_EEPROM, &v), "参数校验仍失败，请重新设定", "D 原因：参数");
        CHECK(ui_ack_reason(0, &v) == NULL && ui_ack_reason(0x8000, &v) == NULL, "D 没有可解释的位返回空");
        in = in_up(1u, 1u);
        in.alarm = ALM_ESTOP;
        in.body_ok = 0;
        v = ui_link_view(&in);
        STREQ(ui_ack_reason(ALM_ESTOP, &v), "急停未解除：体温传感器仍失联", "D 急停残留：说清是体温挡着");
        in.body_ok = 1;
        in.m207_online = 0;
        v = ui_link_view(&in);
        STREQ(ui_ack_reason(ALM_ESTOP, &v), "急停未解除：温控模块仍失联", "D 急停残留：说清是模块挡着");
        in.m207_online = 1;
        v = ui_link_view(&in);
        STREQ(ui_ack_reason(ALM_ESTOP, &v), "急停仍未解除，请再点一次", "D 急停残留：源都好时请再点一次");
    }

    /* ====== E. 主页差分 ====== */
    {
        ui_in_t in = in_up(1000u, 1u);
        ui_link_view_t v = ui_link_view(&in);
        ui_home_t h0, h1;
        ui_home_state(&in, &v, &h0);
        CHECK(h0.link == UI_LINK_UP && h0.module == UI_SRC_OK && h0.body == UI_SRC_OK, "E 在线态");
        CHECK(h0.safe == 0 && h0.estop == 0 && h0.mode == 0 && h0.fan == 0, "E 默认输出全关");
        ui_home_state(&in, &v, &h1);
        CHECK(ui_home_diff(&h0, &h1) == 0, "E 没变化 → 0");

        h1 = h0; h1.mode ^= 1u;              CHECK(ui_home_diff(&h0, &h1) == UI_H_MODE, "E 模式位");
        h1 = h0; h1.fan ^= 1u;               CHECK(ui_home_diff(&h0, &h1) == UI_H_FAN, "E 风扇位");
        h1 = h0; h1.tec1 ^= 1u;              CHECK(ui_home_diff(&h0, &h1) == UI_H_TEC1, "E TEC1 位");
        h1 = h0; h1.tec2 ^= 1u;              CHECK(ui_home_diff(&h0, &h1) == UI_H_TEC2, "E TEC2 位");
        h1 = h0; h1.estop ^= 1u;             CHECK(ui_home_diff(&h0, &h1) == UI_H_ESTOP, "E 急停位");
        h1 = h0; h1.safe ^= 1u;              CHECK(ui_home_diff(&h0, &h1) == UI_H_SAFE, "E 安全态位");
        h1 = h0; h1.body = UI_SRC_LOST;      CHECK(ui_home_diff(&h0, &h1) == UI_H_BODY, "E 体温位");
        h1 = h0; h1.module = UI_SRC_LOST;
        CHECK(ui_home_diff(&h0, &h1) == (UI_H_MODULE | UI_H_LINK), "E 模块位（同时刷状态条）");
        h1 = h0; h1.link = UI_LINK_DOWN;     CHECK(ui_home_diff(&h0, &h1) == UI_H_LINK, "E 链路位");

        /* 输入里的执行器状态进了模型 */
        in.fan = 1; in.tec2 = 1; in.mode = 1;
        ui_home_state(&in, &v, &h1);
        CHECK(h1.fan == 1 && h1.tec2 == 1 && h1.tec1 == 0 && h1.mode == 1, "E 执行器状态取自输入");

        /* 主控断开：执行器/模式/急停一律归零（陈旧值不能上屏当实时）*/
        in.alarm = ALM_ESTOP;
        in.snap_age_ms = 4000u;
        v = ui_link_view(&in);
        ui_home_state(&in, &v, &h1);
        CHECK(h1.link == UI_LINK_DOWN && h1.fan == 0 && h1.tec2 == 0 && h1.mode == 0 && h1.estop == 0,
              "E 主控断开：输出/模式/急停归零");
    }

    printf("test_ui_model: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
