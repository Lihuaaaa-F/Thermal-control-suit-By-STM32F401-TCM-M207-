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
#include <string.h>
#include <stddef.h>

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
#include <math.h>
static float s_hist_tc1[60], s_hist_tc2[60];
static float s_hist_body[60];   /* m1（模式页，2026-11-26）：体温线，与两路水温同一套格子（30s × 60）*/
static uint32_t s_hist_head, s_hist_n, s_hist_last, s_hist_total;
void tcs_hist_tick(uint32_t now_ms)
{
    if (s_hist_last && now_ms - s_hist_last < 30000u) return;
    s_hist_last = now_ms ? now_ms : 1u;
    uint8_t live = 1u;
#if TCS_H743_LINK_ONLY
    /* PROD：屏上数据来自主控快照。链路不在（含上电第一拍、还没收到任何快照）时 g_tcs 里
     * 是零值/陈旧值，不能当成样本——这一格记 NaN，曲线在这里断开，不画一个假的 0°C。
     * 格子照常推进，所以缺口前后的点仍然各占 30s。
     * （2026-11-24 真机：曲线开头有一段 0°C。独立复核指出原先在这里直接 return，
     * 头指针不动，"留缺口"并不成立：缺口前后两个点被画成了相邻。）*/
    live = g_tcs.esp_link_ok;
#endif
    s_hist_tc1[s_hist_head] = live ? g_tcs.tc1_act : NAN;
    s_hist_tc2[s_hist_head] = live ? g_tcs.tc2_act : NAN;
    /* m1（模式页）：体温线。链路不在，或体温探头没接 / 失联（body_sensor_ok=0）的那一格记 NaN——
     * 图上在这里断开，不画成一个假的 0°C。*/
    s_hist_body[s_hist_head] = (live && g_tcs.body_sensor_ok) ? g_tcs.body_temp : NAN;
    s_hist_head = (s_hist_head + 1u) % 60u;
    if (s_hist_n < 60u) s_hist_n++;
    s_hist_total++;                       /* 开机以来的格子总数：只增，曲线页据此判断"来没来新点" */
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
/* m1（模式页）：体温那一串，线性序（旧→新，前 n 个有效）与 tcs_hist_snapshot 相同 */
void tcs_hist_snapshot_body(float out[60], uint32_t * n)
{
    uint32_t cnt = s_hist_n > 60u ? 60u : s_hist_n;
    for (uint32_t i = 0; i < cnt; i++)
        out[i] = s_hist_body[(s_hist_head - cnt + i + 60u) % 60u];
    *n = cnt;
}
uint32_t tcs_hist_count(void) { return s_hist_total; }
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

/* ====== 状态迁移环（S 线 i3，条目 9；e3a 第五节方案第 1 条）======
 * 最近 8 条迁移，落 .noinit。纯逻辑五个函数（control.h），目标机那一份是 g_trace_ring。
 * 判有效：magic + head/cnt 范围 + CRC32（覆盖 crc 之前的全部字节）——随机内容一律判无效。
 * 不做"上电清零"：环的意义正是让软复位之后还能看见复位前走过哪些状态。*/
static uint32_t trace_crc32(const void *p, size_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c ^= b[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1u)));
    }
    return ~c;
}

void tcs_trace_reset(tcs_trace_t *t)
{
    if (!t) return;
    memset(t, 0, sizeof *t);
    t->magic = TCS_TRACE_MAGIC;
    t->crc = trace_crc32(t, offsetof(tcs_trace_t, crc));
}

uint8_t tcs_trace_valid(const tcs_trace_t *t)
{
    if (!t || t->magic != TCS_TRACE_MAGIC) return 0;
    if (t->head >= TCS_TRACE_N || t->cnt > TCS_TRACE_N) return 0;
    return (uint8_t)(trace_crc32(t, offsetof(tcs_trace_t, crc)) == t->crc);
}

void tcs_trace_push(tcs_trace_t *t, const tcs_trace_ent_t *en)
{
    if (!t || !en) return;
    if (!tcs_trace_valid(t)) tcs_trace_reset(t);   /* 上次上电的垃圾 / 半截写 → 从头开始 */
    t->e[t->head] = *en;
    t->head = (uint16_t)((t->head + 1u) % TCS_TRACE_N);
    if (t->cnt < TCS_TRACE_N) t->cnt++;
    t->seq++;
    t->crc = trace_crc32(t, offsetof(tcs_trace_t, crc));
}

uint16_t tcs_trace_count(const tcs_trace_t *t)
{
    return tcs_trace_valid(t) ? t->cnt : 0u;
}

const tcs_trace_ent_t *tcs_trace_at(const tcs_trace_t *t, uint16_t i)
{
    if (!tcs_trace_valid(t) || i >= t->cnt) return NULL;
    return &t->e[(t->head + TCS_TRACE_N - t->cnt + i) % TCS_TRACE_N];   /* 0 = 最旧 */
}

/* 目标机那一份：.noinit（软复位后还在）。MSVC 主机单测没有 .noinit 段，用普通静态。*/
#if defined(__GNUC__)
tcs_trace_t g_trace_ring __attribute__((section(".noinit")));
#else
tcs_trace_t g_trace_ring;
#endif
const tcs_trace_t *control_trace(void) { return &g_trace_ring; }

static uint32_t s_trace_now;     /* 最近一拍的 now_ms（control_poll 里刷；急停落在两拍之间时用它）*/

