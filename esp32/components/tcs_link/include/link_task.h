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
    uint8_t  ver;                /* M11-F-09：对端协议版本（原网页硬编码 1，版本不符看不出来）*/
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
/* M11-F-12：整块快照拷贝（临界区内一次结构体赋值）。取代原 link_status() 指针——
 * 指针让调用方在锁外逐字段读，与链路任务写 0x10 交错会拼出"新温度+旧报警"混合态。*/
void link_status_copy(link_status_t *out);

/* M11-F-10：链路诊断计数（原来只有"收了多少"，出故障分不出类型）。
 * 全部自上次复位累计；RX 侧来自 proto 状态机分桶，TX 侧来自 comm 层。*/
typedef struct {
    uint32_t rx_bytes, rx_frames;
    uint32_t crc_err, len_err;       /* 帧校验失败 / 长度字段非法 */
    uint32_t gap_resets;             /* 半帧静默丢弃（M04-F-03）*/
    uint32_t dup;                    /* 重复帧（对端重传）*/
    uint32_t retry;                  /* 本端累计重传 */
    uint32_t txq_drop, pending_evict;/* 发送/待确认溢出 */
    uint32_t proto_mismatch;         /* STATUS 版本不符帧数（M11-F-09）*/
} link_stats_t;
void link_stats_get(link_stats_t *out);

int  link_send_cmd(uint8_t cmd);                    /* → 0x15，异步等 ACK */
int  link_send_param(uint16_t id, float v);         /* → 0x11（反向镜像：ESP32 权威 → 屏显示）*/
int  link_send_frame_mt(uint8_t mt, const uint8_t *pl, size_t n);  /* R2：0x20/0x21 载荷发送（fire-and-forget，免 ARQ：0x20/0x21 走 comm_send_noarq）；返回 0=入队 1=演示抑制 -1=组帧失败 */
int  link_get_registry(link_reg_t *out, int max);   /* 拷贝注册表缓存 */
void link_request_registry(void);                   /* 发 0x13（握手全量）*/

/* 最近一次 0x11 参数变更（pv 推送源：src=h743 触摸/CLI；含初值缓存）*/
typedef struct { uint16_t id; float v; int64_t us; } link_param_change_t;
/* 最近一次 0x11 快照拷贝。指针版会在锁外逐字段读，与链路任务写交错时出现
 * "新 id + 旧值"（与 M11-F-12 同类），故只提供整块拷贝。*/
void link_last_param_copy(link_param_change_t *out);
float link_param_value(uint16_t id);       /* 注册表初值（无缓存则 NaN→前端用 min）*/

void link_on_event(link_event_cb_t cb);   /* 注册 ws 推送回调（单播即可）*/
void link_demo_note_param(uint16_t id, float v);   /* 演示模型同步（CONFIG_TCS_DEMO 时有效）*/

/* v2：H743（屏幕）下发的 0x15 CMD（8=急停 9=报警确认）——执行端在 ESP32 */
typedef void (*link_cmd_cb_t)(uint8_t cmd, uint8_t seq);
void link_on_cmd(link_cmd_cb_t cb);       /* 注册屏幕命令回调（应用层执行+回 ACK）*/

/* R2 协议换向：H743（屏）写参数帧到本端 → 应用层处理器落权威注册表+M207 verify，
 * 处理器须回调 link_send_ack 回执（破 tcs_core↔tcs_link 组件环的回调注入点）*/
typedef void (*link_setparam_cb_t)(uint16_t id, float v, uint8_t seq);
void link_on_setparam(link_setparam_cb_t cb);
void link_send_ack(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret);
