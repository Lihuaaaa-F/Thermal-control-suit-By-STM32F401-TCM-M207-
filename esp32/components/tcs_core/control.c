/**
 * control.c — 联动控制状态机实现（任务书 §3.6）
 *
 * 自动模式迁移表：
 *   IDLE : body > body_hot+hyst → TC1 设 15.0 + TCSW=1 + 风扇开 → COOL
 *   IDLE : body < body_cold−hyst → TC1 设 45.0 + TCSW=1 + 风扇开 → HEAT
 *   COOL : body < body_hot−hyst → TCSW=0 + 风扇关 → IDLE
 *   HEAT : body > body_cold+hyst → TCSW=0 + 风扇关 → IDLE
 *   任意 : 传感器丢失 / M207 失联 → SAFE（TCSW=0 + 风扇开 + ALM）
 *   SAFE : 双恢复 + 人工确认 → 清报警 → IDLE
 * 滞回算例（默认参数 37.5/36.5/0.2）：>37.7 进制冷、<37.3 退制冷；
 *   <36.3 进制热、>36.7 退制热（单测向量 ±0.001）。
 */
#include "control.h"
#include "param_registry.h"
#include "post.h"

/* 安全锁存并发：ESP32 上是跨任务（control/link/httpd/console）读改写，需临界区；
 * PC 单测是单线程，锁退化为空——保持 control.c 零 HAL/零 FreeRTOS 依赖（既有测试架构）。*/
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"      /* N-04：落盘串行化用 mutex */
static portMUX_TYPE s_safety_mux = portMUX_INITIALIZER_UNLOCKED;
#define SAFETY_LOCK()   taskENTER_CRITICAL(&s_safety_mux)
#define SAFETY_UNLOCK() taskEXIT_CRITICAL(&s_safety_mux)
#else
#define SAFETY_LOCK()   ((void)0)
#define SAFETY_UNLOCK() ((void)0)
#endif

static control_ops_t s_ops;
static ctrl_state_t  s_state = CTRL_IDLE;
static uint32_t      s_last_eval;
static uint8_t       s_boot_ms_base_set;
static uint32_t      s_boot_ms;
static volatile uint8_t s_estop_latch;   /* 仅作急停显示/语义；硬门控见 s_safety_latch（N-05：跨任务写，加 volatile）*/

/* 安全锁存：位含义见 control.h。上电默认"源全恢复"，由 control_poll 每拍刷新。
 * volatile：写侧在临界区，读侧（执行器门控/ack/读 API）裸读，必须每次重取（R1 复核 R-09）。*/
static volatile uint8_t s_safety_latch;
static uint8_t s_safety_ok = 0xFF;

/* 传感器类源（除急停外）的集合：ack 清急停要求这三者全部恢复 */
#define SAFETY_SENSOR_MASK (SAFETY_BIT_OVERTEMP | SAFETY_BIT_BODY_LOST | SAFETY_BIT_M207_LOST)

/* N-04：落盘串行化。trip 与 ack 可能在不同任务并发，直接各自写 NVS 会用旧值覆盖新值。
 * 这里用一把 mutex 串行化"读最新 latch + 写盘"，且**每次写当前值**，所以最后一次写入
 * 必然是最新状态。临界区内不做 NVS（阻塞），只在读 latch 时短暂进临界区。 */
#ifdef ESP_PLATFORM
static SemaphoreHandle_t s_persist_mtx;

static void safety_persist_now(void)
{
    uint8_t v;
    if (!s_ops.safety_persist) return;
    if (s_persist_mtx && xSemaphoreTake(s_persist_mtx, portMAX_DELAY) != pdTRUE) return;
    SAFETY_LOCK();
    v = s_safety_latch;
    SAFETY_UNLOCK();
    s_ops.safety_persist(v);
    if (s_persist_mtx) xSemaphoreGive(s_persist_mtx);
}
#else
static void safety_persist_now(void)
{
    uint8_t v;
    if (!s_ops.safety_persist) return;
    SAFETY_LOCK();
    v = s_safety_latch;
    SAFETY_UNLOCK();
    s_ops.safety_persist(v);
}
#endif

