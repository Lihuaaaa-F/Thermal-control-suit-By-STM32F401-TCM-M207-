/* test_control_manual.c — m1"模式"页的控制内核主机用例（2026-11-26 用户定的新功能）
 *
 * 覆盖：两路手动输出映射 / 只在变化时下发 / 非手动·安全态·越界拒绝 / 上电默认暂停 /
 *       进安全态与切自动时清零 / 风扇联动与恢复 / 模块回读关断后不自动重开 /
 *       屏链路断开 3 秒 = 急停（纯判定 + 急停路径 + 恢复后保持安全态直到确认 + 确认后仍是暂停）。
 * 源 = esp32/components/tcs_core：control.c + control_manual.c + link_loss_guard.c +
 *      param_registry.c + post_pure.c + tcs_link/proto.c。
 * control_service.c 里"判定 → 急停"那 10 行接线的逐字核对在 tests/test_m1_gates.py。
 */
#include <stdio.h>
#include <string.h>
#include "control.h"
#include "control_manual.h"
#include "link_loss_guard.h"
#include "param_registry.h"
#include "post.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); fflush(stdout); } \
} while (0)

/* param_load_eeprom 引用的 eeprom 桩（本测试不调用，仅满足链接）*/
int eeprom_ready(void) { return 0; }
int eeprom_read_image(uint8_t img[EE_IMG_SIZE]) { (void)img; return -1; }

static void set_armed(uint8_t a) { post_arm_commit(a ? POST_ARM_ARMED_ONLINE : POST_ARM_UNARMED_ABSENT); }

/* ====== 输出记录桩：按顺序记下每一次输出 ====== */
typedef struct { char kind; uint8_t ch; uint8_t on; float v; } rec_t;
static rec_t  g_log[256];
static int    g_n;
static uint16_t g_bc_ids[64]; static int g_bc_n;

static void stub_fan(uint8_t on)        { if (g_n < 256) { g_log[g_n].kind = 'F'; g_log[g_n].on = on; g_n++; } }
static void stub_tec(uint8_t ch, uint8_t on) { if (g_n < 256) { g_log[g_n].kind = 'S'; g_log[g_n].ch = ch; g_log[g_n].on = on; g_n++; } }
static void stub_temp(uint8_t ch, float v)   { if (g_n < 256) { g_log[g_n].kind = 'T'; g_log[g_n].ch = ch; g_log[g_n].v = v; g_n++; } }
static void bcast_rec(param_desc_t *p, uint8_t src)
        { (void)src; if (g_bc_n < 64) g_bc_ids[g_bc_n] = p->id; g_bc_n++; }
static int bc_has(uint16_t id)
{
    for (int i = 0; i < g_bc_n && i < 64; i++) if (g_bc_ids[i] == id) return 1;
    return 0;
}

static void log_reset(void) { g_n = 0; g_bc_n = 0; }
/* 最近的第 k 次（从 0 数）记录是不是"通道 ch 的开关 = on" */
static int has_sw(uint8_t ch, uint8_t on)
{
    for (int i = 0; i < g_n; i++)
        if (g_log[i].kind == 'S' && g_log[i].ch == ch && g_log[i].on == on) return 1;
    return 0;
}
static int has_temp(uint8_t ch, float v)
{
    for (int i = 0; i < g_n; i++)
        if (g_log[i].kind == 'T' && g_log[i].ch == ch && g_log[i].v == v) return 1;
    return 0;
}
static int count_kind(char k) { int c = 0; for (int i = 0; i < g_n; i++) if (g_log[i].kind == k) c++; return c; }
static int count_sw_on(void) { int c = 0; for (int i = 0; i < g_n; i++) if (g_log[i].kind == 'S' && g_log[i].on) c++; return c; }

static uint32_t s_now;
static void tick(void) { s_now += 500u; control_poll(s_now); }
static void tick100(void) { s_now += 100u; control_poll(s_now); }

/* 手动模式、未布防（没有体温探头的台架语义）、模块在线、水温正常 */
static void enter_manual(void)
{
    control_init(&(control_ops_t){
        .fan_set = stub_fan, .tec_sw = stub_tec, .tec_temp = stub_temp, .on_state = 0,
    });
    /* 与固件 main.c 同一处装配（除 M207 / 网页钩子）：广播、模式变更通知、写前校验 */
    param_registry_init(&(param_hooks_t){ .on_broadcast = bcast_rec,
                                          .on_mode_change = control_on_mode_change,
                                          .on_check = control_param_check });
    set_armed(0);
    g_tcs.body_sensor_ok = 0;
    g_tcs.m207_online = 1;
    g_tcs.m207_sensor_open = 0;
    g_tcs.tc1_act = 25.0f;
    g_tcs.tec1_on = 0; g_tcs.tec2_on = 0; g_tcs.fan_on = 0;
    s_now = 10000u;
    tick();                                   /* 时间基准 + 手动节拍跑起来 */
    log_reset();
}

static void release_estop_unarmed(void)
{
    tcs_alarm_ack();
    tick();
}

/* ====== r7f（独立复核 R7-01，2026-11-26）：断链 × 安全态原因 矩阵（L04）======
 * control_service.c 的 link_loss_check（控制节拍：先 control_poll，再问一次判定；要触发就走急停路径）
 * 在主机上的同构物。already_safe 一行与固件同一写法——逐字核对在 tests/test_m1_gates.py 第 1 项。*/
