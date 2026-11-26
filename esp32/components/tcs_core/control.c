/**
 * control.c — 联动控制状态机实现（任务书 §3.6）
 *
 * 自动模式迁移表：
 *   IDLE : body > body_hot+hyst → TC1 设 15.0 + TCSW=1 + 风扇开 → COOL
 *   IDLE : body < body_cold−hyst → TC1 设 45.0 + TCSW=1 + 风扇开 → HEAT
 *   COOL : body < body_hot−hyst → TCSW=0 + 风扇关 → IDLE
 *   HEAT : body > body_cold+hyst → TCSW=0 + 风扇关 → IDLE
 *   任意 : 传感器丢失 / M207 失联 → SAFE（TCSW=0 + 风扇开 + ALM）
 *   任意 : 过温（tc1_act 或 tc2_act > 48°C）→ SAFE，不受布防门控（P0-01；TC2 见 r7f R7-08）
 *   任意 : 读数卡死（源自报健康，但读数逐位不变 ≥30s 且 TEC 已开 ≥30s）→ SAFE，不受布防门控
 *          （M09-F-09 / X04-F-06，见下方"卡死值活性判据"）
 *   SAFE : 通信类故障（体温/M207/探头/卡死）源健康 2s → 自动退出；急停/超温 → 人工确认后退出
 *   退出 : 手动模式复原故障前的风扇；仅"体温传感器一个源、且是 2s 自愈退出"时复原手动 TEC2；
 *          自动模式不复原，输出当拍由状态机按体温重新定（见 leave_safe）
 * 滞回算例（默认参数 37.5/36.5/0.2）：>37.7 进制冷、<37.3 退制冷；
 *   <36.3 进制热、>36.7 退制热（单测向量 ±0.001）。
 */
#include "control.h"
#include "param_registry.h"
#include "post.h"
#include <string.h>       /* 卡死判据按位比较读数（memcpy）*/
#include "control_manual.h"   /* m1（模式页）：手动两路 TEC 输出映射 */

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
/* m1（模式页，2026-11-26）：别的任务（链路 / 网页 / 控制台）提出"两路手动输出回暂停"——急停、
 * 切到自动。只登记请求，由 control_poll 在控制任务里执行：手动输出的状态只在一个任务里改。*/
static volatile uint8_t s_manual_reset_req;

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

/* 传感器类源（除急停外）的集合。ack 清急停要求"需要的那几个"全部恢复：
 * 已布防 = 三个都要；未布防 = 不含体温源（见 safety_ack_latched）。*/
#define SAFETY_SENSOR_MASK (SAFETY_BIT_OVERTEMP | SAFETY_BIT_BODY_LOST | SAFETY_BIT_M207_LOST)

/* 2026-11-19（实测阻碍）：通信类故障（体温/M207 失联、探头开路）不是安全事件，
 * 源持续健康 2s 就自动清锁存——否则每次线缆抖动都要人工 ack，实测无法连续跑。
 * 急停与超温**不**在此列，仍必须人工确认（安全红线不变）。*/
#define COMM_AUTO_RECOVER_MS   2000u
#define SAFETY_COMM_MASK       ((uint8_t)(SAFETY_BIT_BODY_LOST | SAFETY_BIT_M207_LOST | SAFETY_BIT_STUCK))
static uint32_t s_comm_ok_since;

/* 落盘的锁存位：STUCK 只在运行时成立，不跨复位（理由见 control.h 的 SAFETY_BIT_STUCK）*/
#define SAFETY_PERSIST_MASK     ((uint8_t)~SAFETY_BIT_STUCK)
#define CTRL_STUCK_WINDOW_MS    30000u  /* 体温卡死窗口：300 拍 × 100ms（审查方案 M09-F-09 的 N=300）*/
#define CTRL_STUCK_WATER_WINDOW_MS 600000u   /* 水温卡死窗口：10 分钟。为什么比体温长得多，见下面的说明 */

/* ====== 卡死值活性判据（M09-F-09 / X04-F-06，审查方案 AD-2）======
 * 合理性滤波只查范围与变化率：恒定值永远"合理"。于是——
 *   体温（TMP117）读数冻结 → 自动模式永远停在 HEAT/COOL，水按 45°C / 15°C 目标一直开；
 *   水温（M207 TCACTTEMP）冻结 → 48°C 硬切断对它完全失效；冻结的若正是模块自己的探头，
 *     模块闭环也一起失明，输出不再有界——这才是真正的"无边界加热"。
 * 判据：源自报健康（体温 ok / 模块在线且探头未开路），但读数**逐位**不变满一个窗口，
 *   且 TEC 已连续开满同一个窗口（两者取较晚者：刚开 TEC 时给被控对象留出响应时间）。
 *   R8-03（独立复核 r8f，2026-11-26）：“两者取较晚者”对**体温**是硬口径——体温这一路的预冻判据已撤掉
 *     （30 秒窗口与健康读数的稳态段同量级，自动进 HEAT 的当拍就可能误进 SAFE）；水温保留预冻（见下）。
 *   - 体温：只在自动模式的 HEAT / COOL 下判——那里是这个读数在驱动决策；手动模式下体温读数不驱动任何输出。
 *   - 水温：TC1 开着就判（自动、手动都算；手动模式下 48°C 硬切断同样依赖这个读数），
 *     但窗口是 10 分钟（CTRL_STUCK_WATER_WINDOW_MS），不是 30 秒。2026-11-26 真机实测（F2，TEC 未开）：
 *     健康的 TCACTTEMP 逐位不变的连续段经常超过 30 秒——风扇关的 3 分钟里两路各 1 段（最长 33 秒），
 *     风扇 100% 的 10 分钟里共 3 段（最长 53 秒）。按 30 秒判，正常恒温时就会误报进安全态。
 *     10 分钟 ≈ 实测最长段的 11 倍。代价：水温冻结要 10 分钟才被发现；每不到 10 分钟"动一下"的冻结读数判不出来。
 *     TEC 开着时的读数抖动还没有实测（用户做 TEC 实测时量，方法见 docs/evidence/S_线/F2_水温读数逐位不变的连续时长.txt）。
 *   源不健康期间不累计（离线 / 探头开路走既有失联路径，不重复报）。
 * 进 SAFE，不受布防门控。报警位借用 ALM_BODY_SENSOR / ALM_SENSOR_OPEN：屏与网页只对已知位出文案，
 *   新增位在两端都不可见（界面文件不在本批次）。这两位在两端都归"自动恢复类"，所以 STUCK 也按
 *   通信类自愈：涉事通道的读数重新开始变化后 2s 清锁存；读数一直不变就一直留在 SAFE。
 *   （代价：时冻时动的传感器每次"动一下"最多换来一个窗口的输出，见结果文档风险一节。）
 * 真机旁证：docs/evidence/S_线/F2_水温读数逐位不变的连续时长.txt。
 *   （更正：e3a 批次曾引 B10 为"健康读数不会 30 秒逐位不变"——B10 是每分钟一个样本，只能说明
 *     相邻两分钟多半不同，推不出 30 秒这一级；F2 用 2–4 Hz 连续采样，结论相反。）*/
