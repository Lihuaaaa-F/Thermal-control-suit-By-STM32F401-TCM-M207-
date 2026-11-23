/**
 * web_status.h — 网页控制台的状态快照 / 变化检测 / 事件日志（WS_PROTOCOL.md v2）
 *
 * 纯逻辑：只依赖 cJSON 与协议头，不碰 ESP 外设/任务 API——主机单测
 * tests/test_web_status.c 直接编译，用真实 cJSON 校验"固件发出来的 JSON"与前端/mock 期望
 * 逐字段一致。取数（g_tcs / control / link / esp_timer）在 websocket.c 的 gather() 里。
 *
 * 并发（设备上）：写日志的可能是 httpd 任务（客户端命令事件）与 WS 推送任务（状态差分事件），
 * 读日志的是 httpd 任务（/api/log、连接首推）与 WS 推送任务（增量推送）。
 * 环形缓冲的索引与整条拷贝都在 WEB_LOCK 里做（几微秒的临界区）；格式化在锁外。
 */
#ifndef WEB_STATUS_H
#define WEB_STATUS_H

#include <stdint.h>
#include <stddef.h>
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 状态快照（一份 st 消息的全部业务字段）====== */
typedef struct {
    uint32_t ts_ms;            /* 设备开机毫秒 */
    float    tc1_act, tc2_act; /* 两路实际水温 */
    float    body;             /* 体温 */
    uint16_t age_ms;           /* 两路水温中较旧一路的采样龄；0xFFFF=未知 */
    float    tc1_set, tc2_set; /* 两路设定 */
    uint8_t  tec1, tec2, fan;  /* 执行器开关 */
    uint8_t  mode;             /* 0 手动 / 1 自动 */
    uint8_t  state;            /* ctrl_state_t：0 待机 / 1 制冷 / 2 制热 / 3 安全态 */
    uint16_t alarm;            /* ALM_* 位 */
    uint8_t  latch;            /* SAFETY_BIT_* 位 */
    uint8_t  armed;            /* post_arm_t：0 未布防 / 1 已布防 / 2 丢失报警中 */
    uint8_t  m207, body_ok, sens_open, screen;   /* 模块在线 / 体温传感器有效 / 水温探头断线 / 屏幕链路存活 */
    uint32_t up_s;             /* 开机秒数 */
    uint8_t  reset;            /* esp_reset_reason_t 原始值 */
    uint32_t heap;             /* 空闲堆字节 */
} web_status_t;

/* 把快照写成 st 对象的业务字段（不含 t/seq/ver/fw）。非有限数一律写 0；
 * 浮点四舍五入到 2 位小数（JSON 里不出现 18.469999 这类尾数）。返回 0=成功，-1=分配失败。*/
int web_status_fill_json(const web_status_t *s, cJSON *root);

/* 两份快照在"使用者关心的字段"上是否有差别（ts_ms / heap 的起伏不算变化）。*/
int web_status_changed(const web_status_t *a, const web_status_t *b);

/* ====== 事件日志（环形，保留最新 WEB_LOG_N 条；按绝对序号访问）====== */
#define WEB_LOG_N        50u
#define WEB_LOG_EV_MAX   16u
#define WEB_LOG_MSG_MAX  72u
/* 一条日志写成 JSON 的最坏长度（ts/lvl/ev/msg 各字段 + 引号括号）；全量缓冲按它预留 */
#define WEB_LOG_ENTRY_MAX 176u
#define WEB_LOG_JSON_CAP (64u + WEB_LOG_N * WEB_LOG_ENTRY_MAX)

typedef struct {
    uint32_t ts_ms;
    uint8_t  lvl;                              /* 'i' info / 'w' warn / 'a' alarm */
    char     ev[WEB_LOG_EV_MAX];               /* 稳定的事件码（前端可按码做特殊展示）*/
    char     msg[WEB_LOG_MSG_MAX];             /* 中文一句话 */
} web_log_t;

void     web_log_reset(void);
uint32_t web_log_total(void);                  /* 开机以来写入的总条数（单调递增）*/

/* 追加一条。ev/msg 超长时按 UTF-8 字符边界截断，绝不劈开一个汉字。*/
void web_log_add(uint32_t ts_ms, char lvl, const char *ev, const char *msg);
/* 同上，msg 用 printf 格式在栈上格式化（缓冲 WEB_LOG_MSG_MAX 字节）。*/
void web_log_addf(uint32_t ts_ms, char lvl, const char *ev, const char *fmt, ...);

/* 按绝对序号取一条的拷贝。序号早于环里最旧的一条 → 返回 -1（已被覆盖）。*/
int web_log_get(uint32_t abs_idx, web_log_t *out);

/* 对"上一份 → 当前"做差分，把发生的事件写进日志环；返回新增条数。
 * prev->ts_ms == 0 视为首次：只建基线，不产生事件。*/
unsigned web_status_diff_log(const web_status_t *prev, const web_status_t *now);

/* 把绝对序号区间 [from, to) 的日志写成一条 log 消息：{"t":"log","list":[...]}。
 * from 早于环里最旧的一条时自动抬到最旧（调用方不必关心环是否回绕）。
 * 返回序列化后的字节数（不含结尾 NUL）；区间为空、cap 不够或分配失败返回 0。*/
size_t web_log_json(uint32_t from, uint32_t to, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* WEB_STATUS_H */
