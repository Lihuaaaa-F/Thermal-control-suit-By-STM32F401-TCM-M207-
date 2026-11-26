/**
 * post.c — 上电自检 + 动态布防三态机实现（H6）
 *
 * 纯逻辑区（PC 单测直接编译）：合理性滤波 / 三态机 / 结果聚合。
 * 目标机区：post_init 在 control_init/m207_init/tmp117_init 之后调用（读各驱动
 * 探测结果）；post_poll 每拍进主循环——M207 宽限内等首联，裁决后角标显示 3s，
 * 每 100ms 步进布防 FSM。alarm/SAFE 仍由 control_poll 统一裁决（单一裁决点）。
 */
#include "post.h"

/* ===================================================================== */
/*                       纯逻辑（PC 单测覆盖）                             */
/* ===================================================================== */

#define POST_BODY_MIN_C    25.0f
#define POST_BODY_MAX_C    45.0f
#define POST_BODY_MAX_RATE 2.0f    /* °C/s */
#define POST_M207_MAX_RATE 10.0f   /* °C/s */

uint8_t post_body_sanity_ok(float prev, float cur, float dt_s)
{
    if (cur < POST_BODY_MIN_C || cur > POST_BODY_MAX_C) return 0;
    if (dt_s <= 0.0f) return 1;                       /* 首样本只查范围 */
    float d = cur - prev;
    if (d < 0) d = -d;
    return (d <= POST_BODY_MAX_RATE * dt_s) ? 1u : 0u;
}

uint8_t post_m207_sanity_ok(float prev, float cur, float dt_s)
{
    if (dt_s <= 0.0f) return 1;
    float d = cur - prev;
    if (d < 0) d = -d;
    return (d <= POST_M207_MAX_RATE * dt_s) ? 1u : 0u;
}

void post_arm_init(post_arm_ctx_t *c)
{
    c->state = (uint8_t)POST_ARM_UNARMED_ABSENT;      /* 上电默认不布防，auto 由 FSM 升入 */
    c->healthy_run = 0;
}

post_arm_t post_arm_step(post_arm_ctx_t *c, uint8_t mode08, uint8_t post_pass,
                         uint8_t healthy, uint8_t acked)
{
    if (mode08 == 2u) {                               /* 强制不布防（安静）*/
        c->state = (uint8_t)POST_ARM_UNARMED_ABSENT;
        c->healthy_run = 0;
        return (post_arm_t)c->state;
    }

    switch (c->state) {
    case (uint8_t)POST_ARM_UNARMED_ABSENT:
        if (mode08 == 1u || (post_pass && healthy)) { /* 强制布防 / auto 首次升入 */
            c->state = (uint8_t)POST_ARM_ARMED_ONLINE;
            c->healthy_run = 0;
        }
        break;

    case (uint8_t)POST_ARM_ARMED_ONLINE:
        if (!healthy) {                               /* 失联沿 → LOST_ALARM */
            c->state = (uint8_t)POST_ARM_LOST_ALARM;
            c->healthy_run = 0;
        }
        break;

    case (uint8_t)POST_ARM_LOST_ALARM:
        if (healthy) {
            if (c->healthy_run < 255u) c->healthy_run++;
        } else {
            c->healthy_run = 0;
        }
        /* 上线补入：人工清报警 且 连续 10 次健康采样 */
        if (acked && c->healthy_run >= POST_ARM_RECOVER_RUN) {
            c->state = (uint8_t)POST_ARM_ARMED_ONLINE;
            c->healthy_run = 0;
        }
        break;

    default:
        c->state = (uint8_t)POST_ARM_UNARMED_ABSENT;
        break;
    }
    return (post_arm_t)c->state;
}

/* ====== 结果聚合 ====== */
static uint8_t s_result[POST_ITEM_N];
static uint8_t s_decided[POST_ITEM_N];

void post_reset_results(void)
{
    for (int i = 0; i < POST_ITEM_N; i++) { s_result[i] = 0; s_decided[i] = 0; }
}

void post_set_result(uint8_t item, uint8_t ok)
{
    if (item >= POST_ITEM_N) return;
    s_result[item] = ok ? 1u : 0u;
    s_decided[item] = 1u;
}

