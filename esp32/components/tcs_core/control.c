/**
 * control.c — 联动控制状态机实现（任务书 §3.6）
 *
 * 自动模式迁移表：
 *   IDLE : body > body_hot+hyst → TC1 设 15.0 + TCSW=1 + 风扇开 → COOL
 *   IDLE : body < body_cold−hyst → TC1 设 45.0 + TCSW=1 + 风扇开 → HEAT
 *   COOL : body < body_hot−hyst → TCSW=0 + 风扇关 → IDLE
 *   HEAT : body > body_cold+hyst → TCSW=0 + 风扇关 → IDLE
 *   任意 : 传感器丢失 / M207 失联 → SAFE（TCSW=0 + 风扇开 + ALM）
 *   SAFE : 通信类故障（体温/M207/探头）源健康 2s → 自动退出；急停/超温 → 人工确认后退出
 *   退出 : 手动模式复原故障前的风扇；仅"体温传感器一个源、且是 2s 自愈退出"时复原手动 TEC2；
 *          自动模式不复原，输出当拍由状态机按体温重新定（见 leave_safe）
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
#include "esp_log.h"           /* 通信类自动恢复留痕（只此一处日志）*/
static portMUX_TYPE s_safety_mux = portMUX_INITIALIZER_UNLOCKED;
#define SAFETY_LOCK()   taskENTER_CRITICAL(&s_safety_mux)
#define SAFETY_UNLOCK() taskEXIT_CRITICAL(&s_safety_mux)
/* X03-F-02：报警位 bitmask 的 |= / &= 是读改写，control/m207/link/httpd 多任务并发，
 * 用独立 spinlock 收敛（不与 SAFETY_LOCK 嵌套：进入 ALM 段前不持有 SAFETY_LOCK）。*/
static portMUX_TYPE s_alm_mux = portMUX_INITIALIZER_UNLOCKED;
#define ALM_LOCK()      taskENTER_CRITICAL(&s_alm_mux)
#define ALM_UNLOCK()    taskEXIT_CRITICAL(&s_alm_mux)
#else
#define SAFETY_LOCK()   ((void)0)
#define SAFETY_UNLOCK() ((void)0)
#define ALM_LOCK()      ((void)0)
#define ALM_UNLOCK()    ((void)0)
#endif

static control_ops_t s_ops;
static ctrl_state_t  s_state = CTRL_IDLE;
static uint32_t      s_last_eval;
static uint8_t       s_boot_ms_base_set;
static uint32_t      s_boot_ms;
static volatile uint8_t s_estop_latch;   /* 仅作急停显示/语义；硬门控见 s_safety_latch（N-05：跨任务写，加 volatile）*/

/* SAFE 期间记忆故障前输出（语义见下方"安全态进入/退出"注释）*/
#define CTRL_SAFE_REASSERT_MS   500u   /* 锁存期间重申 TCSW=0 / 风扇全开的间隔（不随节拍变密）*/
static uint8_t  s_fan_before_safe;     /* 进 SAFE 前的风扇 */
static uint8_t  s_tec2_before_safe;    /* 进 SAFE 前的 TEC2 */
static uint8_t  s_auto_before_safe;    /* 进 SAFE 前是自动模式 */
static uint8_t  s_restore_tec2_ok;     /* 本轮 SAFE 只因体温传感器引起，可复原 TEC2 */
static uint8_t  s_exit_by_recover;     /* 本轮锁存是被"2s 自愈"清空的（人工确认退出时为 0）*/
static uint32_t s_safe_assert_ms;      /* 上次重申安全输出的时刻 */

/* 安全锁存：位含义见 control.h。上电默认"源全恢复"，由 control_poll 每拍刷新。
 * volatile：写侧在临界区，读侧（执行器门控/ack/读 API）裸读，必须每次重取（R1 复核 R-09）。*/
static volatile uint8_t s_safety_latch;
static uint8_t s_safety_ok = 0xFF;