typedef struct {
    uint32_t bits;         /* 最近一个**不同**的读数（按位比较；不用 ==，NaN 也算变化）*/
    uint32_t last_change;  /* 读数最近一次变化的时间；0 = 没有样本（源不健康期间复位）*/
} stuck_src_t;

static stuck_src_t s_stuck_body, s_stuck_tc1;
static stuck_src_t s_stuck_tc2;        /* r7f（R7-08）：TC2 那一路的水温读数——独立水体时它没有任何别的保护在看 */
static volatile uint8_t s_stuck_chn;   /* 本轮 STUCK 锁存由谁引起：bit0=体温 bit1=TC1 水温 bit2=TC2 水温（随锁存位一起清）*/
static uint8_t    s_stuck_watch;       /* R9-02：这次 STUCK 锁存时**正被驱动**的水温通道（bit1=TC1 bit2=TC2）*/
static uint32_t   s_tec_on_since[2];   /* TCx 有效命令最近一次 0→1 的时间（out_tec_sw 里记）*/
static uint8_t    s_tec_cmd_on[2];       /* TEC 命令意图（只有 out_tec_sw 写；取值 control_tec_cmd，X03-F-08）*/
static uint32_t   s_water_pre_ms[2];   /* R9-03：水温预冻挂起的起始时刻（0 = 没挂起）；按通道 TC1/TC2 */
static uint32_t   s_poll_now_ms;       /* 最近一拍的时间戳：out_tec_sw 在拍外被调时用它给 TEC 开沿计时*/
#if CONFIG_TCS_DIAG || CONFIG_TCS_SIM
/* S 线 i3（条目 8）：读数"逐位不变"的最长连续时长（[0] 体温、[1] TC1 水温、[2] TC2 水温）。
 * mstat 用它量 TEC 开着时的读数抖动（F2 第四节第 3 点）；只读导出，不参与任何判据。交付档不编译。
 * R8-06：TC2 单独一路。原先 TC1/TC2 共用 [1]，两路水温混在一起——mstat 的 water max 只会
 * 报"先收尾的那一段"，另一路冻着也看不出来。*/
static uint32_t   s_stuck_max_ms[3];
#endif

static void stuck_feed(stuck_src_t *c, float v, uint32_t now_ms, uint8_t src_ok, uint8_t idx)
{
    uint32_t b;
    if (!src_ok) {                       /* 源不健康：从头再来，恢复后要有完整的一个窗口 */
        c->last_change = 0;
        return;
    }
    memcpy(&b, &v, sizeof(b));
    if (!c->last_change || b != c->bits) {
#if CONFIG_TCS_DIAG || CONFIG_TCS_SIM
        if (c->last_change && now_ms > c->last_change &&
            (now_ms - c->last_change) > s_stuck_max_ms[idx])      /* 一段收尾：记最长 */
            s_stuck_max_ms[idx] = now_ms - c->last_change;
#endif
        c->bits = b;
        c->last_change = now_ms ? now_ms : 1u;   /* 0 是"时间基准未建立"，不能当时间戳 */
    }
}

/* 读数是"活"的：最近一个窗口内变过 */
static uint8_t stuck_live(const stuck_src_t *c, uint32_t now_ms)
{
    return (uint8_t)(c->last_change && (uint32_t)(now_ms - c->last_change) < CTRL_STUCK_WINDOW_MS);
}

/* 卡死成立：读数逐位不变满一个窗口，且 TEC 从那之后（或同时）一直开着。
 * 没有样本（源不健康期间 last_change 被复位）不算卡死——离线走失联路径，不是"读数冻结"。*/
static uint8_t stuck_is(const stuck_src_t *c, uint32_t now_ms, uint32_t on_since)
{
    if (!c->last_change || stuck_live(c, now_ms) || !on_since) return 0;
    return (uint8_t)((uint32_t)(now_ms - on_since) >= CTRL_STUCK_WINDOW_MS);
}

/* 水温卡死的窗口比体温长（2026-11-26，真机实测之后改的；理由见上面"卡死值活性判据"一段）。
 * stuck_is() 仍按 30 秒把一遍（样本存在、TEC 开沿、30 秒没变），再要求距读数最后一次变化已满
 * CTRL_STUCK_WATER_WINDOW_MS。调用方保证 stuck_is(&s_stuck_tc1, …) 已经成立（last_change 非 0）。*/
static uint8_t stuck_water_is(const stuck_src_t *c, uint32_t now_ms)
{
    return (uint8_t)((uint32_t)(now_ms - c->last_change) >= CTRL_STUCK_WATER_WINDOW_MS);
}

/* r7f（独立复核 R7-04，2026-11-26）：TEC 开沿那一刻读数如果**已经**冻满一个窗口，直接算卡死，不再等 30 秒。
 * 那 30 秒的本意是"刚开 TEC 时给被控对象留出响应时间"；读数在 TEC 开之前就冻住时并没有可等的响应，
 * 等下去只是让 48°C 硬切断和卡死判据一起失明（真机场景：探头在 TEC 没开的时候就冻住了）。
 * 写法上只用无符号差值：等价于"TEC 开沿时距读数最后一次变化已满 win"，又避开两个时间戳的排序问题。
 * R8-03（独立复核 r8f，2026-11-26）：只给**水温**两路用（win = CTRL_STUCK_WATER_WINDOW_MS）。体温不用：
 * 它的窗口只有 30 秒，健康读数在稳态本来就可能冻满 30 秒，开沿当拍会误报（用例 K9b）。*/
static uint8_t stuck_prefrozen(const stuck_src_t *c, uint32_t now_ms, uint32_t on_since, uint32_t win)
{
    if (!c->last_change || !on_since) return 0;
    return (uint8_t)((uint32_t)(now_ms - c->last_change)
                     >= (uint32_t)(win + (uint32_t)(now_ms - on_since)));
}

/* R9-03（独立复核 R9，2026-11-26）：水温预冻不再"开 TEC 当拍"就下结论，改成 30 秒确认窗。
 * 旧写法（stuck_prefrozen 直接成立就判卡死）在"TEC 关、热稳态"的台架上不会误报，但保温水箱 / 恒温室里
 * 0.01°C 分辨率下连续 10 分钟逐位不变完全可能——命中时的表现是"按制热就报水温探头断线"，
 * 用户会去拔插探头，是最坏的一类误导（R9-03）。
 * 现在的口径：开沿时预冻成立 → 挂起（s_water_pre_ms），窗口内读数动过一次立刻撤销；
 * 一次都没动（且冻结总时长仍满它自己的 10 分钟窗口）才判卡死。保留 R7-04 的本意
 * （不把盲窗拖满 10 分钟），代价只是这 30 秒。i = 0/1 对应 TC1/TC2。*/
static uint8_t stuck_water_prefrozen(uint8_t i, const stuck_src_t *c, uint32_t now_ms)
{
    if (!s_tec_cmd_on[i] || !c->last_change) { s_water_pre_ms[i] = 0; return 0; }
    if (stuck_live(c, now_ms)) { s_water_pre_ms[i] = 0; return 0; }   /* 读数动过一次 → 撤销 */
    if (!s_water_pre_ms[i]) {
        if (!stuck_prefrozen(c, now_ms, s_tec_on_since[i], CTRL_STUCK_WATER_WINDOW_MS)) return 0;
        s_water_pre_ms[i] = now_ms ? now_ms : 1u;
    }
    return (uint8_t)((uint32_t)(now_ms - s_water_pre_ms[i]) >= CTRL_STUCK_WINDOW_MS);
}

