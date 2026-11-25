/**
 * control_service.h — control FSM 服务任务（R1e；任务书 v11.3 第十一章）
 *
 * 核 1，100ms 节拍（U 线 E1，原 500ms）：每拍 control_poll + 一次布防步进，每 5 拍参数落盘
 * 冲洗（R1e perf 预算 ≤0.1ms）。自动模式的状态机迁移间隔与锁存期重申间隔由 control.c
 * 自己限制，不随本节拍变密。同任务步进 H6 布防三态机（H743 post_poll 同语义；
 * post_pass:=双源健康——ESP32 无全量 POST 电池，偏差已记录 R1.md）；
 * esp_task_wdt 3s 订阅+喂狗（间隙 100ms <1s，F3/F12）。
 */
#ifndef TCS_CONTROL_SERVICE_H
#define TCS_CONTROL_SERVICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "esp_err.h"

esp_err_t control_service_start(void);   /* control_init(ops)+起任务（核 1）+订阅 wdt。ESP_OK / ESP_ERR_NO_MEM / ESP_ERR_INVALID_STATE（P1-12：由 app_main 汇总）*/
uint8_t control_service_armed(void);/* post_armed() 转发（console/日志用）*/
uint32_t control_service_poll_max_us(void);  /* control_poll 段峰值 µs（预算 100）*/
uint32_t control_service_polls(void);        /* 评估拍计数 */
void control_service_hang_test(uint8_t on);  /* E-20：置 1 停喂狗（F12 注入）*/

#ifdef __cplusplus
}
#endif
#endif /* TCS_CONTROL_SERVICE_H */