static int g_ll_estops;                /* 判定为"触发急停"的次数 */
static void link_check(link_loss_guard_t *g, int seen, uint32_t age)
{
    if (link_loss_guard_step(g, seen, age, link_loss_already_safe(control_safety_latched()))
        != TCS_LINK_LOSS_ESTOP) return;
    g_ll_estops++;
    tcs_emergency_stop_src(PARAM_SRC_KERNEL);   /* control_service.c 在这一拍做的事 */
}
/* 屏链路断开 lost_ms：每 100ms 一拍（对端帧龄每拍 +100ms）；从 heal_at 起每拍调一次 heal（故障源恢复）*/
static void link_lost_run(link_loss_guard_t *g, uint32_t lost_ms, uint32_t heal_at, void (*heal)(void))
{
    uint32_t age = 0u;
    for (uint32_t t = 0u; t < lost_ms; t += 100u) {
        if (heal && t >= heal_at) heal();
        tick100();
        age += 100u;
        link_check(g, 1, age);
    }
}
/* 屏链路恢复（对端帧龄 200ms）之后跑 ms 毫秒 */
static void link_alive_run(link_loss_guard_t *g, uint32_t ms, void (*heal)(void))
{
    for (uint32_t t = 0u; t < ms; t += 100u) {
        if (heal) heal();
        tick100();
        link_check(g, 1, 200u);
    }
}
static void heal_m207(void)  { g_tcs.m207_online = 1; }
static void heal_water(void) { g_tcs.tc1_act = 25.0f; }
static uint8_t s_ll_flick;
static void heal_body(void)  { s_ll_flick = (uint8_t)!s_ll_flick; g_tcs.body_sensor_ok = 1; g_tcs.body_temp = s_ll_flick ? 35.99f : 36.01f; }
static void ll_begin(link_loss_guard_t *g) { memset(g, 0, sizeof *g); g_ll_estops = 0; log_reset(); }
static void ll_print(const char *row, const link_loss_guard_t *g)
{
    printf("   L04 %-8s：屏断链 5s 末 estop 判定 %d 次，tripped=%u，锁存=0x%02X，状态=%d，TEC1/TEC2 命令=%u/%u，模式=%u\n",
           row, g_ll_estops, (unsigned)g->tripped, (unsigned)control_safety_latched(), (int)control_state(),
           (unsigned)control_tec_cmd(1), (unsigned)control_tec_cmd(2), (unsigned)g_tcs.mode);
}
/* 自动模式制热中（已布防、体温探头在、模块在线、水温 25°C 不动）*/
static void enter_auto_heat(void)
{
    enter_manual();
    g_tcs.body_sensor_ok = 1;
    set_armed(1);
    (void)param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB);
    g_tcs.body_temp = 36.0f;                 /* < cold - hyst → 制热 */
    tick(); tick();
}

