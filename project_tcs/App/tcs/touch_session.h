/**
 * touch_session.h — 触摸会话状态机（WP-M2，纯逻辑可 PC 单测）
 *
 * 任务书 v10.11 M2①：触摸服务围绕"会话"重写——
 *   - 幽灵会话 = 时长 <UI_TOUCH_GHOST_MAX_MS + 零位移（+面积恒定）→ 整会话丢弃，
 *     跨会话绝不惩罚（取代 H1 帧级 200px 过滤的跨会话误杀，M0①）；
 *   - 活动会话期间第二接触一律忽略并计数（T-3 多点/手掌策略）；
 *   - 会话内单样本瞬移 >UI_TOUCH_JUMP_PX 丢弃该样本、会话继续（防滑条瞬跳）；
 *   - 抬起宽限 UI_TOUCH_LIFT_GRACE_MS：单帧 n==0 毛刺不拆会话；
 *   - 坏包/越界样本防御性丢弃并计数（H11 精神），不影响会话归属；
 *   - 注入样本（injected=1）豁免幽灵判定（T-13：注入走功能路径，不模拟幽灵）。
 *
 * 事件语义（对 UI）：
 *   TS_EVT_DOWN      新会话按下（立即上报，按压高亮零延迟）
 *   TS_EVT_MOVE      会话内样本（每轮询拍一次，含原地样本）
 *   TS_EVT_UP_OK     真实会话抬起（UI 可合成 tap/提交滑条）
 *   TS_EVT_UP_GHOST  幽灵会话抬起（UI 只做清理：灭高亮/回弹滑条，严禁 tap）
 */
#ifndef TOUCH_SESSION_H
#define TOUCH_SESSION_H

#include <stdint.h>
#include "ui_layout.h"

/* 触摸会话参数唯一来源 = ui_layout.h「触摸会话参数」节（禁局部魔法数）*/

/* ===== 输入样本（ft5446.c 映射后的屏幕坐标）===== */
typedef struct {
    uint8_t touched;    /* 1=有触点 */
    uint8_t valid;      /* 0=坏包/越界（已计数，会话机忽略）*/
    uint8_t injected;   /* 1=注入样本（豁免幽灵判定，恒 UP_OK）*/
    uint8_t contacts;   /* 原始触点数 n */
    uint8_t touch_id;   /* 触点 id（buf[2] 高 4 位）*/
    uint8_t area;       /* 触点面积（reg 0x08，可读则参与幽灵判定）*/
    int     x, y;       /* 屏幕像素（已映射）*/
} ts_sample_t;

typedef enum {
    TS_EVT_NONE = 0,
    TS_EVT_DOWN,
    TS_EVT_MOVE,
    TS_EVT_UP_OK,
    TS_EVT_UP_GHOST
} ts_evt_t;

typedef struct {
    ts_evt_t evt;
    int x, y;
    uint32_t dur_ms;    /* UP 时：会话时长 */
    int max_disp;       /* UP 时：会话内最大位移（px，切比雪夫）*/
} ts_out_t;

typedef struct {
    uint8_t  state;         /* 0=IDLE 1=ACTIVE 2=ENDING(抬起宽限) */
    uint32_t t_down;
    uint32_t t_lift;        /* 抬起时刻（时长口径=用户手感，不含宽限）*/
    uint32_t t_last_sample;
    int      x0, y0;        /* 按下点 */
    int      x, y;          /* 最近样本点 */
    int      max_disp;
    uint8_t  contact_id;
    uint8_t  mismatch_run;  /* 连续 id 失配拍数（≥3 判顶替→链式）*/
    uint8_t  area0;         /* 按下时面积（幽灵判定：面积恒定项）*/
    uint8_t  area_last;     /* 最后样本面积（与 area0 比对）*/
    uint8_t  multipoint;    /* 会话内见过第二接触 */
    uint8_t  chained;       /* 链式接续（前会话触点换人）→ 恒非幽灵 */
    uint8_t  injected_s;    /* 本会话含注入样本 */
} ts_state_t;

/* ===== 诊断计数（console touch log stat 可见）===== */
extern volatile uint32_t g_ts_ghost_sessions;   /* 幽灵会话丢弃数 */
extern volatile uint32_t g_ts_multipoint_drops; /* 第二接触忽略数（T-3）*/
extern volatile uint32_t g_ts_badsample_drops;  /* 坏包/越界样本丢弃数 */
extern volatile uint32_t g_ts_jump_drops;       /* 会话内瞬移样本丢弃数 */
extern volatile uint32_t g_ts_glitch_resume;    /* 抬起宽限内复线数 */

void ts_init(ts_state_t *st);
const ts_state_t *ts_debug(const ts_state_t *st);   /* 诊断只读视图 */
void ts_feed(ts_state_t *st, const ts_sample_t *s, uint32_t now_ms, ts_out_t *out);

/* 纯函数：会话幽灵判定（M3 单测直接覆盖；条件全满足=幽灵）
 * dur<上限 && max_disp==0 && 面积恒定 && 无多点 && 非链式 && 非注入 */
uint8_t ts_is_ghost(uint32_t dur_ms, int max_disp, uint8_t area0, uint8_t area1,
                    uint8_t multipoint, uint8_t chained, uint8_t injected);

#endif /* TOUCH_SESSION_H */
