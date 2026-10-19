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
 */
#include "touch_session.h"

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
