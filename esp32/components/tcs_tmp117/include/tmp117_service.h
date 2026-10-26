/**
 * tmp117_service.h — TMP117 体温采样服务（ESP32-S3 主控侧；R1c，任务书 v11.3 第十一章）
 *
 * I2C0（GPIO8 SDA / GPIO9 SCL @100kHz，接线表 T1-T3；内部上拉，模块板载上拉则并联无害）。
 * 100ms 采样（核 1）；大端 2B×0.0078125=°C；滑动均值窗口 8；
 * H6.7 合理性滤波（25~45°C 且 |Δ|≤2°C/s，post_body_sanity_ok）；不合理样本按通信错误计。
 * 健康机：连续 10 错→掉线沿 ALM_BODY_SENSOR（粘滞至人工 ack）；连续 3 成功→恢复数据（报警位保持）。
 * 注入 API（E-21/E-22 与 R1e console 共用）：tmp117_sim_set/disable。
 * 纯函数复用 tmp117_pure.c（Q5 提取自 project_tcs tmp117.c）。
 */
#ifndef TCS_TMP117_SERVICE_H
#define TCS_TMP117_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 服务生命周期 ====== */
int  tmp117_service_start(void);     /* 装 I2C0+起采样任务（核 1）。0=ok */

/* ====== 注入（TCS_SIM 同语义；真机挂 R7 L4 前，E-21/22 走此路径）====== */
void tmp117_sim_set(float v);        /* 假体温注入（直接进滑动均值，恒 ok）*/
void tmp117_sim_disable(void);       /* 关假源 → 走真 I2C（失败=失联路径）*/
uint8_t tmp117_sim_active(void);

/* ====== 状态读侧 ====== */
uint8_t tmp117_body_ok(void);        /* body_sensor_ok 镜像 */
float   tmp117_body_temp(void);      /* 滑动均值 °C */

/* ====== 上电自测（main 在服务启动前调用；纯逻辑+健康机全路径板上取证；证据=[TB117]）====== */
void tmp117_lb_boottest(void);

#ifdef __cplusplus
}
#endif
#endif /* TCS_TMP117_SERVICE_H */
