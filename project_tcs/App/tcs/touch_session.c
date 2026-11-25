/**
 * touch_session.c — 触摸会话状态机实现（WP-M2①，纯逻辑零硬件依赖）
 *
 * 状态：IDLE → ACTIVE →(n==0)→ ENDING(宽限) → IDLE
 *   ACTIVE 期间 n==0 仅进入 ENDING（记 t_lift）；宽限内同 id 复线回 ACTIVE（毛刺
 *   容错）；宽限超时或换 id 结算会话（幽灵判定→UP_OK/UP_GHOST）。会话时长止于
 *   抬起时刻（t_lift），宽限/结算延迟不计入（tap 判定口径=用户手感）。
 *   ACTIVE 期间触点 id 连续 3 拍失配 = 原触点被顶替 → 结算旧会话并链式接续
 *   （防手掌长期遮蔽把会话挂死）。
 * 幽灵会话（时长<50ms+零位移+面积恒定+无多点+非链式+非注入）→ UP_GHOST，
 * UI 只清理不合成 tap；其余会话 UP_OK。跨会话零惩罚（无任何基线遗留）。
 *
 * 触摸链路健康与恢复（M02-F-01）：判定在本文件末尾，接口与设计约束见 touch_session.h。
 */
#include "touch_session.h"
#include <string.h>

volatile uint32_t g_ts_ghost_sessions;
volatile uint32_t g_ts_multipoint_drops;
volatile uint32_t g_ts_badsample_drops;
volatile uint32_t g_ts_jump_drops;
volatile uint32_t g_ts_glitch_resume;

#define TS_IDLE   0u
#define TS_ACTIVE 1u
#define TS_ENDING 2u

static int absi(int v) { return v < 0 ? -v : v; }

uint8_t ts_is_ghost(uint32_t dur_ms, int max_disp, uint8_t area0, uint8_t area1,
                    uint8_t multipoint, uint8_t chained, uint8_t injected)
{
    if (injected) return 0;
    if (chained || multipoint) return 0;
    if (dur_ms >= UI_TOUCH_GHOST_MAX_MS) return 0;
    if (max_disp != 0) return 0;
    if (area0 != area1) return 0;   /* 面积恒定项（reg 0x08 不可读时恒 0=恒等成立）*/
    return 1;
}

void ts_init(ts_state_t *st)
{
    st->state = TS_IDLE;
    st->x0 = st->y0 = st->x = st->y = 0;
    st->t_down = 0;
    st->t_lift = 0;
    st->t_last_sample = 0;
    st->max_disp = 0;
    st->contact_id = 0;
    st->mismatch_run = 0;
    st->area0 = 0;
    st->area_last = 0;
    st->multipoint = 0;
    st->chained = 0;
    st->injected_s = 0;
}

const ts_state_t *ts_debug(const ts_state_t *st) { return st; }

/* 会话结算：按幽灵判定填 UP_OK / UP_GHOST；时长止于抬起/最后样本时刻 */
static void ts_settle(ts_state_t *st, ts_out_t *out)
{
    uint32_t dur = st->t_lift - st->t_down;
    if (ts_is_ghost(dur, st->max_disp, st->area0, st->area_last,
                    st->multipoint, st->chained, st->injected_s)) {
        g_ts_ghost_sessions++;
        out->evt = TS_EVT_UP_GHOST;
    } else {
        out->evt = TS_EVT_UP_OK;
    }
    out->x = st->x;
    out->y = st->y;
    out->dur_ms = dur;
    out->max_disp = st->max_disp;
    st->state = TS_IDLE;
}

/* 链式开新会话（换人顶替）：恒非幽灵——真实接触在屏是既成事实 */
static void ts_chain(ts_state_t *st, const ts_sample_t *s, uint32_t now_ms)
{
    st->state = TS_ACTIVE;
    st->t_down = now_ms;
    st->t_lift = now_ms;
    st->t_last_sample = now_ms;
    st->x0 = st->x = s->x;
    st->y0 = st->y = s->y;
    st->max_disp = 0;
    st->contact_id = s->touch_id;
    st->area0 = st->area_last = s->area;
    st->multipoint = 0;
    st->chained = 1;
    st->injected_s = s->injected;
    st->mismatch_run = 0;
}

