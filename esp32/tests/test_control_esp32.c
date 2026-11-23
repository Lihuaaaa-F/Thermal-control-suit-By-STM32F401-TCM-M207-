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

    /* ====== 2026-11-19：统一命令执行端（网页 WS 与屏幕 0x15 共用一份语义）====== */
    {
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);   /* 手动，便于直控 */
        reset_stubs();
        CHECK(control_apply_cmd(TCS_CMD_TEC1_ON) == 0 && last_tec_on == 1, "cmd tec1 on");
        CHECK(control_apply_cmd(TCS_CMD_TEC1_OFF) == 0 && last_tec_on == 0, "cmd tec1 off");
        {
            int f0 = g_tcs.fan_on;
            CHECK(control_apply_cmd(TCS_CMD_FAN_TOGGLE) == 0 && g_tcs.fan_on != f0,
                  "cmd fan toggle");
        }
        CHECK(control_apply_cmd(TCS_CMD_MODE_AUTO) == 0 && g_tcs.mode == 1, "cmd mode auto");
        CHECK(control_apply_cmd(TCS_CMD_MODE_MANUAL) == 0 && g_tcs.mode == 0, "cmd mode manual");
        CHECK(control_apply_cmd(TCS_CMD_ALARM_ACK) == 0, "cmd alarm ack");
        CHECK(control_apply_cmd(99) == -1, "unknown cmd rejected");
        CHECK(control_apply_cmd(TCS_CMD_M207_SAVE) == -1, "cmd 10 handled by caller (not control)");
    }

    /* ====== 2026-11-23 复核 D0：cmd 11（超温停机）不得被命令统一吞掉 ====== */
    {
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 1.0f, 4);
        g_tcs.tc1_act = 40.0f;          /* 远端还没报超温，靠命令触发 */
        reset_stubs();
        CHECK(control_apply_cmd(TCS_CMD_OVERTEMP_STOP) == 0, "cmd 11 accepted");
        CHECK(control_safety_latched() & SAFETY_BIT_OVERTEMP, "cmd 11 latches OVERTEMP");
        CHECK(tcs_alarm_active(ALM_OVERTEMP) == 1, "cmd 11 raises ALM_OVERTEMP");
        CHECK(last_tec_on == 0 && tec_calls >= 2, "cmd 11 stops both TEC");
        CHECK((control_safety_latched() & SAFETY_BIT_ESTOP) == 0, "cmd 11 is not estop");
        g_tcs.tc1_act = 40.0f;
        tcs_alarm_ack();
        CHECK((control_safety_latched() & SAFETY_BIT_OVERTEMP) == 0, "cmd 11 latch clearable by ack");
    }

    /* ====== 2026-11-19：通信类故障 2s 健康后自动恢复（急停/超温不在此列）====== */
    {
        enter_auto_healthy();
        tick();                       /* 捕获 boot 基准 */
        s_now += 5000u;
        tcs_alarm_raise(ALM_M207);      /* 真实路径：故障分支会同时置显示位 */
        control_test_trip(SAFETY_BIT_M207_LOST);
        tick();
        CHECK(control_state() == CTRL_SAFE, "comm trip -> SAFE");
        CHECK(tcs_alarm_active(ALM_M207) == 1, "comm trip -> ALM_M207");
        /* 源健康持续 2s（4 拍）→ 自动清锁存，无需 ack */
        for (int i = 0; i < 5; i++) { s_now += 500u; control_poll(s_now); }
        CHECK(control_state() == CTRL_IDLE, "comm fault auto-recovers (no ack)");
        CHECK(tcs_alarm_active(ALM_M207) == 0, "comm alarm cleared");

        /* 反证：源不健康时不自动清 */
        control_test_trip(SAFETY_BIT_M207_LOST);
        g_tcs.m207_online = 0;
        for (int i = 0; i < 6; i++) { s_now += 500u; control_poll(s_now); }
        CHECK(control_state() == CTRL_SAFE, "unhealthy source -> stays SAFE");
        g_tcs.m207_online = 1;
        for (int i = 0; i < 5; i++) { s_now += 500u; control_poll(s_now); }
        CHECK(control_state() == CTRL_IDLE, "recovered -> auto exit");

        /* 急停必须人工确认：健康 3s 也不自动清 */
        tcs_emergency_stop();
        for (int i = 0; i < 6; i++) { s_now += 500u; control_poll(s_now); }
        CHECK(control_state() == CTRL_SAFE, "estop stays latched without ack");
        CHECK(tcs_alarm_active(ALM_ESTOP) == 1, "ALM_ESTOP still set");
        tcs_alarm_ack();
        tick();
        CHECK(control_state() == CTRL_IDLE, "estop clears by ack");
    }

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

    /* ====== U 线 W2（2026-11-24）：退出 SAFE 复原故障前输出；100ms 节拍下的时序 ======
     * 判别性：W2-1/W2-3/W2-6/W2-7/W2-8 在旧实现（退出 SAFE 一律 out_fan(0)、TEC 不复原、
     * 每拍重申输出）上必然失败。服务任务的 100ms 节拍本身是 FreeRTOS 代码，
     * 由真机日志时间戳取证（见结果文档），这里验证状态机在 100ms 调用下的行为。*/