uint8_t post_item_ok(uint8_t item)
{
    return (item < POST_ITEM_N) ? s_result[item] : 0u;
}

uint8_t post_item_decided(uint8_t item)
{
    return (item < POST_ITEM_N) ? s_decided[item] : 0u;
}

uint8_t post_all_pass(void)
{
    for (int i = 0; i < POST_ITEM_N; i++)
        if (!s_decided[i] || !s_result[i]) return 0;
    return 1;
}

/* ====== 布防状态（单一存储；目标机 poll 步进后 commit，测试直写）====== */
static post_arm_t s_cur_state = POST_ARM_UNARMED_ABSENT;

void post_arm_commit(post_arm_t st) { s_cur_state = st; }
post_arm_t post_arm_state(void)     { return s_cur_state; }
uint8_t    post_armed(void)         { return (s_cur_state != POST_ARM_UNARMED_ABSENT); }

/* ====== 纯逻辑 5：单项宽限重试（M06-F-12；口径见 post.h）====== */
void post_retry_init(post_retry_t *r, uint32_t now_ms)
{
    r->t0_ms = now_ms;          /* 宽限窗从上电首读那一刻算 */
    r->t_try_ms = now_ms;
    r->tries = 1;
}

uint8_t post_retry_due(const post_retry_t *r, uint32_t now_ms)
{
    return ((uint32_t)(now_ms - r->t_try_ms) >= POST_RETRY_GAP_MS) ? 1u : 0u;
}

uint8_t post_retry_judge(post_retry_t *r, uint8_t item, int ok, uint32_t now_ms)
{
    r->t_try_ms = now_ms;
    if (r->tries < 255u) r->tries++;
    if (ok) {
        post_set_result(item, 1);            /* 窗内成功即 PASS */
        return 1u;
    }
    if ((uint32_t)(now_ms - r->t0_ms) >= POST_RETRY_WAIT_MS) {
        post_set_result(item, 0);            /* 窗到点仍失败 */
        return 1u;
    }
    return 0u;                               /* 还在窗内：不裁决，下次再试 */
}

/* ===================================================================== */
/*                          目标机驱动                                    */
/* ===================================================================== */
#ifndef TCS_UNIT_TEST

#include "param_registry.h"
#include "control.h"        /* ALM_*、tcs_alarm_active */
#include "tcs_proto.h"      /* TCS_PROTO_VER、ALM_* 权威定义 */
#include "ltdc.h"
#include "ft5446.h"
#include "tmp117.h"
#include "m207.h"
#include "tcs_profile.h"    /* TCS_H743_LINK_ONLY：交付档不探测本地 TMP117（P1-06 残余）*/
#include "segger_rtt.h"
#include <stdio.h>              /* snprintf：F16 浮点/长度修饰符日志预格式化 */
#include <string.h>

#define POST_ARM_TICK_MS  100u     /* 布防 FSM 步进节拍（健康采样粒度）*/
#define POST_BADGE_MS     3000u    /* 角标显示 3s 后淡出 */

static post_arm_ctx_t s_arm;
static post_retry_t   s_retry_tp;  /* TMP117：上电首读没过时的宽限重试账本（M06-F-12）*/
static uint32_t s_t0_ms;
static uint8_t  s_done;            /* 六项全部裁决完成 */
static uint32_t s_badge_t0;        /* 角标起始时刻 */
static uint8_t  s_badge_state;     /* 0 未开始 1 显示中 2 已淡出 */
static uint8_t  s_badge_rearm;     /* `post badge` 重显请求（poll 内重锚时刻）*/
static uint32_t s_badge_dur = POST_BADGE_MS;  /* 本次显示时长（默认 3s）*/

uint8_t    post_done(void)        { return s_done; }
uint8_t    post_badge_state(void) { return s_badge_state; }

uint32_t post_result_mask(void)
{
    uint32_t m = 0;
    for (int i = 0; i < POST_ITEM_N; i++) if (s_result[i]) m |= (1u << i);
    return m;
}

