/**
 * m207_service.h — M207 串口服务（ESP32-S3 主控侧；R1b，任务书 v11.3 第十一章）
 *
 * UART2（GPIO21 TX / GPIO47 RX @57600，接线表 v2.0 M1/M2；MAX3232 电平转换）。
 * 50ms 节流队列（正典 §4 铁律）；写后读回校验（设→REPLY=1/8→等≥50ms→查→
 * 不一致重发 1 次→NAK）；轮询状态机（500ms 交替/5 连败离线/1s 探测恢复，F5）。
 * 纯函数复用 m207_proto.c；报警置位走 tcs_alarm_raise（control 统一裁决点，R1e 接 SAFE）。
 */
#ifndef TCS_M207_SERVICE_H
#define TCS_M207_SERVICE_H

#include <stdint.h>
#include "param_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====== param_registry 钩子适配器（main 装配）====== */
void m207_hook_set_temp(uint8_t ch, float v);        /* on_m207_set */
void m207_hook_map_set(uint16_t pid, float v);       /* on_m207_map_set（H10 0x100+）*/

/* ====== 服务生命周期 ====== */
int  m207_service_start(void);   /* 装 UART2 驱动+起任务（核 1）。0=ok */

/* ====== 写路径（异步：经 50ms 节流队列；在线校验+读回确认）======
 * 返回 0=入队 ok；-1=参数错/队列满；-2=服务未启动或 M207 离线（丢弃计数）*/
int  m207_set_temp(uint8_t ch, float v);             /* TCx:TCADJTEMP=%.1f（域 ±20..80）*/
int  m207_tec(uint8_t ch, uint8_t on);               /* TCx:TCSW=0/1 */
int  m207_map_set(uint16_t pid, float v);            /* H10 映射表 0x100+ */
int  m207_send_raw(const char *cmd);                 /* console 调试/COMPUSHLIST 试跑 */
/* R1 M12-F-04：逐参数下发 `!` 保存到模块（网页"保存到模块"按钮）。返回入队条数；-1/-2=失败。
 * PCB/PCRS232 组（0x120+）语法未确认，跳过不下发（M12-F-05）。*/
int  m207_save_all(void);

/* ====== 状态读侧 ====== */
uint8_t m207_online(void);                           /* 0/1（g_tcs.m207_online 同步）*/
float   m207_last_temp(uint8_t ch);                  /* TCACTTEMP 最近值 */
uint32_t m207_temp_age_ms(void);                     /* 双通道温度缓存龄（较旧者 ms；0xFFFF=离线/未知；R2 快照新鲜度）*/
uint16_t m207_stats_nak(void);                       /* NAK 累计（写路径确认失败）*/
uint16_t m207_stats_verify_ok(void);                 /* 写后读回确认成功累计 */
uint16_t m207_stats_resend(void);                    /* 自动重发累计 */
uint16_t m207_stats_offline(void);                   /* 离线沿累计 */
uint16_t m207_stats_wdrop(void);                     /* 离线/未启动拒写累计 */
void    m207_perf_reset(void);                       /* perf 窗口清零 */

/* ====== 报警（F5：置位在服务内；清除走 tcs_alarm_ack 人工确认）====== */
uint8_t m207_alarm_active(void);                     /* ALM_M207 位 */

/* ====== 上电自测（main 在服务启动前调用；独占 UART2；证据=[M207LB]）====== */
void m207_lb_boottest(void);
/* 内部回环（ESP-IDF uart_set_loop_back，WP-F.1 同款真驱动真帧）+ 物理短接探测
 * （若 GPIO21↔47 已短接则自动跑物理回环全套） */

/* ====== R1b 取证专用构建（CONFIG_TCS_M207_SELFLOOP=y）====== */
#if CONFIG_TCS_M207_SELFLOOP
void m207_selfloop_run(void);   /* 应答器注入全路径取证（[M207SL]），~15s */
#endif

/* ====== R1b 真机取证构建（CONFIG_TCS_M207_REALPROBE=y）====== */
#if CONFIG_TCS_M207_REALPROBE
void m207_realprobe_run(void);  /* P-2 判活+协议探针+E-06 60s 录帧（[P2]/[RP]/[E06]）*/
#endif

#ifdef __cplusplus
}
#endif
#endif /* TCS_M207_SERVICE_H */
