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

#ifdef __cplusplus
}
#endif
#endif /* TCS_UI_H */