/* 传感器类源（除急停外）的集合：ack 清急停要求这三者全部恢复 */
#define SAFETY_SENSOR_MASK (SAFETY_BIT_OVERTEMP | SAFETY_BIT_BODY_LOST | SAFETY_BIT_M207_LOST)

/* 2026-11-19（实测阻碍）：通信类故障（体温/M207 失联、探头开路）不是安全事件，
 * 源持续健康 2s 就自动清锁存——否则每次线缆抖动都要人工 ack，实测无法连续跑。
 * 急停与超温**不**在此列，仍必须人工确认（安全红线不变）。*/
#define COMM_AUTO_RECOVER_MS   2000u
#define SAFETY_COMM_MASK       ((uint8_t)(SAFETY_BIT_BODY_LOST | SAFETY_BIT_M207_LOST))
static uint32_t s_comm_ok_since;

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
    /* 除体温传感器之外的任何危险源（急停 / 超温 / 模块）一置位，模块侧状态就不再可信：
     * 本轮 SAFE 不得复原 TEC2。在这里统一作废，调用方（poll、cmd 8/11、M207 任务里的
     * 超温置位）不必各自记，也不必等到下一拍。*/
    if (bit & (uint8_t)~SAFETY_BIT_BODY_LOST) s_restore_tec2_ok = 0;
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
    /* SAFE 记忆量随 init 复位：上电恢复的锁存没有"故障前输出"，退出后一律全关 */
    s_fan_before_safe = 0;
    s_tec2_before_safe = 0;
    s_auto_before_safe = 0;
    s_restore_tec2_ok = 0;
    s_exit_by_recover = 0;
    s_safe_assert_ms = 0;
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
    ALM_LOCK();
    g_tcs.alarm_bits |= bit;
    ALM_UNLOCK();
}

uint8_t tcs_alarm_active(uint16_t bit)
{
    uint16_t v;
    ALM_LOCK();
    v = g_tcs.alarm_bits;
    ALM_UNLOCK();
    return (v & bit) ? 1u : 0u;
}

static void alarm_clear_bits(uint16_t mask)
{
    ALM_LOCK();
    g_tcs.alarm_bits &= (uint16_t)~mask;
    ALM_UNLOCK();
}

/* ====== 安全态进入/退出 ======
 * SAFE 期间记下故障前的输出，退出时复原（U 线 B3）：
 *   - 手动模式的风扇：故障前开着，解除后不该被关掉（原来一律 out_fan(0)）；
 *   - 自动模式不复原：故障前的风扇是状态机按体温开的，而退出后状态机从 IDLE 重新起步，
 *     若这时体温已回到滞回带内，就没有谁再去关它（2026-11-24 独立复核 P1-1：风扇常开）。
 *     所以退出当拍直接评估一次，输出由状态机按当前体温定；
 *   - TEC2（手动通道）：只有"体温传感器"一个危险源、模块全程在线、并且是 2s 自愈退出时
 *     才复原。模块通信/探头类故障期间模块侧状态未知；人工确认退出只说明"此刻源是好的"
 *     （最短 3 个好样本≈0.3s），不够格让 TEC 自动重开；
 *   - 急停会把自动切成手动：故障前的风扇是状态机开的而不是人选的，这时不复原。
 * 上电恢复的锁存没有"故障前"可言（s_state 在 control_init 里直接置 SAFE，
 * 不经 enter_safe），以上记忆量保持 0 → 退出后输出全关。*/
static void safe_outputs(void)
{
    out_tec_sw(1, 0);
    out_tec_sw(2, 0);
    out_fan(1);                       /* 安全态风扇全开 */
    goto_state(CTRL_SAFE);
}

/* 只在进入沿记忆：SAFE 期间再次 enter_safe 时输出已被关掉，不能当成"故障前状态" */
static void safe_remember_outputs(void)
{
    s_fan_before_safe  = g_tcs.fan_on;
    s_tec2_before_safe = g_tcs.tec2_on;
    s_auto_before_safe = (uint8_t)(g_tcs.mode != 0);
    s_restore_tec2_ok  = (uint8_t)((s_safety_latch & (uint8_t)~SAFETY_BIT_BODY_LOST) == 0);
    s_exit_by_recover  = 0;
}

