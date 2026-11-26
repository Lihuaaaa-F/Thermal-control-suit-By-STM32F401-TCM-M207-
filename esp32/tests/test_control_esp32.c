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
/* 失败行立刻落盘：后面某个断言把进程带崩时，前面的失败清单也不丢（输出重定向时 stdout 是全缓冲的）*/
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); fflush(stdout); } \
} while (0)

static void set_armed(uint8_t a)
{
    post_arm_commit(a ? POST_ARM_ARMED_ONLINE : POST_ARM_UNARMED_ABSENT);
}

/* ====== 输出记录桩 ====== */
static int fan_calls, tec_calls, temp_calls;
static uint8_t last_fan; static uint8_t last_tec_on, last_tec_ch;
static float last_temp_v; static uint8_t last_temp_ch;

static void stub_fan(uint8_t on)        { fan_calls++; last_fan = on; }
static int tec_on_calls;                               /* X06-F-03：到过执行器的"开"次数 */
static void stub_tec(uint8_t ch, uint8_t on)  { tec_calls++; last_tec_ch = ch; last_tec_on = on; if (on) tec_on_calls++; }
static void stub_temp(uint8_t ch, float v)    { temp_calls++; last_temp_ch = ch; last_temp_v = v; }

/* P1-09 用：m207_tec 的主机模型——模块离线时"拒收"（回 -2、不出帧），不记送达 */
static uint8_t  s_link_down;              /* 1 = M207 链路断（用例同步把 g_tcs.m207_online 置 0）*/
static uint32_t s_off_delivered_ms;       /* 最近一次"关"真正送达执行器的时刻；0 = 从未 */
static uint32_t s_stub_now_ms;            /* 桩里的时钟（拍前由用例更新）*/
static void stub_tec_strict(uint8_t ch, uint8_t on)
{
    tec_calls++; last_tec_ch = ch; last_tec_on = on;
    if (!on && !s_link_down) s_off_delivered_ms = s_stub_now_ms ? s_stub_now_ms : 1u;
}
/* P1-09 用：安全关断通道（m207_tec_force 的主机模型）——不依赖普通队列，断链时也记下意图 */
static int force_calls;
static void stub_force(uint8_t ch, uint8_t on)
{
    force_calls++; last_tec_ch = ch; last_tec_on = on;
    if (!on) s_off_delivered_ms = s_stub_now_ms ? s_stub_now_ms : 1u;
}

/* S 线 i3（M09-F-09 的注入档口径）：装配方告诉控制核"这一路的读数来自注入"（bit0 = 体温、bit1 = 水温）。
 * 注入值是人给的常数，卡死判据豁免这一路；交付档不接（NULL）。*/
static uint8_t s_exempt_mask;
static uint8_t stub_exempt(uint8_t ch)
{
    return (uint8_t)((ch == 1u) ? (s_exempt_mask & 1u) : ((s_exempt_mask >> 1) & 1u));
}

/* P0-08 持久化钩子桩（文件作用域：MSVC 不支持嵌套函数定义）*/
static uint8_t s_persisted;
static int     s_persist_calls;          /* R9-04：落盘次数（只含 STUCK 的锁存不该产生写）*/
static void    persist_cb(uint8_t latch) { s_persisted = latch; s_persist_calls++; }
static uint8_t load_cb(void)             { return SAFETY_BIT_ESTOP; }
/* S 线 e3a（卡死判据）用：上电无锁存 / 存储里留着 STUCK 位 */
static uint8_t load0_cb(void)            { return 0; }
static uint8_t load_stuck_cb(void)       { return SAFETY_BIT_STUCK; }
/* X06-F-03 用：复位前锁存着超温 + 急停 */
static uint8_t load_ot_estop_cb(void)    { return SAFETY_BIT_OVERTEMP | SAFETY_BIT_ESTOP; }
/* X04-F-07 用：NVS 落盘总是失败 */
static int commit_fail_stub(const uint8_t *img) { (void)img; return -1; }

static void reset_stubs(void)
{
    fan_calls = tec_calls = temp_calls = 0;
    force_calls = 0;
    tec_on_calls = 0;
}

/* 推进 500ms 评估节拍 */
static uint32_t s_now;
static void tick(void) { s_now += 500u; s_stub_now_ms = s_now; control_poll(s_now); }
/* 推进一拍（100ms）并同步桩时钟；P1-09 的用例用 */
static void poll100(void) { s_now += 100u; s_stub_now_ms = s_now; control_poll(s_now); }

/* S 线 e3a（卡死判据）用：推进一拍，同时让**不该被判**的那个通道保持活的读数。
 * 真实传感器的读数一直在抖（真机旁证：TCACTTEMP 相邻两次 ±0.01~0.07°C）；用例里如果让它
 * 一直是常量，水温通道会和体温通道一起被判卡死，断言就分不清是谁触发的。*/
static int s_flick;
#define tick100_tc1_live() do { g_tcs.tc1_act = (s_flick++ & 1) ? 24.99f : 25.01f; tick100(); } while (0)
#define tick_tc1_live()    do { g_tcs.tc1_act = (s_flick++ & 1) ? 24.99f : 25.01f; tick(); } while (0)

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

/* ====== r7f（独立复核 R7-02，2026-11-26）：带 NVS 的重启往返 =====
 * 两份"掉电也在"的副本各一个桩：参数镜像（eeprom_commit 写的那一张）、安全锁存（safety_persist 写的那个字节）。
 * boot_nvs() 的顺序与 main.c 相同：注册表默认 + 钩子 → 读镜像 → control_init（读回锁存）。*/
static uint8_t g_nv_img[EE_IMG_SIZE];    /* NVS 里的参数镜像 */
static int     g_nv_img_have;
static uint8_t g_nv_latch;               /* NVS 里的安全锁存 */
static int     g_nv_latch_have;
static int     g_nv_dead;                /* 1 = 掉电了：之后的镜像写一律不生效（上电前置 0）*/
static int     g_nv_cut;                 /* 1 = 下一次锁存写完就掉电（卡在"锁存已落盘、模式还没落盘"之间）*/
static int     g_nv_commits;             /* eeprom_commit 被调用的次数 */
static int nv_commit(const uint8_t *img)
{
    g_nv_commits++;
    if (g_nv_dead) return -1;
    memcpy(g_nv_img, img, sizeof g_nv_img);
    g_nv_img_have = 1;
    return 0;
}
static void nv_latch_save(uint8_t latch)
{
    g_nv_latch = latch; g_nv_latch_have = 1;
    if (g_nv_cut) { g_nv_cut = 0; g_nv_dead = 1; }
}
static uint8_t nv_latch_load(void) { return g_nv_latch_have ? g_nv_latch : 0u; }
static int nv_img_mode(void)             /* NVS 镜像里 0x06（控制模式）的值；没有镜像 / 没有记录 = -1 */
{
    if (!g_nv_img_have) return -1;
    for (int i = 0; i < g_nv_img[2]; i++) {
        const uint8_t *r = &g_nv_img[4 + 8 * i];
        if (r[0] == 0x06 && r[1] == 0x00) return r[4];
    }
    return -1;
}
static void boot_nvs(int with_nvs)
{
    param_registry_init(&(param_hooks_t){ .eeprom_commit = nv_commit,
                                          .on_mode_change = control_on_mode_change,
                                          .on_check = control_param_check });
    if (with_nvs && g_nv_img_have) { int sk = 0; (void)param_deserialize_ex(g_nv_img, &sk); }
    control_init(&(control_ops_t){
        .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
        .safety_persist = nv_latch_save, .safety_load = nv_latch_load,
    });
}
/* 控制节拍 + 落盘冲洗（固件里 persist_worker 每 500ms 调一次 param_persist_flush）*/
static void nv_run(uint32_t ms)
{
    for (uint32_t t = 0u; t < ms; t += 100u) {
        poll100();
        if (((t + 100u) % 500u) == 0u) param_persist_flush(s_now);
    }
}
static void nv_sources_ok(void)
{
    set_armed(1);
    g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
    g_tcs.tc1_act = 25.0f; g_tcs.body_temp = 36.0f;      /* 体温低于 cold-hyst：一旦是自动模式就会制热 */
}
/* 全新上电（NVS 空）→ 自动制热，"模式 = 自动"已落盘 */
static void nv_fresh_auto_heat(void)
{
    memset(g_nv_img, 0, sizeof g_nv_img);
    g_nv_img_have = 0; g_nv_latch = 0; g_nv_latch_have = 0;
    g_nv_dead = 0; g_nv_cut = 0; g_nv_commits = 0;
    nv_sources_ok();
    boot_nvs(0);
    nv_sources_ok();
    s_now = 10000u;
    (void)param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB);
    reset_stubs();
    nv_run(4000u);
}

/* W2-22：记录最近一次参数广播的 id 与来源（钩子须是文件作用域：MSVC 不支持嵌套函数）*/
static uint16_t g_bc_id;
static uint8_t  g_bc_src;
static void bcast_rec(param_desc_t *p, uint8_t src) { g_bc_id = p->id; g_bc_src = src; }

