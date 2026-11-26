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
/* 安全关断专用入口（P1-09 的另一半，i3）：control 的 ops.tec_force 接到这里。与 m207_tec 的
 * 差别只有一条——模块离线也受理（不回 -2）：意图记进安全关断旁路的保持位，复联第一拍补发
 * TCSW=0，先于普通队列放行；在线时等同 m207_tec(ch, 0)，下一拍优先上线路并读回确认。
 * 不占普通队列，离线沿的清队列碰不到它；每 500ms 的重试由 control 的锁存重申驱动。
 * 只有"关"走这里："开"转回 m207_tec（普通队列）。返回 0=已受理；-1=通道非法。*/
int  m207_tec_force(uint8_t ch, uint8_t on);
int  m207_map_set(uint16_t pid, float v);            /* H10 映射表 0x100+ */
int  m207_send_raw(const char *cmd);                 /* console 调试/COMPUSHLIST 试跑 */
/* R1 M12-F-04：逐参数下发 `!` 保存到模块（网页"保存到模块"按钮）。返回入队条数；-1/-2=失败。
 * PCB/PCRS232 组（0x120+）语法未确认，跳过不下发（M12-F-05）。*/
int  m207_save_all(void);

/* ====== 台架注入（CONFIG_TCS_SIM）：把某一路水温钉在给定值 ======
 * 注入值每拍写进 g_tcs.tc1_act / tc2_act（覆盖模块应答），不碰模块、不发帧。卡死判据对"注入在生效"
 * 的那一路豁免（main.c 的 control_ops.stuck_exempt）：注入值是人给的常数，逐位不变是它的本性
 * （M09-F-09 的注入档口径，S 线 i3）。交付档不编译这些实现（默认档 bin 里查不到）。*/
#if CONFIG_TCS_SIM
void    m207_sim_water(uint8_t ch, float c);   /* ch 1/2；-20..80 之外忽略 */
void    m207_sim_water_off(uint8_t ch);        /* ch 0 = 两路都关 */
uint8_t m207_sim_water_active(void);           /* bit0 = TC1、bit1 = TC2 */
#endif

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

/* ====== 队列 / 发送侧计数（S-G e2：M12-F-08 / X03-F-07 / M12-F-14；排障用，单调累计）======
 * 语义：这五项里任何一项在运行中增长，都说明"调用方以为发出去了、其实没有"或者
 * "模块回了不该回的码"——各自对应一条可见的日志（限频）。*/
typedef struct {
    uint16_t qdrop;        /* 下行队列满被拒的写入（API 返回 -1 的每一次；安全关断帧不进队列，不计这里）*/
    uint16_t superseded;   /* 被更新的同类写取代、没上线路的帧（关断之后作废的"开"）*/
    uint16_t unexpected;   /* 无在途事务、也不在轮询等待窗的 CMD:REPLY（链路活但语义意外）*/
    uint16_t save_ok;      /* 保存帧（`!`）被模块确认（CMD:REPLY=8）*/
    uint16_t save_rej;     /* 保存帧得到其它回码，或应答窗内没有回码 */
    uint16_t linedrop;     /* 没进解析器就被丢弃的行：超长（>64B）整行、空闲 >500ms 的半行、UART 溢出清缓冲 */
    uint32_t txdrop;       /* 发送环满、没交给 UART 的帧 */
} m207_qstats_t;
void     m207_stats_queue(m207_qstats_t *out);       /* 一次取全 */
uint16_t m207_stats_unexpected(void);                /* M12-F-14：控制台 perf 行用 */
uint8_t  m207_save_busy(void);                       /* 1=保存序列进行中（此时再按"保存"应答 BUSY）*/

/* ====== 模块自带保护状态（S-G e2：X04-F-05 ①）======
 * 服务在线时每 60s 读一次 TCx:TCERRORSTATUS? 与 TCx:TCOTPSTATUS?（只读查询，排普通队列）；
 * 复联后马上读一次。这里只做"如实取值"：不报警、不进 SAFE。
 * 为什么不在这里接报警：TCERRORSTATUS 含上电锁存位，要模块断电重启才清；真机实录里 TC1 恒回
 * 239 / 253（传感器开路 / 过压位锁存）。把它直接接成 SAFE，设备一上电就永久停机。
 * 要不要、按哪些位、锁存位怎么对待，由 control 决定（批次 e3a）。*/
typedef struct {
    uint16_t errstat;      /* TC1/TC2:TCERRORSTATUS 原值（8 位：正常=1 故障=0）；0xFFFF=尚未取到 */
    uint16_t otpstat;      /* TCx:TCOTPSTATUS 原值（极性未在真机核实，只透传）；0xFFFF=尚未取到 */
    uint8_t  fault_mask;   /* = m207_errstat_fault_mask(errstat)；尚未取到时为 0 */
    uint8_t  valid;        /* 1=本通道取到过至少一次 */
    uint32_t age_ms;       /* 距最近一次取到的真实毫秒（掉线后继续增长）；0xFFFFFFFF=从未取到 */
} m207_mod_status_t;
int         m207_module_status(uint8_t ch, m207_mod_status_t *out);  /* 0=ok；-1=通道非法或 out 为空 */
uint8_t     m207_errstat_fault_mask(uint16_t errstat);   /* 纯解码：(~errstat)&0x7F —— 置位=该位保护触发或锁存 */
const char *m207_fault_bit_name(uint8_t bit);            /* bit0 external / 1 over-voltage / 2 over-current /
                                                         * 3 over-temp / 4 sensor-open / 5 load-short / 6 load-open
                                                         * （位义取自厂商帮助文件 tc_hlp.xml，未逐位上机核实）*/

/* ====== 电学量：TCx:TCACTVOL / TCx:TCACTCUR 轮询缓存（m1"模式"页，用户 2026-11-26 定的新功能）======
 * 在线时每秒把两路的实际输出电压（V，制热为负）与实际输出电流（A）各问一遍，只借温度轮询的
 * 空档（不拖慢温度轮询）。来源：厂商参数字典与用户手册的只读状态；真机 2026-11-26 实测模块认这两条。
 * 模块不认（回错误码或不应答）→ 连续 3 次后停止轮询，m207_elec_supported() 回 0。*/
#define M207_ELEC_STALE_MS   3000u   /* 数据龄超过它按"未知"处理（正常每秒刷新一遍）*/
typedef struct {
    float    volt, cur;       /* V（制热为负）/ A（非负；应答里的负零已吃掉）*/
    uint32_t age_ms;          /* 电压、电流里较旧那个的数据龄（真实毫秒，掉线后继续增长）；从未取到 = 0xFFFFFFFF */
    uint8_t  valid;           /* 1 = 取到过且不太旧（可以拿来算功率）*/
    uint8_t  fresh;           /* 1 = 取到过（不看龄）*/
} m207_elec_t;
int     m207_elec_get(uint8_t ch, m207_elec_t *out);   /* 0=ok；-1=通道非法或 out 为空 */
uint8_t  m207_elec_supported(void);                    /* 0=模块不认这两条（已停发）；1=支持或尚未判定 */

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