static void trace_note(uint8_t from, uint8_t to, uint8_t ev)
{
    tcs_trace_ent_t en;
    en.ms = s_trace_now;
    en.alarm = g_tcs.alarm_bits;
    en.from = from;
    en.to = to;
    en.ev = ev;
    tcs_trace_push(&g_trace_ring, &en);
}

static void goto_state(ctrl_state_t st)
{
    if (s_state == st) return;
    {
        uint8_t from = (uint8_t)s_state;
        s_state = st;
        trace_note(from, (uint8_t)st, 0u);   /* S 线 i3（条目 9）：状态迁移环 */
    }
    if (s_ops.on_state) s_ops.on_state(st);
}

void control_init(const control_ops_t *ops)
{
    /* 先清空（每个 s_ops.* 调用点都判空，这里再兜一层：不传钩子也能安全运行）*/
    s_ops.fan_set = 0;
    s_ops.tec_sw = 0;
    s_ops.tec_temp = 0;
    s_ops.on_state = 0;
    if (ops) {
#if TCS_H743_LINK_ONLY
        /* 交付档（v2）：TEC / 风扇在主控侧，本机不驱动任何执行器。装配处传了什么本地
         * 执行器钩子都不接——原来交付档也接 fan_set，急停 / 超温兜底会经它把 PB14
         * （v1 接线里的风扇光耦，低=开）写低并一直保持到复位（P1-06 残余）。
         * 只保留 on_state 这种纯通知钩子。急停本身照旧：置本地自持与报警位、
         * 经 s_cmd_hook 把 cmd 8 发给主控。tests/test_prod_link_only.c 按交付档编译盯着这一条。*/
        s_ops.on_state = ops->on_state;
#else
        s_ops = *ops;
#endif
    }
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
    /* 只清本地自持；**不要**清 s_estop_tx_pending（R-04：急停意图必须投递到对端
     * 确认才算完成）。2026-11-19 我一度在这里清零，被既有单测 R-04 抓红——
     * 断链期间按急停、用户先 ack 的话，取消投递等于对端永远不执行本次急停。*/
    control_estop_clear();
    /* 通知对端（屏是主操作载体）：不再用"本地源是否恢复/本地是否 SAFE"做前置门——
     * 断链时本地 g_tcs 仍是旧快照，会把 cmd 9 静默吞掉（2026-11-19 实测）。
     * 对端 tcs_alarm_ack 会复核它自己的源（锁存位/传感器），源不健康时不会清。*/
    if (s_cmd_hook) s_cmd_hook(9);
    g_tcs.alarm_bits &= (uint16_t)~ALM_EEPROM;   /* EEPROM 报警仅提示性 */
}

void tcs_emergency_stop(void)
{
#if TCS_USE_LVGL && TCS_PROFILE >= 1
    /* E-04/TL-9b 取证计数（台架/仿真档才有；2026-11-24 S 线 C01 起交付档不定义这个钩子） */
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
    trace_note((uint8_t)s_state, (uint8_t)CTRL_SAFE, 1u);   /* S 线 i3（条目 9）：急停登记单独记一笔 */
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

/* 超温判据（2026-11-23 复核 A1）：>48°C 立即本地切断 + 请求对端停机。
 * 原来只在 control_poll 里，PROD 把 control_poll 停了 → 屏失去独立超温兜底
 * （只剩被动显示对端 alarm 位）。现在抽成函数：control_poll 用本地/快照值调用，
 * PROD 由主循环在 snap_reassert 之后用快照温度调用（只读快照，不依赖本地外设）。*/
void control_observe_temp(float tc1_act)
{
    if (!(tc1_act > ALM_OVERTEMP_HARD_C)) return;
    tcs_alarm_raise(ALM_OVERTEMP);
    s_overtemp_latch = 1;
    if (s_state == CTRL_SAFE) return;      /* 已切断：重发由 pending 块负责 */
    out_tec_sw(1, 0);
    out_tec_sw(2, 0);
    out_fan(1);
    goto_state(CTRL_SAFE);
    s_overtemp_tx_pending = 1;             /* N-01：独立于急停的停机请求，纳入 200ms 重发 */
    if (s_cmd_hook) s_cmd_hook(TCS_CMD_OVERTEMP_STOP);
}

/* ====== 500ms 评估 ====== */
void control_poll(uint32_t now_ms)
{
    s_trace_now = now_ms;                /* S 线 i3：迁移环的时间戳（急停落在两拍之间时用最近一拍）*/
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
        control_observe_temp(g_tcs.tc1_act);
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

#if TCS_H743_LINK_ONLY
void control_link_follow(void)
{
    /* PROD 不跑 control_poll：本机 SAFE 只由急停/超温兜底进入，原来没人把它切回去
     * （2026-11-23 现场：点横幅确认后设备已恢复，`stat` 仍显示 ctrl=3）。
     * 判据跟设备走——本机无自持/无待投递意图 + 快照无安全类报警 → IDLE；
     * 对端拒绝确认时快照仍带报警位，本机保持 SAFE。只动诊断态：v2 执行端在 ESP32，
     * g_tcs 归快照所有，这里不碰输出。*/
    if (s_state != CTRL_SAFE) return;
    if (s_estop_latch || s_overtemp_latch || s_estop_tx_pending || s_overtemp_tx_pending) return;
    if (g_tcs.alarm_bits & (ALM_ESTOP | ALM_OVERTEMP | ALM_BODY_SENSOR | ALM_M207)) return;
    goto_state(CTRL_IDLE);
}
#endif
