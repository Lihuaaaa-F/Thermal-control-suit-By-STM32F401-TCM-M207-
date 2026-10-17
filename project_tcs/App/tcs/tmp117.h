/**
 * tmp117.h — TMP117 体温传感器（硬件 I2C1 PB8/PB9 @100kHz，任务书 §3.5）
 *
 * 7 位地址 0x48，温度寄存器 0x00，2B 大端，raw×0.0078125 = °C。
 * 100ms 采样，滑动均值窗口 8。
 * 健康判定：连续 10 次通信错误 → body_sensor_ok=0 + ALM_BODY_SENSOR；
 * 恢复：连续 3 次成功 → ok=1（**报警位保持，需人工确认清除**，F3 防间歇接触误判）。
 * TMP117 VCC 禁止接 5V（接线表 #9）。
 */
#ifndef TCS_TMP117_H
#define TCS_TMP117_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TMP117_ADDR7        0x48
#define TMP117_REG_TEMP     0x00
#define TMP117_LSB_C        0.0078125f
#define TMP117_SAMPLE_MS    100u
#define TMP117_AVG_WIN      8
#define TMP117_ERR_TRIP     10     /* 连续 10 错 → 掉线 */
#define TMP117_OK_RUN       3      /* 连续 3 成功 → 恢复数据 */

/* ====== 纯逻辑（PC 单测 §11.2 替代模拟器）====== */
float tmp117_raw_to_c(int16_t raw);

/* 健康计数器（每次采样结果喂一步；返回 body_sensor_ok 当前值 0/1）
 * alarm_set/alarm_clr_req 由 control 侧实现注入：掉线沿置 ALM_BODY_SENSOR；恢复沿只清 ok 不清报警位 */
typedef struct {
    uint8_t ok;             /* 当前 body_sensor_ok */
    uint8_t err_run;        /* 连续错误计数 */
    uint8_t ok_run;         /* 连续成功计数 */
} tmp117_health_t;

void tmp117_health_init(tmp117_health_t *h);
/* step=1 成功 / 0 错误。alarm_raise 在掉线沿被调一次。 */
void tmp117_health_step(tmp117_health_t *h, int step, void (*alarm_raise)(void));

/* ====== 目标机驱动 ====== */
#ifndef TCS_UNIT_TEST

#include "stm32h7xx_hal.h"

void  tmp117_init(I2C_HandleTypeDef *hi2c);
void  tmp117_poll(uint32_t now_ms);   /* 主循环 100ms 节拍调用；更新 g_tcs.body_temp/body_sensor_ok */

#endif /* !TCS_UNIT_TEST */

#ifdef __cplusplus
}
#endif
#endif /* TCS_TMP117_H */
