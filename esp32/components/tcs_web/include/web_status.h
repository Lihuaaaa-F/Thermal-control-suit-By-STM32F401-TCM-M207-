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

/* 两份快照在"使用者关心的字段"上是否有差别（ts_ms / heap / age_ms 的起伏不算变化：
 * age 每 50ms 量化一跳，算进去的话每拍都"变了"，500ms 心跳就永远走不到）。
 * 水温/体温按发出去的两位小数比较——原始 float 的末位抖动不算变化。*/
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
/* 最新一条的拷贝；一条都没有 → -1。*/
int web_log_last(web_log_t *out);

/* 对"上一份 → 当前"做差分，把发生的事件写进日志环；返回新增条数。
 * prev->ts_ms == 0 视为首次：只建基线，不产生事件。*/
unsigned web_status_diff_log(const web_status_t *prev, const web_status_t *now);

/* 把绝对序号区间 [from, to) 的日志写成一条 log 消息：{"t":"log","list":[...]}。
 * from 早于环里最旧的一条时自动抬到最旧（调用方不必关心环是否回绕）。
 * buf 里是结尾带 NUL 的 C 串；返回串长（不含 NUL）。区间为空、cap 放不下整串（含 NUL）
 * 或分配失败返回 0。*/
size_t web_log_json(uint32_t from, uint32_t to, char *buf, size_t cap);

/* ====== link 消息（GET /api/linkstats 的回包）====== */
#define WEB_LINK_N 12u
/* 12 个屏链路计数的 JSON 键；顺序即 web_link_json 的 cnt[] 顺序 */
extern const char *const web_link_keys[WEB_LINK_N];
/* 写成一条 link 对象：{"t":"link",<12 个计数>,"ssid":..,"pass":..,"url":..}。
 * 热点名/密码是使用者可改的自由文本，一律经 cJSON 转义（手拼遇到引号整包就废）。
 * buf 里是结尾带 NUL 的 C 串；返回串长（不含 NUL）。cap 放不下整串、任一指针为空
 * 或分配失败返回 0。*/
size_t web_link_json(const uint32_t cnt[WEB_LINK_N], const char *ssid, const char *pass,
                     const char *url, char *buf, size_t cap);

/* ====== reg 消息（连接建立时推一次的参数表）====== */
#include "param_registry.h"
/* float 用"最短且能还原成同一个 float"的写法交给 cJSON。参数表里是 float，直接按 double
 * 打印会带出 0.10000000149011612 这样的尾数——网页参数页的小数位因此变成 17 位
 * （独立复核 P0）。非有限数归 0（与 st 同口径）。*/
double web_num_short(float v);
/* 把 n 条参数写成 reg 消息：{"t":"reg","list":[{id,type,flags,min,max,step,def,key,name,unit}…]}。
 * ps 是参数描述符指针数组（空指针的位子跳过）；数值一律经 web_num_short。
 * 返回 0=成功，-1=分配失败（root 里可能已有部分内容）。
 * "def" 是**当前值**（字段名沿用 v1 契约）。*/
int web_registry_fill_json(const param_desc_t *const *ps, int n, cJSON *root);
/* 同上并序列化进 buf（结尾带 NUL 的 C 串）；返回串长，cap 不够或失败返回 0。*/
size_t web_registry_json(const param_desc_t *const *ps, int n, char *buf, size_t cap);

/* ====== POST /api/ota?m=… 的 m 取值（M14-F-13）======
 * 入参是查询串里 m 这一项的值（httpd_query_key_value 取出来、已做百分号解码）。
 * 返回 0 = 自拷贝 + 切换；1 = 回滚；-1 = 不是精确的 "0" 或 "1"。
 * 两种模式都会重启设备，所以输入不合法时什么都不做（调用方回 400）——以前是在查询串里找
 * 子串 "m=1"（?m=10、?m=1x 也当回滚），取不到 m 时静默按 0 拷贝 + 重启。*/
int web_ota_mode_value(const char *v);

#ifdef __cplusplus
}
#endif
#endif /* WEB_STATUS_H */
