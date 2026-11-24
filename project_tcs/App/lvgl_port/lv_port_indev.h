#ifndef LV_PORT_INDEV_H
#define LV_PORT_INDEV_H
#include <stdint.h>
#include "lvgl.h"
void lv_port_indev_init(void);
/* 每向 LVGL 报告一次"按下"就加 1（读的次数，不是按压的次数）。三档都有，只读遥测；
 * 急停屏入口判定在别的页面上靠它区分"界面在处理这次按压"和"触摸没送进 LVGL"。*/
extern volatile uint32_t g_indev_pressed_reads;
#endif
