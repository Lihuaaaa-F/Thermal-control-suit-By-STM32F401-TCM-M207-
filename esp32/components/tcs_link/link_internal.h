/**
 * link_internal.h — tcs_link 组件内部接口（M11-F-15，S 线 e1，2026-11-26）
 *
 * 拆分前 link_task.c 一个文件 916 行、六类职责（UART 驱动 / 帧分发 / 注册表镜像 / 演示模拟 /
 * 出厂自环 / 公共 API）。按职责拆成：
 *   link_task.c       UART 发送钩子 + 任务循环 + 启动 + comm 回调（~210 行）
 *   link_dispatch.c   收帧分发 on_frame（STATUS / REGISTRY_ENTRY / SET_PARAM / CMD）+ 版本检查
 *   link_registry.c   注册表种子、0x14 落地、0x13 请求、参数值缓存、最近一次屏写参数
 *   link_demo.c       演示模式（CONFIG_TCS_DEMO；生产档整段不编译）
 *   link_selftest.c   诊断静默 / 芯片内回环 / 开机自环自测（[LPBK]）
 *   link_send.c       上行发送（0x11/0x20/0x21/0x22/0x13/0x90）+ 组帧
 *   link_status.c     状态缓存（s_status）+ 临界区 + 判活 + 统计汇总
 *
 * 本头文件只放"跨文件但要保持在组件内部"的接口；**对外 API 仍只在 include/link_task.h**。
 * 拆分是纯重构：公开符号一个不少、行为一字不变（对照表见 docs/results/S_G_e1_结果.md）。
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"   /* CONFIG_TCS_DIAG 等 sdkconfig 宏：link_task.h 的档位声明要先看到 */
#include "link_task.h"
#include "comm.h"
#include "tcs_proto.h"
#include "driver/uart.h"      /* UART 端口常量（本组件固定 UART1）*/
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 硬件/实例（link_task.c 持有，其余文件只经下面这些访问器）====== */
#define LINK_UART       UART_NUM_1
#define LINK_TXD        17
#define LINK_RXD        18
#define LINK_BAUD       230400
#define LINK_RXBUF      2048
#define LINK_TXBUF      1024
#define LINK_EVT_QUEUE  20

/* 与 H743 param_registry.h 冻结一致：0=f32 1=u8 2=u16（双端同源由单测保证）。
 * 本组件不依赖 tcs_core（组件环），这组常量在那边改必须同步（现有 _Static_assert 门禁）。*/
#ifndef PARAM_TYPE_F32
#define PARAM_TYPE_F32   0
#define PARAM_TYPE_U8    1
#define PARAM_TYPE_U16   2
#define PARAM_TYPE_BTN   3   /* R3-D-1：与 param_registry.h 同步（BTN=动作项，不走 0x11/0x12）*/
#endif

/* 诊断静默（实体在 link_selftest.c；只有 SIM/DIAG 档有这些符号）*/
#if CONFIG_TCS_SIM || CONFIG_TCS_DIAG
uint8_t link_muted(void);
void    link_mute_add_rxdrop(uint32_t n);
#endif

/* comm_init 的临界区钩子（实体在 link_status.c，与 link_internal_lock/unlock 同一把锁）*/
void link_crit_enter(void);
void link_crit_exit(void);

comm_link_t *link_ctx(void);              /* 唯一的 comm 实例 */
uint32_t link_internal_now_ms(void);      /* 与 comm 的 now_ms_fn 同一时基 */

/* ====== 临界区与状态缓存（link_status.c）====== */
void link_internal_lock(void);
void link_internal_unlock(void);
link_status_t *link_status_buf(void);     /* 调用方必须持锁读写 */
void link_status_note_alive(void);        /* 收到对端 STATUS：判活时间戳（持锁内部处理）*/
void link_notify_event(void);             /* 状态/参数变更通知（link_on_event 注册的回调）*/

/* ====== 收帧分发（link_dispatch.c）====== */
void link_dispatch_frame(const uint8_t *payload, size_t len);
uint32_t link_dispatch_proto_mismatch(void);   /* link_stats_get 用 */
uint8_t  link_dispatch_peer_ver(void);
uint16_t link_dispatch_alarm_hint(void);       /* 版本不符"当前"提示位（对上后清 0）*/

/* ====== 注册表（link_registry.c）====== */
void link_registry_init(void);                       /* 灌内置种子（link_start 调）*/
void link_registry_apply_entry(tcs_reg_entry_t *e);  /* 0x14 一条落地（含收齐判定）*/
void link_registry_progress_reset(void);             /* 断链/对端重启：作废收齐标记 */
uint8_t link_registry_done(void);
void link_registry_note_param(uint16_t id, float v); /* 演示档：把参数值写进缓存 + 最近写入 */

/* ====== 演示模式（link_demo.c；CONFIG_TCS_DEMO=0 时都是空实现）====== */
int  link_demo_takeover(void);   /* 上电宽限 + 3s 无 STATUS → 接管 */
void link_demo_step(void);
int  link_demo_cmd(uint8_t cmd);
int  link_demo_suppressed(void); /* 演示档接管中：上行帧不发 */
void link_demo_boot(void);       /* 记上电时刻（link_start 调）*/

/* ====== 发送（link_send.c）====== */
size_t link_frame_build(uint8_t mt, uint8_t seq, const uint8_t *body, size_t blen,
                        uint8_t *out, size_t cap);

/* ====== 核心统计（link_task.c）====== */
uint32_t link_core_txdrop(void);
uint32_t link_core_rx_bytes(void);
uint32_t link_core_rx_frames(void);

#ifdef __cplusplus
}
#endif
