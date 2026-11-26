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
#include "esp_err.h"

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

/* 建 UART + 任务。P1-12 的另一半（S 线 i3）：返回 ESP_OK = 任务已起；ESP_ERR_NO_MEM = 任务没建成
 * （UART 驱动已卸掉）——app_main 的启动汇总据此置 ALM_SERVICE。UART 驱动安装 / 配置失败仍是
 * ESP_ERROR_CHECK（硬件级故障，照旧中止）。*/
esp_err_t link_start(void);               /* 建 UART + 任务 */
void link_loopback_run(void);             /* WP-F.1 开机自环自测（link_start 前独占 UART1，证据=[LPBK] 日志）：
                                            * 屏 / 主控两个身份的链路实例共用一条芯片内回环线 */
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
    uint32_t uart_txdrop;         /* 驱动 TX 环满丢弃（2026-11-23：恒增=链路实际没发出去）*/
    uint32_t echo_drop;          /* 回声：本端发出的帧被回送、按方向位丢弃的帧数（comm 层方向闸门，R5-01；
                                    只在接线/串扰异常或 linkloop 1 时增长）。2026-11-25 之前是"发送窗内
                                    收到等长帧"的计数，口径不同。*/
    uint32_t wrongdir;           /* 方向位说是对端、但方向表不允许的消息号（坏帧或版本不匹配的对端；不是回声）*/
    uint32_t tx_wrongdir;        /* 自检：本端发出了方向表不允许自己发的消息号（应为 0）*/
    uint32_t tx_fail;            /* X03-F-09：link_uart_send 报"没有上线"的帧数（驱动环满 / 诊断静默期；
                                    等于 uart_txdrop + 静默期丢的帧）*/
    uint8_t  arq_ok;             /* comm 层 link_ok：1=最近一次有 ACK / 尚未判死，0=有一帧等 ACK 重传耗尽 */
    uint32_t echo_param;         /* 回声里的 0x11（参数镜像）*/
    uint32_t echo_snap;          /* 回声里的 0x20/0x21（快照/增量）*/
    uint32_t echo_info;          /* 回声里的 0x22（设备信息）*/
    uint32_t echo_ack;           /* 回声里的 0x90（本端应答）*/
    uint32_t echo_getreg;        /* 回声里的 0x13（请求注册表）*/
    uint32_t proto_mismatch;         /* STATUS 版本不符帧数（M11-F-09）*/
    uint8_t  peer_ver;               /* 最近一帧 STATUS 里对端报的协议版本（0 = 还没收到过）——M11-F-09：
                                        网页黄条要用"实际收到的 ver"，不能再用本地常量 */
} link_stats_t;
void link_stats_get(link_stats_t *out);

/* M11-F-09：协议版本不符的提示位。对端 0x10 STATUS 的 ver != TCS_PROTO_VER 时返回 ALM_PROTO，
 * 版本对上以后清回 0；计数在 link_stats_t.proto_mismatch（只增）。
 * 由 tcs_snap.c 在组 0x20/0x21 时按位或进 alarm 字段——快照的 alarm 是"发送那一刻"的值，
 * 不受控制任务每拍重建 g_tcs.alarm_bits 的影响。g_tcs 的消费者（网页 / 屏）要显示这一位，
 * 需要在各自那一路登记（见 docs/results/S_G_e1_结果.md）。*/
uint16_t link_alarm_hint(void);
uint32_t link_proto_mismatch(void);

int  link_send_cmd(uint8_t cmd);                    /* → 0x15，异步等 ACK */
int  link_send_param(uint16_t id, float v);         /* → 0x11（仅 ESP32→H743：权威镜像 → 屏显示跟随；方向表见 protocol/tcs_proto.h）*/
/* → 0x22 设备信息（热点名/密码/网址/固件版本；载荷由调用方按 tcs_pl_info 组好）*/
int  link_send_info(const uint8_t *pl, size_t n);
int  link_send_frame_mt(uint8_t mt, const uint8_t *pl, size_t n);  /* R2：0x20/0x21 载荷发送（fire-and-forget，免 ARQ：0x20/0x21 走 comm_send_noarq）；返回 0=入队 1=演示抑制 -1=组帧失败 */
int  link_get_registry(link_reg_t *out, int max);   /* 拷贝注册表缓存 */
void link_request_registry(void);                   /* 发 0x13（握手全量）*/

/* 最近一次屏的参数写入（0x12）：pv 推送源；含初值缓存 */
typedef struct { uint16_t id; float v; int64_t us; } link_param_change_t;
/* 最近一次屏写参数的快照拷贝。指针版会在锁外逐字段读，与链路任务写交错时出现
 * "新 id + 旧值"（与 M11-F-12 同类），故只提供整块拷贝。*/
void link_last_param_copy(link_param_change_t *out);
float link_param_value(uint16_t id);       /* 注册表初值（无缓存则 NaN→前端用 min）*/

/* m1（模式页，2026-11-26）：屏链路是否断开的事实来源——"屏链路断开 3 秒 = 急停"由它计时
 * （判定在 tcs_core/link_loss_guard.c，调用在 control_service.c）。
 * 上电后收到过屏（对端方向）的合法帧 → 返回 1，*age_ms = 距最近一帧的毫秒数；从未收到 → 返回 0。
 * 只有过了方向闸门的帧才算：本端回送帧、方向不符的帧、坏帧都不刷新。*/
int link_peer_age_ms(uint32_t *age_ms);

void link_on_event(link_event_cb_t cb);   /* 注册 ws 推送回调（单播即可）*/
void link_demo_note_param(uint16_t id, float v);   /* 演示模型同步（CONFIG_TCS_DEMO 时有效）*/
uint8_t link_demo_running(void);   /* 演示态接管中（网页 stale 判据要放过它）*/

/* v2：H743（屏幕）下发的 0x15 CMD（8=急停 9=报警确认）——执行端在 ESP32 */
typedef void (*link_cmd_cb_t)(uint8_t cmd, uint8_t seq);
void link_on_cmd(link_cmd_cb_t cb);       /* 注册屏幕命令回调（应用层执行+回 ACK）*/

/* R2 协议换向：H743（屏）写参数帧到本端 → 应用层处理器落权威注册表+M207 verify，
 * 处理器须回调 link_send_ack 回执（破 tcs_core↔tcs_link 组件环的回调注入点）*/
typedef void (*link_setparam_cb_t)(uint16_t id, float v, uint8_t seq);
void link_on_setparam(link_setparam_cb_t cb);
void link_send_ack(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret);

#if CONFIG_TCS_SIM || CONFIG_TCS_DIAG
/* 链路诊断（现场取证用）：静默 N 毫秒 / 芯片内部 TX→RX 回环。见 link_task.c。
 * esp_err.h 只在这段里需要：bc36788 提交时只在默认档（PROD，不编译这一段）编译验证过，
 * 到了真正编译这一段的 SIM/DIAG 档才暴露"unknown type name 'esp_err_t'"。*/
#include "esp_err.h"
uint8_t    link_muted(void);
void       link_mute_ms(uint32_t ms);
uint32_t   link_mute_rxdrop(void);
esp_err_t  link_loopback(uint8_t on);
#endif