static void enter_safe(void)
{
    if (s_state != CTRL_SAFE) safe_remember_outputs();
    safe_outputs();
}

static void auto_evaluate(uint32_t now_ms);

static void leave_safe(uint32_t now_ms)
{
    uint8_t auto_now = (uint8_t)(g_tcs.mode != 0);
    if (auto_now) {
        out_fan(0);                 /* 风扇归状态机管：先归零，本函数末尾当拍评估 */
    } else {
        /* 故障前是自动、现在是手动 = 急停把设备切成了手动：不复原风扇 */
        out_fan((s_auto_before_safe && !auto_now) ? 0u : s_fan_before_safe);
    }
    if (s_restore_tec2_ok && s_exit_by_recover) out_tec_sw(2, s_tec2_before_safe);
    s_restore_tec2_ok = 0;
    s_exit_by_recover = 0;
    s_last_eval = now_ms - CTRL_EVAL_MS;      /* 让评估节拍闸放行：当拍就评估 */
    goto_state(CTRL_IDLE);
    if (auto_now) auto_evaluate(now_ms);
}

static uint8_t safe_sources_recovered(void)
{
    if (!g_tcs.body_sensor_ok) return 0;
    if (!g_tcs.m207_online) return 0;
    if (g_tcs.m207_sensor_open) return 0;   /* 复核 B4：探头开路不算"源恢复" */
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
    /* X03-F-02：一连串 &= 合成一个临界区，避免并发 raise 的位被中间态抹掉 */
    ALM_LOCK();
    if (!(lat & SAFETY_BIT_BODY_LOST) && g_tcs.body_sensor_ok)
        g_tcs.alarm_bits &= (uint16_t)~ALM_BODY_SENSOR;
    if (!(lat & SAFETY_BIT_M207_LOST) && g_tcs.m207_online)
        g_tcs.alarm_bits &= (uint16_t)~ALM_M207;
    if (!(lat & SAFETY_BIT_M207_LOST) && !g_tcs.m207_sensor_open)
        g_tcs.alarm_bits &= (uint16_t)~ALM_SENSOR_OPEN;
    if (!(lat & SAFETY_BIT_OVERTEMP) && g_tcs.tc1_act <= ALM_OVERTEMP_HARD_C)
        g_tcs.alarm_bits &= (uint16_t)~ALM_OVERTEMP;
    if (!(lat & SAFETY_BIT_ESTOP)) {
        g_tcs.alarm_bits &= (uint16_t)~ALM_ESTOP;
        s_estop_latch = 0;
    }
    g_tcs.alarm_bits &= (uint16_t)~ALM_EEPROM;   /* EEPROM 报警仅提示性 */
    ALM_UNLOCK();
}

void tcs_emergency_stop_src(uint8_t src)
{
    s_estop_latch = 1;
    safety_trip(SAFETY_BIT_ESTOP);          /* P0-04：置锁存后，在途状态机的"开"被 out_tec_sw 压成 0 */
    tcs_alarm_raise(ALM_ESTOP);
    /* 故障前输出要在切手动**之前**记（记忆量里有"当时是不是自动模式"）*/
    if (s_state != CTRL_SAFE) safe_remember_outputs();
    s_restore_tec2_ok = 0;                  /* 急停不复原 TEC（safety_trip 已作废，这里再写一次表明意图）*/
    s_exit_by_recover = 0;
    /* mode=手动（经 param_set 同一数据源路径，自动广播 0x11）。来源照实记：事件日志要能
     * 分清是网页、屏幕还是控制台按的急停（原先一律记成"控制台：控制模式 改为 0"）。*/
    (void)param_set(PARAM_ID_MODE, 0.0f, src);
    safe_outputs();
}

