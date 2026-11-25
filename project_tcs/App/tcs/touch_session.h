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

/* ====== 触摸链路健康与恢复（M02-F-01；纯逻辑，PC 单测直接覆盖）======
 * 背景：触摸总线一死（排线接触不良 / ESD / 从机拉死 SCL），原来既不恢复也不报告——
 * 读失败被当成"合法无触摸"，屏端急停入口随之失灵且无人知晓。
 * 这里只做判定：何时判离线、离线后每一拍该做哪一步。总线动作由驱动层（ft5446.c）执行。
 *
 * 设计约束（改之前先看这两条）：
 *   1. 判离线之后 ft5446_read() 不再碰总线。卡死总线上每次 HAL 读都要等 25ms 超时，
 *      一拍两次读 = 主循环节拍从 10ms 拖到 50ms 以上，而两次急停判定之间的间隔就是急停响应上界。
 *   2. 恢复流程拆成小步，每拍最多执行一步；步与步之间的延时（IC 复位 20ms + ROM 加载 300ms）
 *      靠"等时间到"实现，不阻塞——延时期间 ft5446_service() 立即返回，主循环节拍不变。
 *
 * 调用约定（驱动层每拍一次）：
 *      act = ts_tp_link_step(st, prev_ok, now_ms);   // prev_ok = 上一次返回的动作执行得如何
 *      prev_ok = 执行(act);                            // act == TP_ACT_NONE 时什么都不做
 * 在线时每次读轮询之后：ts_tp_link_poll(st, 读成功, now_ms)，返回 1 = 本次调用把链路判为离线。
 * 所有用到时间的比较都是 (uint32_t)(now - mark) 的差值，毫秒计数回绕安全。 */

#define TP_FAIL_MAX         3u     /* 在线时连续失败轮询数达到此值 → 判离线 */
#define TP_RETRY_DELAY_MS   100u   /* 一轮恢复失败（或刚判离线）后，到下一轮之间的退避 */
#define TP_RST_LOW_MS       20u    /* 恢复时触摸 IC 的 RST 拉低时长（同上电初始化）*/
#define TP_RST_HIGH_MS      300u   /* RST 释放后等 ROM 加载（同上电初始化）*/

typedef enum {
    TP_LINK_ONLINE = 0,    /* 在线：读正常走总线 */
    TP_LINK_WAIT,          /* 离线退避：等 TP_RETRY_DELAY_MS 再来一轮 */
    TP_LINK_BUS_CLEAR,     /* 恢复① 已发"清总线"，等结果 */
    TP_LINK_PERIPH,        /* 恢复② 已发"I2C 外设复位重配"，等结果 */
    TP_LINK_RST_LOW,       /* 恢复③ RST 已拉低，等 TP_RST_LOW_MS */
    TP_LINK_RST_HIGH,      /* 恢复④ RST 已释放，等 TP_RST_HIGH_MS */
    TP_LINK_CFG,           /* 恢复⑤ 已发"重写配置寄存器"，等结果 */
    TP_LINK_VERIFY         /* 恢复⑥ 已发"回读核对"，等结果：过=回在线，不过=退避 */
} tp_link_state_t;

typedef enum {
    TP_ACT_NONE = 0,       /* 本拍无事（在线，或在等延时）*/
    TP_ACT_BUS_CLEAR,      /* SCL 打 9 个脉冲 + STOP；两线都回高才算成功 */
    TP_ACT_PERIPH_INIT,    /* I2C 外设复位 + 重配 */
    TP_ACT_RST_LOW,        /* 触摸 IC 的 RST 拉低 */
    TP_ACT_RST_HIGH,       /* 触摸 IC 的 RST 释放 */
    TP_ACT_CFG,            /* 重写 4 个配置寄存器，全部成功才算成功 */
    TP_ACT_VERIFY          /* 回读核对（ft5446_probe_online），一致才算成功 */
} tp_link_act_t;

typedef struct {
    uint8_t  state;            /* tp_link_state_t */
    uint8_t  streak;           /* 连续失败轮询数（成功清零；饱和在 255）*/
    uint32_t t_mark_ms;        /* 进入当前状态 / 发出上一步的时刻 */
    uint32_t t_offline_ms;     /* 最近一次判离线的时刻 */
    uint32_t fail_total;       /* 累计失败轮询数 */
    uint32_t recover_tries;    /* 启动过的恢复轮数 */
    uint32_t recoveries;       /* 恢复成功次数（离线 → 在线）*/
    uint32_t offline_last_ms;  /* 最近一次离线持续了多久 */
    uint32_t offline_max_ms;   /* 最长一次离线持续了多久 */
} tp_link_t;

void          ts_tp_link_init(tp_link_t *st, int online, uint32_t now_ms);
int           ts_tp_link_poll(tp_link_t *st, int ok, uint32_t now_ms);
tp_link_act_t ts_tp_link_step(tp_link_t *st, int prev_ok, uint32_t now_ms);
/* 台架钩子用：不经过失败计数，直接把在线的链路打成离线（走同一条恢复流程）。离线时无事。*/
void          ts_tp_link_force_offline(tp_link_t *st, uint32_t now_ms);

#endif /* TOUCH_SESSION_H */
