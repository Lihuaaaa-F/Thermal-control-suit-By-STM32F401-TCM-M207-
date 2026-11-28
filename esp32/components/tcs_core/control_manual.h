/**
 * control_manual.h — 手动模式下两路 TEC 的输出映射（新功能批次 m1"模式"页，2026-11-26 用户裁决）
 *
 * 用户原话要点：两路 TEC（界面叫 TEC1 / TEC2）在手动模式下各自点"制冷 / 暂停 / 制热"；
 * 手动时目标温度不显示、不生效（固定沿用自动状态机的 TEC_COOL_TARGET_C / TEC_HEAT_TARGET_C）。
 *
 * 真值 = 参数表里的两个参数：PARAM_ID_TEC1_MANUAL / PARAM_ID_TEC2_MANUAL
 *（0 = 暂停、1 = 制冷、2 = 制热；不持久化，上电一律暂停）。本模块只做三件事：
 *   1. 每个控制节拍把"参数"变成"输出"，只在变化时下发（不每拍重发，不挤占 M207 的 50ms 节流队列）；
 *   2. 风扇联动：任一路制冷 / 制热 → 风扇开；两路都暂停 → 恢复联动之前用户的选择；
 *   3. 进安全态 / 急停 / 切到自动 → 两路回暂停（确认解除后不会自己恢复输出）。
 *
 * 纯逻辑、零 HAL、主机可编译（esp32/tests/test_control_manual.c）。输出经 control_manual_ops_t
 * 回到 control.c 的输出封装——安全门控（锁存期间"开"被压成 0）仍是 control.c 那一个收敛点。
 * 本文件的状态只在控制任务里改（control_poll / enter_safe）；别的任务要清零只登记请求，
 * 由 control_poll 执行（见 control.c 的 s_manual_reset_req）。
 */
#ifndef TCS_CONTROL_MANUAL_H
#define TCS_CONTROL_MANUAL_H

#include <stdint.h>
#include "tcs_proto.h"      /* TCS_TEC_DIR_*：快照里"输出方向"的取值（协议唯一源）；下面只是同值别名 */

#ifdef __cplusplus
extern "C" {
#endif

/* 输出方向（快照 tec_dir 的取值；3 = 通道开着但不是模式页设的，见 control_tec_dir）*/
#define TEC_DIR_PAUSE   TCS_TEC_DIR_PAUSE
#define TEC_DIR_COOL    TCS_TEC_DIR_COOL
#define TEC_DIR_HEAT    TCS_TEC_DIR_HEAT
#define TEC_DIR_EXT     TCS_TEC_DIR_EXT

typedef struct {
    void (*tec_sw)(uint8_t ch, uint8_t on);    /* ch = 1 / 2 */
    int  (*tec_temp)(uint8_t ch, float v);     /* 0 = 模块服务已受理；非 0 = 没受理（离线/忙/被拒）*/
    void (*fan_set)(uint8_t on);
} control_manual_ops_t;

/* 上电 / 单测复位：清全部状态（两路暂停、无联动）。ops 的三个函数都必须非空。*/
void control_manual_init(const control_manual_ops_t *ops);

/* 两路手动输出回暂停。restore_fan = 1：风扇被联动开着时恢复用户原来的选择；
 * = 0：不碰风扇（安全态里风扇全开，解除时由 control.c 的 leave_safe 另行复原）。
 * 参数非 0 时经 param_set(…, 0, PARAM_SRC_KERNEL) 置回——会广播 0x11，屏端镜像跟着回暂停。*/
void control_manual_reset(int restore_fan);

/* 每个控制节拍调用（只在手动模式、非安全态时调，见 control_poll）。*/
void control_manual_poll(void);

/* 写参数前的校验：manual_mode = 当前是手动模式；in_safe = 控制状态机在安全态；
 * latched = 安全锁存非 0。返回 0 放行；否则 PARAM_REJECT_NOT_MANUAL / PARAM_REJECT_SAFE
 *（param_registry.h，屏端据此提示）。*/
int control_manual_check(int manual_mode, int in_safe, int latched);

/* 已下发的手动方向（TEC_DIR_*；下标 ch = 1 / 2；通道非法回暂停）。*/
uint8_t control_manual_dir(uint8_t ch);

/* 用户的风扇选择：风扇正被联动开着时是联动之前记下的那个值，否则就是当前风扇状态。*/
uint8_t control_manual_fan_pref(void);

#ifdef __cplusplus
}
#endif
#endif /* TCS_CONTROL_MANUAL_H */