uint32_t post_done_mask(void)
{
    uint32_t m = 0;
    for (int i = 0; i < POST_ITEM_N; i++) if (s_decided[i]) m |= (1u << i);
    return m;
}

#ifdef TCS_SIM
/* 只在 TCS_SIM 构建：唯一的调用者是 console.c 里同样带 TCS_SIM 的 `sim post fail`。*/
void post_sim_fail_item(uint8_t item)
{
    if (item < POST_ITEM_N) {
        post_set_result(item, 0);
        SEGGER_RTT_printf(0, "[SIM] post item %d forced FAIL\n", item);
    }
}
#endif

/* 重显自检角标（console `post badge [ms]`；彩排/出厂复查/取证用。0=默认 3s）*/
void post_badge_reshow(uint32_t ms)
{
    if (!s_done) return;
    s_badge_dur = ms ? ms : POST_BADGE_MS;
    s_badge_rearm = 1;   /* 时刻由下次 post_poll 以当前 now 重锚 */
    /* F16：自研 RTT printf 无 'l' 长度修饰符（%lu 落 default 吐字面量），预格式化 */
    char b[96];
    snprintf(b, sizeof b, "[POST] badge reshow %lu ms\n", (unsigned long)s_badge_dur);
    SEGGER_RTT_WriteString(b);
}

/* 参数表自洽：序列化 CRC 重算一致 + 记录数与持久化标志一致 + 当前值都在 [min,max] */
static int post_table_check(void)
{
    uint8_t img[EE_IMG_SIZE];
    if (param_serialize(img) != 0) return 0;
    extern uint8_t proto_crc8(const uint8_t *data, size_t len);
    if (proto_crc8(img, EE_CRC_ADDR) != img[EE_CRC_ADDR]) return 0;
    int n = img[2];
    int npersist = 0;
    for (int i = 0; i < param_count(); i++) {
        const param_desc_t *p = param_at(i);
        if (!p) return 0;
        if (p->flags & PF_PERSIST) npersist++;
        if (!(p->value >= p->min && p->value <= p->max)) return 0;
        if (!(p->min < p->max)) return 0;
    }
    return (n == npersist) ? 1 : 0;
}

void post_init(int eeprom_cfg_invalid)
{
    post_reset_results();
    post_arm_init(&s_arm);
    s_done = 0;
    s_badge_state = 0;
    s_t0_ms = 0;

    /* 1. TMP117 Device ID（0x0F == 0x0117；TCS_SIM 假体温源恒过）*/
#ifdef TCS_SIM
    post_set_result(POST_ITEM_TMP117, 1);
#elif TCS_H743_LINK_ONLY
    /* 交付档（v2）：体温传感器在主控侧，本机不探测（I2C1 也不初始化，P1-06 残余）。
     * 该项固定记 FAIL——交付档不跑布防，它只出现在 post 命令的显示里。*/
    post_set_result(POST_ITEM_TMP117, 0);
#else
    /* 台架/仿真档：这里只读一次。没过不急着裁决——慢启动的传感器会在微秒级内连吃
     * 几次 NAK（M06-F-12 原实现：3 次读之间没有延时，当场永久判 FAIL）。
     * 留给 post_poll 在宽限窗内每隔 POST_RETRY_GAP_MS 再试。*/
    {
        uint16_t devid = 0;
        if (tmp117_read_devid(&devid) == 0 && devid == 0x0117u) {
            post_set_result(POST_ITEM_TMP117, 1);
        } else {
            post_retry_init(&s_retry_tp, HAL_GetTick());
        }
    }
#endif

    /* 2. M207 一拍：宽限窗内裁决（post_poll）——先占位不记 decided */

    /* 3. EEPROM：main 已判（离线/镜像 CRC 坏都算 FAIL，不阻塞）*/
    post_set_result(POST_ITEM_EEPROM, (uint8_t)(eeprom_cfg_invalid ? 0 : 1));

    /* 4. 参数表 */
    post_set_result(POST_ITEM_TABLE, (uint8_t)post_table_check());

    /* 5. 屏+触（真实结果：触离线即 FAIL，不粉饰）*/
    post_set_result(POST_ITEM_SCRTOUCH, (uint8_t)(lcd_ready() && touch_ready()));

    /* 6. 版本：协议版本合法 + 固件 BCD 非零 + 注册表非空
     * m1"模式"页（2026-11-26）：协议版本递增到 2（0x20 快照追加输出方向与电压电流），随 protocol/tcs_proto.h 同改 */
    post_set_result(POST_ITEM_VER,
                    (uint8_t)((TCS_PROTO_VER == 2u) && g_tcs.fw_ver != 0u && param_count() > 0));
}

