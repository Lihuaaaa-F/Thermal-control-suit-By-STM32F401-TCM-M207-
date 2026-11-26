/**
 * control.h — 联动控制状态机（任务书 §3.6）+ 安全态 + 报警位管理（§6.2）
 *
 * 每 500ms 评估一次，输入 = body_temp 滑动均值。
 * 手动模式(0)：TEC/风扇由 UI/网页直控，状态机旁路——但安全态优先级高于手动。
 * 自动模式(1)：IDLE/COOL/HEAT/SAFE 迁移表见 .c 顶部注释。
 * TC2 通道维持用户手动设定（联动只控 TC1 贴身水箱）。
 * 安全常量：制热目标 45.0°C / 制冷目标 15.0°C / 过热硬阈值 48°C（不进参数表）。
 *
 * 零 HAL 依赖（PC 单测直接编译）；输出经 control_ops_t 注入：
 * 目标板 main 装配到 fan_set/m207_tec/m207_set_temp，PC 测试桩记录调用。
 */
#ifndef TCS_CONTROL_H
#define TCS_CONTROL_H

#include <stdint.h>
#include "tcs_proto.h"   /* ALM_* 报警位定义（双端同一份，PC 可编译）*/
#include "tcs_profile.h"   /* 档位派生（TCS_H743_LINK_ONLY 缺省值）*/

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 安全常量（硬编码红线，§6.2/§3.6）====== */
#define TEC_HEAT_TARGET_C   45.0f
#define TEC_COOL_TARGET_C   15.0f
#define ALM_OVERTEMP_HARD_C 48.0f
#define CTRL_EVAL_MS        500u
#define CTRL_BOOT_GRACE_MS  5000u   /* 上电 M207 首联宽限（期间不因 online=0 进 SAFE）*/

typedef enum {
    CTRL_IDLE = 0,
    CTRL_COOL,
    CTRL_HEAT,
    CTRL_SAFE,
} ctrl_state_t;

typedef struct {
    void (*fan_set)(uint8_t on);
    void (*tec_sw)(uint8_t ch, uint8_t on);     /* TCx:TCSW */
    void (*tec_temp)(uint8_t ch, float v);      /* TCx:TCADJTEMP（自动模式切目标）*/
    void (*on_state)(ctrl_state_t st);          /* 迁移日志钩子，可 NULL */
} control_ops_t;

void  control_init(const control_ops_t *ops);
void  control_poll(uint32_t now_ms);            /* 主循环 500ms 节拍调用 */
ctrl_state_t control_state(void);

/* ====== 状态迁移环（S 线 i3，条目 9；e3a 第五节方案第 1 条）======
 * 最近 8 条状态迁移，落 .noinit（软复位后还在；掉电丢——H743 没有不掉电的存储）。magic + 累计条数 +
 * CRC32 判整环有效：随机内容一律判无效。写入点：goto_state()（状态机迁移，ev=0）与
 * tcs_emergency_stop()（急停登记，ev=1，from 是当时状态、to = CTRL_SAFE）。
 * 下面五个是纯逻辑（tests/test_control.c 直接编）；目标机那一份见 control_trace()。*/
#define TCS_TRACE_N      8
#define TCS_TRACE_MAGIC  0x54435452u   /* 'TRCT' */
typedef struct {
    uint32_t ms;        /* 写入时刻（control_poll 的 now_ms；急停落在两拍之间时是上一拍）*/
    uint16_t alarm;     /* 当时的报警位 */
    uint8_t  from, to;  /* ctrl_state_t */
    uint8_t  ev;        /* 0 = 状态机迁移；1 = 急停登记 */
} tcs_trace_ent_t;
typedef struct {
    uint32_t magic;
    uint32_t seq;       /* 累计写入条数（环容量 TCS_TRACE_N）*/
    uint16_t head;      /* 下一条写入位置 */
    uint8_t  cnt;       /* 有效条数（≤ TCS_TRACE_N）*/
    uint8_t  pad;
    tcs_trace_ent_t e[TCS_TRACE_N];
    uint32_t crc;       /* 覆盖本结构 crc 之前的全部字节 */
} tcs_trace_t;

