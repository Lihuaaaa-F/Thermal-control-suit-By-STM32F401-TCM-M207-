/**
 * tcs_ui.h — 触摸 UI（任务书 §3.7，像素级规格）
 */
#ifndef TCS_UI_H
#define TCS_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void tcs_ui_init(void);          /* 全屏画主页（ltdc_ready 后调用）*/
void tcs_ui_poll(uint32_t now_ms); /* 主循环每拍调：触摸 20Hz + 脏区重绘 */

/* —— 屏诊断（console scrtest 用，WP-A.2d 噪声二分）—— */
uint32_t tcs_ui_tap_count(void);            /* 触摸点击累计（幻触摸侦测）*/
void tcs_ui_screen_clear(uint16_t rgb565);  /* 纯色清屏+flip（DMA2D 全屏路径自证）*/
void tcs_ui_screen_text(void);              /* 干净底+标定文本(16/24/48px)+flip */
void tcs_ui_force_home(void);               /* 回主页+整页重绘（诊断后恢复）*/
#ifdef TCS_SIM
void tcs_ui_inject_tap(int x, int y);       /* 触摸注入（WP-G 交互自测）*/
void tcs_ui_inject_touch(int down, int x, int y); /* H1 按住序列注入（走真 touch_poll 路径）*/
#endif
void tcs_ui_force_tune(void);               /* 调参页整页重绘（取证/诊断）*/
void tcs_ui_screen_stress(int mode, int n); /* 压测算子（WP-A.2d 分音）：0纯flip 1横幅填充 2横幅文本 3徽章填充 4大字*/

#ifdef __cplusplus
}
#endif
#endif /* TCS_UI_H */
