/* test_control.c — 联动控制状态机 PC 单测（任务书 §5.1 向量）
 * 滞回边界 ±0.001：(37.7,COOL)(37.3,退出)(36.3,HEAT)(36.7,退出)
 * 传感器丢失任意态→SAFE；SAFE 人工确认才回 IDLE；手动模式不联动但安全态仍生效。
 */
#include <stdio.h>
#include <string.h>
#include "control.h"
#include "param_registry.h"
#include "post.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* param_load_eeprom 引用的 eeprom 桩（本测试不调用，仅满足链接）*/
int eeprom_ready(void) { return 0; }
int eeprom_read_image(uint8_t img[EE_IMG_SIZE]) { (void)img; return -1; }

/* H6：布防判据已迁移到 post 三态机（control 经 post_armed() 读取），测试直写状态 */
static void set_armed(uint8_t a)
{
    post_arm_commit(a ? POST_ARM_ARMED_ONLINE : POST_ARM_UNARMED_ABSENT);
}

/* ====== 输出记录桩 ====== */
static int fan_calls, tec_calls, temp_calls;
static uint8_t last_fan; static uint8_t last_tec_on, last_tec_ch;
static float last_temp_v; static uint8_t last_temp_ch;

static void stub_fan(uint8_t on)        { fan_calls++; last_fan = on; }
static void stub_tec(uint8_t ch, uint8_t on)  { tec_calls++; last_tec_ch = ch; last_tec_on = on; }
static void stub_temp(uint8_t ch, float v)    { temp_calls++; last_temp_ch = ch; last_temp_v = v; }

/* P0-02：链路 CMD 钩子桩（文件作用域：MSVC 不支持嵌套函数定义）*/
static int hook_calls;
static void hook_cb(uint8_t cmd) { (void)cmd; hook_calls++; }

static void reset_stubs(void)
{
    fan_calls = tec_calls = temp_calls = 0;
}

/* 推进 500ms 评估节拍 */
static uint32_t s_now;
static void tick(void) { s_now += 500u; control_poll(s_now); }

static void enter_auto_healthy(void)
{
    control_init(&(control_ops_t){
        .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
    });
    param_registry_init(&(param_hooks_t){0});
    set_armed(1);                                      /* H6：布防=完整 FMEA */
    g_tcs.body_sensor_ok = 1;
    g_tcs.m207_online = 1;
    g_tcs.tc1_act = 25.0f;
    s_now = 10000u;   /* 越过 5s 上电宽限 */
    (void)param_set(PARAM_ID_MODE, 1.0f, 4);
    reset_stubs();
}

