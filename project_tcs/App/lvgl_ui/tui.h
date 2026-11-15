#ifndef TUI_H
#define TUI_H

#include "lvgl.h"
#include "param_registry.h"

typedef enum { TUI_HOME = 0, TUI_TUNE, TUI_CHART, TUI_CONN, TUI_DIAG } tui_page_t;

void tui_init(void);
void tui_poll(void);                 /* 500ms 数据刷新 + 横幅态同步（主循环节拍调用） */
void tui_goto(tui_page_t p);
void tui_request(tui_page_t p);      /* 延后切页（LVGL 回调上下文外执行） */
void tui_request_rebuild(void);
void tui_step_param(int dir);   /* R1 M03-F-08：补原型（原为隐式声明）*/      /* 延后重建当前页 */
void tui_tick(void);

/* —— G5 走查钩子（probe-rs 写 g_ui_cmd；=1 only）——
 * 1=dlg_estop 2=dlg_mode 3=goto P1 4=goto P2 5=goto P3 6=fan toggle
 * 7=mode 直切手动 8=banner ack 9=TUNE +step 10=TUNE -step 11=TUNE step切换
 * 12=TUNE 行选择+1 13=HOME 14=TUNE滑条中点 */
extern volatile uint32_t g_ui_cmd;
void tui_dlg_close(void);
void tui_step_toggle(void);
void tui_sel_next(void);
void tui_slider_center(void);
uint16_t tui_sel_param_id(void);                 /* 主循环每拍调用：执行延后动作 + 500ms 数据刷新 */
tui_page_t tui_current(void);

/* —— 页构建器（各页文件实现）—— */
void tui_p0_build(lv_obj_t * scr);
void tui_p1_build(lv_obj_t * scr);
void tui_p2_build(lv_obj_t * scr);
void tui_p3_build(lv_obj_t * scr);
void tui_p0_refresh(void);
void tui_p1_refresh(void);
void tui_p2_refresh(void);

/* —— G5 诊断页（VL-12/U-6/TL-18；仅 g_ui_cmd 100..103 触发）—— */
void tui_diag_build(lv_obj_t * scr, int mode);
void tui_diag_set_mode(int mode);

/* —— 对话框（tui.c 实现；模态覆盖层）—— */
void tui_dlg_estop(void);            /* 急停二次确认 */
void tui_dlg_mode(void);             /* 切自动二次确认 */
int  tui_dlg_active(void);           /* 1=弹窗/遮罩在场（暂停 500ms 刷新） */

/* —— 共享：横幅（二级页驻留规则，UI 规范 v1.2 §B）—— */
void tui_banner_build(lv_obj_t * parent);
void tui_banner_refresh(void);

#endif