/* 涉事通道的读数是否都已重新开始变化（STUCK 自愈与 ack 的依据）*/
static uint8_t stuck_recovered(uint32_t now_ms, uint8_t chn)
{
    if ((chn & 1u) && !stuck_live(&s_stuck_body, now_ms)) return 0;
    if ((chn & 2u) && !stuck_live(&s_stuck_tc1,  now_ms)) return 0;
    if ((chn & 4u) && !stuck_live(&s_stuck_tc2,  now_ms)) return 0;   /* r7f R7-08：TC2 那一路 */
    return 1;
}

/* S 线 i3（M09-F-09 的注入档口径，2026-11-26）：该路的注入源是否在生效（ch 1 = 体温，2 = 水温）。
 * 注入值是人给的常数，"逐位不变"是它的本性，不代表传感器卡死——不豁免的话，仿真 / 自检 / 现场档里
 * 恒定注入 + TEC 开 → 体温 30 秒（水温 10 分钟）必进安全态，打断台架流程。
 * 判据由装配方给（control_ops_t.stuck_exempt；main.c 接注入源，交付档恒 0），控制核不依赖任何注入实现。
 * 生效 / 失效各打一次 W 级日志（注入期间每拍都成立，不能每拍打）。*/
static uint8_t stuck_src_injected(uint8_t ch)
{
    static uint8_t s_logged;                 /* bit0 = 体温，bit1 = 水温 */
    uint8_t m = (uint8_t)(1u << (ch - 1u));
    uint8_t r = (uint8_t)((s_ops.stuck_exempt && s_ops.stuck_exempt(ch)) ? 1u : 0u);
    if (r && !(s_logged & m)) {
        s_logged |= m;
#ifdef ESP_PLATFORM
        ESP_LOGW("ctrl", "stuck check exempt on %s: the reading comes from an injection (constant by design); "
                         "real sensors stay checked", ch == 1u ? "body" : "water");
#endif
    } else if (!r && (s_logged & m)) {
        s_logged &= (uint8_t)~m;
#ifdef ESP_PLATFORM
        ESP_LOGI("ctrl", "stuck check back on %s", ch == 1u ? "body" : "water");
#endif
    }
    return r;
}

/* r7f（独立复核 R7-08，2026-11-26）：两路水温都参与超温硬切断与"是否已经降温"的判据。
 * 仓库里 TC1 / TC2 写作"贴身水箱 / 环境水箱"（WS_PROTOCOL.md），各有一路 TCACTTEMP 探头。原先所有
 * 超温 / 退出 / 确认 / 自愈的判据只看 tc1_act——TC2 若是一路独立水体（用户确认接线之前按独立处理），
 * 手动让 TC2 制热时主控侧没有任何超温保护，只剩模块自带保护（保护位的语义还没有逐位上机核实）。
 * 与 TC1 完全同一口径（48°C，不受布防门控）。用例：tests/test_control_esp32.c 的 K22–K23。*/
static uint8_t water_overtemp(void)
{
    return (uint8_t)(g_tcs.tc1_act > ALM_OVERTEMP_HARD_C || g_tcs.tc2_act > ALM_OVERTEMP_HARD_C);
}

/* 涉事通道：锁存里有 STUCK 才有意义。调用方持有 SAFETY_LOCK（lat 就是锁里读出的那份）*/
static uint8_t stuck_chn_latched(uint8_t lat)
{
    return (uint8_t)((lat & SAFETY_BIT_STUCK) ? s_stuck_chn : 0u);
}

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
    v = s_safety_latch & SAFETY_PERSIST_MASK;   /* STUCK 不落盘 */
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
    v = s_safety_latch & SAFETY_PERSIST_MASK;   /* STUCK 不落盘 */
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
    /* R9-04（独立复核 R9，2026-11-26）：只在"落盘视图真的变了"时写。STUCK 不在 SAFETY_PERSIST_MASK
     * 里，一个只含 STUCK 的 trip 原先也会把同一个值（不变量）再写一遍 NVS——操作员连点"解除"时
     * ack+重锁的写放大就是这么来的（每对 2 次写；10 Hz 一小时 72k 次，8 字节 blob 也在磨损均衡下）。*/
    if (changed && (bit & SAFETY_PERSIST_MASK)) safety_persist_now();   /* 锁外取最新值落盘（N-04） */
}

static void safety_set_ok(uint8_t bit, uint8_t ok)
{
    SAFETY_LOCK();
    if (ok) s_safety_ok |= bit;
    else    s_safety_ok &= (uint8_t)~bit;
    SAFETY_UNLOCK();
}

static uint8_t commissioned(void);   /* 定义在下方"控制节拍"附近；这里只做前置声明 */

/* 人工确认：只清"源已恢复"的位；急停额外要求其余源全部恢复且无其它锁存。
 * 2026-11-25（S 线，交付固件真机发现）：解除急停所需的"其余源"按布防态取——
 *   已布防：体温、模块、水温三个源都要恢复（原口径不变）；
 *   未布防：不含体温源。设备从开机起就没有体温探头时本来就允许手动运行（规格："未布防只记沿"），
 *   若仍要求体温源恢复，急停在这类设备上永远解除不了——而锁存存在 NVS，断电重启也出不来。
 *   "已布防后探头丢了"不受影响：那时 SAFETY_BIT_BODY_LOST 已经锁存，它自己要等探头恢复才清，
 *   所以下面"无其它锁存"这一条会挡住急停的解除。
 * 判别性用例：tests/test_control_esp32.c 的 N1–N4（N1/N3/N4 的解除断言在旧实现上必红）。*/
static void safety_ack_latched(void)
{
    uint8_t lat, ok, drop, after, need, view_changed;
    /* 布防态在锁外读：post 三态机有自己的状态，不在安全锁里调别的模块。*/
    need = (uint8_t)(commissioned() ? SAFETY_SENSOR_MASK
                                    : (SAFETY_SENSOR_MASK & (uint8_t)~SAFETY_BIT_BODY_LOST));
    SAFETY_LOCK();
    lat = s_safety_latch;
    ok  = s_safety_ok;
    drop = (uint8_t)(lat & ok & (uint8_t)~SAFETY_BIT_ESTOP);
    after = (uint8_t)(lat & (uint8_t)~drop);   /* R-07：用"清位后"判断急停，避免两次 ack */
    if ((after & (uint8_t)~SAFETY_BIT_ESTOP) == 0 && (ok & need) == need) {
        drop |= (uint8_t)(after & SAFETY_BIT_ESTOP);
    }
    s_safety_latch = (uint8_t)(lat & (uint8_t)~drop);
    if (!(s_safety_latch & SAFETY_BIT_STUCK)) s_stuck_chn = 0;   /* 涉事通道随锁存位一起清 */
    view_changed = (uint8_t)(((lat ^ s_safety_latch) & SAFETY_PERSIST_MASK) != 0u);
    SAFETY_UNLOCK();
    /* R9-04：清掉的只有 STUCK（不落盘）时不产生 NVS 写——写进去的值和原来逐位一样。*/
    if (drop && view_changed) safety_persist_now();      /* N-04：写当前值，不会用旧快照覆盖并发 trip */
}

