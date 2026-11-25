/**
 * tui.h — 实时 UI 层：页管理 / 状态条 / 报警横幅 / 弹窗 / 提示条
 *
 * 结构（U 线 S2–S4）：
 *   - 纯逻辑在 ui_model.c（连接视图、横幅选型、确认反馈、主页差分），主机可测；
 *   - 绘图原语在 ui_common.c（真按钮、状态胶囊、实测排版）；
 *   - 每页提供 build()（建一次）与 update()（每 UI_TICK_MS 就地刷新；返回 1 表示需要整页重建）；
 *   - tui_tick() 由主循环每拍调用（LVGL 回调上下文之外）：延后动作 → 就地刷新 → 横幅 → 提示。
 * 切页/整页重建只在"结构性变化"时发生；状态与数值变化都走就地更新，不再整页重绘。
 */
#ifndef TUI_H
#define TUI_H

#include "lvgl.h"
#include "param_registry.h"
#include "ui_model.h"
#include "tcs_profile.h"   /* TCS_PROFILE：诊断页与走查钩子只在台架/仿真档（2026-11-24 S 线 C01）*/

typedef enum { TUI_HOME = 0, TUI_TUNE, TUI_CHART, TUI_CONN, TUI_DIAG } tui_page_t;

#define UI_TICK_MS   200u     /* 刷新节拍：状态最坏 200ms + 一帧上屏 */

void        tui_init(void);
void        tui_tick(void);              /* 主循环每拍调用 */
void        tui_goto(tui_page_t p);      /* 立即切页（只在主循环上下文调用）*/
void        tui_request(tui_page_t p);   /* 延后切页（LVGL 回调里用）*/
void        tui_request_rebuild(void);   /* 延后重建当前页 */
tui_page_t  tui_current(void);
int         tui_last_was_rebuild(void);  /* 读取并清零"上一拍做过整页重建"（预算归因用）*/

/* 当前输入与连接视图（tui_tick 每拍刷新一次；页面 update() 从这里取）*/
void tui_input(const ui_in_t **in, const ui_link_view_t **lv);

/* ---- 页面接口（各页文件实现）----
 * 二级页另有 set_banner_room(room)：横幅占 y 64..120，会压住页面内容；room=1 时页面把内容
 * 挪开（只改位置，不重建对象），room=0 时挪回。由 tui.c 在横幅出现/消失时调用。*/
void tui_p0_build(lv_obj_t *scr);   int tui_p0_update(void);   /* 主页 */
void tui_p1_build(lv_obj_t *scr);   int tui_p1_update(void);   void tui_p1_set_banner_room(int room);
void tui_p2_build(lv_obj_t *scr);   int tui_p2_update(void);   void tui_p2_set_banner_room(int room);
void tui_p3_build(lv_obj_t *scr);   int tui_p3_update(void);   void tui_p3_set_banner_room(int room);
#if TCS_PROFILE >= 1
void tui_diag_build(lv_obj_t *scr, int mode);                   /* 诊断页（只走查用；交付档不编译）*/
void tui_diag_set_mode(int mode);
#endif

/* ---- 底栏急停键的屏幕矩形：主页按钮、按下态兜底（tcs_lvgl.c）共用这一处，防两处漂移 ---- */
#define TUI_ESTOP_X  768
#define TUI_ESTOP_Y  464
#define TUI_ESTOP_W  216
#define TUI_ESTOP_H  112

/* ---- 弹窗 ---- */
typedef enum { TUI_DLG_NONE = 0, TUI_DLG_ESTOP, TUI_DLG_MODE } tui_dlg_kind_t;
void tui_dlg_estop(void);            /* 急停二次确认 */
void tui_dlg_mode(void);             /* 切自动二次确认 */
/* 当前开的是哪个弹窗（没有 = TUI_DLG_NONE）。屏上急停的长按直触发只在"急停确认框"开着时
 * 才让位：开着的是别的弹窗时，按住急停键照样直接急停（独立复核 P2-8）。*/
tui_dlg_kind_t tui_dlg_kind(void);
void tui_dlg_close(void);

/* ---- 提示条：顶部居中浮动 3.5s，不挡横幅与底栏急停键 ---- */
void ui_toast(const char *text);

/* ---- 走查钩子（probe 写 g_ui_cmd；只在台架/仿真档，2026-11-24 S 线 C01）----
 * 1=dlg_estop 2=dlg_mode 3=goto 调参 4=goto 曲线 5=goto 连接 6=风扇切换 7=切手动
 * 8=点击横幅 9=TUNE +step 10=TUNE -step 11=TUNE 步进切换 12=TUNE 行选择+1 13=HOME
 * 14=TUNE 滑条中点 15=关弹窗 16=关旧弹窗→开急停弹窗 100..103=诊断页
 * 审查条目 M03-F-12：g_ui_cmd 能开弹窗、切页、切风扇、切手动——交付档里没有这个变量，
 * 也没有消费它的代码。tui_step_toggle 不在此列：调参页自己的步进按钮在用。 */
#if TCS_PROFILE >= 1
extern volatile uint32_t g_ui_cmd;
void tui_step_param(int dir);
void tui_sel_next(void);
void tui_slider_center(void);
/* tui_sel_param_id() 已删（审查条目 M03-F-14 死代码）：2026-11-24 核对全仓零调用者，连台架档里也没有人用。 */
#endif
void tui_step_toggle(void);

/* 运行统计（probe 直读）：[0] 整页重建 [1] 就地更新 [2] 横幅重建 [3] 横幅点击
 * [4] 提示条次数 [5] 状态变化→上屏 最大延迟 ms [6] 最近一次延迟 ms
 * [7] 最近一次提示条出现的时刻 ms（减 g_ui_ack_sent_ms = 点确认→出提示的延迟）*/
extern volatile uint32_t g_ui_stat[8];
extern volatile uint32_t g_ui_probe[80];
/* main.c 在"快照带来状态变化"时写入（ms），用于量状态上屏延迟 */
extern volatile uint32_t g_ui_state_change_ms;
extern volatile uint32_t g_ui_state_change_seq;   /* 每记一次 +1 */
/* 取证用时间戳（ms）：横幅最近一次整条构建的时刻（减 main.c 的 s_snap_ms = 断链→横幅上屏
 * 的延迟，含 500ms 判定窗）；点横幅发出确认的时刻。只写不读，不影响任何行为。*/
extern volatile uint32_t g_ui_bn_built_ms;
extern volatile uint32_t g_ui_ack_sent_ms;

#endif
