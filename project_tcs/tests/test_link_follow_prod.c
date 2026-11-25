/* test_link_follow_prod.c — R5-06 残余（2026-11-25）：按**交付档**编译的主机用例，覆盖
 * control_link_follow（project_tcs/App/tcs/control.c，#if TCS_H743_LINK_ONLY 下）。
 *
 * 为什么单独一份：run_all.sh 里其余用例一律按台架档（-DTCS_PROFILE=1）编译，
 * 交付档专属的 control_link_follow 在那些编译单元里根本不存在——92899c2 加的这段判定
 * 此前在主机门禁里零覆盖，把它的任何一行改回去门禁仍然全绿（R5 复核 R5-06）。
 * 本文件开头用 #error 保证自己只能按交付档编译（-DTCS_PROFILE=0 ⇒ TCS_H743_LINK_ONLY=1）。
 *
 * 交付档下屏（H743）进 SAFE 只有两条路：急停（tcs_emergency_stop）与超温兜底
 * （control_observe_temp）。退出由 control_link_follow 判：
 *   本机无自持 / 无待投递意图（四个标志全清） 且 快照无安全类报警 → IDLE；只动诊断态，不碰输出。
 * 用例经公开 API 驱动这四个标志与快照报警位，不需要可测接缝。
 * 回滚式验证：tests/mutation_link_follow.py 把判据逐个改回错误写法，本用例必须变红。
 */
#include <stdio.h>
#include <string.h>
#include "control.h"
#include "param_registry.h"
#include "post.h"

#if !TCS_H743_LINK_ONLY
#error "本用例必须按交付档编译：-DTCS_PROFILE=0（TCS_H743_LINK_ONLY=1）"
#endif

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* param_load_eeprom 引用的 eeprom 桩（本用例不调用，仅满足链接）*/
int eeprom_ready(void) { return 0; }
int eeprom_read_image(uint8_t img[EE_IMG_SIZE]) { (void)img; return -1; }

/* ====== 输出 / 命令钩子记录桩 ====== */
static int fan_calls, tec_calls, hook_calls, state_calls;
static uint8_t last_fan, last_tec_on, hook_last;
static ctrl_state_t last_state_from, last_state_to;

static void stub_fan(uint8_t on)       { fan_calls++; last_fan = on; }
static void stub_tec(uint8_t ch, uint8_t on) { (void)ch; tec_calls++; last_tec_on = on; }
static void stub_state(ctrl_state_t st)
{
    state_calls++;
    last_state_from = last_state_to;
    last_state_to = st;
}
static void hook_cb(uint8_t cmd) { hook_calls++; hook_last = cmd; }

/* 交付档上电后的样子：主控快照健康的默认值 */
static void boot(void)
{
    control_init(&(control_ops_t){
        .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = 0, .on_state = stub_state,
    });
    param_registry_init(&(param_hooks_t){0});
    post_arm_commit(POST_ARM_ARMED_ONLINE);
    control_set_cmd_hook(hook_cb);
    memset(&g_tcs, 0, sizeof(g_tcs));
    g_tcs.body_sensor_ok = 1;
    g_tcs.m207_online = 1;
    g_tcs.tc1_act = 25.0f;
    fan_calls = tec_calls = hook_calls = state_calls = 0;
    last_state_to = CTRL_IDLE;
}

static const uint16_t SAFETY_ALARMS = (uint16_t)(ALM_ESTOP | ALM_OVERTEMP | ALM_BODY_SENSOR | ALM_M207);

/* 屏上按急停 → 主控已执行（快照出现 ESTOP）→ 主控已确认（快照报警位消失）。
 * 对端确认由主循环做：前者 control_estop_tx_confirm()，后者 control_estop_clear()。*/
static void estop_then_peer(uint8_t peer_executed, uint8_t peer_acked)
{
    tcs_emergency_stop();
    if (peer_executed) { g_tcs.alarm_bits |= ALM_ESTOP; control_estop_tx_confirm(); }
    if (peer_acked)    { g_tcs.alarm_bits &= (uint16_t)~ALM_ESTOP; control_estop_clear(); }
}