/* ====== 安全锁存原语（R1：P0-01/P0-04/P0-08）====== */
static void safety_trip(uint8_t bit)
{
    uint8_t changed;
    SAFETY_LOCK();
    changed = (uint8_t)((s_safety_latch & bit) == 0);
    s_safety_latch |= bit;
    SAFETY_UNLOCK();
    if (changed) safety_persist_now();   /* 锁外取最新值落盘（N-04） */
}

static void safety_set_ok(uint8_t bit, uint8_t ok)
{
    SAFETY_LOCK();
    if (ok) s_safety_ok |= bit;
    else    s_safety_ok &= (uint8_t)~bit;
    SAFETY_UNLOCK();
}

/* 人工确认：只清"源已恢复"的位；急停额外要求其余源全部恢复且无其它锁存。*/
static void safety_ack_latched(void)
{
    uint8_t lat, ok, drop, after;
    SAFETY_LOCK();
    lat = s_safety_latch;
    ok  = s_safety_ok;
    drop = (uint8_t)(lat & ok & (uint8_t)~SAFETY_BIT_ESTOP);
    after = (uint8_t)(lat & (uint8_t)~drop);   /* R-07：用"清位后"判断急停，避免两次 ack */
    if ((after & (uint8_t)~SAFETY_BIT_ESTOP) == 0
        && (ok & SAFETY_SENSOR_MASK) == SAFETY_SENSOR_MASK) {
        drop |= (uint8_t)(after & SAFETY_BIT_ESTOP);
    }
    s_safety_latch = (uint8_t)(lat & (uint8_t)~drop);
    SAFETY_UNLOCK();
    if (drop) safety_persist_now();      /* N-04：写当前值，不会用旧快照覆盖并发 trip */
}

/* ====== 内部输出封装（同步 g_tcs 快照 + 钩子）====== */
static void out_fan(uint8_t on)
{
    g_tcs.fan_on = on ? 1u : 0u;
    if (s_ops.fan_set) s_ops.fan_set(on);
}

static void out_tec_sw(uint8_t ch, uint8_t on)
{
    /* 安全门控的唯一收敛点：锁存期间任何"开"都压成 0（P0-04 的在途状态机覆写在此失效）。
     * 单字节读在 32 位 MCU 上原子；最坏情况是晚一拍看到 trip，而 trip 由 poll 同拍处理。 */
    if (on && s_safety_latch) on = 0;
    if (ch == 1) g_tcs.tec1_on = on ? 1u : 0u;
    else         g_tcs.tec2_on = on ? 1u : 0u;
    if (s_ops.tec_sw) s_ops.tec_sw(ch, on);
}

static void out_tec_temp(uint8_t ch, float v)
{
    if (s_ops.tec_temp) s_ops.tec_temp(ch, v);
}

static void goto_state(ctrl_state_t st)
{
    if (s_state == st) return;
    s_state = st;
    if (s_ops.on_state) s_ops.on_state(st);
}