/* ====== 内部输出封装（同步 g_tcs 快照 + 钩子）====== */
static void out_fan(uint8_t on)
{
    g_tcs.fan_on = on ? 1u : 0u;
    if (s_ops.fan_set) s_ops.fan_set(on);
}

static void out_tec_sw_ex(uint8_t ch, uint8_t on, uint8_t force)
{
    /* 安全门控的唯一收敛点：锁存期间任何"开"都压成 0（P0-04 的在途状态机覆写在此失效）。
     * 单字节读在 32 位 MCU 上原子；最坏情况是晚一拍看到 trip，而 trip 由 poll 同拍处理。 */
    if (on && s_safety_latch) on = 0;
    {
        /* 有效命令的 0→1 沿：卡死判据要用"TEC 实际开了多久"（锁存期间被压成 0 的"开"不算开过）*/
        uint8_t i = (ch == 2) ? 1u : 0u;
        if (on && !s_tec_cmd_on[i]) s_tec_on_since[i] = s_poll_now_ms ? s_poll_now_ms : 1u;
        s_tec_cmd_on[i] = on ? 1u : 0u;
    }
    if (ch == 1) g_tcs.tec1_on = on ? 1u : 0u;
    else         g_tcs.tec2_on = on ? 1u : 0u;
    /* 执行通道：安全关断（force=1）有专门的通道就走它；其余一律普通命令队列 */
    if (force && s_ops.tec_force) s_ops.tec_force(ch, on);
    else if (s_ops.tec_sw) s_ops.tec_sw(ch, on);
}

static void out_tec_sw(uint8_t ch, uint8_t on) { out_tec_sw_ex(ch, on, 0); }

/* TEC 开关状态是否"已知"（P1-09）：模块在线才可信；离线期间 tec*_on 只是本端的命令意图 */
static uint8_t tec_known_now(void)
{
    return (uint8_t)(g_tcs.m207_online ? 0x03u : 0x00u);
}

uint8_t control_tec_known(void) { return tec_known_now(); }

/* 命令意图与模块回显是两个量（X03-F-08）。此前只有 g_tcs.tecN_on 一个字段：控制写命令意图，
 * M207 服务在回 TCSW? 时写模块回显——"控制已判定关闭、执行器实际还开着"时它说哪个都不对。
 *   control_tec_cmd()：命令意图，只有 out_tec_sw() 写（锁存期间被压成 0 的"开"也算"关"）。
 *   control_tec_act()：模块回显 = 独立字段 s_tec_act_on（i3 起只有 control_tec_echo_set 写，M207 服务在
 *   回 TCSW? 时调它；此前读的是与命令混用的 g_tcs.tecN_on）。
 * 复联判据（m207_go_online 里"命令是关就补发 TCSW=0"）应当读 cmd；快照两个字段都带是 tcs_snap.c 的事。*/
uint8_t control_tec_cmd(uint8_t ch) { return (ch == 2) ? s_tec_cmd_on[1] : s_tec_cmd_on[0]; }

/* 模块回显（i3，2026-11-26；P1-09 收口）：独立的字段，只有 M207 服务收到 TCSW? 应答时经
 * control_tec_echo_set() 写。此前 control_tec_act() 读的是 g_tcs.tecN_on——控制每写一次命令就把它
 * 改成命令值，"回显"名不副实：快照里的 tec_act 会在模块还没回话时就跟着命令变，屏分不清
 * "要它关"和"它确实关了"。g_tcs.tecN_on 保持原样（命令与回显混用，旧消费者仍读它）。*/
static uint8_t    s_tec_act_on[2];       /* TEC 模块回显（只有 control_tec_echo_set 写；取值 control_tec_act）*/
uint8_t control_tec_act(uint8_t ch) { return (ch == 2) ? s_tec_act_on[1] : s_tec_act_on[0]; }
void control_tec_echo_set(uint8_t ch, uint8_t on)
{
    if (ch == 1)      s_tec_act_on[0] = on ? 1u : 0u;
    else if (ch == 2) s_tec_act_on[1] = on ? 1u : 0u;
}

static void out_tec_temp(uint8_t ch, float v)
{
    if (s_ops.tec_temp) s_ops.tec_temp(ch, v);
}

/* m1（模式页）：control_manual 的输出经这三条回到上面的封装——安全门控（out_tec_sw 把锁存期间的
 * "开"压成 0）与 g_tcs 同步仍只有这一个收敛点。*/
static void mo_tec_sw(uint8_t ch, uint8_t on) { out_tec_sw(ch, on); }
static void mo_tec_temp(uint8_t ch, float v)  { out_tec_temp(ch, v); }
static void mo_fan_set(uint8_t on)             { out_fan(on); }

static void goto_state(ctrl_state_t st)
{
    if (s_state == st) return;
    s_state = st;
    if (s_ops.on_state) s_ops.on_state(st);
}

/* r7f（独立复核 R7-02 ①，2026-11-26）：上电读到急停锁存 → 模式无条件置回手动，并立刻落盘。
 * 为什么：急停把模式切回手动原先走普通写路径（置脏 → 2 秒防抖 + 500ms 冲洗才落盘），急停锁存却是
 * 立刻落盘的；这 2.5 秒内掉电重启，NVS 里就是"自动模式 + 急停锁存"——人工点一次"解除"，状态机当拍
 * 按体温把 TEC 重新开起来，用户没有重新切过自动（不变量 I1）。这里不等防抖：写不进（返回 < 0）时脏位
 * 还在，persist_worker 会按防抖重试；而且每次上电读到急停锁存都会再置一次。急停路径里那一次同步落盘
 * 见 tcs_emergency_stop_src（②）。用例：tests/test_control_esp32.c 的 R7-02a…e。*/
static void estop_boot_force_manual(void)
{
    (void)param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_KERNEL);
    (void)param_persist_now();                  /* r7f R7-02 ① */
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
    /* 卡死判据从头开始：上电没有"读数多久没变""TEC 开了多久"可言 */
    memset(&s_stuck_body, 0, sizeof(s_stuck_body));
    memset(&s_stuck_tc1, 0, sizeof(s_stuck_tc1));
#if CONFIG_TCS_DIAG || CONFIG_TCS_SIM
    s_stuck_max_ms[0] = s_stuck_max_ms[1] = s_stuck_max_ms[2] = 0;   /* S 线 i3：最长连续时长从这次上电算起 */