#define tick100() do { s_now += 100u; control_poll(s_now); } while (0)

    /* W2-1 手动模式：故障前风扇开着，体温传感器自愈后风扇应复原，且自愈 ≤2.1s */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();                                   /* 捕获 boot 基准 */
    control_manual_fan(1);
    CHECK(g_tcs.fan_on == 1, "W2-1 precondition: manual fan on");
    g_tcs.body_sensor_ok = 0;
    tick100();
    CHECK(control_state() == CTRL_SAFE && g_tcs.fan_on == 1, "W2-1 body lost -> SAFE, fan full");
    g_tcs.body_sensor_ok = 1;
    {
        uint32_t t_ok = s_now;
        int n = 0;
        while (control_state() == CTRL_SAFE && n < 40) { tick100(); n++; }
        uint32_t dt = s_now - t_ok;
        CHECK(control_state() == CTRL_IDLE, "W2-1 auto exit after 2s healthy");
        CHECK(dt >= 2000u && dt <= 2100u, "W2-1 exit within [2000,2100]ms of health");
        CHECK(g_tcs.fan_on == 1, "W2-1 fan restored to pre-fault state");
    }

    /* W2-2 故障前风扇关着 → 退出后仍关（复原不是"一律开"）
     * 注意 g_tcs 不被 control_init 复位：上一段留下 fan=1，这里必须显式关掉再测 */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_fan(0);
    CHECK(g_tcs.fan_on == 0, "W2-2 precondition: fan off");
    g_tcs.body_sensor_ok = 0; tick100();
    CHECK(control_state() == CTRL_SAFE, "W2-2 -> SAFE");
    g_tcs.body_sensor_ok = 1;
    for (int i = 0; i < 25; i++) tick100();
    CHECK(control_state() == CTRL_IDLE && g_tcs.fan_on == 0, "W2-2 fan stays off");

    /* W2-3 手动 TEC2：体温传感器故障（模块全程在线）自愈后复原 */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_tec(2, 1);
    CHECK(g_tcs.tec2_on == 1, "W2-3 precondition: TEC2 on");
    g_tcs.body_sensor_ok = 0; tick100();
    CHECK(g_tcs.tec2_on == 0, "W2-3 SAFE turns TEC2 off");
    g_tcs.body_sensor_ok = 1;
    for (int i = 0; i < 25; i++) tick100();
    CHECK(control_state() == CTRL_IDLE, "W2-3 auto exit");
    CHECK(g_tcs.tec2_on == 1, "W2-3 TEC2 restored after body-sensor-only fault");

    /* W2-4 模块通信故障：自愈后 TEC2 不复原（模块侧状态未知）；风扇复原 */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    s_now += 5000u;                           /* 越过 M207 上电宽限 */
    control_manual_tec(2, 1);
    control_manual_fan(1);
    g_tcs.m207_online = 0; tick100();
    CHECK(control_state() == CTRL_SAFE, "W2-4 m207 loss -> SAFE");
    g_tcs.m207_online = 1;
    for (int i = 0; i < 25; i++) tick100();
    CHECK(control_state() == CTRL_IDLE, "W2-4 auto exit");
    CHECK(g_tcs.tec2_on == 0, "W2-4 TEC2 NOT restored after module fault");
    CHECK(g_tcs.fan_on == 1, "W2-4 fan restored after module fault");

    /* W2-5 急停：自动被切成手动 → 风扇不复原；TEC 全关 */
    enter_auto_healthy();
    g_tcs.body_temp = 40.0f; tick();          /* 自动 COOL：风扇开 */
    CHECK(control_state() == CTRL_COOL && g_tcs.fan_on == 1, "W2-5 precondition: COOL fan on");
    tcs_emergency_stop();
    tcs_alarm_ack();
    tick100();
    CHECK(control_state() == CTRL_IDLE, "W2-5 estop cleared");
    CHECK(g_tcs.fan_on == 0, "W2-5 fan off after estop reset (auto was forced to manual)");
    CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0, "W2-5 TEC stay off after estop reset");

    /* W2-6 自动模式制冷中体温传感器抖动：自愈后风扇不灭，状态机下一拍接管制冷 */
    enter_auto_healthy();
    g_tcs.body_temp = 40.0f; tick();          /* COOL */
    g_tcs.body_sensor_ok = 0; tick100();      /* SAFE */
    CHECK(control_state() == CTRL_SAFE, "W2-6 -> SAFE");
    g_tcs.body_sensor_ok = 1;
    for (int i = 0; i < 25; i++) tick100();
    CHECK(g_tcs.mode == 1, "W2-6 still auto mode");
    CHECK(control_state() == CTRL_COOL, "W2-6 SAFE exited and COOL resumed");
    CHECK(g_tcs.fan_on == 1 && g_tcs.tec1_on == 1, "W2-6 fan never dropped, TEC1 back on");

    /* W2-7 锁存期间重申输出的频率仍是 500ms（节拍变密不给模块加流量）*/
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_test_trip(SAFETY_BIT_ESTOP);
    reset_stubs();
    for (int i = 0; i < 10; i++) tick100();   /* 1000ms */
    CHECK(fan_calls == 2, "W2-7 SAFE re-assert every 500ms (2 in 1000ms)");
    CHECK(control_state() == CTRL_SAFE, "W2-7 stays SAFE");

    /* W2-8 人工确认退出（体温传感器类）：与自愈同样复原 TEC2 */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_tec(2, 1);
    g_tcs.body_sensor_ok = 0; tick100();
    g_tcs.body_sensor_ok = 1;
    tcs_alarm_ack();
    tick100();
    CHECK(control_state() == CTRL_IDLE && g_tcs.tec2_on == 1, "W2-8 ack path restores TEC2 too");

    /* W2-9 超温：解除后风扇复原、TEC2 不复原（超温期间输出必须保持关）*/
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_tec(2, 1);
    control_manual_fan(1);
    g_tcs.tc1_act = 49.0f; tick100();
    CHECK(control_state() == CTRL_SAFE, "W2-9 overtemp -> SAFE");
    g_tcs.tc1_act = 30.0f;
    tcs_alarm_ack();
    tick100();
    CHECK(control_state() == CTRL_IDLE, "W2-9 overtemp cleared");
    CHECK(g_tcs.fan_on == 1 && g_tcs.tec2_on == 0, "W2-9 fan restored, TEC2 stays off");

    /* W2-10 体温传感器故障期间模块又掉线 → 自愈后不复原 TEC2 */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    s_now += 5000u;
    control_manual_tec(2, 1);
    g_tcs.body_sensor_ok = 0; tick100();      /* SAFE，仅体温类 */
    g_tcs.m207_online = 0; tick100();         /* 期间模块掉线 */
    g_tcs.m207_online = 1;
    g_tcs.body_sensor_ok = 1;
    for (int i = 0; i < 25; i++) tick100();
    CHECK(control_state() == CTRL_IDLE && g_tcs.tec2_on == 0, "W2-10 no TEC2 restore if module dropped meanwhile");

    /* W2-11 手动模式下急停（TEC2 原本开着）→ 解除后 TEC2 保持关；风扇回故障前状态 */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_tec(2, 1);
    control_manual_fan(1);
    tcs_emergency_stop();
    CHECK(g_tcs.tec2_on == 0 && g_tcs.fan_on == 1, "W2-11 estop: TEC off, fan full");
    tcs_alarm_ack();
    tick100();
    CHECK(control_state() == CTRL_IDLE && g_tcs.tec2_on == 0, "W2-11 TEC2 stays off after estop reset");
    CHECK(g_tcs.fan_on == 1, "W2-11 fan back to pre-estop (manual) state");

    /* W2-12 短于一个评估周期的故障：退出 SAFE 后下一拍立刻评估，不多等 500ms
     * （时间线：T0 评估进 COOL → T0+100 进 SAFE → T0+200 解除 → T0+300 必须已回 COOL）*/
    enter_auto_healthy();
    g_tcs.body_temp = 40.0f; tick();          /* T0：评估 → COOL */
    g_tcs.body_sensor_ok = 0; tick100();      /* T0+100：SAFE */
    g_tcs.body_sensor_ok = 1;
    tcs_alarm_ack();
    tick100();                                /* T0+200：解除 → IDLE */
    CHECK(control_state() == CTRL_IDLE, "W2-12 ack exits SAFE");
    tick100();                                /* T0+300 */
    CHECK(control_state() == CTRL_COOL, "W2-12 auto evaluation runs on the very next tick");

    /* W2-13 上电恢复的锁存：上电后第一拍（<500ms）就必须断输出，不能等重申间隔 */
    control_init(&(control_ops_t){
        .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp,
        .on_state = 0, .safety_persist = persist_cb, .safety_load = load_cb,
    });
    param_registry_init(&(param_hooks_t){0});
    g_tcs.fan_on = 1;                         /* 假设复位前风扇是开的 */
    s_now = 100u;
    reset_stubs();
    tick100();
    CHECK(fan_calls == 1 && last_fan == 1, "W2-13 restored latch: fan asserted on first poll");
    CHECK(tec_calls == 2 && last_tec_on == 0, "W2-13 restored latch: both TEC off on first poll");

    printf("test_control: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