void control_init(const control_ops_t *ops)
{
    if (ops) s_ops = *ops;
    s_state = CTRL_IDLE;
    s_last_eval = 0;
    s_boot_ms_base_set = 0;
    s_estop_latch = 0;
    g_tcs.alarm_bits = 0;
    /* N-04：落盘串行化用 mutex（只在首次 init 时创建；失败则退化为不加锁）*/
#ifdef ESP_PLATFORM
    if (!s_persist_mtx) s_persist_mtx = xSemaphoreCreateMutex();
#endif
    /* P0-08：先读回持久化锁存，再据此同步 ALM 显示位——复位不得解除安全态。
     * R-05：0xFF = 读回失败/坏记录（"未知"），保守处理为急停锁存 + SAFE，等人工确认；
     *       只有 NVS 明确 NOT_FOUND（首次上电）才返回 0。*/
    {
        uint8_t lat = s_ops.safety_load ? s_ops.safety_load() : 0;
        if (lat == 0xFF) {
            lat = SAFETY_BIT_ESTOP;    /* 未知即按最坏情况处理 */
        }
        s_safety_latch = lat;
    }
    s_safety_ok = 0xFF;
    if (s_safety_latch & SAFETY_BIT_ESTOP)      { s_estop_latch = 1; g_tcs.alarm_bits |= ALM_ESTOP; }
    if (s_safety_latch & SAFETY_BIT_OVERTEMP)   g_tcs.alarm_bits |= ALM_OVERTEMP;
    if (s_safety_latch & SAFETY_BIT_BODY_LOST)  g_tcs.alarm_bits |= ALM_BODY_SENSOR;
    if (s_safety_latch & SAFETY_BIT_M207_LOST)  g_tcs.alarm_bits |= ALM_M207;
    if (s_safety_latch) s_state = CTRL_SAFE;   /* 绕过 on_state：ops 可能尚未装配回调 */
}

void tcs_alarm_raise(uint16_t bit)
{
    g_tcs.alarm_bits |= bit;
}

uint8_t tcs_alarm_active(uint16_t bit)
{
    return (g_tcs.alarm_bits & bit) ? 1u : 0u;
}

/* ====== 安全态进入/退出 ====== */
static void enter_safe(void)
{
    out_tec_sw(1, 0);
    out_tec_sw(2, 0);
    out_fan(1);                       /* 安全态风扇全开 */
    goto_state(CTRL_SAFE);
}

static uint8_t safe_sources_recovered(void)
{
    if (!g_tcs.body_sensor_ok) return 0;
    if (!g_tcs.m207_online) return 0;
    if (g_tcs.tc1_act > ALM_OVERTEMP_HARD_C) return 0;
    return 1;
}

void tcs_alarm_ack(void)
{
    uint8_t lat;
    /* 先按"当前"源状态刷新（不依赖上一拍缓存）：操作员降温后立即 ack 也应生效。*/
    safety_set_ok(SAFETY_BIT_OVERTEMP, (uint8_t)(g_tcs.tc1_act <= ALM_OVERTEMP_HARD_C));
    safety_set_ok(SAFETY_BIT_BODY_LOST, g_tcs.body_sensor_ok);
    safety_set_ok(SAFETY_BIT_M207_LOST, (uint8_t)(g_tcs.m207_online && !g_tcs.m207_sensor_open));
    /* P0：再按"源已恢复"清安全锁存；ALM 显示位跟随锁存，不能比锁存更早消失。*/
    safety_ack_latched();
    /* N-05：一次性取锁存快照，后续显示位判断都用它——避免"锁外读-判-清"与并发 trip 交错，
     * 把刚置上的 ALM 位清掉。*/
    SAFETY_LOCK();
    lat = s_safety_latch;
    SAFETY_UNLOCK();
    if (!(lat & SAFETY_BIT_BODY_LOST) && g_tcs.body_sensor_ok)
        g_tcs.alarm_bits &= (uint16_t)~ALM_BODY_SENSOR;
    if (!(lat & SAFETY_BIT_M207_LOST) && g_tcs.m207_online && !g_tcs.m207_sensor_open)
        g_tcs.alarm_bits &= (uint16_t)~ALM_M207;
    if (!(lat & SAFETY_BIT_OVERTEMP) && g_tcs.tc1_act <= ALM_OVERTEMP_HARD_C)
        g_tcs.alarm_bits &= (uint16_t)~ALM_OVERTEMP;
    if (!(lat & SAFETY_BIT_ESTOP)) {
        g_tcs.alarm_bits &= (uint16_t)~ALM_ESTOP;
        s_estop_latch = 0;
    }
    g_tcs.alarm_bits &= (uint16_t)~ALM_EEPROM;   /* EEPROM 报警仅提示性 */
}

