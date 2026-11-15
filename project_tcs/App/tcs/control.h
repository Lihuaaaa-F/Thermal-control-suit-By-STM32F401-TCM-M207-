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

/* 手动直控（UI/网页/CLI 同一入口；SAFE/急停期 TEC 拒绝、风扇拒绝关）*/
void  control_manual_tec(uint8_t ch, uint8_t on);
void  control_manual_fan(uint8_t on);
/* mode 切换通知（param on_mode_change 钩子转发）；退出自动时保持当前输出 */
void  control_on_mode_change(uint8_t m);

/* ====== 报警位（§6.2）====== */
void  tcs_alarm_raise(uint16_t bit);            /* 置位（幂等）；触发顶栏红横幅+网页横幅 */
uint8_t tcs_alarm_active(uint16_t bit);
/* 人工确认清除（屏按钮 / 0x15 cmd=9 alarm_ack）：
 * 仅当报警源已恢复（传感器 ok、M207 online、tc1_act≤硬阈值、非急停自持）才清位。 */
void  tcs_alarm_ack(void);

#if TCS_USE_LVGL
/* G5：60 点×30s 温度环形（P2 数据源） */
void tcs_hist_tick(uint32_t now_ms);
void tcs_hist_snapshot(float out1[60], float out2[60], uint32_t * n);
#endif

/* 急停（屏弹窗确认 / 0x15 cmd=8 / KEY0 / console estop 同一入口）：
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