int main(void)
{
    /* ====== 滞回边界（默认 37.5/36.5/0.2）====== */
    enter_auto_healthy();
    g_tcs.body_temp = 37.7f + 0.001f;   /* >37.7 进制冷 */
    tick();
    CHECK(control_state() == CTRL_COOL, "37.7+eps -> COOL");
    CHECK(last_temp_ch == 1 && last_temp_v == 15.0f, "cool target 15.0 (常量)");
    CHECK(last_tec_ch == 1 && last_tec_on == 1, "tec1 on");
    CHECK(last_fan == 1, "fan on");

    /* <37.3 退制冷 */
    g_tcs.body_temp = 37.3f - 0.001f;
    tick();
    CHECK(control_state() == CTRL_IDLE, "37.3-eps -> exit COOL");
    CHECK(last_tec_on == 0, "tec off on exit");
    CHECK(last_fan == 0, "fan off on exit");
    /* 滞回带内不抖动：37.5（带内）保持 */
    g_tcs.body_temp = 37.5f;
    tick();
    CHECK(control_state() == CTRL_IDLE, "in-band no action");

    /* <36.3 进制热 */
    g_tcs.body_temp = 36.3f - 0.001f;
    tick();
    CHECK(control_state() == CTRL_HEAT, "36.3-eps -> HEAT");
    CHECK(last_temp_v == 45.0f, "heat target 45.0 (常量)");
    CHECK(last_fan == 1, "fan on heat");
    /* >36.7 退制热 */
    g_tcs.body_temp = 36.7f + 0.001f;
    tick();
    CHECK(control_state() == CTRL_IDLE, "36.7+eps -> exit HEAT");

    /* ====== 传感器丢失任意态 → SAFE（手动模式也生效）====== */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);    /* 手动 */
    control_manual_tec(1, 1);                   /* 手动开 TEC */
    CHECK(last_tec_on == 1, "manual tec on");
    reset_stubs();
    g_tcs.body_sensor_ok = 0;                   /* 拔传感器 */
    tick();
    CHECK(control_state() == CTRL_SAFE, "sensor loss -> SAFE (manual too)");
    CHECK(last_tec_on == 0, "SAFE tec off");
    CHECK(last_fan == 1, "SAFE fan on");
    CHECK(tcs_alarm_active(ALM_BODY_SENSOR) == 1, "ALM_BODY_SENSOR set");

    /* 手动 TEC 在 SAFE 被拒 */
    reset_stubs();
    control_manual_tec(1, 1);
    CHECK(tec_calls == 0, "manual tec blocked in SAFE");
    control_manual_fan(0);
    CHECK(fan_calls == 0, "manual fan-off blocked in SAFE");

    /* 传感器恢复但报警位保持 → 人工确认才回 IDLE（F3）*/
    g_tcs.body_sensor_ok = 1;
    tick();
    CHECK(control_state() == CTRL_SAFE, "alarm latched until ack");
    tcs_alarm_ack();
    tick();
    CHECK(control_state() == CTRL_IDLE, "ack -> IDLE");
    CHECK(tcs_alarm_active(ALM_BODY_SENSOR) == 0, "alarm cleared");

    /* ====== M207 失联 → SAFE（宽限期后）====== */
    enter_auto_healthy();
    tick();              /* 先在健康态跑一拍：boot_ms 基准在此捕获 */
    s_now += 5000u;      /* 越过宽限 */
    g_tcs.m207_online = 0;
    tick();
    CHECK(control_state() == CTRL_SAFE, "m207 loss -> SAFE");
    CHECK(tcs_alarm_active(ALM_M207) == 1, "ALM_M207 set");
    tcs_alarm_ack();     /* m207 仍 offline → 报警位不清 */
    CHECK(tcs_alarm_active(ALM_M207) == 1, "m207 alarm latched while offline");
    g_tcs.m207_online = 1;
    tcs_alarm_ack();
    tick();
    CHECK(control_state() == CTRL_IDLE, "recovered+ack -> IDLE");

    /* ====== M207 上电宽限：前 5s 不因 online=0 进 SAFE ====== */
    control_init(&(control_ops_t){
        .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
    });
    param_registry_init(&(param_hooks_t){0});
    set_armed(1);
    g_tcs.body_sensor_ok = 1;
    g_tcs.m207_online = 0;
    s_now = 1000u;
    tick();
    CHECK(control_state() == CTRL_IDLE, "boot grace no SAFE");
    s_now = 10000u;
    tick();
    CHECK(control_state() == CTRL_SAFE, "after grace SAFE");

    /* ====== 急停 ====== */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 1.0f, 4);
    g_tcs.body_temp = 40.0f;
    tick();   /* COOL */
    reset_stubs();
    tcs_emergency_stop();
    CHECK(control_state() == CTRL_SAFE, "estop -> SAFE");
    CHECK(last_tec_on == 0 && last_fan == 1, "estop tec off fan on");
    CHECK(tcs_alarm_active(ALM_ESTOP) == 1, "ALM_ESTOP set");
    CHECK(param_get(PARAM_ID_MODE) == 0.0f, "estop -> manual mode");
    /* 自持：ack 后仍需 estop 清 latch */
    g_tcs.m207_online = 1;
    tcs_alarm_ack();
    CHECK(tcs_alarm_active(ALM_ESTOP) == 0, "estop latch cleared by ack");
    tick();
    CHECK(control_state() == CTRL_IDLE, "estop cleared -> IDLE");

    /* 过热硬阈值（R1 修复 P0-01：必须切断，不是只报警）
     * R-14：断言前清零桩计数，避免"上一段残留值"造成假阳性。*/
    enter_auto_healthy();
    reset_stubs();
    g_tcs.tc1_act = 48.5f;
    tick();
    CHECK(tcs_alarm_active(ALM_OVERTEMP) == 1, "overtemp alarm at >48");
    CHECK(control_state() == CTRL_SAFE, "P0-01 overtemp -> SAFE");
    CHECK(tec_calls >= 2 && last_tec_on == 0, "P0-01 overtemp both tec off");
    CHECK(fan_calls >= 1 && last_fan == 1, "P0-01 overtemp fan on");
    /* 温度回落但未 ack：仍停在 SAFE（本地超温自持，R-03）*/
    g_tcs.tc1_act = 40.0f;
    tick();
    CHECK(control_state() == CTRL_SAFE, "P0-01 overtemp holds without ack");
    CHECK(control_overtemp_latched() == 1, "P0-01 overtemp local latch set");
    tcs_alarm_ack();
    CHECK(tcs_alarm_active(ALM_OVERTEMP) == 0, "overtemp cleared under 48");
    CHECK(control_overtemp_latched() == 0, "P0-01 overtemp latch cleared by ack");
    tick();
    CHECK(control_state() == CTRL_IDLE, "P0-01 overtemp ack -> IDLE");

    /* R1 修复 P0-02：急停意图幂等（自持后不重复广播；由主循环按 200ms 重发负责）
     * R-15：第二次按键仍要执行本地安全动作，只是不再发第二帧 cmd 8。*/
    {
        control_set_cmd_hook(hook_cb);
        enter_auto_healthy();
        hook_calls = 0;
        tcs_emergency_stop();
        CHECK(hook_calls == 1, "P0-02 estop sends one intent frame");
        CHECK(control_estop_tx_pending() == 1, "P0-02 estop intent pending");
        reset_stubs();
        tcs_emergency_stop();
        CHECK(hook_calls == 1, "P0-02 second estop is idempotent");
        CHECK(tec_calls >= 2 && last_tec_on == 0 && last_fan == 1,
              "P0-02 second estop still forces safe outputs");
        CHECK(control_estop_latched() == 1, "P0-02 estop latch set");
        hook_calls = 0;
        g_tcs.tc1_act = 30.0f;
        g_tcs.body_sensor_ok = 1;
        g_tcs.m207_online = 1;
        tcs_alarm_ack();
        CHECK(control_estop_latched() == 0, "P0-02 ack clears latch");
        /* R-04 判别性：ack 只清"自持"，不清"投递"——断链期间确认后仍要补发 */
        CHECK(control_estop_tx_pending() == 1, "R-04 ack does not cancel pending delivery");
        control_estop_tx_confirm();               /* 模拟快照出现 ALM_ESTOP */
        CHECK(control_estop_tx_pending() == 0, "R-04 peer confirm clears pending");
        CHECK(hook_calls == 1, "P0-02 ack notifies peer (cmd 9)");
        control_set_cmd_hook(0);
    }

    /* ====== WP-A.5 未布防（默认 0）：传感器类异常只记状态沿，不上横幅不进 SAFE ====== */
    enter_auto_healthy();
    set_armed(0);
    reset_stubs();
    g_tcs.body_temp = 37.0f;                           /* 带内温度：FSM 无动作可断言 */
    g_tcs.body_sensor_ok = 0;                          /* 拔体温传感器 */
    tick();
    CHECK(control_state() != CTRL_SAFE, "unarmed: sensor loss no SAFE");
    CHECK(tcs_alarm_active(ALM_BODY_SENSOR) == 0, "unarmed: no ALM_BODY_SENSOR");
    CHECK(fan_calls == 0 && tec_calls == 0, "unarmed: outputs untouched (in-band body)");
    s_now += 5000u;                                    /* 越过 M207 上电宽限 */
    g_tcs.m207_online = 0;                             /* 再拔 M207 */
    tick();
    CHECK(tcs_alarm_active(ALM_M207) == 0, "unarmed: no ALM_M207");
    CHECK(control_state() != CTRL_SAFE, "unarmed: m207 loss no SAFE");

    /* 重新布防 → 同一异常立即进完整 FMEA */
    set_armed(1);
    tick();
    CHECK(control_state() == CTRL_SAFE, "re-arm -> full FMEA SAFE");
    CHECK(tcs_alarm_active(ALM_M207) == 1, "re-arm: ALM_M207 set");
    CHECK(tcs_alarm_active(ALM_BODY_SENSOR) == 1, "re-arm: ALM_BODY_SENSOR set");

    /* 急停不受布防影响（安全边界）*/
    enter_auto_healthy();
    set_armed(0);
    tcs_emergency_stop();
    CHECK(control_state() == CTRL_SAFE && tcs_alarm_active(ALM_ESTOP) == 1,
          "unarmed: estop still SAFE+alarm");

    /* 过热硬阈值不受布防影响（安全边界）*/
    enter_auto_healthy();
    set_armed(0);
    g_tcs.tc1_act = 48.5f;
    tick();
    CHECK(tcs_alarm_active(ALM_OVERTEMP) == 1, "unarmed: overtemp still raises");

    printf("test_control: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
