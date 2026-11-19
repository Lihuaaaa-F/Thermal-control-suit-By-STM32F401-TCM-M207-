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

static control_ops_t s_ops;
static ctrl_state_t  s_state = CTRL_IDLE;
static uint32_t      s_last_eval;
static uint8_t       s_boot_ms_base_set;
static uint32_t      s_boot_ms;
static uint8_t       s_estop_latch;
static uint8_t       s_overtemp_latch;  /* R1 复核 R-03：本地超温自持（不被快照覆盖）*/
static uint8_t       s_estop_tx_pending;   /* R-04：急停意图待对端确认（ack 不清）*/
static uint8_t       s_overtemp_tx_pending;/* N-01：超温停机请求待对端确认（与急停分离）*/
static control_cmd_hook_t s_cmd_hook;   /* v2：急停/确认 → 链路 CMD（main 装配）*/

void control_set_cmd_hook(control_cmd_hook_t cb) { s_cmd_hook = cb; }
uint8_t control_estop_latched(void) { return s_estop_latch; }
uint8_t control_overtemp_latched(void) { return s_overtemp_latch; }
uint8_t control_estop_tx_pending(void) { return s_estop_tx_pending; }
void    control_estop_tx_confirm(void) { s_estop_tx_pending = 0; }
uint8_t control_overtemp_tx_pending(void) { return s_overtemp_tx_pending; }
void    control_overtemp_tx_confirm(void) { s_overtemp_tx_pending = 0; }

/* 清急停自持（本地 ack 或对端 ack 生效时调用）。不清"待投递"标志——投递确认只看对端。*/
void control_estop_clear(void)
{
    s_estop_latch = 0;
    g_tcs.alarm_bits &= (uint16_t)~ALM_ESTOP;
}

#if TCS_USE_LVGL
/* G5：60 点×30s 温度环形（P2 曲线数据源；UI 只读） */
#include <string.h>
static float s_hist_tc1[60], s_hist_tc2[60];
static uint32_t s_hist_head, s_hist_n, s_hist_last;
void tcs_hist_tick(uint32_t now_ms)
{
    if (s_hist_last && now_ms - s_hist_last < 30000u) return;
    s_hist_last = now_ms ? now_ms : 1u;
    s_hist_tc1[s_hist_head] = g_tcs.tc1_act;
    s_hist_tc2[s_hist_head] = g_tcs.tc2_act;
    s_hist_head = (s_hist_head + 1u) % 60u;
    if (s_hist_n < 60u) s_hist_n++;
}
void tcs_hist_snapshot(float out1[60], float out2[60], uint32_t * n)
{
    uint32_t cnt = s_hist_n > 60u ? 60u : s_hist_n;
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t idx = (s_hist_head - cnt + i + 60u) % 60u;
        out1[i] = s_hist_tc1[idx];
        out2[i] = s_hist_tc2[idx];
    }
    *n = cnt;
}
#endif

/* ====== 内部输出封装（同步 g_tcs 快照 + 钩子）====== */
static void out_fan(uint8_t on)
{
    g_tcs.fan_on = on ? 1u : 0u;
    if (s_ops.fan_set) s_ops.fan_set(on);
}

static void out_tec_sw(uint8_t ch, uint8_t on)
{
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
    /* N-06：新增的两个静态量也要复位——否则软重初始化/测试复用会带着旧锁存与旧投递意图 */
    s_overtemp_latch = 0;
    s_estop_tx_pending = 0;
    s_overtemp_tx_pending = 0;
    g_tcs.alarm_bits = 0;
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
    /* 报警源恢复 + 人工确认才清位（§6.2）*/
    if (g_tcs.body_sensor_ok) g_tcs.alarm_bits &= (uint16_t)~ALM_BODY_SENSOR;
    if (g_tcs.m207_online)    g_tcs.alarm_bits &= (uint16_t)~ALM_M207;
    if (g_tcs.tc1_act <= ALM_OVERTEMP_HARD_C) {
        g_tcs.alarm_bits &= (uint16_t)~ALM_OVERTEMP;
        s_overtemp_latch = 0;            /* R-03：只有 ack 且温度已回落才清本地超温自持 */
        s_overtemp_tx_pending = 0;       /* N-01：隐患已被确认，未送达的停机请求作废 */
    }
    control_estop_clear();
    /* N-03：非急停报警（BODY/M207/OVERTEMP）的屏 ack 也必须通知对端，
     * 否则 ESP32 侧锁存永远清不掉（屏是主操作载体）。
     * 只在"本地已无急停自持"时发：避免本地急停未清却把对端急停清掉；
     * 对端 tcs_alarm_ack 会复核它自己的源是否恢复，所以**不要**在这里用
     * 本地 SAFE/源恢复做前置门——2026-11-19 实测：断链后本地 g_tcs 仍是旧快照，
     * 点横幅会因此静默不发 cmd 9，表现为"屏幕确认了但设备不恢复"。*/
    if (!s_estop_latch && s_cmd_hook) s_cmd_hook(9);
    g_tcs.alarm_bits &= (uint16_t)~ALM_EEPROM;   /* EEPROM 报警仅提示性 */
}