/* 旧签名的单测调用按"控制台"来源走（固件里台架自测也是这个口径）*/
#define control_apply_cmd(c) control_apply_cmd_src((c), PARAM_SRC_CONSOLE)

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
    /* 体温仍是上一段留下的 30.0 且在自动模式：退出 SAFE 的当拍状态机就接管 → 直接 HEAT
     * （2026-11-24 起：自动模式退出 SAFE 不复原故障前输出，当拍按体温重新定）*/
    CHECK(control_state() == CTRL_HEAT, "sensor open cleared+ack -> SAFE left at once (auto resumes HEAT)");

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
     * 判别性以 tests/mutation_control.py 的实测为准（旧实现上哪些用例红、每个"改坏一处"
     * 的变异是否被抓住，都由它逐条打印）。服务任务的 100ms 节拍本身是 FreeRTOS 代码，
     * 主机测不到，由真机日志时间戳取证（见结果文档 §7）；这里验证状态机在 100ms 调用下的行为。*/
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
        /* 精确值：恢复后第 1 拍（+100ms）起计时，满 2000ms 的那一拍退出 → 距恢复 2100ms。
         * 写成区间的话，把 2s 改成 1.9s 也落在区间里（独立复核指出过），所以这里卡死。*/
        CHECK(dt == 2100u, "W2-1 exit exactly 2000ms after the first healthy poll");
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

    /* W2-6 自动模式制冷中体温传感器抖动：自愈后仍是自动模式，并且回到 COOL（风扇/TEC1 开）*/
    enter_auto_healthy();
    g_tcs.body_temp = 40.0f; tick();          /* COOL */
    g_tcs.body_sensor_ok = 0; tick100();      /* SAFE */
    CHECK(control_state() == CTRL_SAFE, "W2-6 -> SAFE");
    g_tcs.body_sensor_ok = 1;
    for (int i = 0; i < 25; i++) tick100();
    CHECK(g_tcs.mode == 1, "W2-6 still auto mode");
    CHECK(control_state() == CTRL_COOL, "W2-6 SAFE exited and COOL resumed");
    CHECK(g_tcs.fan_on == 1 && g_tcs.tec1_on == 1, "W2-6 fan and TEC1 on again (state machine's doing)");

    /* W2-7 锁存期间重申输出的频率仍是 500ms（节拍变密不给模块加流量）*/
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_test_trip(SAFETY_BIT_ESTOP);
    reset_stubs();
    for (int i = 0; i < 10; i++) tick100();   /* 1000ms */
    CHECK(fan_calls == 2, "W2-7 SAFE re-assert every 500ms (2 in 1000ms)");
    CHECK(control_state() == CTRL_SAFE, "W2-7 stays SAFE");

    /* W2-8 人工确认退出（体温传感器类）：不复原 TEC2——人工确认只说明"此刻源是好的"，
     * 没有"连续健康 2s"作保证；TEC 自动重开只留给自愈退出（W2-3）。风扇照常复原。*/
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_tec(2, 1);
    control_manual_fan(1);
    g_tcs.body_sensor_ok = 0; tick100();
    g_tcs.body_sensor_ok = 1;
    tcs_alarm_ack();
    tick100();
    CHECK(control_state() == CTRL_IDLE && g_tcs.tec2_on == 0, "W2-8 ack path does NOT restore TEC2");
    CHECK(g_tcs.fan_on == 1, "W2-8 ack path restores the fan");

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

    /* W2-12 短于一个评估周期的故障：退出 SAFE 的当拍就评估，不等下一个评估周期
     * （时间线：T0 评估进 COOL → T0+100 进 SAFE → T0+200 解除，当拍即回 COOL）*/
    enter_auto_healthy();
    g_tcs.body_temp = 40.0f; tick();          /* T0：评估 → COOL */
    g_tcs.body_sensor_ok = 0; tick100();      /* T0+100：SAFE */
    g_tcs.body_sensor_ok = 1;
    tcs_alarm_ack();
    tick100();                                /* T0+200：解除 → 当拍评估 */
    CHECK(control_state() == CTRL_COOL && g_tcs.fan_on == 1 && g_tcs.tec1_on == 1,
          "W2-12 auto evaluation runs on the very tick SAFE is left");

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

    /* ====== 2026-11-24 独立复核后补的用例（P1-1 / P1-3 / P2-1 / P1-2）====== */

    /* W2-14 自动模式制冷中体温传感器失联；恢复时体温已回到滞回带内 → 退出当拍 IDLE、风扇关、TEC1 关。
     * 回归用例：第一版把"故障前风扇=开"复原了，而状态机从 IDLE 重新起步就不再管风扇 → 风扇常开。*/
    enter_auto_healthy();
    g_tcs.body_temp = 40.0f; tick();          /* COOL：风扇开、TEC1 开 */
    CHECK(control_state() == CTRL_COOL && g_tcs.fan_on == 1, "W2-14 precondition: COOL fan on");
    g_tcs.body_sensor_ok = 0; tick100();      /* SAFE */
    CHECK(control_state() == CTRL_SAFE && g_tcs.fan_on == 1, "W2-14 -> SAFE, fan full");
    g_tcs.body_sensor_ok = 1;
    g_tcs.body_temp = 37.5f;                  /* 恢复时体温在带内 */
    {
        int n = 0;
        while (control_state() == CTRL_SAFE && n < 40) { tick100(); n++; }
        CHECK(control_state() == CTRL_IDLE, "W2-14 exits SAFE");
        CHECK(g_tcs.fan_on == 0, "W2-14 fan off on the very tick SAFE is left (auto, body in band)");
        CHECK(g_tcs.tec1_on == 0, "W2-14 TEC1 off after exit");
        for (int i = 0; i < 20; i++) tick100();
        CHECK(control_state() == CTRL_IDLE && g_tcs.fan_on == 0 && g_tcs.tec1_on == 0,
              "W2-14 stays IDLE with everything off");
    }

    /* W2-15 自动模式制冷中失联；恢复时体温仍然高 → 退出当拍就回 COOL（风扇/TEC1 开），不等下一个评估周期 */
    enter_auto_healthy();
    g_tcs.body_temp = 40.0f; tick();          /* COOL */
    g_tcs.body_sensor_ok = 0; tick100();      /* SAFE */
    g_tcs.body_sensor_ok = 1;
    {
        int n = 0;
        while (control_state() == CTRL_SAFE && n < 40) { tick100(); n++; }
        CHECK(control_state() == CTRL_COOL && g_tcs.fan_on == 1 && g_tcs.tec1_on == 1,
              "W2-15 resumes COOL on the very tick SAFE is left");
    }

    /* W2-16 故障前 TEC2 本来是关的：仅体温传感器故障自愈后仍然是关（复原 = 回到故障前，不是"一律开"）*/
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_tec(2, 0);
    g_tcs.body_sensor_ok = 0; tick100();
    g_tcs.body_sensor_ok = 1;
    for (int i = 0; i < 25; i++) tick100();
    CHECK(control_state() == CTRL_IDLE && g_tcs.tec2_on == 0, "W2-16 TEC2 stays off (was off before the fault)");

    /* W2-17 仅体温传感器故障进 SAFE，期间水温探头断线又接回 → 模块侧状态不再可信，自愈后不复原 TEC2 */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_tec(2, 1);
    g_tcs.body_sensor_ok = 0; tick100();      /* SAFE，仅体温类 */
    g_tcs.m207_sensor_open = 1; tick100();    /* 期间探头断线 */
    g_tcs.m207_sensor_open = 0;               /* 又接回（"模块在线"标志全程没变）*/
    g_tcs.body_sensor_ok = 1;
    for (int i = 0; i < 25; i++) tick100();
    CHECK(control_state() == CTRL_IDLE && g_tcs.tec2_on == 0,
          "W2-17 no TEC2 restore after probe opened meanwhile");

    /* W2-18 仅体温传感器故障进 SAFE，期间叠加超温并先解除超温，之后体温才自愈 → 自愈退出也不复原 TEC2
     * （这一轮 SAFE 里出现过别的危险源，"只因体温传感器"已不成立）*/
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_tec(2, 1);
    g_tcs.body_sensor_ok = 0; tick100();      /* SAFE，仅体温类 */
    g_tcs.tc1_act = 49.0f; tick100();         /* 期间叠加超温 */
    g_tcs.tc1_act = 30.0f;
    tcs_alarm_ack();                          /* 超温人工解除；体温仍失联 → 还在 SAFE */
    tick100();
    CHECK(control_state() == CTRL_SAFE, "W2-18 still SAFE (body still lost)");
    g_tcs.body_sensor_ok = 1;
    for (int i = 0; i < 25; i++) tick100();   /* 体温自愈 */
    CHECK(control_state() == CTRL_IDLE, "W2-18 SAFE left by auto-recovery");
    CHECK(g_tcs.tec2_on == 0, "W2-18 no TEC2 restore: overtemp was stacked during this SAFE");

    /* W2-19 仅体温传感器故障进 SAFE 期间按急停，不等下一拍立刻解除 → 不复原 TEC2 */
    enter_auto_healthy();
    (void)param_set(PARAM_ID_MODE, 0.0f, 4);
    tick();
    control_manual_tec(2, 1);
    g_tcs.body_sensor_ok = 0; tick100();      /* SAFE，仅体温类 */
    g_tcs.body_sensor_ok = 1;
    tcs_emergency_stop();                     /* 急停与解除之间不跑 control_poll */
    tcs_alarm_ack();
    tick100();
    CHECK(control_state() == CTRL_IDLE, "W2-19 SAFE left");
    CHECK(g_tcs.tec2_on == 0, "W2-19 no TEC2 restore: estop happened during this SAFE");

    /* W2-20 自动模式下（TEC2 是手动通道）仅体温传感器故障自愈：TEC2 复原，风扇按体温当拍定 */
    enter_auto_healthy();
    g_tcs.body_temp = 37.5f; tick();          /* 带内：IDLE，风扇关 */
    control_manual_tec(2, 1);
    g_tcs.body_sensor_ok = 0; tick100();
    g_tcs.body_sensor_ok = 1;
    for (int i = 0; i < 25; i++) tick100();
    CHECK(control_state() == CTRL_IDLE && g_tcs.tec2_on == 1 && g_tcs.fan_on == 0,
          "W2-20 auto mode: TEC2 restored, fan decided by state machine (off in band)");

    /* W2-21 毫秒时间戳跨 2^31 微秒（开机 35 分 47 秒）不回跳；到 2^32 毫秒（49.7 天）正常回绕 */
    {
        int64_t jump_us = 2147483648LL;                   /* 2^31 µs */
        uint32_t a = control_ms_from_us(jump_us - 500);
        uint32_t b = control_ms_from_us(jump_us + 500);   /* 两者相隔 1ms，正好跨过 2^31 */
        CHECK(a == 2147483u && b == 2147484u, "W2-21 ms timestamps continuous across 2^31 us");
        CHECK(b - a == 1u, "W2-21 1ms later reads as 1ms later (old formula: 71 minutes backwards)");
        CHECK(control_ms_from_us(0) == 0u && control_ms_from_us(999) == 0u
              && control_ms_from_us(1000) == 1u, "W2-21 sub-millisecond truncation");
        CHECK(control_ms_from_us(4294967295000LL) == 4294967295u
              && control_ms_from_us(4294967296000LL) == 0u, "W2-21 wraps at 2^32 ms as a plain counter");
    }

    /* ====== 2026-11-24 网页复核后补：命令来源如实进参数表；急停/超温锁存期间不得切自动 ====== */

    /* W2-22 谁下的命令就记谁：网页/屏幕/控制台各有各的来源，事件日志据此写"谁改的"。
     * 原先改模式一律记成"网页"、急停强制切手动一律记成"控制台"。*/
    {
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
        });
        param_registry_init(&(param_hooks_t){ .on_broadcast = bcast_rec, .on_check = control_param_check });
        set_armed(1);
        g_tcs.body_sensor_ok = 1;
        g_tcs.m207_online = 1;
        g_tcs.m207_sensor_open = 0;
        g_tcs.tc1_act = 25.0f;
        s_now = 10000u;
        g_bc_src = 0;
        CHECK(control_apply_cmd_src(TCS_CMD_MODE_AUTO, PARAM_SRC_WEB) == 0 && g_tcs.mode == 1 &&
              g_bc_id == PARAM_ID_MODE && g_bc_src == PARAM_SRC_WEB, "W2-22 web mode switch is tagged web");
        CHECK(control_apply_cmd_src(TCS_CMD_MODE_MANUAL, PARAM_SRC_TOUCH) == 0 && g_tcs.mode == 0 &&
              g_bc_src == PARAM_SRC_TOUCH, "W2-22 screen mode switch is tagged screen");
        CHECK(control_apply_cmd(TCS_CMD_MODE_AUTO) == 0 && g_bc_src == PARAM_SRC_CONSOLE,
              "W2-22 old entry point is tagged console");

        /* W2-23 急停锁存期间不得切到自动——"急停复位后不得自动重启"。原先锁存期间就能把模式
         * 预先切成自动，解除的那一拍状态机立刻重新开 TEC，等于绕过了"复位后要单独启动"。*/
        control_apply_cmd_src(TCS_CMD_ESTOP, PARAM_SRC_TOUCH);
        CHECK(g_bc_id == PARAM_ID_MODE && g_bc_src == PARAM_SRC_TOUCH,
              "W2-22 estop's forced-manual carries the estop's own source (was console)");
        CHECK(g_tcs.mode == 0 && control_state() == CTRL_SAFE, "W2-23 precondition: estop, manual, SAFE");
        CHECK(param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB) == -2 && g_tcs.mode == 0,
              "W2-23 param_set(mode=1) refused while estop latched");
        CHECK(control_apply_cmd_src(TCS_CMD_MODE_AUTO, PARAM_SRC_WEB) == -2 && g_tcs.mode == 0,
              "W2-23 cmd 5 refused while estop latched");
        tcs_alarm_ack();
        tick100();
        CHECK(control_state() == CTRL_IDLE, "W2-23 estop cleared");
        CHECK(control_apply_cmd_src(TCS_CMD_MODE_AUTO, PARAM_SRC_WEB) == 0 && g_tcs.mode == 1,
              "W2-23 auto is allowed once the estop is cleared");
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        control_test_trip(SAFETY_BIT_OVERTEMP);
        tcs_alarm_raise(ALM_OVERTEMP);
        CHECK(param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB) == -2,
              "W2-23 auto refused while overtemp latched");
        tcs_alarm_ack();
        CHECK(control_safety_latched() == 0 && param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB) == 0,
              "W2-23 auto allowed once overtemp is cleared");
        /* 通信类故障的锁存不在此列：那是会自己好的故障，自动模式恢复后按体温接管 */
        g_tcs.body_sensor_ok = 0; tick100();
        CHECK(control_state() == CTRL_SAFE, "W2-23 precondition: comm-type SAFE");
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        CHECK(param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB) == 0,
              "W2-23 auto is NOT blocked by a comm-type latch");
    }

    /* ====== 2026-11-25 S 线（交付固件真机发现）：未布防 + 体温探头缺失时，急停解除不了 ======
     * 现场：设备从开机起就没有体温探头（未布防，规格允许手动运行），交付固件上急停后 ack 不清——
     * safety_ack_latched 要求"三个传感器源全部恢复"才清急停，而体温源在这台设备上永远不会恢复；
     * 锁存又存 NVS，断电重启也出不来。修法：未布防时体温源不计入解除急停的前置条件。
     * 下面 N1 / N3 / N4 的"解除"断言在旧实现上必然失败（已实跑确认），N2 守住布防态不被放松。*/
    {
        /* N1 现场场景：未布防、手动、无体温探头、模块在线、水温正常 → 急停 → ack → 必须能解除 */
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        set_armed(0);
        g_tcs.body_sensor_ok = 0;
        tick();
        CHECK(control_state() == CTRL_IDLE, "N1 unarmed, no body probe: manual idle (not SAFE)");
        control_manual_fan(0);                     /* 急停前风扇定为"关"：解除后应恢复到这个状态 */
        reset_stubs();
        tcs_emergency_stop();
        CHECK(control_state() == CTRL_SAFE && tcs_alarm_active(ALM_ESTOP) == 1, "N1 estop -> SAFE + ALM_ESTOP");
        CHECK(last_fan == 1, "N1 estop: fan on");
        CHECK(g_tcs.mode == 0, "N1 estop: mode manual");
        reset_stubs();
        tcs_alarm_ack();
        CHECK(tcs_alarm_active(ALM_ESTOP) == 0, "N1 ack clears ALM_ESTOP without body probe (unarmed)");
        CHECK(control_safety_latched() == 0, "N1 ack clears the safety latch");
        tick();
        CHECK(control_state() == CTRL_IDLE, "N1 ack -> IDLE");
        CHECK(g_tcs.mode == 0, "N1 after release: stays manual (no automatic restart)");
        CHECK(last_fan == 0, "N1 after release: fan back to pre-estop state (off)");
        CHECK(param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB) == 0, "N1 auto allowed again after release");

        /* N2 已布防后探头丢了：仍然必须等探头恢复——布防态的保护不放松 */
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        tick();
        g_tcs.body_sensor_ok = 0;                 /* 已布防 → 拔探头 */
        tick();
        CHECK(control_state() == CTRL_SAFE, "N2 armed: body loss -> SAFE");
        tcs_emergency_stop();                     /* 叠加急停 */
        tcs_alarm_ack();                          /* 探头仍断线 → 两个锁存都不清 */
        tick();
        CHECK(control_state() == CTRL_SAFE, "N2 armed: ack refused while body probe still lost");
        CHECK(tcs_alarm_active(ALM_ESTOP) == 1, "N2 armed: ALM_ESTOP stays");
        g_tcs.body_sensor_ok = 1;                 /* 探头接回 */
        tcs_alarm_ack();
        tick();
        CHECK(control_state() == CTRL_IDLE && tcs_alarm_active(ALM_ESTOP) == 0,
              "N2 armed: probe back + ack -> released");

        /* N3 未布防、无体温探头，但模块离线时 ack：不解除（模块源仍要求在线） */
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        set_armed(0);
        g_tcs.body_sensor_ok = 0;
        tick();
        s_now += 5000u;                           /* 越过 M207 上电宽限（未布防时离线不进 SAFE）*/
        tcs_emergency_stop();
        g_tcs.m207_online = 0;
        tcs_alarm_ack();
        tick();
        CHECK(control_state() == CTRL_SAFE && tcs_alarm_active(ALM_ESTOP) == 1,
              "N3 unarmed: ack refused while M207 offline");
        g_tcs.m207_online = 1;
        tcs_alarm_ack();
        tick();
        CHECK(control_state() == CTRL_IDLE && tcs_alarm_active(ALM_ESTOP) == 0,
              "N3 unarmed: M207 back + ack -> released");

        /* N4 未布防、无体温探头，超温未退时 ack：不解除；降温后解除 */
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        set_armed(0);
        g_tcs.body_sensor_ok = 0;
        tick();
        tcs_emergency_stop();
        g_tcs.tc1_act = 49.0f;
        tick();                                   /* 超温不受布防影响：OVERTEMP 也锁存 */
        tcs_alarm_ack();
        tick();
        CHECK(control_state() == CTRL_SAFE, "N4 unarmed: still SAFE while overtemp");
        g_tcs.tc1_act = 30.0f;
        tick();
        tcs_alarm_ack();
        tick();
        CHECK(control_state() == CTRL_IDLE, "N4 unarmed: cooled + ack -> IDLE");
        CHECK(tcs_alarm_active(ALM_ESTOP) == 0 && tcs_alarm_active(ALM_OVERTEMP) == 0, "N4 both cleared");

        /* N5 已布防，探头在两拍之间丢了（还没被锁存），这时急停并立刻 ack：
         *    解除急停仍要看体温源——没恢复就不清（变异验证 M29 抓的就是"已布防时不再要求体温源"）*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        tick();
        g_tcs.body_sensor_ok = 0;                 /* 已布防 → 拔探头，但还没跑 control_poll */
        tcs_emergency_stop();                     /* 急停 */
        tcs_alarm_ack();                          /* 立刻确认：体温源不健康 → 急停不清 */
        CHECK(tcs_alarm_active(ALM_ESTOP) == 1, "N5 armed: estop not cleared while body source unhealthy");
        tick();                                   /* 这一拍把 BODY_LOST 也锁存上 */
        CHECK(control_state() == CTRL_SAFE, "N5 armed: SAFE continues (body loss now latched)");
        g_tcs.body_sensor_ok = 1;
        tcs_alarm_ack();
        tick();
        CHECK(control_state() == CTRL_IDLE && tcs_alarm_active(ALM_ESTOP) == 0, "N5 armed: probe back + ack -> released");
    }

    /* ====== 2026-11-25 S 线 e3a（M09-F-09 / X04-F-06）：读数卡死 → SAFE ======
     * 合理性滤波只查范围与变化率，恒定读数永远"合理"：体温冻结则自动模式永远停在 HEAT/COOL，
     * 水温冻结则 48°C 硬切断失明。判据：源自报健康 + 读数逐位不变 ≥30s（水温是 10 分钟，2026-11-26 真机实测后改）+ TEC 已开 ≥30s → 锁存 STUCK、
     * 进 SAFE，不受布防门控。时间全部用 100ms 拍推进（现场服务就是 100ms 一拍）。
     * 判别性与变异验证见 tests/mutation_control.py 的 M31 起。*/
    {
        /* K1 体温冻在 30.0，自动模式 HEAT：TEC 开满 30 s 才判，差一拍都不判 */
        enter_auto_healthy();
        g_tcs.body_temp = 30.0f;                 /* 远低于 cold-hyst，且从此不变 */
        tick();                                  /* 评估进 HEAT：TC1 在这一拍开 */
        CHECK(control_state() == CTRL_HEAT && g_tcs.tec1_on == 1, "K1 precondition: HEAT, TC1 on");
        for (int i = 0; i < 299; i++) tick100_tc1_live();   /* 29.9 s：还不到窗口 */
        CHECK(control_state() == CTRL_HEAT, "K1 frozen body: no trip before 30 s");
        tick100_tc1_live();                      /* 整 30.0 s */
        CHECK(control_state() == CTRL_SAFE, "K1 frozen body + TC1 on for 30 s -> SAFE");
        CHECK((control_safety_latched() & SAFETY_BIT_STUCK) != 0, "K1 STUCK latched");
        CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0, "K1 both TEC off");
        CHECK(tcs_alarm_active(ALM_BODY_SENSOR) == 1, "K1 ALM_BODY_SENSOR raised");
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 0, "K1 body-only: no ALM_SENSOR_OPEN");
        CHECK(g_tcs.fan_on == 1, "K1 fan full");

        /* K2 未布防同样进 SAFE（卡死判据不受布防门控；未布防时普通的体温失联是不进 SAFE 的）*/
        enter_auto_healthy();
        set_armed(0);
        g_tcs.body_temp = 30.0f;
        tick();
        CHECK(control_state() == CTRL_HEAT, "K2 precondition: unarmed auto HEAT");
        for (int i = 0; i < 300; i++) tick100_tc1_live();
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K2 unarmed: frozen body still -> SAFE");
        CHECK(tcs_alarm_active(ALM_BODY_SENSOR) == 1, "K2 unarmed: ALM_BODY_SENSOR raised");

        /* K11 读数一直不变 → 一直留在 SAFE；K12 这时 ack 不清（读数没恢复变化就不算恢复），
         *     借用的报警位也不清——设备停在 SAFE 时屏上说得出原因 */
        for (int i = 0; i < 600; i++) tick100_tc1_live();  /* 再过 60 s */
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K11 still frozen -> stays SAFE");
        tcs_alarm_ack();
        CHECK((control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K12 ack cannot clear STUCK while the reading is still frozen");
        CHECK(tcs_alarm_active(ALM_BODY_SENSOR) == 1, "K12 alarm bit stays while STUCK is latched");
        CHECK(control_state() == CTRL_SAFE, "K12 still SAFE after ack");

        /* K10 读数重新开始变化 → 2 s 后自愈退出 SAFE、清报警，自动模式当拍重新评估 */
        {
            uint32_t t_ok = s_now;
            int n = 0;
            while (control_state() == CTRL_SAFE && n < 40) {
                g_tcs.body_temp = (n & 1) ? 29.99f : 30.01f;   /* 活了：每拍都在变 */
                tick100_tc1_live();
                n++;
            }
            CHECK(control_state() == CTRL_HEAT, "K10 reading moves again -> SAFE left, HEAT resumes");
            CHECK(control_safety_latched() == 0 && tcs_alarm_active(ALM_BODY_SENSOR) == 0,
                  "K10 latch and alarm cleared");
            /* 与 W2-1 同一口径：恢复后第 1 拍起计时，满 2000ms 的那一拍退出 → 距恢复 2100ms */
            CHECK(s_now - t_ok == 2100u, "K10 exit exactly 2000 ms after the first live poll");

            /* K15 只动了一下又冻住：自愈后 TEC 最多再开一个窗口，然后再次进 SAFE。
             *     时冻时动的传感器，每次"动一下"换来的输出不超过一个窗口——把这条实际行为钉住。*/
            for (int i = 0; i < 280; i++) tick100_tc1_live();   /* 距读数最后一次变化 28 s */
            CHECK(control_state() == CTRL_HEAT, "K15 no re-trip before the window ends");
            for (int i = 0; i < 25; i++) tick100_tc1_live();
            CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
                  "K15 frozen again -> SAFE again");
        }

        /* K3 反证：体温每拍都在变（活着的传感器）→ HEAT 开满 60 s 也不进 SAFE */
        enter_auto_healthy();
        g_tcs.body_temp = 30.0f; tick();
        CHECK(control_state() == CTRL_HEAT, "K3 precondition: HEAT");
        for (int i = 0; i < 600; i++) {
            g_tcs.body_temp = (i & 1) ? 29.99f : 30.01f;
            g_tcs.tc1_act   = (i & 1) ? 24.99f : 25.01f;
            tick100();
        }
        CHECK(control_state() == CTRL_HEAT && (control_safety_latched() & SAFETY_BIT_STUCK) == 0,
              "K3 live body: no trip after 60 s of HEAT");

        /* K4 反证：读数不变但 TEC 没开（带内 IDLE）→ 不判卡死（判据要求 TEC 在输出）*/
        enter_auto_healthy();
        g_tcs.body_temp = 37.0f; tick();
        for (int i = 0; i < 120; i++) tick();          /* 60 s，体温一直是 37.0 */
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
              "K4 frozen body with TEC off -> no trip");

        /* K5 手动模式下体温读数不驱动任何输出：冻住也不判
         *     （否则"环境水箱 TEC2 开着 + 安静的体温探头"会被误停机）*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        g_tcs.body_temp = 30.0f;
        tick();
        control_manual_tec(2, 1);                       /* 手动开 TEC2（环境水箱）*/
        for (int i = 0; i < 120; i++) tick();
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
              "K5 manual mode: frozen body does not trip");

        /* K6 水温读数冻结（48°C 硬切断对它失明）：TC1 开着满**水温窗口**（10 分钟）→ SAFE + ALM_SENSOR_OPEN
         *     （手动模式同样判）。2026-11-26 真机实测（F2）之后窗口由 30 秒改为 10 分钟：健康的读数逐位不变
         *     30–53 秒是常事，按 30 秒判会在正常恒温时误报。*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        tick();                                         /* 喂进第一个水温样本 */
        g_tcs.tc1_act = 30.0f; tick();
        control_manual_tec(1, 1);                       /* TC1 在拍外开：开沿记在最近一拍的时间戳上 */
        CHECK(control_state() == CTRL_IDLE && g_tcs.tec1_on == 1,
              "K6 precondition: manual, TC1 on, water frozen at 30.0");
        for (int i = 0; i < 299; i++) tick100();
        CHECK(control_state() == CTRL_IDLE, "K6 frozen water: no trip before 30 s");
        for (int i = 0; i < 5600; i++) tick100();       /* 累计 589.9 s */
        CHECK(control_state() == CTRL_IDLE, "K6 frozen water: no trip before 600 s (589.9 s)");
        for (int i = 0; i < 200; i++) tick100();        /* 累计 609.9 s */
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K6 frozen water + TC1 on for 600 s -> SAFE");
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 1 && tcs_alarm_active(ALM_BODY_SENSOR) == 0,
              "K6 ALM_SENSOR_OPEN raised (water channel)");
        CHECK(g_tcs.tec1_on == 0, "K6 TC1 off");

        /* K17 X04-F-06 关单条件：体温活着（一直在动）、水温冻在 30°C，TC1 加热满水温窗口（10 分钟）→ 进 SAFE */
        enter_auto_healthy();
        g_tcs.body_temp = 37.0f; tick();                            /* 带内：先喂一拍 IDLE 样本 */
        g_tcs.tc1_act = 30.0f; g_tcs.body_temp = 30.0f; tick();     /* 这一拍进 HEAT */
        CHECK(control_state() == CTRL_HEAT, "K17 precondition: HEAT with water frozen at 30.0");
        for (int i = 0; i < 5900; i++) { g_tcs.body_temp = (i & 1) ? 29.99f : 30.01f; tick100(); }
        CHECK(control_state() == CTRL_HEAT, "K17 frozen water: no trip before 600 s");
        for (int i = 0; i < 200; i++) { g_tcs.body_temp = (i & 1) ? 29.99f : 30.01f; tick100(); }
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K17 frozen water while heating for 600 s -> SAFE even though the body reading is live");
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 1 && tcs_alarm_active(ALM_BODY_SENSOR) == 0,
              "K17 water channel blamed, not body");

        /* K7 反证：水温读数每拍都在变（活着的传感器），TC1 开 60 s 不误报 */
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        tick();
        control_manual_tec(1, 1);
        for (int i = 0; i < 600; i++) { g_tcs.tc1_act = (i & 1) ? 18.43f : 18.46f; tick100(); }
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
              "K7 live water reading: no trip after 60 s with TC1 on");

        /* K20 反证（2026-11-26 真机 F2）：健康的水温读数会连续 50 秒逐位不变、然后才跳 0.01°C。
         *     TC1 开着按这个节奏走 11 分钟 → 不判卡死（30 秒的窗口会把这种读数误判掉）。*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        g_tcs.tc1_act = 18.30f; tick();
        control_manual_tec(1, 1);
        for (int i = 0; i < 6600; i++) {
            if ((i % 500) == 499) g_tcs.tc1_act += 0.01f;       /* 每 50 秒跳一次 */
            tick100();
        }
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0 && g_tcs.tec1_on == 1,
              "K20 healthy reading with 50 s constant runs: no trip in 11 min with TC1 on");

        /* K21 代价（如实钉住）：读数每 9 分钟才动一下 → 判不出卡死（水温窗口 10 分钟）*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        g_tcs.tc1_act = 18.30f; tick();
        control_manual_tec(1, 1);
        for (int i = 0; i < 10800; i++) {                      /* 18 分钟 */
            if ((i % 5400) == 5399) g_tcs.tc1_act += 0.01f;
            tick100();
        }
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
              "K21 a reading that moves once every 9 min is not called stuck (cost of the 10 min window)");

        /* K8 反证：水温不变但 TC1 没开 → 不判。要跑满水温窗口（10 分钟）：
         *     只跑 60 秒的话，"不看 TEC 是否开着"的变异（M34）在 10 分钟窗口下抓不到。*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        tick();
        for (int i = 0; i < 6200; i++) tick();
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
              "K8 frozen water with TC1 off -> no trip, even after 10 min");

        /* K9 读数在 TEC 开之前**不久**冻住（10 秒）：从 TEC 开起再等满一个窗口才判（给被控对象留响应时间）。
         *     r8f（R8-03）之后体温这一路的预冻判据已撤掉：不管开沿前冻了多久，都从 TEC 开起再等满一个
         *     窗口（见 K9b）。水温保留预冻判据（长窗口 10 分钟，见 K9c）：读数在 TEC 开之前就冻满
         *     10 分钟时当拍就判。*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        g_tcs.body_temp = 30.0f; tick();
        for (int i = 0; i < 20; i++) tick_tc1_live();    /* 手动模式下体温冻 10 s（不足一个窗口）*/
        (void)param_set(PARAM_ID_MODE, 1.0f, 4);       /* 切自动：下一拍进 HEAT */
        tick();
        CHECK(control_state() == CTRL_HEAT, "K9 precondition: HEAT starts with a recently-frozen reading");
        for (int i = 0; i < 299; i++) tick100_tc1_live();
        CHECK(control_state() == CTRL_HEAT,
              "K9 a reading frozen shortly before TEC turn-on: the window counts from the turn-on");
        tick100_tc1_live();
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K9 trips one window after TEC turn-on");

        /* K9b（R8-03，独立复核 r8f）体温读数在 TEC 开之前已经冻了 60 秒：**不**当拍判卡死，仍从 TEC
         *     开沿起算满 30 秒才判。那 30 秒是唯一给“被控对象响应时间”的机会；r7f 的体温预冻判据已撤掉
         *     （自动模式进 HEAT 的当拍就可能误进 SAFE，报警还写“水温探头断线”）。水温仍保留预冻
         *     （长窗口 10 分钟远大于健康读数的连续不变段，见 K9c）。*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        g_tcs.body_temp = 30.0f; tick();
        for (int i = 0; i < 120; i++) tick_tc1_live();   /* 手动模式下体温已冻 60 s（超过一个窗口）*/
        (void)param_set(PARAM_ID_MODE, 1.0f, 4);
        tick();                                           /* 这一拍进 HEAT、TEC1 开 */
        CHECK(control_state() == CTRL_HEAT, "K9b precondition: HEAT starts with a long-frozen reading");
        for (int i = 0; i < 299 && control_state() == CTRL_HEAT; i++) tick100_tc1_live();
        CHECK(control_state() == CTRL_HEAT,
              "K9b [R8-03] a long-frozen body reading still gets the 30 s grace from TEC turn-on");
        tick100_tc1_live();
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K9b one window after TEC turn-on the body reading is judged stuck");

        /* K9c（r7f R7-04 / R9-03）水温在 TC1 没开的时候就冻了 11 分钟：TC1 一开不再"当拍"判卡死，
         *     先挂起进 30 秒确认窗（R9-03：热稳态下 10 分钟逐位不变完全可能，"按制热就报探头断线"
         *     会让人去拔插探头）；窗口内读数一次都没动才判。旧的"开沿当拍"断言在 R9-03 之后按设计
         *     不再成立，判别力转移到下面两条（不是削弱：还多了"动一下就撤销"的反向用例 K27）。*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        g_tcs.tc1_act = 30.0f; tick();                    /* 喂进第一个样本，之后不再变 */
        for (int i = 0; i < 6800; i++) tick100();         /* 11 分钟，TC1 关着 → 不判 */
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
              "K9c precondition: water frozen for 11 min with TC1 off, no trip");
        control_manual_tec(1, 1);                         /* TC1 开（开沿记在最近一拍的时间戳上）*/
        for (int i = 0; i < 299; i++) tick100();          /* 29.9 秒：确认窗还没满 */
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
              "K9c [R9-03] prefrozen water is not judged on the turn-on tick (30 s confirmation)");
        tick100(); tick100();                             /* 30.1 秒，一次都没动 */
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K9c prefrozen water trips once the 30 s confirmation window expires without movement");

        /* K24（R8-02，独立复核 r8f）体温先判出来时，水温如果只是**稳态不动**（30 秒级；健康读数的常态，
         *     F2 实测 30–53 秒）不得记进涉事通道。否则体温一恢复，2 秒自愈还要求“水温也活”，设备被按在
         *     SAFE 里（ack 也不行），屏上报警还写错通道。r7f R7-04 原来这条用的就是 30 秒短窗口（tc1_pre）。*/
        enter_auto_healthy();
        g_tcs.body_temp = 30.0f; g_tcs.tc1_act = 25.0f;
        tick();                                           /* HEAT，TEC1 开；两路读数从此都不动 */
        CHECK(control_state() == CTRL_HEAT, "K24 precondition: HEAT with both readings frozen");
        for (int i = 0; i < 301; i++) tick100();
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K24 frozen body reading -> STUCK");
        CHECK(tcs_alarm_active(ALM_BODY_SENSOR) == 1, "K24 the body channel is blamed");
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 0,
              "K24 [R8-02] a merely steady water reading is not blamed (30 s is not stuck for water)");
        {
            int n = 0;
            while (control_state() == CTRL_SAFE && n < 40) {   /* 体温恢复变化、水温仍稳态 → 2 秒自愈 */
                g_tcs.body_temp = (n & 1) ? 29.99f : 30.01f;
                tick100();
                n++;
            }
            CHECK(control_state() == CTRL_HEAT,
                  "K24 [R8-02] the body recovering releases SAFE although the water reading stayed steady");
        }

        /* K24b（R8-02 对照）水温**真的**冻满自己的长窗口（10 分钟）：这一路仍要记进涉事通道——
         *     体温单独恢复不得重开 TEC（对仍然冻着的水温读数，48°C 硬切断是失明的）。
         *     时序：TEC 关着先冻 580 秒（不足长窗口，所以开沿不预判），开 TEC 后再过 30 秒水温
         *     正好越过 600 秒 → 与体温同一拍判出。*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);          /* 手动 */
        g_tcs.tc1_act = 25.0f; tick();                    /* 水温从这里开始不动 */
        for (int i = 0; i < 5800; i++) { g_tcs.body_temp = (i & 1) ? 36.99f : 37.01f; tick100(); }
        CHECK(control_safety_latched() == 0, "K24b precondition: 580 s of frozen water with TEC off, no trip");
        (void)param_set(PARAM_ID_MODE, 1.0f, 4);          /* 自动 → HEAT，TEC1 开；体温从此冻在 30 */
        g_tcs.body_temp = 30.0f;
        tick();
        CHECK(control_state() == CTRL_HEAT,
              "K24b precondition: HEAT starts (water 580 s frozen, not yet past its 10-minute window)");
        for (int i = 0; i < 310 && control_state() == CTRL_HEAT; i++) tick100();
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K24b both readings frozen -> STUCK");
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 1 && tcs_alarm_active(ALM_BODY_SENSOR) == 1,
              "K24b the water reading crossed its own 10-minute window: blamed too");
        {
            int on = 0;
            for (int i = 0; i < 60; i++) {                 /* 6 秒：体温恢复变化、水温仍冻着 */
                g_tcs.body_temp = (i & 1) ? 29.99f : 30.01f;
                tick100();
                if (control_tec_cmd(1) || control_tec_cmd(2)) on++;
            }
            CHECK(on == 0 && control_state() == CTRL_SAFE,
                  "K24b the body recovering alone does not restart the TEC while the water is still frozen");
        }
        {
            int n = 0;
            while (control_state() == CTRL_SAFE && n < 40) {   /* 水温也动起来 → 2 秒自愈 */
                g_tcs.body_temp = (n & 1) ? 29.99f : 30.01f;
                g_tcs.tc1_act   = (n & 1) ? 24.99f : 25.01f;
                tick100();
                n++;
            }
            CHECK(control_state() == CTRL_HEAT,
                  "K24b once the water reading moves too -> self-heals back to HEAT");
        }

        /* K26（R9-02，独立复核 R9）两路**同时**冻住、体温先判出来：水温自己的长窗口（10 分钟）
         *     在锁存期间满了之后必须记进涉事通道并报借来的水温位。否则体温那一路一恢复，
         *     锁存连同涉事通道一起清掉，输出就对着一个真冻住的水温读数开下去——48°C 硬切断对
         *     冻住的读数是失明的（R9 在主机上量到 557.9 秒）。判别性：mutation_control.py 的 M80。*/
        enter_auto_healthy();
        g_tcs.body_temp = 30.0f; g_tcs.tc1_act = 25.0f;
        tick();                                           /* HEAT，TEC1 开；两路读数从此都不动 */
        for (int i = 0; i < 301; i++) tick100();          /* 30.1 s：体温先判出 */
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K26 precondition: body first -> STUCK");
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 0, "K26 precondition: the water channel is not blamed yet");
        for (int i = 0; i < 6100; i++) tick100();         /* 两路都还冻着：水温的长窗口也满了 */
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 1,
              "K26 [R9-02] the water channel is recorded once its own 10-minute window is full");
        {
            int on = 0;
            for (int i = 0; i < 30; i++) {                /* 体温恢复变化、水温仍冻着 */
                g_tcs.body_temp = (i & 1) ? 29.99f : 30.01f;
                tick100();
                if (control_tec_cmd(1) || control_tec_cmd(2)) on++;
            }
            CHECK(control_state() == CTRL_SAFE && on == 0,
                  "K26 [R9-02] body recovery alone does not resume output while the water is frozen");
        }
        {
            int n = 0;
            while (control_state() == CTRL_SAFE && n < 40) {   /* 水温也动起来 → 2 秒自愈 */
                g_tcs.body_temp = (n & 1) ? 29.99f : 30.01f;
                g_tcs.tc1_act   = (n & 1) ? 24.99f : 25.01f;
                tick100();
                n++;
            }
            CHECK(control_state() == CTRL_HEAT, "K26 once the water reading moves too -> self-heals back to HEAT");
        }

        /* K27（R9-03）预冻挂起后、确认窗内读数动过一次 → 撤销，不判卡死。
         *     判别性：mutation_control.py 的 M82。*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);          /* 手动：TC1 关着 */
        g_tcs.tc1_act = 30.0f; tick();                    /* 喂进第一个样本 */
        for (int i = 0; i < 6800; i++) tick100();         /* 水温冻 680 秒（TC1 关着，不判）*/
        control_manual_tec(1, 1);                         /* 开 TC1 → 预冻挂起 */
        for (int i = 0; i < 50; i++) tick100();           /* 5 秒 */
        g_tcs.tc1_act = 30.5f;                            /* 读数动一下 → 撤销 */
        for (int i = 0; i < 600; i++) tick100();          /* 再 60 秒；冻结从这次变化重新算 */
        CHECK(control_safety_latched() == 0 && control_state() != CTRL_SAFE,
              "K27 [R9-03] a reading that moves inside the confirmation window cancels the trip");

        /* K28（R9-04，独立复核 R9）只含 STUCK 的锁存不落盘（STUCK 不在 SAFETY_PERSIST_MASK 里）：
         *     trip 与 ack 都不该产生 NVS 写——写进去的值和原来逐位一样。操作员连点"解除"时的
         *     写放大（ack + 重锁 = 2 次写/对）就是靠这一条与 R9-01 的 link_down 回绝一起消掉的。*/
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp,
            .on_state = 0, .safety_persist = persist_cb, .safety_load = load0_cb,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(1);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 30.0f; g_tcs.tc1_act = 25.0f;
        s_now = 10000u;
        (void)param_set(PARAM_ID_MODE, 1.0f, 4);
        tick();
        s_persist_calls = 0;
        for (int i = 0; i < 301; i++) tick100();
        CHECK((control_safety_latched() & SAFETY_BIT_STUCK) != 0, "K28 precondition: STUCK latched");
        CHECK(s_persist_calls == 0, "K28 [R9-04] tripping a STUCK-only latch writes nothing");
        g_tcs.body_temp = 30.5f;                          /* 读数动一下 → stuck_recovered 成立 */
        tick100();
        s_persist_calls = 0;
        tcs_alarm_ack();
        CHECK(control_safety_latched() == 0, "K28 precondition: the ack cleared the STUCK latch");
        CHECK(s_persist_calls == 0, "K28 [R9-04] clearing a STUCK-only latch writes nothing either");

        /* K14 源不健康时不累计：未布防 + 模块离线 + TC1 开着 + 水温陈旧不变
         *     → 走既有失联路径，不报卡死（离线不是"读数冻结"）*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        set_armed(0);
        tick();
        s_now += 5000u;                                 /* 越过 M207 上电宽限 */
        g_tcs.m207_online = 0;
        control_manual_tec(1, 1);
        for (int i = 0; i < 6200; i++) tick();     /* 跑满水温窗口：M36（源不健康不清样本）要靠这里抓 */
        CHECK((control_safety_latched() & SAFETY_BIT_STUCK) == 0, "K14 unhealthy source is not a stuck reading");
        CHECK(control_state() != CTRL_SAFE, "K14 unarmed + module offline: no SAFE (existing semantics untouched)");

        /* K13 STUCK 不落盘：复位后"哪个通道、冻在哪个值"都无从得知，落盘不增加保护 */
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp,
            .on_state = 0, .safety_persist = persist_cb, .safety_load = load0_cb,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(1);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 30.0f; g_tcs.tc1_act = 25.0f;
        s_now = 10000u;
        (void)param_set(PARAM_ID_MODE, 1.0f, 4);
        tick();
        for (int i = 0; i < 300; i++) tick100();
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K13 precondition: STUCK latched");
        CHECK((s_persisted & SAFETY_BIT_STUCK) == 0, "K13 STUCK is never written to persistent storage");

        /* K16 存储里留着 STUCK 位（别的固件写的）：上电读回即丢——通道归属已丢失，
         *     留着会让没有体温探头的设备永远等不到"体温读数恢复变化" */
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp,
            .on_state = 0, .safety_persist = persist_cb, .safety_load = load_stuck_cb,
        });
        param_registry_init(&(param_hooks_t){0});
        CHECK(control_safety_latched() == 0 && control_state() == CTRL_IDLE,
              "K16 a STUCK bit found in storage is dropped at boot");

        /* ====== K22 / K23（r7f，独立复核 R7-08，P1）：TC2 那一路的水温参与超温硬切断与卡死判据 ======
         * 仓库里 TC1 / TC2 写作"贴身水箱 / 环境水箱"，各有一路 TCACTTEMP 探头。用户确认水路拓扑之前按
         * "两路独立水体"处理：TC1 的探头测不到 TC2 的热量。修前 tc2_act 不进任何保护判据——手动让
         * TC2 制热时主控侧没有超温保护（I5）。判别性由 tests/mutation_control.py 的 M60–M65 给出。*/

        /* K22 TC2 水温 90°C、两路都在输出 → 当拍锁存 OVERTEMP + 报警 + 两路命令为关；
         *     TC2 降温之前 ack 不清，降温 + ack 才退出 */
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
            .tec_force = stub_force,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(0);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 37.0f; g_tcs.tc1_act = 30.0f; g_tcs.tc2_act = 30.0f;
        s_now = 10000u; s_stub_now_ms = s_now; s_link_down = 0;
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        poll100();
        control_manual_tec(1, 1);
        control_manual_tec(2, 1);
        CHECK(g_tcs.tec1_on == 1 && g_tcs.tec2_on == 1 && control_state() == CTRL_IDLE
              && control_safety_latched() == 0,
              "K22 precondition: manual, both TEC on, TC2 water 30");
        reset_stubs();
        g_tcs.tc2_act = 90.0f;                     /* TC2 那一路的水温（独立水体）*/
        poll100();
        CHECK((control_safety_latched() & SAFETY_BIT_OVERTEMP) != 0,
              "K22 TC2 water over 48 latches OVERTEMP");
        CHECK(tcs_alarm_active(ALM_OVERTEMP) == 1, "K22 ALM_OVERTEMP raised");
        CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0 && control_state() == CTRL_SAFE,
              "K22 both TEC off and SAFE (the same shape as a TC1 overtemp)");
        CHECK(force_calls == 2 && last_tec_on == 0, "K22 both 'off' went out through the safety channel");
        g_tcs.tc1_act = 30.0f;                     /* TC1 早已正常 */
        tcs_alarm_ack();
        CHECK((control_safety_latched() & SAFETY_BIT_OVERTEMP) != 0,
              "K22 ack cannot clear while the TC2 water is still over 48");
        CHECK(control_state() == CTRL_SAFE, "K22 still SAFE while the TC2 water is over 48");
        g_tcs.tc2_act = 30.0f;                     /* TC2 降温 */
        tcs_alarm_ack();
        poll100();
        CHECK(control_safety_latched() == 0 && control_state() == CTRL_IDLE,
              "K22 TC2 cooled down + ack -> released");

        /* K23 TC2 命令开着 + tc2_act 逐位不变满水温窗口（10 分钟）→ STUCK + ALM_SENSOR_OPEN、两路关；
         *     TC1 那一路读数一直活着（不干扰通道归属）；TC2 读数仍冻着时 ack 不清，重新变化后 2 秒自愈 */
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(0);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 37.0f; g_tcs.tc1_act = 25.0f; g_tcs.tc2_act = 25.0f;
        s_now = 10000u;
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        poll100();                                 /* 喂进第一个样本 */
        control_manual_tec(2, 1);                  /* TC2 制热；tc2_act 从此不动 */
        CHECK(g_tcs.tec2_on == 1 && control_state() == CTRL_IDLE && control_safety_latched() == 0,
              "K23 precondition: manual, TC2 on, TC2 water frozen");
        for (int i = 0; i < 5900; i++) { g_tcs.tc1_act = (i & 1) ? 24.99f : 25.01f; poll100(); }
        CHECK(control_state() == CTRL_IDLE && (control_safety_latched() & SAFETY_BIT_STUCK) == 0,
              "K23 frozen TC2 water: no trip before 600 s");
        for (int i = 0; i < 200; i++) { g_tcs.tc1_act = (i & 1) ? 24.99f : 25.01f; poll100(); }
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K23 frozen TC2 water + TC2 on for 600 s -> SAFE");
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 1 && tcs_alarm_active(ALM_BODY_SENSOR) == 0,
              "K23 the water channel is blamed (ALM_SENSOR_OPEN), not the body");
        CHECK(g_tcs.tec2_on == 0 && g_tcs.tec1_on == 0, "K23 both TEC off");
        tcs_alarm_ack();                           /* TC2 读数还冻着：ack 不能越过 */
        CHECK((control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K23 ack cannot clear while the TC2 reading is still frozen");
        {   /* TC2 读数重新开始变化 → 2 秒自愈退出 SAFE */
            int n = 0;
            while (control_state() == CTRL_SAFE && n < 40) {
                g_tcs.tc2_act = (n & 1) ? 24.99f : 25.01f;
                g_tcs.tc1_act = (n & 1) ? 24.99f : 25.01f;
                poll100();
                n++;
            }
            CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
                  "K23 TC2 reading moves again -> self-heals out of SAFE");
        }

        /* ====== K25（R8-01，独立复核 r8f）：TC2-only 卡死的“解除”不得把借来的 ALM_SENSOR_OPEN 清掉 ======
         * 通道归属已经扩到 bit2（判据 / 自愈 / ack 的 ok 判据三处都认 4u），只有“报警位清除”这一处还只认
         * bit1。TC2-only 卡死时按一次“解除”：锁存正确留着（设备仍在 SAFE），报警位却被清空——屏与网页
         * 一条横幅都不出，屏还回“已解除”。STUCK 不落盘，操作员的下一步通常是断电重启，重启后这个仍冻着
         * 的 TC2 探头不再有任何保护。判别性：tests/mutation_control.py 的 M77（改回只认 bit1 必须被抓红）。*/
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);        /* 手动 */
        g_tcs.body_temp = 37.0f; g_tcs.tc1_act = 25.0f; g_tcs.tc2_act = 25.0f;
        tick();
        for (int i = 0; i < 6100; i++) { g_tcs.tc1_act = (i & 1) ? 24.99f : 25.01f; tick100(); }
        CHECK(control_safety_latched() == 0, "K25 precondition: TC2 frozen 610 s with TC2 off -> no trip");
        control_manual_tec(2, 1);                       /* 开 TC2：预冻挂起 → 30 秒确认窗后判卡死 */
        for (int i = 0; i < 299; i++) tick100();          /* 29.9 秒：确认窗还没满 */
        CHECK(control_safety_latched() == 0 && control_state() == CTRL_IDLE,
              "K25 [R9-03] prefrozen TC2 water is not judged on the first poll after 'on'");
        tick100(); tick100();                             /* 30.1 秒，一次都没动 */
        CHECK((control_safety_latched() & SAFETY_BIT_STUCK) != 0 && control_state() == CTRL_SAFE,
              "K25 prefrozen TC2 water trips once the 30 s confirmation window expires");
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 1 && tcs_alarm_active(ALM_BODY_SENSOR) == 0,
              "K25 ALM_SENSOR_OPEN visible before ack (TC2 is the only blamed channel)");
        tcs_alarm_ack();
        CHECK((control_safety_latched() & SAFETY_BIT_STUCK) != 0 && control_state() == CTRL_SAFE,
              "K25 ack cannot clear the latch while the TC2 reading is still frozen");
        CHECK(tcs_alarm_active(ALM_SENSOR_OPEN) == 1,
              "K25 [R8-01] ALM_SENSOR_OPEN survives the ack (the only visible reason for this SAFE)");
        {   /* TC2 读数恢复变化 → 2 秒自愈退出 */
            int n = 0;
            while (control_state() == CTRL_SAFE && n < 40) {
                g_tcs.tc2_act = (n & 1) ? 24.99f : 25.01f;
                g_tcs.tc1_act = (n & 1) ? 24.99f : 25.01f;
                poll100();
                n++;
            }
            CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0,
                  "K25 TC2 reading moves again -> self-heals out of SAFE");
        }
    }

    /* ====== 2026-11-25 S 线 e3a（P1-09）：SAFE 必须到得了执行器 ======
     * 普通命令队列在模块离线时拒收（m207_tec 回 -2），离线沿还会被清空——"关"送不到执行器。
     * 控制核这边：① SAFE 的 TCSW=0（进入沿 + 锁存期间每 500ms 的重申）走单独的安全关断通道
     * ops.tec_force（没提供时退化为 tec_sw）；② 做出 tec_known：模块离线时开关状态是"未知"，
     * 快照不得显示"已关"。执行器那一侧（m207_tec_force：离线记下意图、复联第一帧补发）在
     * tcs_m207（e2 批次），这里用主机模型 stub_force 代表它。变异验证见 M43 起。*/
    {
        /* P9-1 / P9-2 / P9-4 / P9-5：安全关断通道 */
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
            .tec_force = stub_force,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(0);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 37.0f; g_tcs.tc1_act = 25.0f;
        s_now = 10000u; s_stub_now_ms = s_now; s_link_down = 0;
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);        /* 手动 */
        reset_stubs();
        control_manual_tec(1, 1);                       /* 手动开着 TC1 */
        CHECK(g_tcs.tec1_on == 1 && tec_calls == 1 && force_calls == 0,
              "P9-5 manual on goes through the normal channel only");
        reset_stubs();
        tcs_emergency_stop();
        CHECK(control_state() == CTRL_SAFE, "P9-1 estop -> SAFE");
        CHECK(force_calls == 2 && tec_calls == 0,
              "P9-1 SAFE turns both TEC off through the force channel, not the queue");
        CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0, "P9-1 command intent: both off");
        CHECK(control_tec_known() == 3, "P9-7 link up: TEC state is known");

        reset_stubs();
        for (int i = 0; i < 10; i++) poll100();         /* 1000ms */
        CHECK(force_calls == 4,
              "P9-2 SAFE re-assert uses the force channel every 500ms (2 x 2 channels in 1 s)");
        CHECK(tec_calls == 0, "P9-2 re-assert never touches the normal queue");
        CHECK(control_state() == CTRL_SAFE, "P9-2 stays SAFE");

        /* 解除（未布防 + 源健康 → ack 可清），下一拍离开 SAFE */
        tcs_alarm_ack();
        poll100();
        CHECK(control_state() == CTRL_IDLE, "P9 precondition: SAFE left");

        /* P9-4 cmd 11（屏侧观测到超温的停机请求）：同样走安全关断通道 */
        reset_stubs();
        CHECK(control_apply_cmd(TCS_CMD_OVERTEMP_STOP) == 0, "P9-4 cmd 11 accepted");
        CHECK(force_calls == 2 && tec_calls == 0 && last_tec_on == 0,
              "P9-4 cmd 11 stops both TEC through the force channel");
        g_tcs.tc1_act = 30.0f;
        poll100();
        tcs_alarm_ack();
        poll100();
        CHECK(control_state() == CTRL_IDLE, "P9 precondition: SAFE left again");

        /* P9-5 自动状态机的"开"只走普通通道（安全关断通道只留给"关"）*/
        g_tcs.body_temp = 30.0f;
        (void)param_set(PARAM_ID_MODE, 1.0f, 4);
        reset_stubs();
        tick();
        CHECK(control_state() == CTRL_HEAT && g_tcs.tec1_on == 1, "P9-5 precondition: HEAT");
        CHECK(tec_calls >= 1 && force_calls == 0, "P9-5 the state machine's 'on' never uses the force channel");

        /* P9-3 没有提供安全关断通道（e2 接线之前）：退化为普通通道，行为与原先一致 */
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(0);
        s_now = 10000u; s_stub_now_ms = s_now;
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        reset_stubs();
        tcs_emergency_stop();
        CHECK(tec_calls == 2 && last_tec_on == 0 && control_state() == CTRL_SAFE,
              "P9-3 without a force channel SAFE falls back to the normal channel");

        /* P9-6（X06-F-03 ②）断链 + 急停：关断必须在 3s 内到达执行器；离线期开关状态是"未知"
         *    主机模型：stub_tec_strict = m207_tec（离线拒收、不出帧）；stub_force = m207_tec_force
         *    （断链时也记下意图）。s_off_delivered_ms = "关"真正送达执行器的时刻。*/
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec_strict, .tec_temp = stub_temp, .on_state = 0,
            .tec_force = stub_force,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(0);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 37.0f; g_tcs.tc1_act = 25.0f;
        s_now = 10000u; s_stub_now_ms = s_now; s_link_down = 0; s_off_delivered_ms = 0;
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        poll100();                                     /* 捕获 boot 基准 */
        reset_stubs();
        control_manual_tec(1, 1);                      /* 加热中 */
        CHECK(g_tcs.tec1_on == 1 && control_tec_known() == 3,
              "P9-6 precondition: TC1 on, link up, state known");
        s_link_down = 1;
        g_tcs.m207_online = 0;                         /* 拔 M207 通信线（模块仍供电、TEC 负载仍在）*/
        {
            uint32_t t0 = s_now;
            s_off_delivered_ms = 0;
            tcs_emergency_stop();                      /* 离线期间触发急停 */
            CHECK(control_state() == CTRL_SAFE && g_tcs.tec1_on == 0,
                  "P9-6 estop while the link is down -> SAFE, command intent off");
            CHECK(s_off_delivered_ms != 0 && (uint32_t)(s_off_delivered_ms - t0) <= 3000u,
                  "P9-6 'off' reaches the actuator channel within 3 s even with the link down");
        }
        for (int i = 0; i < 10; i++) poll100();
        /* i3（P1-09 的另一半）：离线期没有一帧能发出去，但关断必须每 500ms 照样敲安全关断通道——
         * 服务侧靠它保持"有关断还没送到"（m207_tec_force 的保持位），复联第一帧才补得上。
         * 1 s 内：急停当拍 2 次 + 两次重申各 2 次。变异 M55。*/
        CHECK(force_calls == 6,
              "P9-6 the shutdown keeps going through the force channel every 500ms while the link is down");
        CHECK(control_tec_known() == 0,
              "P9-6 while the module is offline the TEC state is UNKNOWN, not 'off'");
        CHECK(g_tcs.tec_known == 0, "P9-6 snapshot copy of tec_known is 0 while offline");
        CHECK(control_state() == CTRL_SAFE, "P9-6 stays SAFE while offline");
        s_link_down = 0;
        g_tcs.m207_online = 1;                         /* 复联 */
        poll100();
        CHECK(control_tec_known() == 3 && g_tcs.tec_known == 3, "P9-6 link back -> TEC state known again");
        reset_stubs();
        for (int i = 0; i < 10; i++) poll100();
        CHECK(force_calls == 4 && last_tec_on == 0,
              "P9-6 after reconnect the off is still pushed every 500ms");
    }

    /* ====== 2026-11-26 S 线 e3a（X03-F-08）：命令意图 ≠ 模块回显 ======
     * g_tcs.tec1_on 原先一个字段两种语义：控制写命令意图，M207 服务回 TCSW? 时写回显。
     * 现在命令意图另有取值接口 control_tec_cmd()（只有 out_tec_sw 写），卡死判据读它。*/
    {
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        tick();
        control_manual_tec(1, 1);
        control_tec_echo_set(1, 1);                   /* 模块回读：开着（i3 起回显是独立字段）*/
        CHECK(control_tec_cmd(1) == 1 && control_tec_act(1) == 1, "K18 precondition: command on, echo on");
        control_tec_echo_set(1, 0);                   /* 晚到的回显：模块回的是"关"（独立字段）*/
        g_tcs.tec1_on = 0;                            /* 同一应答也写旧混用字段——真机的 RX 处理是两处都写 */
        CHECK(control_tec_cmd(1) == 1 && control_tec_act(1) == 0,
              "K18 command intent and module echo are two different quantities");
        g_tcs.tc1_act = 30.0f; tick();                /* 喂进第一个水温样本 */
        for (int i = 0; i < 299; i++) tick100();
        CHECK(control_state() == CTRL_IDLE, "K18 no trip before the window");
        /* 水温窗口 2026-11-26 起是 10 分钟（见 K6）：回显是"关"也照样判，只是要等满水温窗口 */
        for (int i = 0; i < 5600; i++) tick100();
        CHECK(control_state() == CTRL_IDLE, "K18 no trip before 600 s either");
        for (int i = 0; i < 200; i++) tick100();
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K18 a late 'off' echo cannot hide a frozen water reading while TC1 is commanded on (600 s)");
        /* 关断后两个量一致：都是"关" */
        control_manual_tec(2, 1);                     /* SAFE 期被拒 */
        CHECK(control_tec_cmd(2) == 0 && control_tec_act(2) == 0, "K18 in SAFE both channels read off");
    }

    /* ====== 2026-11-26 S 线 i3（P1-09 收口）：模块回显是独立的字段，不跟控制自己的命令走 =====
     * control_tec_act() 原先读 g_tcs.tecN_on：控制每写一次命令就把它改成命令值，"回显"名不副实——
     * 快照里的 tec_act 会在模块还没回话时就跟着命令变，屏分不清"要它关"和"它确实关了"。
     * 现在只有 M207 服务收到 TCSW? 应答时经 control_tec_echo_set() 写它。变异 M56。*/
    {
        enter_auto_healthy();
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        tick();
        control_manual_tec(1, 1);                     /* 命令：开 */
        CHECK(control_tec_cmd(1) == 1 && control_tec_act(1) == 0,
              "K19 a command does not forge the echo: the module has not answered yet");
        control_tec_echo_set(1, 1);                   /* 模块回读：开着 */
        control_tec_echo_set(2, 0);
        CHECK(control_tec_act(1) == 1 && control_tec_act(2) == 0, "K19 the echo follows the module's read-back");
        control_manual_tec(1, 0);                     /* 命令：关；回显还没到 */
        CHECK(control_tec_cmd(1) == 0 && control_tec_act(1) == 1,
              "K19 'told to turn off' is not 'it is off': act still says on until the module answers");
        control_tec_echo_set(1, 0);                   /* 模块回读：关了 */
        CHECK(control_tec_act(1) == 0, "K19 echo off once the module confirms");
        control_tec_echo_set(0, 1); control_tec_echo_set(3, 1);
        CHECK(control_tec_act(1) == 0 && control_tec_act(2) == 0, "K19 a bad channel writes nothing");
        g_tcs.tec1_on = 1;                            /* 旧字段被别处改写，不串到回显里 */
        CHECK(control_tec_act(1) == 0, "K19 the echo field is independent of the shared g_tcs.tecN_on");
    }

    /* ====== 2026-11-26 S 线 i3（M09-F-09 的注入档口径）：注入源在生效的那一路不判卡死 ======
     * 体温：自动 HEAT + 冻着的体温 30 秒会进 SAFE（K1–K5）；注入在生效时豁免。
     * 水温：手动 + TEC1 开 + 冻着的水温 10 分钟会进 SAFE（K6）；注入在生效时豁免。
     * 注入关掉之后，两条判据立刻回来（真实传感器照旧受保护；交付档不接 hook，等于恒关）。变异 M57–M59。*/
    {
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
            .stuck_exempt = stub_exempt,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(0);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 30.0f; g_tcs.tc1_act = 25.0f;
        s_now = 10000u; s_stub_now_ms = s_now;
        (void)param_set(PARAM_ID_MODE, 1.0f, 4);       /* 自动 */
        tick();
        CHECK(control_state() == CTRL_HEAT && g_tcs.tec1_on == 1, "K22 precondition: auto HEAT");
        s_exempt_mask = 1u;                             /* 体温注入在生效 */
        for (int i = 0; i < 70; i++) tick_tc1_live();   /* 35 秒：体温冻着，水温活着 */
        CHECK(control_state() == CTRL_HEAT && (control_safety_latched() & SAFETY_BIT_STUCK) == 0,
              "K22 an injected body reading is exempt from the stuck check (35 s frozen, still HEAT)");
        s_exempt_mask = 0u;                             /* 注入关掉 */
        for (int i = 0; i < 70; i++) tick_tc1_live();   /* 又 35 秒，体温仍冻着 */
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K22 with the injection off a frozen body reading trips after 30 s again");

        /* 水温那一半 */
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
            .stuck_exempt = stub_exempt,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(0);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 37.0f; g_tcs.tc1_act = 25.0f;
        s_now = 10000u;
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);        /* 手动 */
        tick();
        control_manual_tec(1, 1);                       /* TEC1 开 */
        s_exempt_mask = 2u;                             /* 水温注入在生效 */
        for (int i = 0; i < 6300; i++) tick100();       /* 630 秒：水温冻着，超过 10 分钟窗口 */
        CHECK(control_state() == CTRL_IDLE && (control_safety_latched() & SAFETY_BIT_STUCK) == 0 && g_tcs.tec1_on == 1,
              "K22 an injected water reading is exempt from the stuck check (630 s frozen, still running)");
        s_exempt_mask = 0u;
        for (int i = 0; i < 6300; i++) tick100();
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
              "K22 with the injection off a frozen water reading trips after the 10-minute window");
    }

    /* ====== 2026-11-26 S 线 e3a（X06-F-03）：5 条 P0 安全路径的判别性用例 ======
     * 审查指出"超温用例只断言报警位"。每条 P0 配一句直接断言执行器与状态的话；"回退则红"由
     * tests/mutation_control.py 的 M47–M50 给出（把修复回退成修前写法，必须被抓住）。
     *   ① 超温 → tec1_on==0 && state==SAFE            Z1（M47 / M50）
     *   ② 急停 + 断链 → 3 s 内 TEC=0                   P9-6（M43；修前的 control.c 上红的是 P9-6×4）
     *   ③ pindrv 在交付档被拒                          H743 侧：tools/site/verify_prod_clean.py（构建矩阵里跑）
     *   ④ SAFE 不被在途状态机覆盖                      Z2 + 既有的 P0-04 两条（M48）
     *   ⑤ 复位后仍 SAFE                                Z3（M49）*/
    {
        /* Z1 未布防 + 手动 + 两路 TEC 正在输出 → 水温 49°C：当拍双 TEC 关、进 SAFE、风扇全开 */
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
            .tec_force = stub_force,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(0);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 37.0f; g_tcs.tc1_act = 40.0f;
        s_now = 10000u; s_stub_now_ms = s_now; s_link_down = 0;
        (void)param_set(PARAM_ID_MODE, 0.0f, 4);
        poll100();                                     /* 捕获 boot 基准 */
        control_manual_tec(1, 1);                      /* 手动加热中 */
        control_manual_tec(2, 1);
        CHECK(g_tcs.tec1_on == 1 && g_tcs.tec2_on == 1 && control_state() == CTRL_IDLE,
              "Z1 precondition: manual, both TEC on");
        reset_stubs();
        g_tcs.tc1_act = 49.0f;
        poll100();                                     /* 当拍 */
        CHECK(g_tcs.tec1_on == 0 && control_state() == CTRL_SAFE,
              "Z1 overtemp: tec1_on==0 AND state==SAFE on the same tick");
        CHECK(g_tcs.tec2_on == 0, "Z1 overtemp: the other channel is off too");
        CHECK(force_calls == 2 && last_tec_on == 0, "Z1 overtemp: both 'off' went out through the safety channel");
        CHECK(g_tcs.fan_on == 1, "Z1 overtemp: fan full");
        g_tcs.tc1_act = 30.0f;                         /* 回落但未 ack */
        reset_stubs();
        for (int i = 0; i < 10; i++) poll100();
        control_manual_tec(1, 1);
        CHECK(control_state() == CTRL_SAFE && g_tcs.tec1_on == 0 && tec_on_calls == 0,
              "Z1 stays SAFE with TEC off until acknowledged");

        /* Z2 在途状态机盖不过 SAFE：自动 COOL（体温仍高，状态机每个评估拍都"想"继续制冷）时急停，
         *    之后连跑 6 s + 手动"开"——一次"开"都没有到过执行器 */
        enter_auto_healthy();
        g_tcs.body_temp = 40.0f; tick();                /* COOL：TC1 开、风扇开 */
        CHECK(control_state() == CTRL_COOL && g_tcs.tec1_on == 1, "Z2 precondition: auto COOL with TC1 on");
        tcs_emergency_stop();
        CHECK(control_state() == CTRL_SAFE && g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0,
              "Z2 estop: SAFE, both TEC off at once");
        reset_stubs();
        for (int i = 0; i < 12; i++) tick();            /* 6 s，体温一直是 40°C */
        control_manual_tec(1, 1);                       /* 在途 / 网页的"开" */
        control_manual_tec(2, 1);
        CHECK(control_state() == CTRL_SAFE, "Z2 in-flight state machine cannot pull the device out of SAFE");
        CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0 && tec_on_calls == 0,
              "Z2 not a single 'on' reached the actuator in 6 s of in-flight state machine + manual attempts");

        /* Z3 复位后仍 SAFE：锁存里存着超温 + 急停 → 上电就是 SAFE，第一拍双 TEC 关、风扇全开；
         *    水温仍高时 ack 不解除 */
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp,
            .on_state = 0, .safety_persist = persist_cb, .safety_load = load_ot_estop_cb,
        });
        param_registry_init(&(param_hooks_t){0});
        CHECK(control_state() == CTRL_SAFE, "Z3 reset with a persisted latch: SAFE before the first poll");
        g_tcs.tec1_on = 1; g_tcs.tec2_on = 1;          /* 假设复位前两路都在输出 */
        g_tcs.tc1_act = 49.0f;
        s_now = 100u; s_stub_now_ms = s_now;
        reset_stubs();
        poll100();
        CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0 && control_state() == CTRL_SAFE,
              "Z3 first poll after reset: both TEC off, still SAFE");
        CHECK(fan_calls >= 1 && last_fan == 1, "Z3 fan full from the first poll");
        tcs_alarm_ack();
        CHECK(control_safety_latched() != 0 && control_state() == CTRL_SAFE,
              "Z3 ack cannot clear it while the water is still over 48");
    }

    /* ====== 2026-11-26 S 线 e3a（X04-F-07）：参数存储出故障要看得见 ====== */
    {
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
        });
        param_registry_init(&(param_hooks_t){ .eeprom_commit = commit_fail_stub });
        set_armed(0);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 37.0f; g_tcs.tc1_act = 25.0f;
        s_now = 10000u;
        (void)param_set(PARAM_ID_HYST, 0.4f, 4);
        param_persist_flush(s_now);
        s_now += PARAM_PERSIST_DEBOUNCE_MS;
        param_persist_flush(s_now);                   /* commit 失败 */
        control_poll(s_now);
        CHECK(param_persist_fail_count() == 1 && tcs_alarm_active(ALM_EEPROM) == 1,
              "E3 a failed parameter write raises ALM_EEPROM on the next control tick");
        tcs_alarm_ack();
        CHECK(tcs_alarm_active(ALM_EEPROM) == 0, "E3 ALM_EEPROM is informational: ack clears it");
        control_poll(s_now + 100u);
        CHECK(tcs_alarm_active(ALM_EEPROM) == 1, "E3 re-raised on the next tick while the failure count stands");
    }