void ts_feed(ts_state_t *st, const ts_sample_t *s, uint32_t now_ms, ts_out_t *out)
{
    out->evt = TS_EVT_NONE;
    out->x = s->x;
    out->y = s->y;
    out->dur_ms = 0;
    out->max_disp = 0;

    /* 坏包/越界：防御性丢弃并计数，不碰会话（H11 精神）*/
    if (!s->valid) {
        g_ts_badsample_drops++;
        return;
    }

    switch (st->state) {
    case TS_IDLE:
        if (!s->touched) return;
        st->state = TS_ACTIVE;
        st->t_down = now_ms;
        st->t_lift = now_ms;
        st->t_last_sample = now_ms;
        st->x0 = st->x = s->x;
        st->y0 = st->y = s->y;
        st->max_disp = 0;
        st->contact_id = s->touch_id;
        st->area0 = st->area_last = s->area;
        st->multipoint = 0;
        st->chained = 0;
        st->injected_s = s->injected;
        st->mismatch_run = 0;
        out->evt = TS_EVT_DOWN;
        break;

    case TS_ACTIVE:
        if (s->touched && s->contacts > 1u && s->touch_id == st->contact_id) {
            /* T-3：真多点同现（本触点仍在+他人加入）→ 忽略额外接触并计数；
             * 会话毒化（multipoint=1，抬手不再判幽灵——手掌场景保守放清理路径）*/
            g_ts_multipoint_drops++;
            st->multipoint = 1;
            return;
        }
        if (s->touched && s->touch_id != st->contact_id) {
            /* T-3：id 失配 = 第二接触/上报伪影 → 忽略并计数。
             * 连续 3 拍失配 = 原触点已被顶替 → 结算旧会话（按自身证据判幽灵，
             * 不毒化）并链式接续，防手掌长期遮蔽把会话挂死。*/
            g_ts_multipoint_drops++;
            if (++st->mismatch_run < 3u) return;
            ts_settle(st, out);
            ts_chain(st, s, now_ms);
            return;   /* 本拍只出 UP，新会话样本下一拍并入 */
        }
        if (!s->touched) {
            st->state = TS_ENDING;          /* 进宽限，毛刺不立即拆会话 */
            st->t_lift = now_ms;
            return;
        }
        /* 会话内瞬移样本：丢弃该样本、会话继续（防滑条瞬跳）*/
        if (absi(s->x - st->x) > UI_TOUCH_JUMP_PX || absi(s->y - st->y) > UI_TOUCH_JUMP_PX) {
            g_ts_jump_drops++;
            return;
        }
        st->x = s->x;
        st->y = s->y;
        st->t_lift = now_ms;
        st->t_last_sample = now_ms;
        st->mismatch_run = 0;
        if (absi(s->x - st->x0) > st->max_disp) st->max_disp = absi(s->x - st->x0);
        if (absi(s->y - st->y0) > st->max_disp) st->max_disp = absi(s->y - st->y0);
        if (s->injected) st->injected_s = 1;
        st->area_last = s->area;
        out->evt = TS_EVT_MOVE;
        break;

    case TS_ENDING:
        if (s->touched && s->touch_id == st->contact_id) {
            /* 宽限内同 id 复线：毛刺，会话延续 */
            st->state = TS_ACTIVE;
            g_ts_glitch_resume++;
            if (absi(s->x - st->x) <= UI_TOUCH_JUMP_PX && absi(s->y - st->y) <= UI_TOUCH_JUMP_PX) {
                st->x = s->x;
                st->y = s->y;
                st->t_lift = now_ms;
                out->evt = TS_EVT_MOVE;
            }
            return;
        }
        if (s->touched) {
            /* 换人：结算旧会话 → 链式开新会话（UP 事件优先本拍）*/
            ts_settle(st, out);
            ts_chain(st, s, now_ms);
            return;
        }
        /* 宽限窗口内仍空：等毛刺复线；窗口过界：结算 */
        if (now_ms - st->t_lift < UI_TOUCH_LIFT_GRACE_MS) return;
        ts_settle(st, out);
        break;

    default:
        st->state = TS_IDLE;
        break;
    }
}

/* ====== 触摸链路健康与恢复（M02-F-01；纯逻辑，驱动层执行动作）====== */

/* 一轮恢复失败：退避。离线起点 t_offline_ms 不动（离线时长从掉线那一刻算）。*/
static void tp_link_backoff(tp_link_t *st, uint32_t now_ms)
{
    st->state      = (uint8_t)TP_LINK_WAIT;
    st->t_mark_ms  = now_ms;
}

/* 掉线：退避 + 记离线起点。恢复成功时用 now - t_offline_ms 结算离线时长。*/
static void tp_link_offline(tp_link_t *st, uint32_t now_ms)
{
    tp_link_backoff(st, now_ms);
    st->t_offline_ms = now_ms;
}