int main(void)
{
    /* ---- A：不在 SAFE 时 follow 什么都不做（连 COOL 也不会被拉回 IDLE），不碰输出 ---- */
    boot();
    control_link_follow();
    CHECK(control_state() == CTRL_IDLE && state_calls == 0, "A: IDLE 时 follow 不产生状态迁移");
    {
        /* 交付档主循环不跑 control_poll；这里借它把状态机推到 COOL，只为证明 follow 只管 SAFE */
        g_tcs.mode = 1; g_tcs.body_temp = 38.5f;
        control_poll(10000u);
        control_poll(10600u);
        CHECK(control_state() == CTRL_COOL, "A: 前置——状态机在 COOL");
        {
            int f = fan_calls, t = tec_calls;
            control_link_follow();
            CHECK(control_state() == CTRL_COOL, "A: COOL 时 follow 不改状态");
            CHECK(fan_calls == f && tec_calls == t, "A: follow 不碰输出");
        }
    }

    /* ---- B：急停进入 SAFE；逐步清除；只有全部清了才退出 ---- */
    boot();
    tcs_emergency_stop();
    CHECK(control_state() == CTRL_SAFE, "B: 急停进入 SAFE");
    CHECK(tec_calls == 2 && last_tec_on == 0 && last_fan == 1, "B: 进 SAFE 的输出（TEC 关、风扇开）");
    CHECK(hook_calls == 1 && hook_last == TCS_CMD_ESTOP, "B: 下发了一次急停命令");
    {
        int f = fan_calls, t = tec_calls, h = hook_calls;
        int sc = state_calls;                    /* 已记了进 SAFE 的那一次 IDLE→SAFE */
        control_link_follow();
        CHECK(control_state() == CTRL_SAFE, "B: 自持 + 待投递 + 快照带 ESTOP → 不退出");
        /* 主控已执行（快照带 ESTOP）：只清"待投递" */
        g_tcs.alarm_bits |= ALM_ESTOP;
        control_estop_tx_confirm();
        control_link_follow();
        CHECK(control_state() == CTRL_SAFE, "B: 主控已执行、自持未清 → 不退出");
        /* 主控已确认（快照报警位消失）：清自持；但本机快照里那一位还在的这一拍不退出 */
        control_estop_clear();
        g_tcs.alarm_bits |= ALM_ESTOP;
        control_link_follow();
        CHECK(control_state() == CTRL_SAFE, "B: 本机标志全清、快照仍带 ESTOP → 不退出");
        CHECK(state_calls == sc, "B: 不退出期间不产生任何状态迁移");
        /* 下一帧快照到了：报警位消失 → 退出 */
        g_tcs.alarm_bits &= (uint16_t)~ALM_ESTOP;
        control_link_follow();
        CHECK(control_state() == CTRL_IDLE, "B: 四个标志全清且快照无安全类报警 → IDLE");
        CHECK(last_state_from == CTRL_SAFE && last_state_to == CTRL_IDLE && state_calls == sc + 1,
              "B: 状态回调看到的恰好是一次 SAFE→IDLE");
        control_link_follow();
        CHECK(state_calls == sc + 1, "B: 已在 IDLE 时再调 follow 不重复迁移");
        CHECK(fan_calls == f && tec_calls == t && hook_calls == h, "B: follow 全程不碰输出、不发命令");
    }

    /* ---- C：超温兜底进入 SAFE；确认口径独立（超温确认 + 温度回落后的人工确认）---- */
    boot();
    control_observe_temp(50.0f);
    CHECK(control_state() == CTRL_SAFE && (g_tcs.alarm_bits & ALM_OVERTEMP), "C: 超温进入 SAFE");
    CHECK(hook_calls == 1 && hook_last == TCS_CMD_OVERTEMP_STOP, "C: 下发了一次超温停机请求");
    control_link_follow();
    CHECK(control_state() == CTRL_SAFE, "C: 刚进 SAFE 不退出");
    g_tcs.alarm_bits &= (uint16_t)~ALM_OVERTEMP;   /* 快照：主控已确认 */
    control_overtemp_tx_confirm();
    control_link_follow();
    CHECK(control_state() == CTRL_SAFE, "C: 待投递已清、本地超温自持未清 → 不退出");
    tcs_alarm_ack();                                /* 温度已回落（tc1_act=25）→ 清自持 */
    control_link_follow();
    CHECK(control_state() == CTRL_IDLE, "C: 自持清掉、快照无安全类报警 → IDLE");

    /* ---- D：四个标志各自单独存在时都拦住退出 ---- */
    /* D1 只剩急停自持（待投递已清、快照报警位已消失） */
    boot(); estop_then_peer(1, 0);
    control_link_follow();
    CHECK(control_state() == CTRL_SAFE, "D1: 只剩急停自持 → 不退出");
    /* D2 只剩急停待投递（自持已清、快照报警位已消失；对端其实还没执行） */
    boot();
    tcs_emergency_stop();
    control_estop_clear();                          /* 本机自持清了，但没人确认过投递 */
    g_tcs.alarm_bits = 0;
    control_link_follow();
    CHECK(control_state() == CTRL_SAFE, "D2: 只剩急停待投递 → 不退出");
    /* D3 只剩超温自持（待投递已清） */
    boot();
    control_observe_temp(50.0f);
    g_tcs.alarm_bits = 0;
    control_overtemp_tx_confirm();
    control_link_follow();
    CHECK(control_state() == CTRL_SAFE, "D3: 只剩超温自持 → 不退出");
    /* D4 超温待投递：只要超温自持在它就一定同时在（tcs_alarm_ack 一次清两者，没有"只剩它"的
     * 公开路径）——由 D3 与变异脚本的 F3（不看两个待投递标志）共同覆盖 */

    /* ---- E：本机标志全清，但快照带任一安全类报警 → 保持 SAFE（对端拒绝确认时就是这样）---- */
    {
        const uint16_t each[4] = { ALM_ESTOP, ALM_OVERTEMP, ALM_BODY_SENSOR, ALM_M207 };
        for (int i = 0; i < 4; i++) {
            boot(); estop_then_peer(1, 1);
            g_tcs.alarm_bits = each[i];
            control_link_follow();
            if (control_state() != CTRL_SAFE) printf("  报警位 0x%04x 没拦住退出\n", each[i]);
            CHECK(control_state() == CTRL_SAFE, "E: 快照带安全类报警 → 不退出");
        }
    }

    /* ---- F：非安全类报警（EEPROM / 探头开路）不拦退出；四类安全报警全都没有才退出 ---- */
    boot(); estop_then_peer(1, 1);
    g_tcs.alarm_bits = (uint16_t)(ALM_EEPROM | ALM_SENSOR_OPEN);
    control_link_follow();
    CHECK(control_state() == CTRL_IDLE, "F: 非安全类报警不拦退出");
    boot(); estop_then_peer(1, 1);
    g_tcs.alarm_bits = (uint16_t)(SAFETY_ALARMS + 1u);   /* 安全类之外的所有位 */
    control_link_follow();
    CHECK(control_state() == CTRL_IDLE, "F: 安全类之外的位置满也不拦");

    printf("test_link_follow_prod: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