#endif
    memset(&s_stuck_tc2, 0, sizeof(s_stuck_tc2));   /* r7f R7-08：TC2 那一路 */
    s_stuck_chn = 0;
    s_stuck_watch = 0;      /* R9-02 */
    memset(s_tec_on_since, 0, sizeof(s_tec_on_since));
    memset(s_tec_cmd_on, 0, sizeof(s_tec_cmd_on));
    memset(s_water_pre_ms, 0, sizeof(s_water_pre_ms));   /* R9-03：预冻确认窗从头开始 */
    memset(s_tec_act_on, 0, sizeof(s_tec_act_on));   /* i3：模块回显从头开始，上电还没有回读过 */
    s_poll_now_ms = 0;
    control_manual_init(&(control_manual_ops_t){ mo_tec_sw, mo_tec_temp, mo_fan_set });   /* m1 */
    s_manual_reset_req = 0;                                                              /* m1 */
    g_tcs.alarm_bits = 0;
    g_tcs.tec_known = tec_known_now();  /* P1-09：模块还没上线就是"未知"，不是"已关" */
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
        s_safety_latch = (uint8_t)(lat & SAFETY_PERSIST_MASK);   /* 旧固件写进去的 STUCK 位读回即丢 */
    }
    s_safety_ok = 0xFF;
    if (s_safety_latch & SAFETY_BIT_ESTOP)      { s_estop_latch = 1; g_tcs.alarm_bits |= ALM_ESTOP; }
    if (s_safety_latch & SAFETY_BIT_OVERTEMP)   g_tcs.alarm_bits |= ALM_OVERTEMP;
    if (s_safety_latch & SAFETY_BIT_BODY_LOST)  g_tcs.alarm_bits |= ALM_BODY_SENSOR;
    if (s_safety_latch & SAFETY_BIT_M207_LOST)  g_tcs.alarm_bits |= ALM_M207;
    if (s_safety_latch) s_state = CTRL_SAFE;   /* 绕过 on_state：ops 可能尚未装配回调 */
    /* r7f（独立复核 R7-02 ①）：读到急停锁存 → 模式无条件回手动并立刻落盘，理由见 estop_boot_force_manual */
    if (s_safety_latch & SAFETY_BIT_ESTOP) estop_boot_force_manual();
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
    out_tec_sw_ex(1, 0, 1);                     /* P1-09：安全关断走专门的通道，不占普通队列 */
    out_tec_sw_ex(2, 0, 1);
    out_fan(1);                       /* 安全态风扇全开 */
    goto_state(CTRL_SAFE);
}

/* 只在进入沿记忆：SAFE 期间再次 enter_safe 时输出已被关掉，不能当成"故障前状态" */
static void safe_remember_outputs(void)
{
    s_fan_before_safe  = control_manual_fan_pref();   /* m1：风扇被联动开着时，记的是用户原来的选择 */
    s_tec2_before_safe = g_tcs.tec2_on;
    /* m1（模式页）：TEC2 是模式页设的方向开着的 → 进安全态时参数已清回暂停，自愈退出时也不复原
     *（用户裁决：安全优先，确认解除后不得自己恢复输出）。下面 B3 的复原只留给网页 / 台架的旧命令。*/
    if (control_manual_dir(2) != TEC_DIR_PAUSE) s_tec2_before_safe = 0;
    s_auto_before_safe = (uint8_t)(g_tcs.mode != 0);
    s_restore_tec2_ok  = (uint8_t)((s_safety_latch & (uint8_t)~SAFETY_BIT_BODY_LOST) == 0);
    s_exit_by_recover  = 0;
}

static void enter_safe(void)
{
    if (s_state != CTRL_SAFE) safe_remember_outputs();
    safe_outputs();
    /* m1（模式页，用户 2026-11-26）：进安全态 = 两路手动输出回暂停、风扇联动解除——确认解除后
     * 不得自己恢复输出。幂等：锁存期间每 500ms 重申会再进来，参数已是 0 就什么都不做。
     * 不碰风扇：安全态风扇全开，解除时 leave_safe 复原上面记下的用户选择。*/
    control_manual_reset(0);
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

/* 退出 SAFE 所需的"源恢复"：已布防 = 体温 + 模块 + 水温；未布防 = 模块 + 水温。
 * 2026-11-25（S 线）：未布防时体温探头缺失不计入——规格"未布防只记沿"，这类设备本来就允许手动运行；
 * 口径与 safety_ack_latched 一致（那边的"无其它锁存"会挡住"已布防后探头丢了"的情况）。
 * 退出后在自动模式下的保护另有其人：下方 M06-F-06"没有有效体温样本不得启动自动冷热输出"。*/
static uint8_t safe_sources_recovered(void)
{
    if (commissioned() && !g_tcs.body_sensor_ok) return 0;
    if (!g_tcs.m207_online) return 0;
    if (g_tcs.m207_sensor_open) return 0;   /* 复核 B4：探头开路不算"源恢复" */
    if (water_overtemp()) return 0;         /* r7f R7-08：两路水温都算（原来看 tc1_act）*/
    return 1;
}

void tcs_alarm_ack(void)
{
    uint8_t lat;
    uint8_t stuck_chn;      /* STUCK 涉事通道：bit0=体温 bit1=水温；锁存里没有 STUCK 则为 0 */
    /* 先按"当前"源状态刷新（不依赖上一拍缓存）：操作员降温后立即 ack 也应生效。*/
    safety_set_ok(SAFETY_BIT_OVERTEMP, (uint8_t)!water_overtemp());   /* r7f R7-08：两路水温 */
    safety_set_ok(SAFETY_BIT_BODY_LOST, g_tcs.body_sensor_ok);
    safety_set_ok(SAFETY_BIT_M207_LOST, (uint8_t)(g_tcs.m207_online && !g_tcs.m207_sensor_open));
    /* 卡死（STUCK）：按涉事通道"读数是否重新开始变化"刷新。活性是窗口概念，依赖 control_poll 的
     * 样本跟踪，拍外没有自己的时钟，所以时间戳取最近一拍。*/
    {
        SAFETY_LOCK();
        stuck_chn = stuck_chn_latched(s_safety_latch);
        SAFETY_UNLOCK();
        safety_set_ok(SAFETY_BIT_STUCK, stuck_recovered(s_poll_now_ms, stuck_chn));
    }
    /* P0：再按"源已恢复"清安全锁存；ALM 显示位跟随锁存，不能比锁存更早消失。*/
    safety_ack_latched();
    /* N-05：一次性取锁存快照，后续显示位判断都用它——避免"锁外读-判-清"与并发 trip 交错，
     * 把刚置上的 ALM 位清掉。*/
    SAFETY_LOCK();
    lat = s_safety_latch;
    stuck_chn = stuck_chn_latched(lat);     /* 与 lat 同一次快照：涉事通道和锁存位对得上 */
    SAFETY_UNLOCK();
    /* X03-F-02：一连串 &= 合成一个临界区，避免并发 raise 的位被中间态抹掉 */
    ALM_LOCK();
    if (!(lat & SAFETY_BIT_BODY_LOST) && !(stuck_chn & 1u) && g_tcs.body_sensor_ok)
        g_tcs.alarm_bits &= (uint16_t)~ALM_BODY_SENSOR;
    if (!(lat & SAFETY_BIT_M207_LOST) && g_tcs.m207_online)
        g_tcs.alarm_bits &= (uint16_t)~ALM_M207;
    /* 卡死借用的报警位：涉事通道读数还没恢复变化时不清——否则设备停在 SAFE 而屏上说不出原因。
     * R8-01（独立复核 r8f，2026-11-26）：通道归属按 bit1 / bit2 **两路都在**看。原先只认 bit1（TC1），
     * TC2-only 卡死时一次“解除”就把这一位清掉了：锁存仍在 SAFE（正确），屏/网页却一条横幅都不出、
     * 屏还回“已解除”。STUCK 不落盘 → 操作员通常接着断电重启，重启后仍冻着的 TC2 探头不再有保护。*/
    if (!(lat & SAFETY_BIT_M207_LOST) && !g_tcs.m207_sensor_open && !(stuck_chn & (2u | 4u)))
        g_tcs.alarm_bits &= (uint16_t)~ALM_SENSOR_OPEN;
    if (!(lat & SAFETY_BIT_OVERTEMP) && !water_overtemp())   /* r7f R7-08：两路水温 */
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
    /* r7f（独立复核 R7-02 ②，2026-11-26）：模式 = 手动必须与急停锁存一起落盘，不能等 2 秒防抖 + 500ms 冲洗。
     * 急停后这 2.5 秒内掉电重启，NVS 里就是"自动 + 急停锁存"：人工点一次解除，状态机当拍就把 TEC 开回来。
     * 放在 safe_outputs() 之后：先把两路 TCSW=0 发出去，再做这次可能阻塞几十~几百毫秒的 NVS 写。
     * 值没变（已经是手动）时不产生写；写不进时脏位还在，persist_worker 会重试，上电还有 ① 兜底。*/
    (void)param_persist_now();                  /* r7f R7-02 ② */
    /* m1（模式页）：两路手动输出回暂停（输出上面已全停）。急停可能来自别的任务：只登记请求，
     * 由 control_poll 在控制任务里执行，见 s_manual_reset_req。*/
    s_manual_reset_req = 1;
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
    /* m1（模式页）：两路 TEC 手动输出参数——只在手动模式、且不在安全态时可写；拒绝带可区分的原因码
     *（屏端据此提示）。判据在 control_manual.c（纯逻辑，主机可测）。*/
    if (id == PARAM_ID_TEC1_MANUAL || id == PARAM_ID_TEC2_MANUAL)
        return control_manual_check(g_tcs.mode == 0, s_state == CTRL_SAFE, s_safety_latch != 0);
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
    case TCS_CMD_FAN_TOGGLE:
        /* r7f（独立复核 R7-03，2026-11-26）：自动模式 COOL / HEAT 下风扇是"TEC 在输出"的一部分——
         * 拒绝"关风扇"（回与安全态同一个拒绝码；只有真要关的时候才拒，风扇不知为何关着时"开"照收）。*/
        if (g_tcs.fan_on && g_tcs.mode != 0 && (s_state == CTRL_COOL || s_state == CTRL_HEAT)) return -2;
        control_manual_fan(g_tcs.fan_on ? 0 : 1); return 0;
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
        out_tec_sw_ex(1, 0, 1);                  /* P1-09：停机请求的安全关断同样走专门通道 */
        out_tec_sw_ex(2, 0, 1);
        return 0;
    default:                    return -1;
    }
}