void tcs_emergency_stop(void)
{
    s_estop_latch = 1;
    safety_trip(SAFETY_BIT_ESTOP);          /* P0-04：置锁存后，在途状态机的"开"被 out_tec_sw 压成 0 */
    tcs_alarm_raise(ALM_ESTOP);
    /* mode=手动（经 param_set 同一数据源路径，自动广播 0x11）*/
    (void)param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_CONSOLE);
    out_tec_sw(1, 0);
    out_tec_sw(2, 0);
    out_fan(1);
    goto_state(CTRL_SAFE);
}

/* ====== 手动直控 ====== */
void control_manual_tec(uint8_t ch, uint8_t on)
{
    if (s_state == CTRL_SAFE) return;             /* 安全态优先于手动 */
    out_tec_sw(ch, on);
}

void control_manual_fan(uint8_t on)
{
    if (s_state == CTRL_SAFE) return;             /* SAFE 期风扇锁全开 */
    out_fan(on);
}

void control_on_mode_change(uint8_t m)
{
    (void)m;   /* 手动进出不强制改输出；自动评估在下一拍 control_poll 接管 */
}

/* H6 动态布防：FMEA 判据由 post 三态机裁决（0x08=0 auto/1 强制布防/2 强制不布防；
 * UNARMED_ABSENT/ARMED_ONLINE/LOST_ALARM 见 post.h）。判据语义不变：
 * 未布防：传感器类异常只记状态沿，不上横幅不进 SAFE；布防：完整 FMEA。
 * ALM_OVERTEMP 与急停不受布防影响（安全红线不变）。 */
static uint8_t commissioned(void)
{
    return post_armed();
}