void     tcs_trace_reset(tcs_trace_t *t);
void     tcs_trace_push(tcs_trace_t *t, const tcs_trace_ent_t *en);
uint8_t  tcs_trace_valid(const tcs_trace_t *t);
uint16_t tcs_trace_count(const tcs_trace_t *t);                 /* 无效环 → 0 */
const tcs_trace_ent_t *tcs_trace_at(const tcs_trace_t *t, uint16_t i);  /* i = 0 最旧；越界 NULL */
/* 目标机上的那一份（.noinit）；console 的 fault 命令与启动打印都读它 */
const tcs_trace_t *control_trace(void);
#if TCS_H743_LINK_ONLY
void  control_link_follow(void);            /* PROD：本地诊断态跟随对端退出 SAFE（主循环每拍）*/
#endif

/* 手动直控（UI/网页/CLI 同一入口；SAFE/急停期 TEC 拒绝、风扇拒绝关）*/
void  control_manual_tec(uint8_t ch, uint8_t on);
void  control_manual_fan(uint8_t on);
/* 风扇切换：PROD（v2 纯显示终端）下发 0x15 CMD 5 由 ESP32 执行；
 * BENCH/SIM 保持本地直控（台架没有对端也能驱动）。*/
void  control_fan_toggle(void);
/* 超温判据（PROD 由主循环用快照温度调用；BENCH 由 control_poll 调用）。*/
void  control_observe_temp(float tc1_act);
/* mode 切换通知（param on_mode_change 钩子转发）；退出自动时保持当前输出 */
void  control_on_mode_change(uint8_t m);

/* ====== 报警位（§6.2）====== */
void  tcs_alarm_raise(uint16_t bit);            /* 置位（幂等）；触发顶栏红横幅+网页横幅 */
uint8_t tcs_alarm_active(uint16_t bit);
/* 人工确认清除（屏按钮 / 0x15 cmd=9 alarm_ack）：
 * 仅当报警源已恢复（传感器 ok、M207 online、tc1_act≤硬阈值、非急停自持）才清位。 */
void  tcs_alarm_ack(void);

#if TCS_USE_LVGL
/* G5：60 点×30s 温度环形（P2 数据源）。链路不在的格子是 NaN（曲线在此断开）；
 * tcs_hist_count 是开机以来的格子总数（只增），页面据此判断"来没来新点"。 */
void tcs_hist_tick(uint32_t now_ms);
void tcs_hist_snapshot(float out1[60], float out2[60], uint32_t * n);
uint32_t tcs_hist_count(void);
/* m1（模式页，2026-11-26）：曲线页的体温线。与上面同一套格子、同一个线性序；
 * 链路不在，或体温探头没接 / 失联的那一格是 NaN（曲线在此断开）。*/
void tcs_hist_snapshot_body(float out[60], uint32_t * n);
#endif

/* 急停（屏弹窗确认 / 0x15 cmd=8 / 板载 KEY0 / console estop 同一入口）：
 * 双通道 TCSW=0 + 风扇开 + mode=手动 + ALM_ESTOP + 广播 */
void  tcs_emergency_stop(void);

/* v2 补充：TEC/风扇在 ESP32 侧执行，本地急停必须同时下发链路 CMD(0x15)。
 * main.c 装配钩子（发帧）；未装=NULL 时仅本地语义（PC 单测/仿真）。
 * cmd 语义同 0x15：8=急停 9=报警确认。*/
typedef void (*control_cmd_hook_t)(uint8_t cmd);
void    control_set_cmd_hook(control_cmd_hook_t cb);
uint8_t control_estop_latched(void);   /* 本地急停自持位（屏显报警用）*/
uint8_t control_overtemp_latched(void);/* R1 复核 R-03：本地超温自持（不被 snap_reassert 覆盖）*/
uint8_t control_estop_tx_pending(void);/* R1 复核 R-04：急停意图待对端确认（ack 不清）*/
void    control_estop_tx_confirm(void);/* 对端快照出现 ALM_ESTOP 时调用 */
/* N-01：超温停机请求的投递状态（与急停分离；本地 ack 清隐患时作废）*/
uint8_t control_overtemp_tx_pending(void);
void    control_overtemp_tx_confirm(void);
/* R1 修复 P0-02：对端已执行并随后 ack（快照曾出现 ALM_ESTOP 又消失）时清本地自持，
 * 避免"网页/对端确认后本端仍每拍重发"的两端拉锯。不改变其他报警位。*/
void    control_estop_clear(void);

#ifdef __cplusplus
}
#endif
#endif /* TCS_CONTROL_H */
