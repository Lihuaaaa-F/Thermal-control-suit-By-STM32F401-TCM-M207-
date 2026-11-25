/* test_prod_link_only.c — 交付档下 control 不驱动任何本地执行器
 *（S-G h1a，2026-11-25；审查条目 P1-06 的残余部分；独立复核 R5-06：交付档分支在主机门禁里
 * 原来零覆盖）
 *
 * 必须按交付档语义编译：-DTCS_PROFILE=0（= TCS_H743_LINK_ONLY = 1；不定义时 tcs_profile.h
 * 的缺省也是它）。run_all.sh 里这一行自带开关——其余各行一律按台架语义（TCS_PROFILE=1）
 * 编译，走不到交付档的分支。
 *
 * 背景：交付档原来也把 fan_set 接进 control。急停（屏幕 / KEY0 / 控制台 estop 是同一个入口）
 * 和超温兜底都会经它把 PB14 写低——v1 接线表里 PB14 是"→ 风扇光耦，低电平=开"，v2 里风扇
 * 归 ESP32（GPIO42）。现在交付档的 control_init 拒收任何本地执行器钩子。
 *
 * 这里故意把三个执行器钩子都接上"探针"：交付档的任何一条路径只要调到它们，这个用例就红。*/
#include <stdio.h>
#include <string.h>
#include "control.h"
#include "param_registry.h"
#include "post.h"

#if !TCS_H743_LINK_ONLY
#error "test_prod_link_only.c 必须按交付档编译：-DTCS_PROFILE=0（TCS_H743_LINK_ONLY=1）"
#endif

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* ====== 本地执行器探针：被调一次就记一次 ===== */
static int fan_calls, tec_calls, temp_calls, state_calls;
static void spy_fan(uint8_t on)            { (void)on; fan_calls++; }
static void spy_tec(uint8_t ch, uint8_t on) { (void)ch; (void)on; tec_calls++; }
static void spy_temp(uint8_t ch, float v)  { (void)ch; (void)v; temp_calls++; }
static void spy_state(ctrl_state_t st)     { (void)st; state_calls++; }
static int act_calls(void) { return fan_calls + tec_calls + temp_calls; }

/* ====== 链路命令钩子探针（交付档里急停/确认/风扇/超温都从这里发往 ESP32）===== */
static uint8_t cmd_log[256];
static int cmd_n;
static void cmd_cb(uint8_t cmd) { if (cmd_n < (int)sizeof cmd_log) cmd_log[cmd_n++] = cmd; }
static int cmd_count(uint8_t cmd)
{
    int n = 0;
    for (int i = 0; i < cmd_n; i++) if (cmd_log[i] == cmd) n++;
    return n;
}

/* 按交付档的装配方式起步：执行器钩子照样递进去（交付档必须拒收），其余回到上电状态 */
static void boot_prod(void)
{
    control_set_cmd_hook(cmd_cb);
    cmd_n = 0;
    fan_calls = tec_calls = temp_calls = state_calls = 0;
    control_init(&(control_ops_t){
        .fan_set = spy_fan, .tec_sw = spy_tec, .tec_temp = spy_temp, .on_state = spy_state,
    });
    param_registry_init(&(param_hooks_t){0});
    post_arm_commit(POST_ARM_UNARMED_ABSENT);
    g_tcs.body_sensor_ok = 1;
    g_tcs.m207_online = 1;
    g_tcs.tc1_act = 25.0f;
    g_tcs.body_temp = 36.5f;
    g_tcs.mode = 0;
}

static void test_estop(void)
{
    printf("[prod_estop]\n");
    boot_prod();
    CHECK(act_calls() == 0, "control_init itself touches no actuator");

    /* 急停：屏幕热区 / KEY0 / 控制台 estop 都走这一个函数 */
    tcs_emergency_stop();
    CHECK(cmd_n == 1 && cmd_log[0] == 8, "estop: cmd 8 handed to the link hook exactly once");
    CHECK(act_calls() == 0, "estop: no local actuator call (PB14 / M207 UART untouched)");
    CHECK(control_estop_latched() == 1 && (g_tcs.alarm_bits & ALM_ESTOP) != 0, "estop: local latch + alarm bit");
    CHECK(control_estop_tx_pending() == 1, "estop: delivery intent pending");
    CHECK(control_state() == CTRL_SAFE, "estop: local state SAFE");
    CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0 && g_tcs.fan_on == 1,
          "estop: local display state as before (TEC off, fan on)");
    CHECK((uint8_t)param_get(PARAM_ID_MODE) == 0, "estop: mode back to manual");

    /* 再按一次：本地动作照做、不再重复发首帧（重发归主循环）；仍然不碰执行器 */
    tcs_emergency_stop();
    CHECK(cmd_n == 1, "estop again: not re-sent from here");
    CHECK(act_calls() == 0, "estop again: still no actuator");
    /* 对端确认了投递、但本地还在自持：再按也不算"新的一轮"，首帧不重发 */
    control_estop_tx_confirm();
    tcs_emergency_stop();
    CHECK(cmd_n == 1, "estop while still latched: first frame not re-sent");
    CHECK(control_estop_tx_pending() == 1, "estop while latched: delivery intent armed again");
    CHECK(act_calls() == 0, "estop while latched: no actuator");

    /* 确认报警：cmd 9 给对端；本地清自持 */
    tcs_alarm_ack();
    CHECK(cmd_n == 2 && cmd_log[1] == 9, "ack: cmd 9 sent");
    CHECK(control_estop_latched() == 0, "ack clears the local latch");
    CHECK(act_calls() == 0, "ack: no actuator");

    /* 自持清掉之后再急停：这是新的一轮，首帧重新发 */
    tcs_emergency_stop();
    CHECK(cmd_n == 3 && cmd_log[2] == 8, "estop after ack: new round, cmd 8 sent again");
    CHECK(act_calls() == 0, "estop after ack: no actuator");

    /* 不传任何钩子也一样能急停（不因空钩子解引用而倒下）*/
    control_init(NULL);
    cmd_n = 0;
    tcs_emergency_stop();
    CHECK(cmd_n == 1 && cmd_log[0] == 8, "estop with no ops at all: cmd 8 still sent");
    printf("  prod_estop done\n");
}