/* ====== 500ms 评估 ====== */
void control_poll(uint32_t now_ms)
{
    if (!s_boot_ms_base_set) {
        s_boot_ms = now_ms;
        s_boot_ms_base_set = 1;
    }

    /* R1 P1-04：参数落盘连续失败 → 可见报警（提示性，ack 可清）*/
    if (param_persist_fail_count() > 0) tcs_alarm_raise(ALM_EEPROM);

    /* 过热硬切断（任意模式、任意布防态）：先锁存，再由锁存统一门控执行器。
     * 修复 P0-01：原实现只 tcs_alarm_raise，TEC 继续按状态机输出（UI 却写"已停止加热"）。*/
    if (g_tcs.tc1_act > ALM_OVERTEMP_HARD_C) {
        tcs_alarm_raise(ALM_OVERTEMP);
        safety_trip(SAFETY_BIT_OVERTEMP);
        safety_set_ok(SAFETY_BIT_OVERTEMP, 0);
    } else {
        safety_set_ok(SAFETY_BIT_OVERTEMP, 1);
    }

    /* 锁存期间：任何模式都不再评估，直接停在 SAFE，并每拍重申 TCSW=0（在线时 500ms 一次）。
     * R-18：M207 离线时 m207_tec 直接返回 -2（不入队），本处重发并不产生帧；
     * 离线期的兜底是复联重同步（m207_go_online 里补发 TCSW=0）。P1-09 的彻底修法
     * （m207_tec_force 强制归零通道）仍在待办，勿据本注释认为离线已被覆盖。*/
    if (s_safety_latch) {
        enter_safe();
        return;
    }

    /* F1/F2：传感器丢失 / M207 失联 → SAFE（含手动模式；M207 有 5s 上电宽限）
     * WP-A.5 commissioned：未布防（默认）→ 只记状态沿（上位机可查询），不置报警位
     * 不进 SAFE；布防后同一异常立即进完整 FMEA。 */
    /* M12-F-01：通道传感器断线（模块报 errcode=2、参数族隐藏）时 M207 仍"在线"，
     * tc1_act 只是冻结值——过温判据对它完全失效。按"温度源丢失"处理，与失联同级。*/
    uint8_t m207_src_lost = (uint8_t)((!g_tcs.m207_online) || g_tcs.m207_sensor_open);
    uint8_t m207_fault = (uint8_t)(m207_src_lost && (now_ms - s_boot_ms >= CTRL_BOOT_GRACE_MS));
    uint8_t sensor_fault = (!g_tcs.body_sensor_ok) || m207_fault;
    static uint8_t s_unarmed_fault_prev;
    if (sensor_fault && !commissioned()) {
        if (!s_unarmed_fault_prev) {
            s_unarmed_fault_prev = 1;   /* 沿标记（PC 单测/统计可见；日志由各传感器驱动侧打）*/
        }
    } else {
        s_unarmed_fault_prev = 0;
    }
    if (sensor_fault && commissioned()) {
        if (!g_tcs.body_sensor_ok) { tcs_alarm_raise(ALM_BODY_SENSOR); safety_trip(SAFETY_BIT_BODY_LOST); }
        if (m207_fault)            { tcs_alarm_raise(ALM_M207);        safety_trip(SAFETY_BIT_M207_LOST); }
        enter_safe();
        return;
    }
    /* 源恢复位：供 ack 判定"能否清锁存"（未布防时也维护，避免急停 ack 被误阻）*/
    safety_set_ok(SAFETY_BIT_BODY_LOST, g_tcs.body_sensor_ok);
    safety_set_ok(SAFETY_BIT_M207_LOST, (uint8_t)(g_tcs.m207_online && !g_tcs.m207_sensor_open));

    if (s_state == CTRL_SAFE) {
        /* SAFE 退出：双恢复 + 人工确认（急停自持需 ack）*/
        if (safe_sources_recovered() && !s_estop_latch && tcs_alarm_active(ALM_BODY_SENSOR) == 0
            && tcs_alarm_active(ALM_M207) == 0 && tcs_alarm_active(ALM_ESTOP) == 0) {
            out_fan(0);
            goto_state(CTRL_IDLE);
        }
        return;
    }

    /* 手动模式：状态机旁路（输出保持人工值）*/
    if (g_tcs.mode == 0) {
        goto_state(CTRL_IDLE);
        return;
    }

    if (now_ms - s_last_eval < CTRL_EVAL_MS) return;
    /* R1 修复 M09-F-04：固定节拍累加（原来直接赋 now_ms，499ms 早醒会跳掉一整拍 → 实际 1000ms）*/
    s_last_eval += CTRL_EVAL_MS;
    if (now_ms - s_last_eval >= CTRL_EVAL_MS) s_last_eval = now_ms;   /* 长时间停摆后重新对齐 */

    float body  = g_tcs.body_temp;
    float hot   = param_get(PARAM_ID_BODY_HOT);
    float cold  = param_get(PARAM_ID_BODY_COLD);
    float hyst  = param_get(PARAM_ID_HYST);

    switch (s_state) {
    case CTRL_IDLE:
        if (body > hot + hyst) {
            out_tec_temp(1, TEC_COOL_TARGET_C);
            out_tec_sw(1, 1);
            out_fan(1);
            goto_state(CTRL_COOL);
        } else if (body < cold - hyst) {
            out_tec_temp(1, TEC_HEAT_TARGET_C);
            out_tec_sw(1, 1);
            out_fan(1);
            goto_state(CTRL_HEAT);
        }
        break;
    case CTRL_COOL:
        if (body < hot - hyst) {
            out_tec_sw(1, 0);
            out_fan(0);
            goto_state(CTRL_IDLE);
        }
        break;
    case CTRL_HEAT:
        if (body > cold + hyst) {
            out_tec_sw(1, 0);
            out_fan(0);
            goto_state(CTRL_IDLE);
        }
        break;
    default:
        s_state = CTRL_IDLE;
        break;
    }
}

ctrl_state_t control_state(void) { return s_state; }

/* ====== 安全锁存/急停投递读侧（console/网页/诊断）====== */
uint8_t control_safety_latched(void) { return s_safety_latch; }
void    control_safety_trip(uint8_t bit) { safety_trip(bit); }

#ifdef TCS_UNIT_TEST
void control_test_trip(uint8_t bit) { safety_trip(bit); }
#endif