int main(void)
{
    /* ====== M01 上电默认暂停；两个参数的标志 ====== */
    enter_manual();
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f && param_get(PARAM_ID_TEC2_MANUAL) == 0.0f,
          "M01 boot: both manual params default to pause");
    {
        const param_desc_t *a = param_find(PARAM_ID_TEC1_MANUAL);
        const param_desc_t *b = param_find(PARAM_ID_TEC2_MANUAL);
        CHECK(a && b && a->type == PARAM_TYPE_U8 && b->type == PARAM_TYPE_U8, "M01 both registered as U8");
        CHECK(a && (a->flags & PF_RW) && (a->flags & PF_SCREEN), "M01 TEC1: RW + SCREEN");
        CHECK(a && !(a->flags & PF_PERSIST) && !(a->flags & PF_WEB), "M01 TEC1: not persisted, not on web");
        CHECK(b && !(b->flags & PF_PERSIST) && !(b->flags & PF_WEB), "M01 TEC2: not persisted, not on web");
        CHECK(a && a->min == 0.0f && a->max == 2.0f, "M01 range 0..2");
    }
    CHECK(control_manual_dir(1) == TEC_DIR_PAUSE && control_manual_dir(2) == TEC_DIR_PAUSE,
          "M01 boot: nothing applied");
    CHECK(control_tec_dir(1) == TEC_DIR_PAUSE && control_tec_dir(2) == TEC_DIR_PAUSE,
          "M01 boot: snapshot direction = pause");

    /* ====== M02 两路映射 ====== */
    CHECK(param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK) == 0, "M02 write TEC1=cool accepted");
    CHECK(g_tcs.tec1_on == 0, "M02 not applied until the control tick (no I/O from the caller's task)");
    tick();
    CHECK(has_temp(1, TEC_COOL_TARGET_C) && has_sw(1, 1), "M02 TEC1 cool: target 15.0 + TCSW=1");
    CHECK(g_tcs.tec1_on == 1 && control_manual_dir(1) == TEC_DIR_COOL, "M02 TEC1 applied = cool");
    CHECK(control_tec_dir(1) == TEC_DIR_COOL, "M02 snapshot dir TEC1 = cool");
    log_reset();
    CHECK(param_set(PARAM_ID_TEC2_MANUAL, 2.0f, PARAM_SRC_LINK) == 0, "M02 write TEC2=heat accepted");
    tick();
    CHECK(has_temp(2, TEC_HEAT_TARGET_C) && has_sw(2, 1), "M02 TEC2 heat: target 45.0 + TCSW=1");
    CHECK(control_tec_dir(2) == TEC_DIR_HEAT, "M02 snapshot dir TEC2 = heat");
    CHECK(control_tec_dir(1) == TEC_DIR_COOL, "M02 TEC1 unaffected by TEC2 write");
    log_reset();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 2.0f, PARAM_SRC_LINK);
    tick();
    CHECK(has_temp(1, TEC_HEAT_TARGET_C) && has_sw(1, 1), "M02 TEC1 cool->heat re-sends target + on");
    CHECK(control_tec_dir(1) == TEC_DIR_HEAT, "M02 snapshot dir TEC1 = heat");
    log_reset();
    (void)param_set(PARAM_ID_TEC2_MANUAL, 0.0f, PARAM_SRC_LINK);
    tick();
    CHECK(count_kind('S') == 1 && has_sw(2, 0) && count_kind('T') == 0, "M02 pause: only TCSW=0, no target");
    CHECK(control_tec_dir(2) == TEC_DIR_PAUSE, "M02 snapshot dir TEC2 = pause");

    /* ====== M03 只在变化时下发 ======
     * 水温读数要"活着"（每拍都在变）：合并进 e3a 的卡死判据之后，手动制热 + 水温读数 30 秒不变会进安全态
     * （那正是 M13 要验证的行为）；读数不抖的话这里量到的是安全态的输出，不是"没有重发"。*/
    log_reset();
    for (int i = 0; i < 100; i++) { g_tcs.tc1_act = (i & 1) ? 24.99f : 25.01f; tick(); }   /* 50 秒，参数不变 */
    CHECK(control_state() == CTRL_IDLE, "M03 live water: no SAFE");
    CHECK(g_n == 0, "M03 no re-send while params unchanged (100 ticks, 0 outputs)");

    /* ====== M04 越界拒绝（不钳制）===== */
    CHECK(param_set(PARAM_ID_TEC1_MANUAL, 3.0f, PARAM_SRC_LINK) == PARAM_REJECT_RANGE, "M04 3 rejected");
    CHECK(param_set(PARAM_ID_TEC1_MANUAL, 1.5f, PARAM_SRC_LINK) == PARAM_REJECT_RANGE, "M04 1.5 rejected");
    CHECK(param_set(PARAM_ID_TEC2_MANUAL, -1.0f, PARAM_SRC_LINK) == PARAM_REJECT_RANGE, "M04 -1 rejected");
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 2.0f && param_get(PARAM_ID_TEC2_MANUAL) == 0.0f,
          "M04 rejected writes leave values untouched (old code clamped 3 -> 2)");
    log_reset();
    g_tcs.tc1_act = 24.99f; tick();
    CHECK(g_n == 0, "M04 rejected writes produce no output");

    /* ====== M05 非手动模式拒绝 ====== */
    enter_manual();
    g_tcs.body_sensor_ok = 1;                        /* 自动模式要有体温样本 */
    CHECK(param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB) == 0, "M05 switch to auto");
    tick();
    CHECK(param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK) == PARAM_REJECT_NOT_MANUAL,
          "M05 manual param rejected in auto mode");
    CHECK(param_set(PARAM_ID_TEC2_MANUAL, 2.0f, PARAM_SRC_LINK) == PARAM_REJECT_NOT_MANUAL,
          "M05 manual param TEC2 rejected in auto mode");
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f, "M05 value unchanged");
    log_reset();
    tick();
    CHECK(g_n == 0, "M05 rejected write -> no output");

    /* ====== M06 切到自动：两路清零、第二路关、第一路交给状态机 ====== */
    enter_manual();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);
    (void)param_set(PARAM_ID_TEC2_MANUAL, 2.0f, PARAM_SRC_LINK);
    tick();
    CHECK(g_tcs.tec1_on == 1 && g_tcs.tec2_on == 1, "M06 precondition: both on in manual");
    log_reset();
    CHECK(param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB) == 0, "M06 switch to auto");
    tick();
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f && param_get(PARAM_ID_TEC2_MANUAL) == 0.0f,
          "M06 switching to auto clears both manual params");
    CHECK(has_sw(1, 0) && has_sw(2, 0) && count_sw_on() == 0, "M06 both TEC switched off at the switch");
    CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0, "M06 no output left running unmanaged");
    CHECK(control_manual_dir(1) == TEC_DIR_PAUSE && control_manual_dir(2) == TEC_DIR_PAUSE,
          "M06 manual applied state cleared");
    CHECK(bc_has(PARAM_ID_TEC1_MANUAL) && bc_has(PARAM_ID_TEC2_MANUAL),
          "M06 clearing is broadcast to the screen (0x11 mirror follows)");
    /* 自动状态机接管：体温 38.0 → 第一路制冷；第二路保持暂停 */
    g_tcs.body_sensor_ok = 1;
    g_tcs.body_temp = 38.0f;
    tick(); tick();
    CHECK(control_state() == CTRL_COOL, "M06 auto FSM takes over (body 38.0 -> COOL)");
    CHECK(has_temp(1, TEC_COOL_TARGET_C) && has_sw(1, 1), "M06 FSM drives TEC1 cool");
    CHECK(control_tec_dir(1) == TEC_DIR_COOL && control_tec_dir(2) == TEC_DIR_PAUSE,
          "M06 snapshot: TEC1 cool by FSM, TEC2 pause");

    /* ====== M07 风扇联动与恢复 ====== */
    enter_manual();
    CHECK(g_tcs.fan_on == 0, "M07 precondition: fan off");
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);
    tick();
    CHECK(g_tcs.fan_on == 1, "M07 TEC running -> fan on");
    CHECK(control_manual_fan_pref() == 0, "M07 user's fan choice remembered (off)");
    /* 网页的风扇切换在联动期间把它关了：下一拍开回来 */
    log_reset();
    control_manual_fan(0);
    CHECK(g_tcs.fan_on == 0, "M07 (setup) fan forced off by other entry");
    tick100();
    CHECK(g_tcs.fan_on == 1, "M07 fan forced back on while a TEC runs");
    log_reset();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 0.0f, PARAM_SRC_LINK);
    tick();
    CHECK(g_tcs.fan_on == 0, "M07 both paused -> fan back to user's choice (off)");
    CHECK(control_manual_fan_pref() == 0, "M07 pref off");
    /* 用户原本开着风扇：联动结束后仍然开着 */
    enter_manual();
    control_manual_fan(1);
    CHECK(g_tcs.fan_on == 1, "M07 (setup) user turned fan on");
    (void)param_set(PARAM_ID_TEC2_MANUAL, 2.0f, PARAM_SRC_LINK);
    tick();
    CHECK(g_tcs.fan_on == 1, "M07 fan stays on with TEC2 heat");
    CHECK(control_manual_fan_pref() == 1, "M07 user's fan choice remembered (on)");
    (void)param_set(PARAM_ID_TEC2_MANUAL, 0.0f, PARAM_SRC_LINK);
    tick();
    CHECK(g_tcs.fan_on == 1, "M07 fan still on after both paused (user's choice was on)");
    /* 两路都在跑：只关一路，风扇仍开 */
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);
    (void)param_set(PARAM_ID_TEC2_MANUAL, 1.0f, PARAM_SRC_LINK);
    tick();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 0.0f, PARAM_SRC_LINK);
    tick();
    CHECK(g_tcs.fan_on == 1, "M07 one channel still running -> fan stays on");

    /* ====== M08 进安全态：两路清零；自愈解除后不自己恢复输出；风扇回到用户选择 ====== */
    enter_manual();
    control_manual_fan(0);
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);
    (void)param_set(PARAM_ID_TEC2_MANUAL, 2.0f, PARAM_SRC_LINK);
    tick();
    CHECK(g_tcs.tec1_on == 1 && g_tcs.tec2_on == 1 && g_tcs.fan_on == 1, "M08 precondition: both on, fan linked");
    log_reset();
    set_armed(1);                                    /* 布防后拔体温探头 → 完整 FMEA → SAFE */
    g_tcs.body_sensor_ok = 1; tick();                /* 已布防且探头在：不进 SAFE */
    CHECK(control_state() == CTRL_IDLE, "M08 (sanity) armed + probe present: no SAFE");
    g_tcs.body_sensor_ok = 0;
    tick();
    CHECK(control_state() == CTRL_SAFE, "M08 probe lost (armed) -> SAFE");
    CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0, "M08 SAFE: both TEC off");
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f && param_get(PARAM_ID_TEC2_MANUAL) == 0.0f,
          "M08 entering SAFE clears both manual params");
    CHECK(control_manual_dir(1) == TEC_DIR_PAUSE && control_manual_dir(2) == TEC_DIR_PAUSE,
          "M08 entering SAFE clears applied state");
    CHECK(control_tec_dir(1) == TEC_DIR_PAUSE && control_tec_dir(2) == TEC_DIR_PAUSE,
          "M08 snapshot shows pause");
    log_reset();
    g_tcs.body_sensor_ok = 1;                        /* 探头接回：2 秒自愈（通信类）*/
    for (int i = 0; i < 8; i++) tick();
    CHECK(control_state() == CTRL_IDLE, "M08 comm fault self-recovered -> IDLE");
    CHECK(count_sw_on() == 0 && g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0,
          "M08 outputs NOT restored after self-recovery");
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f && param_get(PARAM_ID_TEC2_MANUAL) == 0.0f,
          "M08 params stay paused after self-recovery");
    CHECK(g_tcs.fan_on == 0, "M08 fan back to user's choice (off) after leaving SAFE");

    /* ====== M09 急停：清零；确认解除后回到手动、两路仍是暂停；安全态下拒绝 ====== */
    enter_manual();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);
    (void)param_set(PARAM_ID_TEC2_MANUAL, 1.0f, PARAM_SRC_LINK);
    tick();
    log_reset();
    tcs_emergency_stop_src(PARAM_SRC_TOUCH);
    CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0 && g_tcs.fan_on == 1, "M09 estop: TEC off, fan on");
    CHECK(control_state() == CTRL_SAFE && tcs_alarm_active(ALM_ESTOP) == 1, "M09 estop: SAFE + ALM_ESTOP");
    tick();
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f && param_get(PARAM_ID_TEC2_MANUAL) == 0.0f,
          "M09 estop clears both manual params");
    CHECK(control_manual_dir(1) == TEC_DIR_PAUSE && control_manual_dir(2) == TEC_DIR_PAUSE,
          "M09 estop clears applied state");
    CHECK(bc_has(PARAM_ID_TEC1_MANUAL) && bc_has(PARAM_ID_TEC2_MANUAL),
          "M09 clearing is broadcast to the screen");
    CHECK(param_set(PARAM_ID_TEC1_MANUAL, 2.0f, PARAM_SRC_LINK) == PARAM_REJECT_SAFE,
          "M09 manual param rejected while estop latched");
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f, "M09 rejected write leaves pause");
    release_estop_unarmed();
    CHECK(control_state() == CTRL_IDLE, "M09 ack -> leaves SAFE");
    CHECK(g_tcs.mode == 0, "M09 back in manual mode");
    CHECK(count_sw_on() == 0 && g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0, "M09 outputs still paused after ack");
    CHECK(control_tec_dir(1) == TEC_DIR_PAUSE && control_tec_dir(2) == TEC_DIR_PAUSE,
          "M09 snapshot still pause after ack");
    /* 解除之后重新点"制冷"才会输出 */
    CHECK(param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK) == 0, "M09 write accepted after ack");
    tick();
    CHECK(g_tcs.tec1_on == 1 && control_tec_dir(1) == TEC_DIR_COOL, "M09 user re-tap restarts output");

    /* ====== M10 超温锁存期间同样拒绝 ====== */
    enter_manual();
    g_tcs.tc1_act = 49.0f;
    tick();
    CHECK(control_state() == CTRL_SAFE, "M10 overtemp -> SAFE");
    CHECK(param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK) == PARAM_REJECT_SAFE,
          "M10 manual param rejected while overtemp latched");

    /* ====== M11 模块回读关断：不自动重开，参数置回暂停 ====== */
    enter_manual();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);
    tick();
    CHECK(g_tcs.tec1_on == 1, "M11 precondition: TEC1 cooling");
    log_reset();
    g_tcs.tec1_on = 0;                               /* 复联重同步读到模块实际是关的（TCSW?=0）*/
    tick();
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f, "M11 module reports off -> param back to pause");
    CHECK(count_sw_on() == 0, "M11 no automatic restart");
    CHECK(control_tec_dir(1) == TEC_DIR_PAUSE, "M11 snapshot shows pause");
    CHECK(bc_has(PARAM_ID_TEC1_MANUAL), "M11 pause broadcast to the screen (0x11)");

    /* ====== M12 网页 / 台架直控（旧命令）开着：如实报"开，但方向不是模式页设的" ====== */
    enter_manual();
    control_manual_tec(2, 1);
    tick();
    CHECK(g_tcs.tec2_on == 1, "M12 (setup) legacy TEC2 on");
    CHECK(control_tec_dir(2) == TEC_DIR_EXT, "M12 legacy on reported as EXT");
    CHECK(control_tec_dir(1) == TEC_DIR_PAUSE, "M12 TEC1 untouched");
    /* 模式页设的方向 + 旧命令把它关了：参数回暂停 */
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);
    tick();
    log_reset();
    control_manual_tec(1, 0);
    tick();
    CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f && g_tcs.tec1_on == 0, "M12 legacy off -> param pause, stays off");

    /* ====== L01 纯判定：屏链路断开 3 秒 = 急停 ====== */
    {
        link_loss_guard_t g = { 0 };
        CHECK(link_loss_guard_step(NULL, 1, 100000u, 0) == TCS_LINK_LOSS_NONE, "L01 NULL guard -> none");
        CHECK(link_loss_guard_step(&g, 0, 100000u, 0) == TCS_LINK_LOSS_NONE, "L01 never connected -> no estop");
        CHECK(link_loss_guard_step(&g, 1, 0u, 0) == TCS_LINK_LOSS_NONE, "L01 link alive -> none");
        CHECK(link_loss_guard_step(&g, 1, 2900u, 0) == TCS_LINK_LOSS_NONE, "L01 2.9 s -> no estop");
        CHECK(link_loss_guard_step(&g, 1, 2999u, 0) == TCS_LINK_LOSS_NONE, "L01 2.999 s -> no estop");
        CHECK(link_loss_guard_step(&g, 1, 3000u, 0) == TCS_LINK_LOSS_ESTOP, "L01 3.0 s -> estop");
        CHECK(link_loss_guard_step(&g, 1, 3100u, 0) == TCS_LINK_LOSS_NONE, "L01 no repeat while still lost");
        CHECK(link_loss_guard_step(&g, 1, 60000u, 0) == TCS_LINK_LOSS_NONE, "L01 still no repeat after a minute");
        CHECK(link_loss_guard_step(&g, 1, 100u, 0) == TCS_LINK_LOSS_NONE, "L01 link back -> none, re-armed");
        CHECK(link_loss_guard_step(&g, 1, 3000u, 0) == TCS_LINK_LOSS_ESTOP, "L01 next loss triggers again");
        CHECK(link_loss_guard_step(&g, 1, 100u, 1) == TCS_LINK_LOSS_NONE, "L01 recovered (safe) -> none");
        CHECK(link_loss_guard_step(&g, 1, 3000u, 1) == TCS_LINK_LOSS_NONE, "L01 already safe -> no repeat trigger");
        CHECK(link_loss_guard_step(&g, 1, 4000u, 0) == TCS_LINK_LOSS_NONE,
              "L01 stays quiet until the link is back (no estop/ack loop)");
        CHECK(link_loss_guard_step(&g, 1, 100u, 0) == TCS_LINK_LOSS_NONE, "L01 link back again");
        CHECK(link_loss_guard_step(&g, 1, 3000u, 0) == TCS_LINK_LOSS_ESTOP, "L01 re-armed after recovery");
        CHECK(link_loss_guard_step(&g, 0, 100000u, 0) == TCS_LINK_LOSS_NONE, "L01 never-connected wins over a long age");
    }

    /* ====== L02 2.9 秒不触发：输出原样 ====== */
    enter_manual();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);
    (void)param_set(PARAM_ID_TEC2_MANUAL, 2.0f, PARAM_SRC_LINK);
    tick();
    log_reset();
    {
        link_loss_guard_t g = { 0 };
        for (uint32_t a = 500u; a <= 2900u; a += 500u)
            CHECK(link_loss_guard_step(&g, 1, a, control_state() == CTRL_SAFE) == TCS_LINK_LOSS_NONE,
                  "L02 2.9 s lost -> no estop");
        CHECK(control_state() == CTRL_IDLE && g_tcs.tec1_on == 1 && g_tcs.tec2_on == 1,
              "L02 outputs untouched at 2.9 s");
        /* 3.0 秒：触发 → 走急停同一条路径 */
        CHECK(link_loss_guard_step(&g, 1, 3000u, control_state() == CTRL_SAFE) == TCS_LINK_LOSS_ESTOP,
              "L02 3.0 s lost -> estop");
        tcs_emergency_stop_src(PARAM_SRC_KERNEL);   /* control_service.c 在这一拍做的事 */
        CHECK(g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0, "L02 estop: both TEC off");
        CHECK(g_tcs.fan_on == 1, "L02 estop: fan on");
        CHECK(control_state() == CTRL_SAFE && tcs_alarm_active(ALM_ESTOP) == 1, "L02 estop latched");
        CHECK(g_tcs.mode == 0, "L02 estop forces manual");
        tick();
        CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f && param_get(PARAM_ID_TEC2_MANUAL) == 0.0f,
              "L02 link-loss estop clears both manual params");
        /* 链路恢复：保持安全态，直到确认 */
        log_reset();
        for (int i = 0; i < 12; i++) {              /* 6 秒，链路一直在 */
            CHECK(link_loss_guard_step(&g, 1, 200u, control_state() == CTRL_SAFE) == TCS_LINK_LOSS_NONE,
                  "L02 recovered -> guard quiet");
            tick();
        }
        CHECK(control_state() == CTRL_SAFE && tcs_alarm_active(ALM_ESTOP) == 1,
              "L02 stays in SAFE after the link is back (until acknowledged)");
        CHECK(count_sw_on() == 0, "L02 nothing restarted while waiting");
        release_estop_unarmed();
        CHECK(control_state() == CTRL_IDLE, "L02 ack after recovery -> leaves SAFE");
        CHECK(count_sw_on() == 0 && g_tcs.tec1_on == 0 && g_tcs.tec2_on == 0, "L02 outputs still paused after ack");
        CHECK(param_get(PARAM_ID_TEC1_MANUAL) == 0.0f && param_get(PARAM_ID_TEC2_MANUAL) == 0.0f,
              "L02 params still paused after ack");
        CHECK(control_tec_dir(1) == TEC_DIR_PAUSE && control_tec_dir(2) == TEC_DIR_PAUSE,
              "L02 snapshot still pause after ack");
    }

    /* ====== L03 从未连上：一直不触发 ====== */
    enter_manual();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);
    tick();
    {
        link_loss_guard_t g = { 0 };
        for (uint32_t a = 3000u; a <= 60000u; a += 3000u)
            CHECK(link_loss_guard_step(&g, 0, a, control_state() == CTRL_SAFE) == TCS_LINK_LOSS_NONE,
                  "L03 never connected -> never estop");
        CHECK(control_state() == CTRL_IDLE && g_tcs.tec1_on == 1, "L03 manual output keeps running (web-only bench)");
    }

    /* ====== L04 断链 × 安全态原因 矩阵（r7f，独立复核 R7-01；由 R7 的复现用例 t1 改写）======
     * 不变量 I2：屏链路断开 3 秒 = 急停；链路恢复后保持安全态，直到屏上确认。
     * 修前：control_service.c 把"状态机在安全态"一律当成"这次断开处理过了"。模块失联 / 体温失联 / 读数卡死的
     * 安全态会在源恢复 2 秒后自愈——屏断着链，故障一自愈设备就自己把 TEC 开了回来，没有人确认过。
     * 修后：already_safe 只认急停 / 超温锁存着的安全态（link_loss_already_safe）。
     * 每一行：先进入对应原因的安全态，再让屏断链 5 秒（第 1.5 秒起故障源恢复），然后链路恢复再跑 10 秒。
     *   急停 / 超温（只有确认能解除）：不重复触发、不叠加急停位，留在安全态；
     *   模块失联 / 体温失联 / 读数卡死（会自愈）：断链满 3 秒判一次急停，锁存里出现急停位，故障好了也不出去。*/
    {
        link_loss_guard_t g;
        uint8_t lat;

        /* ① 急停：屏先按了急停、然后线才断 */
        enter_auto_heat();
        CHECK(control_state() == CTRL_HEAT && g_tcs.tec1_on == 1, "L04 estop row: precondition HEAT");
        tcs_emergency_stop_src(PARAM_SRC_TOUCH);
        ll_begin(&g);
        link_lost_run(&g, 5000u, 0u, 0);
        ll_print("急停", &g);
        lat = control_safety_latched();
        CHECK(g_ll_estops == 0 && g.tripped == 1, "L04 estop row: link loss in an estop SAFE does not trigger again");
        CHECK(lat == SAFETY_BIT_ESTOP, "L04 estop row: latch unchanged (estop only)");
        CHECK(control_state() == CTRL_SAFE && control_tec_cmd(1) == 0 && control_tec_cmd(2) == 0,
              "L04 estop row: still SAFE, both TEC off");
        log_reset();
        link_alive_run(&g, 10000u, 0);
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_ESTOP) != 0,
              "L04 estop row: link back -> stays SAFE until acknowledged");
        CHECK(count_sw_on() == 0, "L04 estop row: no TEC on while waiting");
        tcs_alarm_ack(); tick100();
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0 && count_sw_on() == 0 && g_tcs.mode == 0,
              "L04 estop row: acknowledged -> IDLE in manual mode, nothing restarted");

        /* ② 超温：同上——断链不叠加急停位；降了温、链路也恢复了，仍要确认才出去 */
        enter_manual();
        g_tcs.tc1_act = 49.0f; tick();
        CHECK(control_state() == CTRL_SAFE && control_safety_latched() == SAFETY_BIT_OVERTEMP,
              "L04 overtemp row: precondition SAFE (OVERTEMP)");
        ll_begin(&g);
        link_lost_run(&g, 5000u, 1500u, heal_water);
        ll_print("超温", &g);
        CHECK(g_ll_estops == 0 && g.tripped == 1, "L04 overtemp row: link loss in an overtemp SAFE does not trigger again");
        CHECK(control_safety_latched() == SAFETY_BIT_OVERTEMP, "L04 overtemp row: no estop bit added");
        CHECK(control_state() == CTRL_SAFE && control_tec_cmd(1) == 0 && control_tec_cmd(2) == 0,
              "L04 overtemp row: still SAFE, both TEC off");
        log_reset();
        link_alive_run(&g, 10000u, heal_water);
        CHECK(control_state() == CTRL_SAFE && control_safety_latched() == SAFETY_BIT_OVERTEMP,
              "L04 overtemp row: cooled down + link back -> still SAFE until acknowledged");
        CHECK(count_sw_on() == 0, "L04 overtemp row: no TEC on while waiting");
        tcs_alarm_ack(); tick100();
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0 && count_sw_on() == 0,
              "L04 overtemp row: acknowledged -> IDLE, nothing restarted");

        /* ③ 模块失联（会自愈的安全态）：屏断链 3 秒 → 急停；模块回来也不自己恢复输出 */
        enter_auto_heat();
        s_now += 5000u;                            /* 越过 5 s 上电宽限 */
        g_tcs.m207_online = 0; tick100();
        CHECK(control_state() == CTRL_SAFE && control_safety_latched() == SAFETY_BIT_M207_LOST,
              "L04 m207 row: precondition SAFE (M207_LOST, no estop)");
        ll_begin(&g);
        link_lost_run(&g, 5000u, 1500u, heal_m207);
        ll_print("模块失联", &g);
        lat = control_safety_latched();
        CHECK(g_ll_estops == 1 && g.tripped == 1, "L04 m207 row: link lost 3 s -> estop tripped exactly once");
        CHECK((lat & SAFETY_BIT_ESTOP) != 0, "L04 m207 row: estop bit in the latch");
        CHECK(control_state() == CTRL_SAFE && control_tec_cmd(1) == 0 && control_tec_cmd(2) == 0,
              "L04 m207 row: still SAFE, both TEC off (module back at 1.5 s, no self-restart)");
        CHECK(g_tcs.mode == 0, "L04 m207 row: estop forces manual");
        log_reset();
        link_alive_run(&g, 10000u, heal_m207);
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_ESTOP) != 0 && count_sw_on() == 0,
              "L04 m207 row: link back + fault gone -> stays SAFE until acknowledged, no TEC on");
        tcs_alarm_ack(); tick100();
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0 && count_sw_on() == 0 && g_tcs.mode == 0,
              "L04 m207 row: acknowledged -> IDLE in manual mode, nothing restarted");

        /* ④ 体温失联（会自愈）：同上 */
        enter_auto_heat();
        g_tcs.body_sensor_ok = 0; tick100();
        CHECK(control_state() == CTRL_SAFE && control_safety_latched() == SAFETY_BIT_BODY_LOST,
              "L04 body row: precondition SAFE (BODY_LOST, no estop)");
        ll_begin(&g);
        link_lost_run(&g, 5000u, 1500u, heal_body);
        ll_print("体温失联", &g);
        lat = control_safety_latched();
        CHECK(g_ll_estops == 1 && g.tripped == 1, "L04 body row: link lost 3 s -> estop tripped exactly once");
        CHECK((lat & SAFETY_BIT_ESTOP) != 0, "L04 body row: estop bit in the latch");
        CHECK(control_state() == CTRL_SAFE && control_tec_cmd(1) == 0 && control_tec_cmd(2) == 0,
              "L04 body row: still SAFE, both TEC off (probe back at 1.5 s, no self-restart)");
        log_reset();
        link_alive_run(&g, 10000u, heal_body);
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_ESTOP) != 0 && count_sw_on() == 0,
              "L04 body row: link back + fault gone -> stays SAFE until acknowledged, no TEC on");
        tcs_alarm_ack(); tick100();
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0 && count_sw_on() == 0 && g_tcs.mode == 0,
              "L04 body row: acknowledged -> IDLE in manual mode, nothing restarted");

        /* ⑤ 读数卡死（会自愈）：体温冻住、TEC 开满 30 秒进的安全态；同上 */
        enter_auto_heat();
        for (int i = 0; i < 301; i++) { g_tcs.tc1_act = (i & 1) ? 24.99f : 25.01f; tick100(); }
        CHECK(control_state() == CTRL_SAFE && control_safety_latched() == SAFETY_BIT_STUCK,
              "L04 stuck row: precondition SAFE (STUCK, no estop)");
        ll_begin(&g);
        link_lost_run(&g, 5000u, 1500u, heal_body);
        ll_print("读数卡死", &g);
        lat = control_safety_latched();
        CHECK(g_ll_estops == 1 && g.tripped == 1, "L04 stuck row: link lost 3 s -> estop tripped exactly once");
        CHECK((lat & SAFETY_BIT_ESTOP) != 0, "L04 stuck row: estop bit in the latch");
        CHECK(control_state() == CTRL_SAFE && control_tec_cmd(1) == 0 && control_tec_cmd(2) == 0,
              "L04 stuck row: still SAFE, both TEC off (reading live again at 1.5 s, no self-restart)");
        log_reset();
        link_alive_run(&g, 10000u, heal_body);
        CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_ESTOP) != 0 && count_sw_on() == 0,
              "L04 stuck row: link back + reading live -> stays SAFE until acknowledged, no TEC on");
        tcs_alarm_ack(); tick100();
        CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0 && count_sw_on() == 0 && g_tcs.mode == 0,
              "L04 stuck row: acknowledged -> IDLE in manual mode, nothing restarted");
    }

    /* ====== M13–M15 水温卡死在手动模式下的口径（2026-11-26，合并 e3a 之后加的）======
     * 合并之后这里当场变红：e3a 的水温卡死按 30 秒判，而手动模式是把水稳在一个固定目标上的，
     * 读数 30 秒不变是常态（真机实测 F2：健康读数逐位不变 30–53 秒是常事）。现在水温窗口是 10 分钟。*/
    /* M13 手动制热、水温冻在 25°C：10 分钟内不判（制热照常），满 10 分钟进安全态。*/
    enter_manual();
    g_tcs.tc1_act = 25.0f; tick();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 2.0f, PARAM_SRC_LINK);   /* 制热 */
    tick();
    CHECK(control_tec_dir(1) == TEC_DIR_HEAT && g_tcs.tec1_on == 1, "M13 precondition: manual heat on");
    for (int i = 0; i < 1160; i++) tick();                           /* 累计 580 秒 */
    CHECK(control_state() == CTRL_IDLE && (control_safety_latched() & SAFETY_BIT_STUCK) == 0 && g_tcs.tec1_on == 1,
          "M13 frozen water: no trip before 10 min (manual heat keeps running)");
    for (int i = 0; i < 60; i++) tick();                              /* 累计 610 秒 */
    CHECK(control_state() == CTRL_SAFE && (control_safety_latched() & SAFETY_BIT_STUCK) != 0,
          "M13 frozen water for 10 min + heat -> SAFE");
    CHECK(g_tcs.tec1_on == 0, "M13 TEC1 off");
    /* M14 手动制冷、水温冻住：30 秒不判（制冷能力不足时水会停在稳态上）。*/
    enter_manual();
    g_tcs.tc1_act = 25.0f; tick();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 1.0f, PARAM_SRC_LINK);   /* 制冷 */
    tick();
    CHECK(control_tec_dir(1) == TEC_DIR_COOL && g_tcs.tec1_on == 1, "M14 precondition: manual cool on");
    for (int i = 0; i < 400; i++) tick();                             /* 200 秒 */
    CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0 && g_tcs.tec1_on == 1,
          "M14 frozen water while cooling: no trip at 30 s (nor at 200 s)");
    /* M15 手动制热、水温冻在 42°C（已到目标附近）：30 秒不判。*/
    enter_manual();
    g_tcs.tc1_act = 42.0f; tick();
    (void)param_set(PARAM_ID_TEC1_MANUAL, 2.0f, PARAM_SRC_LINK);
    tick();
    CHECK(control_tec_dir(1) == TEC_DIR_HEAT && g_tcs.tec1_on == 1, "M15 precondition: manual heat on at 42.0");
    for (int i = 0; i < 400; i++) tick();
    CHECK(control_state() == CTRL_IDLE && control_safety_latched() == 0 && g_tcs.tec1_on == 1,
          "M15 frozen water near the heat target: no trip at 30 s (nor at 200 s)");

    printf("test_control_manual: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
