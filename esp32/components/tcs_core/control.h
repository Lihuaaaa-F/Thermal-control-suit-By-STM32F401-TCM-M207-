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

/* ====== 安全锁存位（R1 修复 P0-01/P0-04/P0-08）======
 * 语义：危险源置位后，out_tec_sw 的"开"被无条件压成 0，直到人工 ack 且源已恢复。
 * 与 ALM_* 的区别：ALM 是显示位；latch 是执行器硬门控。同一事件同时置两者，
 * 但清位条件不同（ALM 可被显示层覆盖，latch 只有 ack 能清）。
 * 持久化：main 注入 safety_persist/safety_load；PC 单测传 NULL，退化为纯 RAM。 */
#define SAFETY_BIT_OVERTEMP   (1u << 0)
#define SAFETY_BIT_BODY_LOST  (1u << 1)
#define SAFETY_BIT_M207_LOST  (1u << 2)
#define SAFETY_BIT_ESTOP      (1u << 3)

typedef struct {
    void (*fan_set)(uint8_t on);
    void (*tec_sw)(uint8_t ch, uint8_t on);     /* TCx:TCSW */
    void (*tec_temp)(uint8_t ch, float v);      /* TCx:TCADJTEMP（自动模式切目标）*/
    void (*on_state)(ctrl_state_t st);          /* 迁移日志钩子，可 NULL */
    void    (*safety_persist)(uint8_t latch);   /* latch 变化时落盘，可 NULL */
    uint8_t (*safety_load)(void);               /* 启动读回 latch，可 NULL */
} control_ops_t;

void  control_init(const control_ops_t *ops);
/* 控制节拍（U 线 E1：服务任务每 100ms 调一次；自动模式的状态机评估仍每 CTRL_EVAL_MS 一次）。
 * 100ms 粒度决定：故障发现、通信类自愈计时、退出 SAFE；锁存期间给模块重申输出的
 * 频率由 control.c 限在 500ms。*/
void  control_poll(uint32_t now_ms);
ctrl_state_t control_state(void);
/* 微秒计时 → 毫秒时间戳（control_poll 的 now_ms 由它来）。必须走 64 位除：
 * 先截成 int32 再除，开机 35 分 47 秒后时间戳会倒跳 71 分钟。*/
uint32_t control_ms_from_us(int64_t us);

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

/* 急停（屏弹窗确认 / 网页 / 0x15 cmd=8 / console estop 同一入口；ESP32 侧没有物理急停键）：
 * 双通道 TCSW=0 + 风扇开 + mode=手动 + ALM_ESTOP + 广播。
 * _src 版本记下是谁按的（PARAM_SRC_*，进事件日志）；无参版本按"控制台"记。*/
void  tcs_emergency_stop_src(uint8_t src);
void  tcs_emergency_stop(void);

/* ====== 安全锁存读侧（R1 修复；console/网页/诊断用）====== */
uint8_t control_safety_latched(void);           /* 非 0 = 仍在安全锁存 */
/* N-01：设备服务层（M207 解析等）可直接置安全锁存——超温切断不得只依赖 control 任务存活。*/
void    control_safety_trip(uint8_t bit);

/* 统一命令执行端（网页 WS 与屏幕 0x15 共用一份语义，避免两处 switch 漂移）。
 * 覆盖 cmd 1..9（TEC1/2 开/关、风扇切换、自动/手动、急停、确认）；
 * 返回 0=执行成功，-1=未知命令，-2=被拒绝（SAFE 下的手动执行器命令，或急停/超温锁存期间
 * 切自动）。
 * 10（保存到模块）依赖 tcs_m207，由调用方处理；11（屏侧观测到超温的停机请求）只给屏链路用，
 * 网页不得下发（websocket.c 只放行 1..10）。
 * _src 版本由调用方说明命令来自哪里（PARAM_SRC_*），改模式与急停时原样带进参数表；
 * 无参版本按"控制台"（台架自测、单测）。*/
int control_apply_cmd_src(uint8_t cmd, uint8_t src);
int control_apply_cmd(uint8_t cmd);

/* 参数写入前的安全校验：接到 param_hooks_t.on_check。返回 0 放行，-2 拒绝。
 * 现有规则：急停/超温锁存期间不得切到自动模式（急停复位后不得自动重启）。*/
int  control_param_check(uint16_t id, float v);

#ifdef TCS_UNIT_TEST
/* 测试专用：不经过 poll 直接置锁存，用于验证"锁存门控"独立于状态机（R1 复核 R-14）。
 * 只在单测构建可见，生产固件不编入。*/
void control_test_trip(uint8_t bit);
#endif

#ifdef __cplusplus
}
#endif
#endif /* TCS_CONTROL_H */