#if CONFIG_TCS_DIAG || CONFIG_TCS_SIM
    /* ====== S 线 i3（条目 8）：读数"逐位不变"的最长连续时长（mstat 的输入；只在诊断 / 仿真档编译）======
     * 交付档不编译这段实现（control_stuck_max_ms），所以这两条也在同一个 #if 里。*/
    {
        control_init(&(control_ops_t){
            .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
        });
        param_registry_init(&(param_hooks_t){0});
        set_armed(0);
        g_tcs.body_sensor_ok = 1; g_tcs.m207_online = 1; g_tcs.m207_sensor_open = 0;
        g_tcs.body_temp = 37.0f; g_tcs.tc1_act = 25.0f;
        s_now = 10000u;
        poll100();
        /* 水温每拍变一次（0.01°C 级），体温一直冻着：10 秒 */
        for (int i = 0; i < 100; i++) {
            g_tcs.tc1_act = (i & 1) ? 24.99f : 25.01f;
            poll100();
        }
        CHECK(control_stuck_max_ms(2u, s_now) < 1000u, "S1 water keeps moving: its longest frozen run stays small");
        CHECK(control_stuck_max_ms(1u, s_now) >= 9900u && control_stuck_max_ms(1u, s_now) <= 10100u,
              "S1 body: the ongoing frozen run is counted (>= 9.9 s)");
        uint32_t mx = control_stuck_max_ms(1u, s_now);
        g_tcs.body_temp = 37.5f; poll100();      /* 变一下：这一段收尾（记进最大值）*/
        CHECK(control_stuck_max_ms(1u, s_now) >= mx, "S1 after the change the closed run is kept as the max");
        for (int i = 0; i < 150; i++) poll100(); /* 再冻 15 秒：当前这一段比最大值长 */
        CHECK(control_stuck_max_ms(1u, s_now) > mx,
              "S1 the current run takes over once it is longer than the closed one");
        CHECK(control_stuck_max_ms(3u, s_now) == 0, "S1 bad channel -> 0");
    }
