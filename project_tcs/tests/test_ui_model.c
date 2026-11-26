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
    in.tec_known = 0x03u;                   /* S 线 i3：两路 TEC 开关状态可信（未知的情形在 test_ui_known.c）*/
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
        CHECK(led == UI_LED_RED, "B 从未连上是红灯（不是绿灯）");

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

        /* 屏端已记下急停、却没送到主控（链路断着）：比"连接中断"本身更要紧，单独一种横幅。
         * 这时点它不能发确认——确认只会把屏端自己的记号清掉。*/
        in = in_up(1000u, 3u);
        in.snap_age_ms = 5000u;
        in.alarm = ALM_ESTOP;              /* 这一位是屏端自己置的本地自持，不是主控说的 */
        in.estop_local = 1u;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_ESTOP_UNSENT && b.red == 1 && b.tap == UI_BN_TAP_HINT,
              "C 急停没送到主控：红色、点击只给提示（不发确认）");
        STREQ(b.title, "急停尚未送达主控", "C 急停没送达标题");
        CHECK(strstr(b.detail, "立即送达") != NULL && strstr(b.detail, "5s 前") != NULL,
              "C 急停没送达：说明连上后会送达，并带数据龄");
        {   /* 提示语可能为空：先判空再用，别让测试靠崩溃变红 */
            const char *hint = ui_banner_tap_hint(&b);
            CHECK(hint != NULL && strstr(hint, "立即送达") != NULL, "C 急停没送达的点击提示");
        }
        in.snap_age_ms = UI_LINK_NEVER;
        v = ui_link_view(&in); ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_ESTOP_UNSENT && strstr(b.detail, "等待主控") != NULL,
              "C 从未连上 + 急停没送达");

        /* 急停：主控快照带了急停位、且执行器状态确实是"TEC 全关 + 风扇开"，才说"TEC 已停"。
         * "TEC 已停"是主控快照里的事实，不是屏端按了键就能替它说的话（独立复核 P1-1）。*/
        in = in_up(1000u, 3u);
        in.alarm = ALM_ESTOP;
        in.estop_local = 1u;
        in.estop_peer = 1u;
        in.fan = 1u;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_ESTOP && b.red == 1 && b.tap == UI_BN_TAP_ACK, "C 急停：红、需点击解除");
        STREQ(b.title, "急停已触发", "C 急停标题");
        CHECK(strstr(b.detail, "TEC 已停") != NULL && strstr(b.detail, "风扇全速") != NULL
              && strstr(b.detail, "点击解除") != NULL,
              "C 急停第二行：主控确认、状态相符后才说 TEC 已停、风扇全速");
        CHECK(b.extra == 0, "C 只有一条报警时 extra=0");
        CHECK(ui_banner_tap_hint(&b) == NULL, "C 需点击解除的横幅没有\"自动恢复\"提示");

        in.alarm = ALM_ESTOP | ALM_M207 | ALM_EEPROM;
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_ESTOP && b.extra == 2, "C 急停优先，另计还有 2 条");

        /* 主控快照里还没有急停位（指令还在路上，或主控没执行）：不能替它说"TEC 已停" */
        in.alarm = ALM_ESTOP;
        in.estop_peer = 0u;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_ESTOP_PENDING && b.red == 1 && b.tap == UI_BN_TAP_HINT,
              "C 急停已发出、主控未确认：红，点击只给提示");
        STREQ(b.title, "急停指令已发出", "C 急停未确认标题");
        CHECK(strstr(b.detail, "TEC 已停") == NULL && strstr(b.detail, "等待主控确认") != NULL,
              "C 急停未确认：不说 TEC 已停");
        {   const char *hint = ui_banner_tap_hint(&b);
            CHECK(hint != NULL && strstr(hint, "确认") != NULL, "C 急停未确认的点击提示");
        }

        /* 主控报了急停位，但快照里的执行器状态还不是"全关 + 风扇开"：不写"TEC 已停" */
        in.estop_peer = 1u;
        in.fan = 0u;
        v = ui_link_view(&in);
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_ESTOP && b.tap == UI_BN_TAP_ACK
              && strstr(b.detail, "TEC 已停") == NULL
              && strstr(b.detail, "主控已执行急停") != NULL && strstr(b.detail, "点击解除") != NULL,
              "C 主控报了急停但风扇没开：不说风扇全速，仍可点击解除");
        in.fan = 1u; in.tec2 = 1u;
        v = ui_link_view(&in); ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_ESTOP && strstr(b.detail, "TEC 已停") == NULL,
              "C 主控报了急停但 TEC2 还开着：不说 TEC 已停");
        in.tec2 = 0u;
        v = ui_link_view(&in);

        in.alarm = ALM_OVERTEMP;
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_OVERTEMP && b.red == 1 && b.tap == UI_BN_TAP_ACK, "C 超温：红、需点击解除");
        STREQ(b.title, "水温过高", "C 超温标题");
        in.alarm = ALM_OVERTEMP | ALM_BODY_SENSOR;
        ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_OVERTEMP && b.extra == 1, "C 超温优先，另计还有 1 条");

        /* 断开满 100s：改说"N 分钟前"（与状态胶囊同一口径）*/
        in = in_up(1000u, 3u);
        in.snap_age_ms = 125000u;
        v = ui_link_view(&in); ui_banner_select(&in, &v, &b);
        CHECK(b.kind == UI_BN_LINK_LOST && strstr(b.detail, "2 分钟前") != NULL
              && strstr(b.detail, "已过期") == NULL, "C 断开满 100s 改说\"N 分钟前\"");
        in = in_up(1000u, 3u);
        in.snap_age_ms = 99000u;
        v = ui_link_view(&in); ui_banner_select(&in, &v, &b);
        CHECK(strstr(b.detail, "99s 前") != NULL, "C 断开 99s 仍说秒");
        in = in_up(1000u, 3u);
        v = ui_link_view(&in);

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

        /* 点击前刚到过一帧新快照、点击后链路断了：屏上残留的报警位说明不了"没解除"，
         * 结论只能是"主控没回话"（独立复核 P2-3：原先报成"急停仍未解除，请再点一次"）*/
        in = in_up(7000u, 30u);
        in.alarm = ALM_ESTOP;
        ui_ack_start(&a, &in);
        in.snap_seq = 31u;                       /* 来了一帧"新"快照 */
        in.now_ms += UI_ACK_SETTLE_MS;
        in.snap_age_ms = 900u;                   /* 但链路已经断了 */
        still = 0xFFFF;
        CHECK(ui_ack_poll(&a, &in, &still) == UI_ACK_NO_REPLY && still == 0,
              "D 点击后断链：报没回话，不拿陈旧报警位报\"未解除\"");

        /* 放弃等待（切页 / 开弹窗时调用）：之后不再出任何结果 */
        in = in_up(8000u, 40u);
        ui_ack_start(&a, &in);
        ui_ack_cancel(&a);
        in.now_ms += UI_ACK_SETTLE_MS + UI_ACK_TIMEOUT_MS;
        CHECK(a.waiting == 0 && ui_ack_poll(&a, &in, &still) == UI_ACK_WAIT,
              "D ui_ack_cancel 之后不再出结果");

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
        CHECK(h0.estop == 0 && h0.mode == 0 && h0.fan == 0, "E 默认输出全关");
        ui_home_state(&in, &v, &h1);
        CHECK(ui_home_diff(&h0, &h1) == 0, "E 没变化 → 0");

        h1 = h0; h1.mode ^= 1u;              CHECK(ui_home_diff(&h0, &h1) == UI_H_MODE, "E 模式位");
        h1 = h0; h1.fan ^= 1u;               CHECK(ui_home_diff(&h0, &h1) == UI_H_FAN, "E 风扇位");
        h1 = h0; h1.tec1 ^= 1u;              CHECK(ui_home_diff(&h0, &h1) == UI_H_TEC1, "E TEC1 位");
        h1 = h0; h1.tec2 ^= 1u;              CHECK(ui_home_diff(&h0, &h1) == UI_H_TEC2, "E TEC2 位");
        h1 = h0; h1.estop ^= 1u;             CHECK(ui_home_diff(&h0, &h1) == UI_H_ESTOP, "E 急停位");
        h1 = h0; h1.body = UI_SRC_LOST;      CHECK(ui_home_diff(&h0, &h1) == UI_H_BODY, "E 体温位");
        h1 = h0; h1.module = UI_SRC_LOST;
        CHECK(ui_home_diff(&h0, &h1) == (UI_H_MODULE | UI_H_LINK), "E 模块位（同时刷状态条）");
        h1 = h0; h1.link = UI_LINK_DOWN;     CHECK(ui_home_diff(&h0, &h1) == UI_H_LINK, "E 链路位");

        /* 输入里的执行器状态进了模型 */
        in.fan = 1; in.tec2 = 1; in.mode = 1;
        ui_home_state(&in, &v, &h1);
        CHECK(h1.fan == 1 && h1.tec2 == 1 && h1.tec1 == 0 && h1.mode == 1, "E 执行器状态取自输入");

        /* 主控在线、屏端记下了急停：急停键变灰 */
        in.estop_local = 1u;
        v = ui_link_view(&in);
        ui_home_state(&in, &v, &h1);
        CHECK(h1.estop == 1, "E 急停已记下：急停位 = 1（键变灰）");

        /* 主控断开：执行器/模式归零（陈旧值不能上屏当实时）；急停键看屏端自己记没记 */
        in.snap_age_ms = 4000u;
        v = ui_link_view(&in);
        ui_home_state(&in, &v, &h1);
        CHECK(h1.link == UI_LINK_DOWN && h1.fan == 0 && h1.tec2 == 0 && h1.mode == 0 && h1.estop == 1,
              "E 主控断开：输出/模式归零；已记下的急停保持（指令还没送到，键仍灰）");
        in.estop_local = 0u;
        ui_home_state(&in, &v, &h1);
        CHECK(h1.estop == 0, "E 主控断开且没记下急停：急停键是红的（急停必须一直可点）");

        /* r7f（独立复核 R7-06，2026-11-26）：首页风扇卡片在链路不新鲜时不发 0x15（与模式页同一条口径）。
         * 修前 clk_fan 无条件发帧：快照停更时点它，手动模式暂停里的风扇就是真被切了。*/
        in.estop_local = 0u;
        in.snap_age_ms = 4000u;                        /* 快照停更：链路不新鲜 */
        v = ui_link_view(&in);
        ui_home_state(&in, &v, &h1);
        CHECK(h1.link == UI_LINK_DOWN, "R7-06 首页：主控断开");
        {
            const char *blk = ui_home_fan_block(&h1);   /* 修前没有这道门：桩回 NULL，下面必红 */
            CHECK(blk != NULL && strcmp(blk, "主控连接中断，现在不能操作") == 0,
                  "R7-06 首页风扇卡片：点了只提示");
        }
        in = in_up(2000u, 9u);                         /* 链路在：照常可用 */
        v = ui_link_view(&in);
        ui_home_state(&in, &v, &h1);
        CHECK(ui_home_fan_block(&h1) == NULL, "R7-06 首页风扇卡片：链路在时不算拦");
        in.snap_age_ms = UI_LINK_NEVER;                /* 从未连上（台架只接网页）同样不新鲜 */
        v = ui_link_view(&in);
        ui_home_state(&in, &v, &h1);
        {
            const char *blk = ui_home_fan_block(&h1);
            CHECK(blk != NULL && strcmp(blk, "主控连接中断，现在不能操作") == 0,
                  "R7-06 首页风扇卡片：从未连上也提示");
        }
    }

    /* ====== F. m1"模式"页（用户 2026-11-26 定的新功能）====== */
    {
        ui_in_t in;
        ui_link_view_t v;
        ui_mode_state_t m0, m1;
        ui_mode_cmd_t c, c2;
        char ptext[32], pw[32], vi[48];
        uint8_t enabled;

        /* F1 首页模式键：文字恒为当前模式（不再写"切自动 / 切手动"），点击进模式页 */
        ui_mode_key(1, 0, ptext, sizeof ptext, &enabled);
        STREQ(ptext, "手动", "F1 在线 + 手动：键上写'手动'");
        CHECK(enabled == 1, "F1 在线：可点");
        ui_mode_key(1, 1, ptext, sizeof ptext, &enabled);
        STREQ(ptext, "自动", "F1 在线 + 自动：键上写'自动'");
        ui_mode_key(0, 1, ptext, sizeof ptext, &enabled);
        CHECK(enabled == 0, "F1 主控断开：不可点");

        /* F2 协议里的方向值 → 枚举 */
        CHECK(ui_mode_dir_of(0) == UI_TD_PAUSE && ui_mode_dir_of(1) == UI_TD_COOL
              && ui_mode_dir_of(2) == UI_TD_HEAT && ui_mode_dir_of(3) == UI_TD_EXT
              && ui_mode_dir_of(7) == UI_TD_EXT && ui_mode_dir_of(15) == UI_TD_EXT,
              "F2 方向值：0/1/2，其余都算'开着但不是模式页设的'");

        /* F3 手动、在线、无报警：两路方向与电学量来自快照 */
        in = in_up(10000u, 100u);
        in.mode = 0; in.fan = 1;                      /* 联动：TEC1 在制冷，风扇被开着 */
        in.dir1 = TCS_TEC_DIR_COOL; in.dir2 = TCS_TEC_DIR_PAUSE;
        in.tec_valid = 3u; in.tec_v1 = -8.234f; in.tec_i1 = 3.104f; in.tec_v2 = 0.02f; in.tec_i2 = 0.0f;
        v = ui_link_view(&in);
        ui_mode_state(&in, &v, &m0);
        CHECK(m0.link == UI_LINK_UP && m0.manual == 1 && m0.safe == 0 && m0.keys_on == 1, "F3 手动可操作");
        CHECK(m0.dir[0] == UI_TD_COOL && m0.dir[1] == UI_TD_PAUSE, "F3 两路方向");
        CHECK(m0.elec_ok[0] == 1 && m0.elec_ok[1] == 1, "F3 两路电学量有效");
        CHECK(m0.fan == 1 && m0.fan_locked == 1, "F3 有 TEC 在制冷 → 风扇被联动");
        CHECK(ui_mode_tap_block(&m0) == NULL, "F3 方向键可点");
        STREQ(ui_mode_fan_block(&m0), "TEC 工作时风扇保持开启", "F3 联动期间风扇键不可关");
        in.fan = 0;                                   /* 联动期间风扇竟是被关着的：这时点它是想开，不拦 */
        ui_mode_state(&in, &v, &m1);
        CHECK(m1.fan_locked == 1 && ui_mode_fan_block(&m1) == NULL, "F3 联动期间风扇没开：点它=开，放行");
        /* 差分位 */
        m1 = m0; m1.dir[1] = UI_TD_HEAT;
        CHECK(ui_mode_diff(&m0, &m1) == UI_M_DIR2, "F3 方向位");
        m1 = m0; m1.elec_ok[0] = 0;
        CHECK(ui_mode_diff(&m0, &m1) == UI_M_ELEC1, "F3 电学量位");
        m1 = m0; m1.fan = 0;
        CHECK(ui_mode_diff(&m0, &m1) == UI_M_FAN, "F3 风扇位");
        m1 = m0; m1.safe = 1;
        CHECK(ui_mode_diff(&m0, &m1) == UI_M_SAFE, "F3 安全态位");
        m1 = m0; m1.manual = 0;
        CHECK(ui_mode_diff(&m0, &m1) == UI_M_MODE, "F3 模式位");
        m1 = m0; m1.estop = 1;
        CHECK(ui_mode_diff(&m0, &m1) == UI_M_ESTOP, "F3 急停位");
        CHECK(ui_mode_diff(&m0, &m0) == 0, "F3 没变不出位");

        /* F4 主控断开：方向未知、数值不可用、键不可点（陈旧值不上屏当实时）*/
        in.snap_age_ms = 4000u;
        v = ui_link_view(&in);
        ui_mode_state(&in, &v, &m1);
        CHECK(m1.link == UI_LINK_DOWN && m1.dir[0] == UI_TD_UNKNOWN && m1.elec_ok[0] == 0
              && m1.keys_on == 0 && m1.manual == 0 && m1.fan_locked == 0, "F4 主控断开：全部转灰");
        m1 = m0; m1.link = UI_LINK_DOWN;
        CHECK(ui_mode_diff(&m0, &m1) == UI_M_LINK, "F4 链路位");
        STREQ(ui_mode_tap_block(&m1), "主控连接中断，现在不能操作", "F4 主控断开：点了只提示");
        /* r7f（独立复核 R7-06）：风扇键同一口径——链路不新鲜时不发 0x15（修前这里返回 NULL，照发）*/
        {
            const char *blk = ui_mode_fan_block(&m1);
            CHECK(blk != NULL && strcmp(blk, "主控连接中断，现在不能操作") == 0,
                  "F4 主控断开：风扇键点了只提示");
        }

        /* F5 自动模式：没有方向键；方向照显（状态机给的）；风扇不锁 */
        in = in_up(10000u, 100u);
        in.mode = 1; in.dir1 = TCS_TEC_DIR_HEAT; in.fan = 1;
        v = ui_link_view(&in);
        ui_mode_state(&in, &v, &m1);
        CHECK(m1.manual == 0 && m1.keys_on == 0 && m1.dir[0] == UI_TD_HEAT, "F5 自动：只看不操作");
        CHECK(m1.fan_locked == 0 && ui_mode_fan_block(&m1) == NULL, "F5 自动：风扇键不锁");
        STREQ(ui_mode_tap_block(&m1), "只能在手动模式下调节", "F5 自动：方向键点不了");

        /* F6 安全态（急停 / 超温 / 模块 / 探头 / 体温报警未解除）：键不可点，提示先解除报警 */
        {
            const uint16_t bits[] = { ALM_ESTOP, ALM_OVERTEMP, ALM_M207, ALM_SENSOR_OPEN, ALM_BODY_SENSOR };
            for (size_t k = 0; k < sizeof bits / sizeof bits[0]; k++) {
                in = in_up(10000u, 100u);
                in.mode = 0; in.alarm = bits[k];
                v = ui_link_view(&in);
                ui_mode_state(&in, &v, &m1);
                CHECK(m1.safe == 1 && m1.keys_on == 0, "F6 安全态下方向键不可点");
                STREQ(ui_mode_tap_block(&m1), "安全态下不可操作，请先解除报警", "F6 提示先解除报警");
            }
            in = in_up(10000u, 100u);          /* 只是没有体温探头（未布防时不上报警）：不算安全态 */
            in.mode = 0; in.body_ok = 0; in.alarm = 0;
            v = ui_link_view(&in);
            ui_mode_state(&in, &v, &m1);
            CHECK(m1.safe == 0 && m1.keys_on == 1, "F6 没接体温探头：手动照常可操作");
            in.alarm = ALM_EEPROM;
            ui_mode_state(&in, &v, &m1);
            CHECK(m1.safe == 0, "F6 EEPROM 报警只是提示：不算安全态");
        }

        /* F7 按钮高亮：以主控回读的实际输出为准；断链全部置灰 */
        in = in_up(10000u, 100u);
        in.mode = 0; in.dir1 = TCS_TEC_DIR_COOL; in.dir2 = TCS_TEC_DIR_EXT; in.tec_valid = 0;
        v = ui_link_view(&in);
        ui_mode_state(&in, &v, &m1);
        CHECK(ui_mode_btn_style(&m1, UI_TD_COOL) == UI_MD_BTN_ON
              && ui_mode_btn_style(&m1, UI_TD_PAUSE) == UI_MD_BTN_NORMAL
              && ui_mode_btn_style(&m1, UI_TD_HEAT) == UI_MD_BTN_NORMAL, "F7 制冷中：只有'制冷'高亮");
        in.dir1 = TCS_TEC_DIR_EXT;                 /* 通道开着，但不是模式页设的：三个键都不高亮 */
        ui_mode_state(&in, &v, &m1);
        CHECK(ui_mode_btn_style(&m1, UI_TD_PAUSE) == UI_MD_BTN_NORMAL
              && ui_mode_btn_style(&m1, UI_TD_COOL) == UI_MD_BTN_NORMAL
              && ui_mode_btn_style(&m1, UI_TD_HEAT) == UI_MD_BTN_NORMAL, "F7 网页直控开着：三个键都不高亮");
        in.snap_age_ms = 9000u;
        v = ui_link_view(&in);
        ui_mode_state(&in, &v, &m1);
        CHECK(ui_mode_btn_style(&m1, UI_TD_COOL) == UI_MD_BTN_OFF
              && ui_mode_btn_style(&m1, UI_TD_PAUSE) == UI_MD_BTN_OFF
              && ui_mode_btn_style(&m1, UI_TD_HEAT) == UI_MD_BTN_OFF, "F7 主控断开：三个键都置灰");

        /* F8 状态文字 */
        STREQ(ui_mode_dir_text(UI_TD_PAUSE), "暂停", "F8 暂停");
        STREQ(ui_mode_dir_text(UI_TD_COOL), "制冷中", "F8 制冷中");
        STREQ(ui_mode_dir_text(UI_TD_HEAT), "制热中", "F8 制热中");
        STREQ(ui_mode_dir_text(UI_TD_EXT), "输出中", "F8 网页直控：输出中");
        STREQ(ui_mode_dir_text(UI_TD_UNKNOWN), "未知", "F8 未知");

        /* F9 功率 = |电压| × 电流；一位小数；电压电流各两位；未知 */
        in = in_up(10000u, 100u);
        in.mode = 0; in.tec_valid = 1u; in.tec_v1 = -8.234f; in.tec_i1 = 3.104f;
        v = ui_link_view(&in);
        ui_mode_state(&in, &v, &m1);
        CHECK(ui_mode_power(&in, &m1, 0, pw, sizeof pw, vi, sizeof vi) == 1, "F9 TEC1 有数");
        STREQ(pw, "25.6 W", "F9 功率一位小数（8.234 × 3.104 = 25.56）");
        STREQ(vi, "-8.23 V · 3.10 A", "F9 电压电流各两位（制热电压为负）");
        CHECK(ui_mode_power(&in, &m1, 1, pw, sizeof pw, vi, sizeof vi) == 0, "F9 TEC2 没取到");
        STREQ(pw, "未知", "F9 功率未知");
        STREQ(vi, "未知", "F9 电压电流未知");
        in.tec_v1 = -0.004f; in.tec_i1 = 0.0f;     /* 关着时的毛刺：不能显示 -0.00 */
        ui_mode_state(&in, &v, &m1);
        (void)ui_mode_power(&in, &m1, 0, pw, sizeof pw, vi, sizeof vi);
        STREQ(pw, "0.0 W", "F9 关着时是 0.0 W，不是'未知'");
        STREQ(vi, "0.00 V · 0.00 A", "F9 不出现 -0.00");
        in.snap_age_ms = 9000u;
        v = ui_link_view(&in);
        ui_mode_state(&in, &v, &m1);
        CHECK(ui_mode_power(&in, &m1, 0, pw, sizeof pw, vi, sizeof vi) == 0, "F9 主控断开：数据缺失");

        /* F10 点了之后：以主控回读为准，不抢先高亮 */
        in = in_up(1000u, 5u);                 /* 点击那一刻现读：seq=5，方向 = 暂停 */
        in.mode = 0; in.dir1 = TCS_TEC_DIR_PAUSE;
        ui_mode_cmd_start(&c, &in, TCS_TEC_DIR_COOL);
        CHECK(c.waiting == 1 && c.seq_at_send == 5u && c.dir == TCS_TEC_DIR_COOL, "F10 开始等");
        c2 = c;                                /* 点击前那一帧（同序号）就算方向对上也不算 */
        CHECK(ui_mode_cmd_poll(&c2, &in, UI_TD_COOL) == UI_MC_WAIT && c2.waiting == 1, "F10 旧快照不算回话");
        in.now_ms = 1100u;
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_PAUSE) == UI_MC_WAIT, "F10 100ms：继续等");
        in.now_ms = 1300u; in.snap_seq = 6u; in.dir1 = TCS_TEC_DIR_COOL;   /* 新快照、方向已经是制冷 */
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_COOL) == UI_MC_DONE && c.waiting == 0, "F10 主控回读对了 → 成了");
        in = in_up(2000u, 6u); in.mode = 0; in.dir1 = TCS_TEC_DIR_PAUSE;
        ui_mode_cmd_start(&c, &in, TCS_TEC_DIR_HEAT);
        in.now_ms = 2300u; in.snap_seq = 7u;
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_PAUSE) == UI_MC_WAIT, "F10 300ms：静置之前不下结论");
        in.now_ms = 2500u;
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_PAUSE) == UI_MC_REJECTED && c.waiting == 0, "F10 静置后仍未变 → 没接受");
        /* 被拒的原因按主控当前状态说 */
        in = in_up(2000u, 6u); in.mode = 0; in.alarm = ALM_ESTOP; in.dir1 = TCS_TEC_DIR_PAUSE;
        ui_mode_cmd_start(&c, &in, TCS_TEC_DIR_HEAT);
        in.now_ms = 2500u; in.snap_seq = 7u;
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_PAUSE) == UI_MC_SAFE, "F10 安全态下被拒");
        in.alarm = 0; in.mode = 1;
        ui_mode_cmd_start(&c, &in, TCS_TEC_DIR_HEAT);
        in.now_ms = 3000u; in.snap_seq = 8u;
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_PAUSE) == UI_MC_NOT_MANUAL, "F10 不在手动模式");
        /* 链路断了 / 一直没来新快照：主控没回话 */
        in = in_up(2000u, 6u); in.mode = 0; in.dir1 = TCS_TEC_DIR_PAUSE;
        ui_mode_cmd_start(&c, &in, TCS_TEC_DIR_COOL);
        in.now_ms = 2600u;                      /* 没有新快照 */
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_PAUSE) == UI_MC_WAIT, "F10 600ms 没新快照：再等等");
        in.now_ms = 2801u;
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_PAUSE) == UI_MC_NO_REPLY && c.waiting == 0,
              "F10 超过 800ms 没新快照 → 没回话");
        in = in_up(2000u, 6u); in.mode = 0;
        ui_mode_cmd_start(&c, &in, TCS_TEC_DIR_COOL);
        in.now_ms = 2600u; in.snap_age_ms = 9000u;
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_PAUSE) == UI_MC_NO_REPLY, "F10 等待中链路断了 → 没回话");
        CHECK(ui_mode_cmd_poll(&c, &in, UI_TD_PAUSE) == UI_MC_DONE, "F10 不在等待时：无事");
        STREQ(ui_mode_cmd_hint(UI_MC_NO_REPLY), "主控没有响应，请检查连接", "F10 提示");
        STREQ(ui_mode_cmd_hint(UI_MC_SAFE), "安全态下不可操作，请先解除报警", "F10 提示");
        STREQ(ui_mode_cmd_hint(UI_MC_NOT_MANUAL), "只能在手动模式下调节", "F10 提示");
        CHECK(ui_mode_cmd_hint(UI_MC_DONE) == NULL, "F10 成了不出提示（高亮自己会动）");
    }

    printf("test_ui_model: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