static void test_manual_and_overtemp(void)
{
    printf("[prod_manual_overtemp]\n");
    boot_prod();

    /* 手动直控 / 风扇切换：交付档里风扇切换是发给主控的命令，不走本地 */
    control_manual_fan(0);
    control_manual_fan(1);
    control_manual_tec(1, 1);
    control_manual_tec(2, 0);
    control_fan_toggle();
    control_on_mode_change(1);
    CHECK(act_calls() == 0, "manual controls: no local actuator call");
    CHECK(cmd_count(TCS_CMD_FAN_TOGGLE) == 1 && cmd_n == 1, "fan toggle: one cmd to the master, nothing else");

    /* 超温兜底（交付档主循环每拍用快照温度调它）：请求对端停机，不碰执行器 */
    boot_prod();
    g_tcs.tc1_act = 50.0f;
    control_observe_temp(g_tcs.tc1_act);
    CHECK(cmd_n == 1 && cmd_log[0] == TCS_CMD_OVERTEMP_STOP, "overtemp: stop request sent to the master");
    CHECK(act_calls() == 0, "overtemp: no local actuator call");
    CHECK(control_overtemp_latched() == 1 && control_state() == CTRL_SAFE, "overtemp: local latch + SAFE");
    control_observe_temp(g_tcs.tc1_act);
    CHECK(cmd_n == 1, "overtemp again: not re-sent from here");
    CHECK(act_calls() == 0, "overtemp again: no actuator");
    printf("  prod_manual_overtemp done\n");
}

/* control_poll 在交付档本来不跑。这里照样跑一遍：万一哪天有人在交付档调了它，
 * 也不许碰本地执行器；同时确认本地状态机该进的 SAFE / 该退的 IDLE 都还对。*/
static void test_poll_soak(void)
{
    printf("[prod_poll_soak]\n");
    boot_prod();
    post_arm_commit(POST_ARM_ARMED_ONLINE);      /* 最严：布防 = 完整 FMEA，最容易去动执行器 */

    uint32_t t = 0;
    int saw_safe = 0, saw_idle = 0, max_act = 0;
    for (int i = 0; i < 6000; i++, t += 10u) {
        switch (i / 500) {
        case 0:  /* 上电宽限内，全健康，自动模式 */ 
            g_tcs.mode = 1; g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1;
            g_tcs.tc1_act = 25.0f; g_tcs.body_temp = 36.5f; break;
        case 1:  /* 体温过热 → 该进 COOL */ 
            g_tcs.body_temp = 40.0f; break;
        case 2:  /* 体温过低 → 该进 HEAT */
            g_tcs.body_temp = 34.0f; break;
        case 3:  /* 传感器丢失 → 该进 SAFE */
            g_tcs.body_sensor_ok = 0; break;
        case 4:  /* 水温超硬阈值 */
            g_tcs.body_sensor_ok = 1; g_tcs.tc1_act = 55.0f; break;
        case 5:  /* M207 失联 */
            g_tcs.tc1_act = 25.0f; g_tcs.m207_online = 0; break;
        case 6:  /* 全部恢复 */
            g_tcs.m207_online = 1; g_tcs.body_temp = 36.5f; break;
        default: break;
        }
        control_poll(t);
        control_link_follow();
        if (control_state() == CTRL_SAFE) saw_safe = 1;
        if (control_state() == CTRL_IDLE) saw_idle = 1;
        if (act_calls() > max_act) max_act = act_calls();
        /* 本地自持清掉，让 link_follow 有机会把诊断态切回 IDLE（和主循环里 ack 之后的情形一样）*/
        if (i % 500 == 499) {
            g_tcs.alarm_bits = 0;
            control_estop_tx_confirm();
            control_overtemp_tx_confirm();
            control_link_follow();
        }
    }
    CHECK(max_act == 0, "control_poll/link_follow soak: local actuators never called");
    CHECK(saw_safe, "soak: SAFE still entered locally when it should be");
    CHECK(saw_idle, "soak: local diagnostic state still returns to IDLE");
    CHECK(cmd_count(TCS_CMD_OVERTEMP_STOP) >= 1, "soak: overtemp stop request still goes to the master");
    CHECK(cmd_count(8) == 0, "soak: control_poll never emits an estop by itself");
    CHECK(state_calls > 0, "soak: on_state notification hook still wired in the delivery profile");
    printf("  prod_poll_soak done (cmd_n=%d)\n", cmd_n);
}

int main(void)
{
    test_estop();
    test_manual_and_overtemp();
    test_poll_soak();
    printf("test_prod_link_only: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
