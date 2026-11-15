/**
 * link_task.h — ESP32-S3 ↔ H743 链路任务（UART1 @230400 GPIO17/18）
 *
 * ESP32 = 纯桥：不存业务参数、不做决策（D 职责）。缓存 H743 推来的 0x10 快照、
 * 0x11 参数、0x14 注册表，供 Web 层查询；上行 0x12/0x15 经 comm ARQ（ACK 必达）。
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "tcs_proto.h"

#define LINK_REGISTRY_MAX   64   /* R1 P1-01：权威表 50 条 + 余量（原 32 截 18 条）*/
#define LINK_MAX_KEY        24
#define LINK_MAX_NAME       48
#define LINK_MAX_UNIT       8

typedef struct {
    /* 0x10 快照 */
    uint8_t  valid;
    uint32_t seq;
    float    tc1_act, tc2_act, body;
    uint8_t  tec1, tec2, fan, mode;
    uint16_t alarm;
    uint8_t  online;
    int64_t  last_status_us;    /* 断链 3s 判定（esp_timer）*/
} link_status_t;

typedef struct {
    uint8_t used;
    tcs_reg_entry_t e;
} link_reg_t;

typedef void (*link_event_cb_t)(void);   /* 参数/注册表/状态变更通知（ws 推送）*/

void link_start(void);                    /* 建 UART + 任务 */
void link_loopback_run(void);             /* WP-F.1 真机自环自测（link_start 前独占 UART1，证据=[LPBK] 日志）*/
bool link_alive(void);                    /* 0x10 3s 内有新鲜帧 */
const link_status_t *link_status(void);   /* 快照指针（内部加锁，读后尽快用）*/

int  link_set_param(uint16_t id, float v);          /* → 0x12，异步等 ACK */
int  link_send_cmd(uint8_t cmd);                    /* → 0x15，异步等 ACK */
int  link_send_frame_mt(uint8_t mt, const uint8_t *pl, size_t n);  /* R2：0x20/0x21 载荷发送（fire-and-forget，ARQ 同 0x10 惯例=4 拷贝+收端去重）*/
int  link_get_registry(link_reg_t *out, int max);   /* 拷贝注册表缓存 */
void link_request_registry(void);                   /* 发 0x13（握手全量）*/

/* 最近一次 0x11 参数变更（pv 推送源：src=h743 触摸/CLI；含初值缓存）*/
typedef struct { uint16_t id; float v; int64_t us; } link_param_change_t;
const link_param_change_t *link_last_param(void);
float link_param_value(uint16_t id);       /* 注册表初值（无缓存则 NaN→前端用 min）*/

/* 最后一次 0x90 ACK（web_ack/clamp toast 用）*/
typedef struct { uint8_t status; float ret; uint16_t id; int64_t us; } link_ack_t;
const link_ack_t *link_last_ack(void);

void link_on_event(link_event_cb_t cb);   /* 注册 ws 推送回调（单播即可）*/

/* v2：H743（屏幕）下发的 0x15 CMD（8=急停 9=报警确认）——执行端在 ESP32 */
typedef void (*link_cmd_cb_t)(uint8_t cmd, uint8_t seq);
void link_on_cmd(link_cmd_cb_t cb);       /* 注册屏幕命令回调（应用层执行+回 ACK）*/

/* R2 协议换向：H743（屏）写参数帧到本端 → 应用层处理器落权威注册表+M207 verify，
 * 处理器须回调 link_send_ack 回执（破 tcs_core↔tcs_link 组件环的回调注入点）*/
typedef void (*link_setparam_cb_t)(uint16_t id, float v, uint8_t seq);
void link_on_setparam(link_setparam_cb_t cb);
void link_send_ack(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret);