void tcs_emergency_stop(void)
{
#if TCS_USE_LVGL
    /* E-04/TL-9b 取证计数（=1 构建才有；=0 产物保持字节恒等，TL-17） */
    extern volatile uint32_t g_estop_calls;
    g_estop_calls++;
#endif
    /* R1 复核 R-15：本地动作每次都做（第二/第三次按键仍要复位输出与模式），
     * 只有"给对端发 cmd 8"做幂等——重发由主循环按 200ms 负责。*/
    uint8_t already = s_estop_latch;
    s_estop_latch = 1;
    s_estop_tx_pending = 1;   /* R-04：意图待投递；ack 不清，只有对端确认才清 */
    tcs_alarm_raise(ALM_ESTOP);
    /* mode=手动（经 param_set 同一数据源路径，自动广播 0x11）*/
    (void)param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_CONSOLE);
    out_tec_sw(1, 0);
    out_tec_sw(2, 0);
    out_fan(1);
    goto_state(CTRL_SAFE);
    /* v2：TEC/风扇在 ESP32，本地动作无物理效果——必须下发 0x15 cmd=8。
     * R-15：只有"首次"发帧；重发由主循环按 200ms 负责（本地动作上面每次都做了）。*/
    if (!already && s_cmd_hook) s_cmd_hook(8);
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

void control_fan_toggle(void)
{
#if TCS_H743_LINK_ONLY
    /* v2：执行端在 ESP32。原来这里调 control_manual_fan()——本机没有 M207，
     * 只改 g_tcs 显示，200ms 后被快照打回，表现为「按了没反应/又跳回去」。*/
    if (s_cmd_hook) s_cmd_hook(TCS_CMD_FAN_TOGGLE);
#else
    control_manual_fan(g_tcs.fan_on ? 0 : 1);
#endif
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
#if TCS_USE_LVGL
    tcs_hist_tick(now_ms);
#endif
    if (!s_boot_ms_base_set) {
        s_boot_ms = now_ms;
        s_boot_ms_base_set = 1;
    }

    /* R1 P1-04：参数落盘连续失败 → 可见报警（提示性，ack 可清）*/
    if (param_persist_fail_count() > 0) tcs_alarm_raise(ALM_EEPROM);

    /* 过热硬切断（R1 修复 P0-01）：任意模式、任意布防态。
     * R-03：置本地自持位（不被 snap_reassert 覆盖），只有 ack 且温度回落才清。
     * N-01：仍然下发一次 cmd 8 —— 这是"停止请求"（不是驱动执行器），是跨端兜底：
     * 若 ESP32 的 control 任务创建失败/卡死，H743 的这条请求仍能让对端执行急停。
     * 只在首次进入 SAFE 时发，重发由主循环的 pending 块负责。*/
    if (g_tcs.tc1_act > ALM_OVERTEMP_HARD_C) {
        tcs_alarm_raise(ALM_OVERTEMP);
        s_overtemp_latch = 1;
        if (s_state != CTRL_SAFE) {
            out_tec_sw(1, 0);
            out_tec_sw(2, 0);
            out_fan(1);
            goto_state(CTRL_SAFE);
            s_overtemp_tx_pending = 1;    /* N-01：独立于急停的停机请求，纳入 200ms 重发 */
            if (s_cmd_hook) s_cmd_hook(TCS_CMD_OVERTEMP_STOP);
        }
        return;
    }

    /* F1/F2：传感器丢失 / M207 失联 → SAFE（含手动模式；M207 有 5s 上电宽限）
     * WP-A.5 commissioned：未布防（默认）→ 只记状态沿（上位机可查询），不置报警位
     * 不进 SAFE；布防后同一异常立即进完整 FMEA。 */
    uint8_t m207_fault = (!g_tcs.m207_online) && (now_ms - s_boot_ms >= CTRL_BOOT_GRACE_MS);
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
        if (!g_tcs.body_sensor_ok) tcs_alarm_raise(ALM_BODY_SENSOR);
        if (m207_fault)            tcs_alarm_raise(ALM_M207);
        enter_safe();
        return;
    }

    if (s_state == CTRL_SAFE) {
        /* SAFE 退出：双恢复 + 人工确认（急停自持需 ack）。
         * R1：补超温判据——用**本地自持位**而不是显示位（R-03：显示位每 10ms 被快照覆盖）。*/
        if (safe_sources_recovered() && !s_estop_latch && !s_overtemp_latch
            && tcs_alarm_active(ALM_BODY_SENSOR) == 0
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
    s_last_eval = now_ms;

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