void post_poll(uint32_t now_ms)
{
    if (s_t0_ms == 0) s_t0_ms = now_ms ? now_ms : 1u;

    /* TMP117：上电首读没过的，在宽限窗内按间隔重试（M06-F-12）*/
    if (!s_decided[POST_ITEM_TMP117] && post_retry_due(&s_retry_tp, now_ms)) {
        uint16_t devid = 0;
        int ok = (tmp117_read_devid(&devid) == 0 && devid == 0x0117u);
        (void)post_retry_judge(&s_retry_tp, POST_ITEM_TMP117, ok, now_ms);
    }

    /* M207 一拍裁决：在线即刻过；宽限 2.5s 到点仍离线记 FAIL */
    if (!s_decided[POST_ITEM_M207]) {
        if (g_tcs.m207_online) {
            post_set_result(POST_ITEM_M207, 1);
        } else if (now_ms - s_t0_ms >= POST_M207_WAIT_MS) {
            post_set_result(POST_ITEM_M207, 0);
        }
    }

    if (!s_done && post_done_mask() == (1u << POST_ITEM_N) - 1u) {
        s_done = 1;
        s_badge_t0 = now_ms;
        s_badge_state = 1;
        SEGGER_RTT_printf(0, "[POST] done pass=0x%02x (fail=0x%02x)\n",
                          (unsigned)post_result_mask(),
                          (unsigned)(~post_result_mask() & ((1u << POST_ITEM_N) - 1u)));
    }

    /* 角标 3s 淡出（RGB565 无 alpha：顶栏重绘后即消失（旧 tcs_ui 已删，LVGL 横幅由 tui 重建））*/
    if (s_badge_rearm) {
        s_badge_rearm = 0;
        s_badge_t0 = now_ms;
        s_badge_state = 1;
    }
    if (s_badge_state == 1 && now_ms - s_badge_t0 >= s_badge_dur) {
        s_badge_state = 2;
    }

    /* 布防 FSM 100ms 步进 */
    static uint32_t last_step = 0;
    if (now_ms - last_step < POST_ARM_TICK_MS) return;
    last_step = now_ms;

    uint8_t healthy = (g_tcs.body_sensor_ok && g_tcs.m207_online) ? 1u : 0u;
    uint8_t acked = (!(g_tcs.alarm_bits & ALM_BODY_SENSOR) && !(g_tcs.alarm_bits & ALM_M207)
                     && !(g_tcs.alarm_bits & ALM_SENSOR_OPEN)) ? 1u : 0u;
    uint8_t mode08 = (uint8_t)param_get(PARAM_ID_COMMISSIONED);
    post_arm_t prev = (post_arm_t)s_arm.state;
    post_arm_t st = post_arm_step(&s_arm, mode08, post_all_pass(), healthy, acked);
    post_arm_commit(st);
    if (st == POST_ARM_ARMED_ONLINE && prev == POST_ARM_LOST_ALARM)
        SEGGER_RTT_WriteString("[POST] arm LOST->ARMED (10 healthy samples)\n");
    else if (st == POST_ARM_LOST_ALARM && prev == POST_ARM_ARMED_ONLINE)
        SEGGER_RTT_WriteString("[POST] arm ARMED->LOST (source lost)\n");
    else if (st == POST_ARM_ARMED_ONLINE && prev == POST_ARM_UNARMED_ABSENT)
        SEGGER_RTT_WriteString("[POST] arm UNARMED->ARMED (post pass + sources healthy)\n");
}

#endif /* !TCS_UNIT_TEST */