int control_apply_cmd(uint8_t cmd) { return control_apply_cmd_src(cmd, PARAM_SRC_CONSOLE); }

void control_on_mode_change(uint8_t m)
{
    (void)m;   /* 手动进出不强制改输出；自动评估在下一拍 control_poll 接管 */
    /* m1（模式页）：切到自动 = 两路手动输出回暂停（第二路关，第一路交给自动状态机）。
     * 模式变更来自链路 / 网页 / 控制台任务：只登记，由 control_poll 在控制任务里执行。*/
    if (m != 0u) s_manual_reset_req = 1;
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
    s_poll_now_ms = now_ms;                /* out_tec_sw 在拍外被调时用它给 TEC 开沿计时 */
    g_tcs.tec_known = tec_known_now();     /* P1-09：快照用的缓存副本，最多晚一拍 */
    if (!s_boot_ms_base_set) {
        s_boot_ms = now_ms;
        s_boot_ms_base_set = 1;
        s_safe_assert_ms = now_ms - CTRL_SAFE_REASSERT_MS;   /* 见下方"锁存期间"注释 */
    }

    /* m1（模式页）：执行别的任务登记的"两路手动输出回暂停"（急停、切到自动）。
     * 安全态里不碰风扇——安全态风扇全开，解除时 leave_safe 另行复原。*/
    if (s_manual_reset_req) {
        s_manual_reset_req = 0;
        control_manual_reset(!(s_safety_latch || s_state == CTRL_SAFE));
    }

    /* R1 P1-04：参数落盘连续失败 → 可见报警（提示性，ack 可清）*/
    if (param_persist_fail_count() > 0) tcs_alarm_raise(ALM_EEPROM);

    /* 过热硬切断（任意模式、任意布防态）：先锁存，再由锁存统一门控执行器。
     * 修复 P0-01：原实现只 tcs_alarm_raise，TEC 继续按状态机输出（UI 却写"已停止加热"）。*/
    if (g_tcs.tc1_act > ALM_OVERTEMP_HARD_C) {
        tcs_alarm_raise(ALM_OVERTEMP);
        safety_trip(SAFETY_BIT_OVERTEMP);
        safety_set_ok(SAFETY_BIT_OVERTEMP, 0);
    } else if (g_tcs.tc2_act > ALM_OVERTEMP_HARD_C) {   /* r7f R7-08：TC2 那一路的水温同样硬切断 */
        tcs_alarm_raise(ALM_OVERTEMP);
        safety_trip(SAFETY_BIT_OVERTEMP);
        safety_set_ok(SAFETY_BIT_OVERTEMP, 0);
    } else {
        safety_set_ok(SAFETY_BIT_OVERTEMP, 1);
    }

    /* 卡死值活性判据（M09-F-09 / X04-F-06）：不受布防门控；与上面的过温硬切断同一位置、同一做法——
     * 先锁存，锁存块当拍进 SAFE。样本喂入放在锁存块之前：已经在 SAFE 里时也要继续跟踪，
     * "读数重新开始变化"是 STUCK 自愈与 ack 的依据。*/
    {
        uint8_t m207_src_ok = (uint8_t)(g_tcs.m207_online && !g_tcs.m207_sensor_open);
        uint8_t tc1_stuck, body_stuck, stuck_chn;
        uint8_t tc2_stuck;                     /* r7f R7-08：TC2 那一路的水温 */
        stuck_feed(&s_stuck_body, g_tcs.body_temp, now_ms, g_tcs.body_sensor_ok, 0u);
        stuck_feed(&s_stuck_tc1, g_tcs.tc1_act, now_ms, m207_src_ok, 1u);
        stuck_feed(&s_stuck_tc2, g_tcs.tc2_act, now_ms, m207_src_ok, 2u);   /* r7f R7-08；R8-06：TC2 单独一路（idx=2），不再与 TC1 共用统计 */
        tc1_stuck  = (uint8_t)(s_tec_cmd_on[0] && stuck_is(&s_stuck_tc1, now_ms, s_tec_on_since[0]));   /* X03-F-08：读命令意图，不读回显 */
        tc1_stuck  = (uint8_t)(tc1_stuck && stuck_water_is(&s_stuck_tc1, now_ms));   /* 水温窗口 10 分钟（2026-11-26）*/
        tc2_stuck  = (uint8_t)(s_tec_cmd_on[1] && stuck_is(&s_stuck_tc2, now_ms, s_tec_on_since[1]));   /* r7f R7-08：TC2 命令开着 */
        tc2_stuck  = (uint8_t)(tc2_stuck && stuck_water_is(&s_stuck_tc2, now_ms));                     /* 同一水温窗口 */
        body_stuck = (uint8_t)(g_tcs.mode != 0 && (s_state == CTRL_HEAT || s_state == CTRL_COOL)
                               && stuck_is(&s_stuck_body, now_ms, s_tec_on_since[0]));
        /* r7f（独立复核 R7-04）：起步盲窗——TEC 开沿那一刻读数如果已经冻满自己的窗口，直接判卡死。
         * R8-03（独立复核 r8f，2026-11-26）：只保留给**水温**两路。体温这一路的预冻判据撤掉了：
         * 体温窗口只有 30 秒，健康读数在稳态本来就可能冻满 30 秒（F2 实测 30–53 秒），自动模式进
         * HEAT 的当拍就会误进 SAFE（报警还写“水温探头断线”）；而且它把 30 秒宽限——唯一给被控
         * 对象留响应时间的机会——从“开 TEC 后 30 秒”提前到了“开 TEC 那一拍”。水温的长窗口
         * （10 分钟）远大于健康读数的连续不变段，保留预冻不会误报，所以不动（K9c）。
         * R9-03（独立复核 R9，2026-11-26）：预冻改成 30 秒确认窗（stuck_water_prefrozen）——
         * 挂起后读数动过一次就撤销，一次都没动才判。*/
        tc1_stuck  = (uint8_t)(tc1_stuck
                               || (stuck_water_prefrozen(0u, &s_stuck_tc1, now_ms)
                                   && stuck_water_is(&s_stuck_tc1, now_ms)));
        tc2_stuck  = (uint8_t)(tc2_stuck
                               || (stuck_water_prefrozen(1u, &s_stuck_tc2, now_ms)
                                   && stuck_water_is(&s_stuck_tc2, now_ms)));
        /* R9-02（独立复核 R9，2026-11-26）：体温这一路先判出来时，水温如果**也已经冻满它自己的
         * 长窗口**（10 分钟，不是 R8-02 撤掉的 30 秒短窗口），同一拍一并记进涉事通道。
         * 别用短窗口：健康水温稳态本来就会冻 30–53 秒（F2 实测），那会让体温一次卡死之后
         * 设备被正常不动的水温按在 SAFE 里（R8-02）。*/
        {
            uint8_t tc1_pre = (uint8_t)(s_tec_cmd_on[0] && s_stuck_tc1.last_change
                                        && stuck_water_is(&s_stuck_tc1, now_ms));
            uint8_t tc2_pre = (uint8_t)(s_tec_cmd_on[1] && s_stuck_tc2.last_change
                                        && stuck_water_is(&s_stuck_tc2, now_ms));
            if (body_stuck) { tc1_stuck |= tc1_pre; tc2_stuck |= tc2_pre; }   /* 报警位随下面同一条路径置 */
        }
        /* S 线 i3（M09-F-09 的注入档口径）：注入源在生效的那一路读数恒等于人给的值，不判卡死（排在预冻之后）。
         * 交付档装配方不接 stuck_exempt，行为不变。TC1 / TC2 同属"水温"注入源（现场档的 water 命令）。*/
        if (body_stuck && stuck_src_injected(1u)) body_stuck = 0;
        if (tc1_stuck  && stuck_src_injected(2u)) tc1_stuck  = 0;
        if (tc2_stuck  && stuck_src_injected(2u)) tc2_stuck  = 0;
        if (tc1_stuck || body_stuck || tc2_stuck) {
            if (body_stuck) tcs_alarm_raise(ALM_BODY_SENSOR);
            if (tc1_stuck || tc2_stuck)  tcs_alarm_raise(ALM_SENSOR_OPEN);   /* 两路水温共用借来的这一位 */
            SAFETY_LOCK();
            /* R8-02（独立复核 r8f，2026-11-26）：r7f R7-04 曾在这里用 30 秒短窗口（tc1_pre）把“体温先判出来时
             * 同拍也没动”的水温一并记进涉事通道。健康读数的稳态本来就会 30–53 秒逐位不变（真机 F2 实测），
             * 于是体温探头一次卡死之后，设备要等**正常不动**的水温动一下才肯退出 SAFE（ack 也不行），
             * 屏上报警还写错通道。现在只由水温自己的判据决定是否记入（长窗口 10 分钟，见 tc1_stuck /
             * tc2_stuck）：真冻住的那一路照样记；注入档的豁免也走 tc1_stuck（stuck_src_injected(2)）。*/
            s_stuck_chn |= (uint8_t)((body_stuck ? 1u : 0u) | (tc1_stuck ? 2u : 0u) | (tc2_stuck ? 4u : 0u));
            /* R9-02：记下这一次锁存时哪一路水温**正被驱动**。锁存期间 TEC 命令全被压成 0，
             * 长窗口复查只能看这两路——没被驱动过的通道（例如从没用过的 TC2）读数冻着与
             * 48°C 硬切断无关，不该被拖进涉事通道。*/
            s_stuck_watch = (uint8_t)((s_tec_cmd_on[0] ? 2u : 0u) | (s_tec_cmd_on[1] ? 4u : 0u));
            SAFETY_UNLOCK();
            safety_trip(SAFETY_BIT_STUCK);
        }
        /* R9-02（独立复核 R9，2026-11-26）：STUCK 锁存期间 TEC 命令已经被压成 0，tc1_stuck / tc2_stuck
         * 都不再成立；但"读数逐位不变满 10 分钟"这件事与 TEC 无关（健康水温实测最长 53 秒，F2）。
         * 锁存期间照样看这两路的长窗口：成立就记进涉事通道 + 报借来的水温位。否则体温那一路一恢复，
         * 锁存（连同涉事通道）直接清掉，输出就对着一个真冻住的水温读数开下去——48°C 硬切断失明。
         * 只记长窗口、不记 30 秒短窗口：后者在健康稳态上就会把设备按在 SAFE 里（R8-02）。*/
        if ((s_safety_latch & SAFETY_BIT_STUCK) && m207_src_ok && !stuck_src_injected(2u)) {
            uint8_t wet = 0;
            if ((s_stuck_watch & 2u) && s_stuck_tc1.last_change && stuck_water_is(&s_stuck_tc1, now_ms)) wet |= 2u;
            if ((s_stuck_watch & 4u) && s_stuck_tc2.last_change && stuck_water_is(&s_stuck_tc2, now_ms)) wet |= 4u;
            if (wet) {
                SAFETY_LOCK();
                s_stuck_chn |= wet;
                SAFETY_UNLOCK();
                tcs_alarm_raise(ALM_SENSOR_OPEN);
            }
        } else if (!(s_safety_latch & SAFETY_BIT_STUCK)) {
            s_stuck_watch = 0;      /* 锁存清掉（ack / 自愈）→ 本次的驱动通道记录一并作废 */
        }
        SAFETY_LOCK();
        stuck_chn = stuck_chn_latched(s_safety_latch);
        SAFETY_UNLOCK();
        safety_set_ok(SAFETY_BIT_STUCK, stuck_recovered(now_ms, stuck_chn));
    }

    /* 锁存期间：任何模式都不再评估，直接停在 SAFE，并按 CTRL_SAFE_REASSERT_MS 重申 TCSW=0。
     * P1-09（i3 收口，2026-11-26）：进入沿的关断与这里的重申都走 ops.tec_force → m207_tec_force：
     * 不占普通队列，模块离线也受理（服务侧记保持位，复联第一帧补发，先于普通队列放行）。
     * 离线期本处的重申不产生帧——它的作用是每 500ms 再敲一次安全关断通道，让服务侧的保持位
     * 一直是"有关断还没送到执行器"；读回到关之后服务侧自己解除保持。*/
    /* 通信类故障自动恢复（见 COMM_AUTO_RECOVER_MS 注释）。必须放在"锁存即返回"之前，
     * 且健康判据直接取 g_tcs（s_safety_ok 在锁存期间不刷新）。
     * 计时器只在"有锁存"期间累计：无锁存时清零，避免上次锁存的时间戳让下一次一进锁存
     * 就满足 2s（单测里连跑多段会踩到，现场表现为"刚跳 SAFE 又自己弹回"）。*/
    if (!s_safety_latch) s_comm_ok_since = 0;
    if (s_safety_latch) {
        uint8_t comm_ok = (uint8_t)(g_tcs.body_sensor_ok && g_tcs.m207_online
                                    && !g_tcs.m207_sensor_open
                                    && !water_overtemp());   /* r7f R7-08：两路水温 */
        /* 通信类位各看各的源：失联类看 comm_ok，STUCK 只看涉事通道的读数是否重新开始变化。
         * 锁存里没有 STUCK 时与原先逐位等价（ready 恒为"全部失联类位"或 0）。*/
        uint8_t stuck_chn;
        uint8_t ready;
        SAFETY_LOCK();
        stuck_chn = stuck_chn_latched(s_safety_latch);
        SAFETY_UNLOCK();
        ready = (uint8_t)((comm_ok ? (uint8_t)(SAFETY_BIT_BODY_LOST | SAFETY_BIT_M207_LOST) : 0u)
                          | (stuck_recovered(now_ms, stuck_chn) ? SAFETY_BIT_STUCK : 0u));
        if ((s_safety_latch & SAFETY_COMM_MASK) != 0
            && (uint8_t)(s_safety_latch & (uint8_t)~ready) == 0) {
            if (now_ms == 0) {
                /* 复核 B3：0 是"时间基准未建立"，不能当时间戳——否则 0-1 溢出成立即自愈 */
            } else if (!s_comm_ok_since) {
                s_comm_ok_since = now_ms;
            } else if ((uint32_t)(now_ms - s_comm_ok_since) >= COMM_AUTO_RECOVER_MS) {
                uint8_t emptied;          /* 这次自愈把锁存清空了（没有急停/超温留着）*/
                SAFETY_LOCK();
                s_safety_latch &= (uint8_t)~SAFETY_COMM_MASK;
                if (!(s_safety_latch & SAFETY_BIT_STUCK)) s_stuck_chn = 0;   /* 涉事通道随锁存位一起清 */
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

    /* r7f（独立复核 R7-03，2026-11-26）：自动模式 COOL / HEAT 下风扇与状态机的输出联动，每拍重申一次。
     * 手动模式本来就有 control_manual_poll 的 fan_link 每拍重申；自动模式原先只在进 COOL / HEAT 那一拍
     * 开一次风扇——网页 / 屏上点一下风扇就把风扇关了，TEC 继续按状态机运行，直到下一次状态迁移，
     * 剩余保护只剩 48°C 硬切断和模块自身保护。放这里：安全态在上面已返回（TEC 命令必为 0）。
     * 只管状态机自己的 COOL / HEAT：自动 IDLE 下网页旧命令开的 TEC2 不在此列（F 模式页 D3 的已知项；
     * W2-20 的"TEC2 复原后风扇归状态机管"也保持不变）。*/
    if (!g_tcs.fan_on && g_tcs.mode != 0 && (s_state == CTRL_COOL || s_state == CTRL_HEAT)) out_fan(1);

    /* 手动模式：状态机旁路（输出保持人工值）*/
    if (g_tcs.mode == 0) {
        goto_state(CTRL_IDLE);
        /* m1（模式页）：两路 TEC 按手动参数驱动（只在变化时下发）+ 风扇联动，见 control_manual.c */
        control_manual_poll();
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

#if CONFIG_TCS_DIAG || CONFIG_TCS_SIM
/* S 线 i3（条目 8）：体温 / 水温读数"逐位不变"的最长连续时长（毫秒；含正在进行的这一段）。
 * which：1 = 体温、2 = TC1 水温、3 = TC2 水温（R8-06：两路水温分开统计，不再混在一起）。
 * 只给 mstat 读，不参与判据；交付档不编译。*/
uint32_t control_stuck_max_ms(uint8_t which, uint32_t now_ms)
{
    uint8_t i;
    const stuck_src_t *c;
    uint32_t mx;
    if (which < 1u || which > 3u) return 0;
    i = (uint8_t)(which - 1u);
    c = (which == 1u) ? &s_stuck_body : (which == 2u) ? &s_stuck_tc1 : &s_stuck_tc2;
    mx = s_stuck_max_ms[i];
    if (c->last_change && now_ms > c->last_change) {
        uint32_t cur = now_ms - c->last_change;
        if (cur > mx) mx = cur;
    }
    return mx;
}
#endif

/* m1（模式页）：快照用的"输出方向"（低半字节第一路、高半字节第二路）。
 *   通道关 → 暂停；自动模式第一路 → 状态机（COOL 制冷 / HEAT 制热）；
 *   手动模式 → 实际已下发的手动方向；通道开着但不是模式页设的（网页 / 台架的旧命令）→ TEC_DIR_EXT。*/
uint8_t control_tec_dir(uint8_t ch)
{
    uint8_t on, d;
    if (ch == 1u)      on = (uint8_t)(g_tcs.tec1_on ? 1u : 0u);
    else if (ch == 2u) on = (uint8_t)(g_tcs.tec2_on ? 1u : 0u);
    else               return TEC_DIR_PAUSE;
    /* i3（P1-09 收口，2026-11-26）：该通道的开关状态不可信（模块离线）→ 暂停。这时 g_tcs.tecN_on
     * 只是本端的命令意图，上一次的方向还在里面，但执行器实际在做什么无从得知。屏另据快照的
     * tec_known 位把这一路画成"未知"。M16；变异 C13。*/
    if (!(tec_known_now() & (uint8_t)(1u << (ch - 1u)))) return TEC_DIR_PAUSE;
    if (!on) return TEC_DIR_PAUSE;
    if (g_tcs.mode != 0u && ch == 1u) {
        if (s_state == CTRL_COOL) return TEC_DIR_COOL;
        if (s_state == CTRL_HEAT) return TEC_DIR_HEAT;
    }
    d = control_manual_dir(ch);
    return (uint8_t)(d ? d : TEC_DIR_EXT);
}

#ifdef TCS_UNIT_TEST
void control_test_trip(uint8_t bit) { safety_trip(bit); }
#endif
