/**
 * fan_service.h — 风扇驱动（ESP32-S3；R1d，任务书 v11.3 第十一章）
 *
 * GPIO48 → 光耦，**低电平=开**（接线表 F1；48 若接 RGB 灯随亮=无害）。
 * LEDC PWM 25kHz/10bit；占空比 API 0-100（低电平占空比=百分比）；默认二值 0/100。
 * control_ops.fan_set(uint8_t on) 由 R1e 装配到 fan_set_on（0/100 直控）。
 * P-2 电压两态（万用表）挂账用户；板上自测做电平回读软件子集（[FANLB]，L3）。
 */
#ifndef TCS_FAN_SERVICE_H
#define TCS_FAN_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAN_GPIO        42   /* v2.1 换脚：48 与板载 WS2812 RGB 共脚（PWM 被灯珠当噪声），42 空闲无特殊功能 */
#define FAN_FREQ_HZ     25000
#define FAN_DUTY_MAX    100

int fan_service_start(void);          /* LEDC 初始化（duty=0 关）。0=ok */

void fan_set_duty(uint8_t pct);       /* 0-100；0=关（引脚恒高），100=全开（恒低）*/
void fan_set_on(uint8_t on);          /* 二值包装（control ops 用）：0→0 / 非0→100 */
uint8_t fan_get_duty(void);           /* 当前设定值（0-100）*/

void fan_ws2812_black_frame(int gpio_num);  /* 对任意脚发 WS2812 全黑帧（LED 排查/清灯）*/
void fan_ws2812_ledtest(int gpio_num);      /* 红闪 700ms→黑（人眼可视自证帧时序+引脚）*/

/* 上电自测：0/100 两态引脚电平回读（低=开红线）+ 中点不炸；证据=[FANLB] */
void fan_lb_boottest(void);

#ifdef __cplusplus
}
#endif
#endif /* TCS_FAN_SERVICE_H */