void tcs_emergency_stop(void) { tcs_emergency_stop_src(PARAM_SRC_CONSOLE); }

/* 参数写入前的安全校验（main 把它接进 param_hooks_t.on_check，所有写路径都会经过）：
 * 急停或超温锁存期间不得切到自动模式。
 * "急停复位后不得自动重启"：锁存期间先把模式切成自动，解除的那一拍状态机就会重新开 TEC，
 * 等于绕过了"复位后要单独下启动指令"。所以"切自动"被挡在锁存之外——解除之后必须再明确切一次。
 * 通信类故障（体温/模块）的锁存不在此列：那是会自己好的故障，自动模式恢复后按体温接管。*/
int control_param_check(uint16_t id, float v)
{
    if (id == PARAM_ID_MODE && v != 0.0f
        && (s_safety_latch & (uint8_t)(SAFETY_BIT_ESTOP | SAFETY_BIT_OVERTEMP))) return -2;
    return 0;
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

int control_apply_cmd_src(uint8_t cmd, uint8_t src)
{
    /* 复核 D1：SAFE 下手动执行器命令会被 control_manual_* 静默吞掉，原来仍回 0
     * → 网页/屏幕都显示"成功"。这里显式拒绝（-2），由调用方映射成 BUSY。*/
    if (control_state() == CTRL_SAFE
        && (cmd == TCS_CMD_TEC1_ON || cmd == TCS_CMD_TEC1_OFF
            || cmd == TCS_CMD_TEC2_ON || cmd == TCS_CMD_TEC2_OFF
            || cmd == TCS_CMD_FAN_TOGGLE)) {
        return -2;
    }
    int rv;
    switch (cmd) {
    case TCS_CMD_TEC1_ON:       control_manual_tec(1, 1); return 0;
    case TCS_CMD_TEC1_OFF:      control_manual_tec(1, 0); return 0;
    case TCS_CMD_TEC2_ON:       control_manual_tec(2, 1); return 0;
    case TCS_CMD_TEC2_OFF:      control_manual_tec(2, 0); return 0;
    case TCS_CMD_FAN_TOGGLE:    control_manual_fan(g_tcs.fan_on ? 0 : 1); return 0;
    /* 改模式：来源原样带进参数表（事件日志据此写"网页/屏幕/控制台"）；被安全校验拒绝
     * （急停/超温锁存期间切自动）时 param_set 返回 -2，这里如实往上带 */
    case TCS_CMD_MODE_AUTO:     rv = param_set(PARAM_ID_MODE, 1.0f, src); return rv < 0 ? rv : 0;
    case TCS_CMD_MODE_MANUAL:   rv = param_set(PARAM_ID_MODE, 0.0f, src); return rv < 0 ? rv : 0;
    case TCS_CMD_ESTOP:         tcs_emergency_stop_src(src); return 0;
    case TCS_CMD_ALARM_ACK:     tcs_alarm_ack(); return 0;
    case TCS_CMD_OVERTEMP_STOP:
        /* R2-N-01：屏观测到超温 → 请求停机。只锁 OVERTEMP + 停两条 TCSW，
         * 不置急停锁存、不改手动模式（与 cmd 8 语义分离）。
         * 2026-11-23 复核 D0：统一命令入口时这一段被删，cmd 11 落到 default→BAD_ID，
         * 屏侧 5Hz 重发且对端继续加热——P0 级回归，已补回并加单测。*/
        control_safety_trip(SAFETY_BIT_OVERTEMP);
        tcs_alarm_raise(ALM_OVERTEMP);
        out_tec_sw(1, 0);
        out_tec_sw(2, 0);
        return 0;
    default:                    return -1;
    }
}

int control_apply_cmd(uint8_t cmd) { return control_apply_cmd_src(cmd, PARAM_SRC_CONSOLE); }

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

/* 自动模式的状态机迁移，按 CTRL_EVAL_MS 节拍（任务本身的节拍见 control_service.c，比这密）。
 * control_poll 每拍调用；leave_safe 退出当拍也调一次，输出不必等到下一个评估周期。*/
static void auto_evaluate(uint32_t now_ms)
{
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

/* ====== 每拍评估（任务每 100ms 调一次，见 control_service.c）====== */
void control_poll(uint32_t now_ms)
{
    if (!s_boot_ms_base_set) {
        s_boot_ms = now_ms;
        s_boot_ms_base_set = 1;
        s_safe_assert_ms = now_ms - CTRL_SAFE_REASSERT_MS;   /* 见下方"锁存期间"注释 */
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

    /* 锁存期间：任何模式都不再评估，直接停在 SAFE，并按 CTRL_SAFE_REASSERT_MS 重申 TCSW=0。
     * R-18：M207 离线时 m207_tec 直接返回 -2（不入队），本处重发并不产生帧；
     * 离线期的兜底是复联重同步（m207_go_online 里补发 TCSW=0）。P1-09 的彻底修法
     * （m207_tec_force 强制归零通道）仍在待办，勿据本注释认为离线已被覆盖。*/
    /* 通信类故障自动恢复（见 COMM_AUTO_RECOVER_MS 注释）。必须放在"锁存即返回"之前，
     * 且健康判据直接取 g_tcs（s_safety_ok 在锁存期间不刷新）。
     * 计时器只在"有锁存"期间累计：无锁存时清零，避免上次锁存的时间戳让下一次一进锁存
     * 就满足 2s（单测里连跑多段会踩到，现场表现为"刚跳 SAFE 又自己弹回"）。*/
    if (!s_safety_latch) s_comm_ok_since = 0;
    if (s_safety_latch) {
        uint8_t comm_ok = (uint8_t)(g_tcs.body_sensor_ok && g_tcs.m207_online
                                    && !g_tcs.m207_sensor_open
                                    && g_tcs.tc1_act <= ALM_OVERTEMP_HARD_C);
        if (comm_ok && (s_safety_latch & SAFETY_COMM_MASK)) {
            if (now_ms == 0) {
                /* 复核 B3：0 是"时间基准未建立"，不能当时间戳——否则 0-1 溢出成立即自愈 */
            } else if (!s_comm_ok_since) {
                s_comm_ok_since = now_ms;
            } else if ((uint32_t)(now_ms - s_comm_ok_since) >= COMM_AUTO_RECOVER_MS) {
                uint8_t emptied;          /* 这次自愈把锁存清空了（没有急停/超温留着）*/
                SAFETY_LOCK();
                s_safety_latch &= (uint8_t)~SAFETY_COMM_MASK;
                emptied = (uint8_t)(s_safety_latch == 0);
                SAFETY_UNLOCK();
                alarm_clear_bits((uint16_t)(ALM_M207 | ALM_BODY_SENSOR | ALM_SENSOR_OPEN));
                safety_persist_now();
                s_comm_ok_since = 0;
                /* 清空后本拍就会走到 leave_safe：只有这种"自愈退出"才允许复原 TEC2 */
                s_exit_by_recover = emptied;
#ifdef ESP_PLATFORM
                ESP_LOGW("ctrl", "comm fault auto-recovered (%ums healthy; estop/overtemp stay manual)",
                         (unsigned)COMM_AUTO_RECOVER_MS);
#endif
                (void)COMM_AUTO_RECOVER_MS;
            }
        } else {
            s_comm_ok_since = 0;
        }
    }

    if (s_safety_latch) {
        /* 期间模块掉线 / 探头断线 / 冒出别的危险源 → 模块侧状态不再可信，退出后不复原 TEC2 */
        if (!g_tcs.m207_online || g_tcs.m207_sensor_open
            || (s_safety_latch & (uint8_t)~SAFETY_BIT_BODY_LOST)) {
            s_restore_tec2_ok = 0;
        }
        /* 进入沿立刻断输出；之后按 CTRL_SAFE_REASSERT_MS 重申（节拍是 100ms，重申仍是 500ms）。
         * 首拍 s_safe_assert_ms 已在时间基准建立时置成"过期"，保证上电恢复的锁存第一拍就断输出。*/
        if (s_state != CTRL_SAFE || (uint32_t)(now_ms - s_safe_assert_ms) >= CTRL_SAFE_REASSERT_MS) {
            s_safe_assert_ms = now_ms;
            enter_safe();
        }
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
        if (m207_fault) {
            /* 语义分离（2026-11-16）：通信失联与探头开路是两种故障，报两句话。
             * SAFE 走同一条锁存（SAFETY_BIT_M207_LOST 表示"温度源不可用"）。*/
            if (!g_tcs.m207_online)     tcs_alarm_raise(ALM_M207);
            if (g_tcs.m207_sensor_open) tcs_alarm_raise(ALM_SENSOR_OPEN);
            safety_trip(SAFETY_BIT_M207_LOST);
        }
        enter_safe();
        return;
    }
    /* 源恢复位：供 ack 判定"能否清锁存"（未布防时也维护，避免急停 ack 被误阻）*/
    safety_set_ok(SAFETY_BIT_BODY_LOST, g_tcs.body_sensor_ok);
    safety_set_ok(SAFETY_BIT_M207_LOST, (uint8_t)(g_tcs.m207_online && !g_tcs.m207_sensor_open));

    if (s_state == CTRL_SAFE) {
        /* SAFE 退出：双恢复 + 人工确认（急停自持需 ack）*/
        if (safe_sources_recovered() && !s_estop_latch && tcs_alarm_active(ALM_BODY_SENSOR) == 0
            && tcs_alarm_active(ALM_M207) == 0 && tcs_alarm_active(ALM_SENSOR_OPEN) == 0
            && tcs_alarm_active(ALM_OVERTEMP) == 0    /* 复核 B4：显式依赖，别靠锁存门隐式兜底 */
            && tcs_alarm_active(ALM_ESTOP) == 0) {
            leave_safe(now_ms);      /* 复原故障前的风扇 / 手动 TEC2（见 enter_safe 上方注释）*/
        }
        return;
    }

    /* 手动模式：状态机旁路（输出保持人工值）*/
    if (g_tcs.mode == 0) {
        goto_state(CTRL_IDLE);
        return;
    }

    /* M06-F-06：没有有效体温样本时不得启动/继续自动冷热输出（未布防也一样）。
     * 上电默认 body_temp=0.0 曾是"合法样本"：auto 首个 500ms 评估就按 0°C 开 HEAT，
     * 传感器没接也照样加热。已输出的通道先归零、风扇停，再停在 IDLE 等样本。*/
    if (!g_tcs.body_sensor_ok) {
        if (s_state == CTRL_HEAT || s_state == CTRL_COOL) {
            out_tec_sw(1, 0);
            out_tec_sw(2, 0);
            out_fan(0);
        }
        goto_state(CTRL_IDLE);
        return;
    }

    auto_evaluate(now_ms);
}

ctrl_state_t control_state(void) { return s_state; }

uint32_t control_ms_from_us(int64_t us)
{
    /* 先把微秒截成 int32 再除是错的：开机 35 分 47 秒（2^31 µs）后它变成负数，毫秒时间戳
     * 从 2147483 直接跳到 4292789813——相当于倒退 71 分钟，此后回跳前记的时间戳做无符号
     * 差值全都"早已到期"（2s 自愈、5s 上电宽限立即成立）。必须用 64 位除。
     * （独立复核 P1-2，2026-11-24；原先写在 control_service.c 里，主机测不到。）*/
    return (uint32_t)((uint64_t)us / 1000u);
}

/* ====== 安全锁存/急停投递读侧（console/网页/诊断）====== */
uint8_t control_safety_latched(void) { return s_safety_latch; }
void    control_safety_trip(uint8_t bit) { safety_trip(bit); }

#ifdef TCS_UNIT_TEST
void control_test_trip(uint8_t bit) { safety_trip(bit); }
#endif