static void tp_link_back_online(tp_link_t *st, uint32_t now_ms)
{
    uint32_t dur = now_ms - st->t_offline_ms;
    st->state  = (uint8_t)TP_LINK_ONLINE;
    st->streak = 0;
    st->recoveries++;
    st->offline_last_ms = dur;
    if (dur > st->offline_max_ms) st->offline_max_ms = dur;
}

void ts_tp_link_init(tp_link_t *st, int online, uint32_t now_ms)
{
    memset(st, 0, sizeof(*st));
    if (online) st->state = (uint8_t)TP_LINK_ONLINE;
    else        tp_link_offline(st, now_ms);
}

int ts_tp_link_poll(tp_link_t *st, int ok, uint32_t now_ms)
{
    if (ok) {
        st->streak = 0;
        return 0;
    }
    if (st->streak < 255u) st->streak++;      /* 饱和：不回绕成 0 */
    st->fail_total++;
    if (st->state != (uint8_t)TP_LINK_ONLINE) return 0;   /* 已离线：只记账 */
    if (st->streak < TP_FAIL_MAX) return 0;
    tp_link_offline(st, now_ms);
    return 1;
}

void ts_tp_link_force_offline(tp_link_t *st, uint32_t now_ms)
{
    if (st->state == (uint8_t)TP_LINK_ONLINE) tp_link_offline(st, now_ms);
}

tp_link_act_t ts_tp_link_step(tp_link_t *st, int prev_ok, uint32_t now_ms)
{
    /* 刚发出的动作，结果留到下一毫秒再消费：每拍最多走一步由这里保证，不看调用方节拍。*/
    int fresh = (now_ms == st->t_mark_ms);

    switch ((tp_link_state_t)st->state) {
    case TP_LINK_ONLINE:
        return TP_ACT_NONE;

    case TP_LINK_WAIT:
        if ((uint32_t)(now_ms - st->t_mark_ms) < TP_RETRY_DELAY_MS) return TP_ACT_NONE;
        st->recover_tries++;
        st->state     = (uint8_t)TP_LINK_BUS_CLEAR;
        st->t_mark_ms = now_ms;
        return TP_ACT_BUS_CLEAR;

    /* 清总线没过 = 两线还被拉死：不去碰 I2C 外设，退避后再试。
     * 下一轮恢复的第一步永远从这里走，所以卡死的总线上不会出现任何 HAL I2C 事务。*/
    case TP_LINK_BUS_CLEAR:
        if (fresh) return TP_ACT_NONE;
        if (!prev_ok) { tp_link_backoff(st, now_ms); return TP_ACT_NONE; }
        st->state     = (uint8_t)TP_LINK_PERIPH;
        st->t_mark_ms = now_ms;
        return TP_ACT_PERIPH_INIT;

    case TP_LINK_PERIPH:
        if (fresh) return TP_ACT_NONE;
        if (!prev_ok) { tp_link_backoff(st, now_ms); return TP_ACT_NONE; }
        st->state     = (uint8_t)TP_LINK_RST_LOW;
        st->t_mark_ms = now_ms;
        return TP_ACT_RST_LOW;

    case TP_LINK_RST_LOW:                    /* 等够 TP_RST_LOW_MS 再释放，期间每拍立即返回 */
        if ((uint32_t)(now_ms - st->t_mark_ms) < TP_RST_LOW_MS) return TP_ACT_NONE;
        st->state     = (uint8_t)TP_LINK_RST_HIGH;
        st->t_mark_ms = now_ms;
        return TP_ACT_RST_HIGH;

    case TP_LINK_RST_HIGH:                   /* 等 ROM 加载 */
        if ((uint32_t)(now_ms - st->t_mark_ms) < TP_RST_HIGH_MS) return TP_ACT_NONE;
        st->state     = (uint8_t)TP_LINK_CFG;
        st->t_mark_ms = now_ms;
        return TP_ACT_CFG;

    case TP_LINK_CFG:
        if (fresh) return TP_ACT_NONE;
        if (!prev_ok) { tp_link_backoff(st, now_ms); return TP_ACT_NONE; }
        st->state     = (uint8_t)TP_LINK_VERIFY;
        st->t_mark_ms = now_ms;
        return TP_ACT_VERIFY;

    case TP_LINK_VERIFY:
        if (fresh) return TP_ACT_NONE;
        if (!prev_ok) { tp_link_backoff(st, now_ms); return TP_ACT_NONE; }
        tp_link_back_online(st, now_ms);
        return TP_ACT_NONE;

    default:                                 /* 状态字损坏：按掉线处理，从退避重来 */
        tp_link_offline(st, now_ms);
        return TP_ACT_NONE;
    }
}