#endif
    /* ====== r7f（独立复核 R7-02，2026-11-26，P1）：急停后 2.5 秒内掉电重启带回"自动 + 急停锁存" =====
     * 不变量 I1：急停之后人工确认也不恢复任何输出，只有用户重新按才恢复。
     * 现象（R7 的 t2 复现）：急停把模式切回手动走普通写路径（置脏 → 2 秒防抖 + 500ms 冲洗才落盘），
     * 急停锁存却是立刻落盘的——这 2.5 秒内掉电重启，NVS 里就是"自动模式 + 急停锁存"；人工点一次"解除"，
     * 状态机当拍按体温把 TEC 又开了起来，用户没有重新切过自动。
     * 修法两处：① 上电读到急停锁存 → 模式无条件置回手动并立刻落盘；② 急停路径里"模式 = 手动"同步落盘。
     * 用例：急停之后 0 ~ 3 秒每隔 100ms 做一次"重新 init + 读回镜像与锁存"，断言重启后的
     * （模式, 锁存, 状态）与事件之后一致、人工确认后没有任何输出。*/
    {
        int n0, bad_mode = 0, bad_out = 0, iters = 0;

        /* R7-02 a（②）：急停路径同步落盘——急停返回时 NVS 镜像里已经是"手动"，不必等任何冲洗节拍 */
        nv_fresh_auto_heat();
        CHECK(g_nv_img_have && nv_img_mode() == 1, "R7-02a setup: mode=auto already persisted before the estop");
        n0 = g_nv_commits;
        tcs_emergency_stop_src(PARAM_SRC_TOUCH);
        CHECK(g_nv_commits == n0 + 1 && nv_img_mode() == 0,
              "R7-02a the estop path persists mode=manual at once (no debounce tick in between)");
        CHECK(g_tcs.mode == 0 && (g_nv_latch & SAFETY_BIT_ESTOP) != 0,
              "R7-02a RAM: manual + estop latched");

        /* R7-02 b（①）：掉电正好卡在"锁存已落盘、模式还没落盘"之间 → 重启仍必须是手动 */
        nv_fresh_auto_heat();
        g_nv_cut = 1;                              /* 下一次锁存写完就掉电：之后的镜像写不生效 */
        tcs_emergency_stop_src(PARAM_SRC_TOUCH);
        CHECK((g_nv_latch & SAFETY_BIT_ESTOP) != 0 && nv_img_mode() == 1,
              "R7-02b setup: latch saved, the image still says auto (the 2.5 s window)");
        g_nv_dead = 0;                             /* 上电 */
        n0 = g_nv_commits;
        boot_nvs(1);
        CHECK(g_tcs.mode == 0 && control_state() == CTRL_SAFE
              && control_safety_latched() == SAFETY_BIT_ESTOP,
              "R7-02b boot with an estop latch forces manual mode (mode, latch, state)");
        CHECK(nv_img_mode() == 0 && g_nv_commits > n0,
              "R7-02b the forced manual mode is persisted right at boot");
        nv_sources_ok();
        reset_stubs();
        tcs_alarm_ack();
        nv_run(1500u);
        CHECK(control_state() == CTRL_IDLE && tec_on_calls == 0 && control_tec_cmd(1) == 0 && g_tcs.mode == 0,
              "R7-02b after the ack: no output at all (the user never re-selected auto)");

        /* R7-02 c：急停之后 0 ~ 3 秒每隔 100ms 掉电重启一次（31 次），每次都要（手动, 急停, SAFE），
         *          人工确认后没有任何输出。修前：2.5 秒内的 25 次都带回自动模式，确认后 TEC1 被重新开起来。*/
        for (uint32_t waited = 0u; waited <= 3000u; waited += 100u) {
            nv_fresh_auto_heat();
            tcs_emergency_stop_src(PARAM_SRC_TOUCH);
            nv_run(waited);                        /* 防抖窗口里等这么久，然后掉电 */
            g_nv_dead = 0;
            boot_nvs(1);                           /* 上电 */
            iters++;
            if (!(g_tcs.mode == 0 && control_safety_latched() == SAFETY_BIT_ESTOP
                  && control_state() == CTRL_SAFE)) bad_mode++;
            nv_sources_ok();
            reset_stubs();
            tcs_alarm_ack();
            nv_run(1500u);
            if (!(control_state() == CTRL_IDLE && tec_on_calls == 0
                  && control_tec_cmd(1) == 0 && control_tec_cmd(2) == 0)) bad_out++;
        }
        printf("   R7-02c 急停后 0~3000ms 掉电重启 %d 次：带回自动模式 %d 次，确认后 TEC 被开 %d 次\n",
               iters, bad_mode, bad_out);
        CHECK(bad_mode == 0, "R7-02c a reboot 0~3 s after the estop always comes back manual + estop + SAFE");
        CHECK(bad_out == 0, "R7-02c acknowledging after such a reboot never restarts any output");

        /* R7-02 d（对照）：超温事件不碰模式——重启前后的（模式, 锁存, 状态）本来就一致，
         *      说明上面的扫描真的在读重启后的值，不是恒过 */
        for (uint32_t waited = 0u; waited <= 3000u; waited += 1500u) {
            nv_fresh_auto_heat();
            g_tcs.tc1_act = 49.0f;
            poll100();
            CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_OVERTEMP) != 0
                  && g_tcs.mode == 1, "R7-02d setup: auto mode, overtemp latched");
            nv_run(waited);
            g_nv_dead = 0;
            boot_nvs(1);
            CHECK(g_tcs.mode == 1 && control_safety_latched() == SAFETY_BIT_OVERTEMP
                  && control_state() == CTRL_SAFE,
                  "R7-02d overtemp does not touch the mode: (mode, latch, state) survive the reboot as they were");
        }

        /* R7-02 e（写不进时）：镜像写一直失败也要保证①——RAM 里是手动、状态是 SAFE；NVS 恢复后
         *      防抖冲洗把"手动"补上 */
        nv_fresh_auto_heat();
        g_nv_dead = 1;                             /* 掉电以后的镜像写全部失败 */
        tcs_emergency_stop_src(PARAM_SRC_TOUCH);
        CHECK(g_tcs.mode == 0 && control_state() == CTRL_SAFE && (g_nv_latch & SAFETY_BIT_ESTOP) != 0,
              "R7-02e the estop still leaves the RAM state safe when the image write fails");
        g_nv_dead = 0;                             /* NVS 恢复 */
        nv_run(3500u);
        CHECK(nv_img_mode() == 0, "R7-02e the dirty 'manual' is retried and lands once NVS works again");
    }

    printf("test_control: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
