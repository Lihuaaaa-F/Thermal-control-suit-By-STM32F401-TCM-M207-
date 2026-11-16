/* test_control_esp32.c — 联动控制状态机 PC 单测（R1e；向量自 project_tcs test_control.c
 * 同源移植——Q5/Q10：滞回四向量 ±0.001 预期先于板上 E-21 注入取证）
 * 源=ESP32 侧 tcs_core（control.c+param_registry.c+post_pure.c+tcs_link/proto.c）。
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

/* P0-08 持久化钩子桩（文件作用域：MSVC 不支持嵌套函数定义）*/
static uint8_t s_persisted;
static void    persist_cb(uint8_t latch) { s_persisted = latch; }
static uint8_t load_cb(void)             { return SAFETY_BIT_ESTOP; }

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

    /* ====== M06-F-06：无有效体温样本 → auto 不得开 HEAT（默认未布防场景）====== */
    enter_auto_healthy();
    set_armed(0);              /* 上电默认未布防：只记沿、不进 SAFE，最容易"带病加热" */
    tick();                    /* 捕获 boot 基准 */
    (void)param_set(PARAM_ID_MODE, 1.0f, 4);
    g_tcs.body_sensor_ok = 0;  /* 传感器未接（上电默认）*/
    g_tcs.body_temp = 0.0f;    /* 旧实现把这个 0 当真实体温 → 判 HEAT */
    reset_stubs();
    tick();
    CHECK(control_state() == CTRL_IDLE, "no body sample -> IDLE (unarmed)");
    CHECK(last_tec_on == 0, "no body sample -> TEC stays off");
    /* 有样本后才允许进入 HEAT */
    g_tcs.body_sensor_ok = 1;
    g_tcs.body_temp = 30.0f;   /* < cold-hyst → HEAT */
    tick();
    CHECK(control_state() == CTRL_HEAT, "valid cold sample -> HEAT");

    /* ====== M12-F-01：M207 在线但通道传感器断线 → 按温度源丢失进 SAFE ====== */
    enter_auto_healthy();
    tick();                    /* 捕获 boot 基准 */
    s_now += 5000u;            /* 越过宽限 */
    g_tcs.m207_online = 1;
    g_tcs.m207_sensor_open = 1;
    tick();
    CHECK(control_state() == CTRL_SAFE, "ch sensor open -> SAFE");
    /* 语义分离：探头开路报 ALM_SENSOR_OPEN，不能借用"通信失联"位 */
    CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 1, "ALM_SENSOR_OPEN set");
    CHECK(tcs_alarm_active(ALM_M207) == 0, "ALM_M207 NOT used for open probe");
    tcs_alarm_ack();           /* 传感器仍断线 → 锁存不清 */
    CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 1, "sensor-open latched through ack");
    g_tcs.m207_sensor_open = 0;
    tcs_alarm_ack();
    CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 0, "sensor-open cleared by ack");
    tick();
    CHECK(control_state() == CTRL_IDLE, "sensor open cleared+ack -> IDLE");

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

    /* 过热硬阈值 */
    enter_auto_healthy();
    g_tcs.tc1_act = 48.5f;
    tick();
    CHECK(tcs_alarm_active(ALM_OVERTEMP) == 1, "overtemp alarm at >48");
    g_tcs.tc1_act = 40.0f;
    tcs_alarm_ack();
    CHECK(tcs_alarm_active(ALM_OVERTEMP) == 0, "overtemp cleared under 48");

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

    /* ====== R1 修复的判别性用例（旧实现必然失败）====== */

    /* P0-01：超温必须"切断"，不是只报警。
     * R-14：先 reset_stubs，断言用调用计数（旧实现的 last_tec_on/last_fan 是上一段残留 → 假阳性）。*/
    enter_auto_healthy();
    reset_stubs();
    g_tcs.tc1_act = 49.0f;
    tick();
    CHECK(control_state() == CTRL_SAFE, "P0-01 overtemp -> SAFE");
    CHECK(tec_calls >= 2 && last_tec_on == 0, "P0-01 overtemp -> both TEC off");
    CHECK(fan_calls >= 1 && last_fan == 1, "P0-01 overtemp -> fan on");
    CHECK(control_safety_latched() != 0, "P0-01 overtemp -> latch set");
    /* 温度回落但未 ack：不得自动退出 SAFE（旧实现 alarm 会自己消失）*/
    g_tcs.tc1_act = 40.0f;
    tick();
    CHECK(control_state() == CTRL_SAFE, "P0-01 overtemp latch holds without ack");
    tcs_alarm_ack();
    tick();
    CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
          "P0-01 ack path returns IDLE (smoke)");

    /* P0-04（门控本身，与状态机无关）：锁存置位而 state 仍 IDLE 时，手动"开"必须被压成 0。
     * R-14：旧实现没有锁存，这条永远通过不了（manual 会直接写 1）——真正的判别性用例。*/
    enter_auto_healthy();
    reset_stubs();
    control_test_trip(SAFETY_BIT_ESTOP);       /* 只置锁存，不走 FSM */
    control_manual_tec(1, 1);
    CHECK(tec_calls == 1 && last_tec_on == 0, "P0-04 gate blocks manual on while IDLE+latched");
    CHECK(control_state() == CTRL_IDLE, "P0-04 gate does not change state");

    /* P0-04（整链路）：急停后在途手动/状态机都不能把 TEC 重新打开 */
    enter_auto_healthy();
    tcs_emergency_stop();
    reset_stubs();
    control_manual_tec(1, 1);                  /* 模拟控制台/网页的在途写 */
    /* SAFE 短路 + 锁存双保险：manual 直接返回，连 out_tec_sw 都不进 */
    CHECK(tec_calls == 0 && last_tec_on == 0, "P0-04 manual blocked after estop (smoke: SAFE short-circuit)");
    g_tcs.body_temp = 30.0f;                   /* 自动模式想制热 */
    tick();
    CHECK(last_tec_on == 0, "P0-04 poll cannot re-enable TEC while latched");
    CHECK(control_state() == CTRL_SAFE, "P0-04 still SAFE while latched (smoke)");

    /* P0-08：持久化锁存在启动时恢复 → 复位不能解除安全态*/
    {
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp,
            .on_state = 0, .safety_persist = persist_cb, .safety_load = load_cb,
        });
        param_registry_init(&(param_hooks_t){0});
        CHECK(control_safety_latched() == SAFETY_BIT_ESTOP, "P0-08 latch restored at init");
        CHECK(control_state() == CTRL_SAFE, "P0-08 restored latch -> SAFE at boot");
        CHECK(tcs_alarm_active(ALM_ESTOP) == 1, "P0-08 restored latch -> ALM_ESTOP");
        s_persisted = 0;
        g_tcs.tc1_act = 49.0f;
        tick();
        CHECK((s_persisted & SAFETY_BIT_OVERTEMP) != 0, "P0-08 trip persists latch");
    }

    /* ack 条件：源未恢复时 ack 不得清锁存*/
    enter_auto_healthy();
    tcs_emergency_stop();
    g_tcs.body_sensor_ok = 0;
    tcs_alarm_ack();
    CHECK(control_safety_latched() != 0, "ack blocked while source not recovered");
    g_tcs.body_sensor_ok = 1;
    tcs_alarm_ack();
    CHECK(control_safety_latched() == 0, "ack clears after source recovered");

    printf("test_control: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
