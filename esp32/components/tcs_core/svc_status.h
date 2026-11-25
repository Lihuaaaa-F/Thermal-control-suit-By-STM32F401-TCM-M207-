/**
 * svc_status.h — 服务启动结果汇总（P1-12）
 *
 * 现象：上电时 WiFi + HTTP + WS + PSRAM 已占内部堆，某次 xTaskCreatePinnedToCore 因堆不足回 pdFAIL
 * → 服务内部 return -1 → app_main 不接返回值 → 继续走到末尾打印 "... all up"。屏端没有快照、
 * 网页没有推送，日志却说全部就绪。
 *
 * 这里把"谁没起来"收成一张位图，启动序列结束后统一出报告：
 *   - 全部成功才打 "all up"；
 *   - 有失败：一条 E 级日志带上名单，**不打 all up**，返回失败位图——调用方据此置 ALM_SERVICE。
 * 接口没有返回值、结果无从核实的服务（link_start / web_start 是 void）如实记成"未核实"：
 * 既不算失败，也不冒充成功，各有 W 级日志提示。
 *
 * 纯逻辑、零 HAL；日志经回调（目标板上 main.c 接到 ESP_LOG）。
 * 主机用例 tests/test_tcs_core.c 的 S 系列；变异 tests/mutation_core.py 的 C3x。
 */
#ifndef TCS_SVC_STATUS_H
#define TCS_SVC_STATUS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 启动序里逐个登记的服务（位序 = 名单里的固定输出顺序）*/
#define SVC_BIT_SOFTAP   (1u << 0)
#define SVC_BIT_LINK     (1u << 1)
#define SVC_BIT_WEB      (1u << 2)
#define SVC_BIT_M207     (1u << 3)
#define SVC_BIT_TMP117   (1u << 4)
#define SVC_BIT_CTRL     (1u << 5)
#define SVC_BIT_CONSOLE  (1u << 6)
#define SVC_BIT_SNAP     (1u << 7)
#define SVC_BIT_BOOTREC  (1u << 8)
#define SVC_BIT_ALL      ((1u << 9) - 1u)

typedef struct {
    uint32_t noted;       /* 登记过的服务 */
    uint32_t failed;      /* 启动失败的服务 */
    uint32_t unverified;  /* 接口没有返回值、结果无从核实的服务 */
} svc_status_t;

#define SVC_LOG_I 0
#define SVC_LOG_W 1
#define SVC_LOG_E 2
typedef void (*svc_log_fn)(void *arg, int level, const char *text);

void     svc_init(svc_status_t *s);
/* 登记一个服务的启动结果：rv == 0 成功，非 0（-1、esp_err_t 都行）失败。
 * 位为 0、同时含多位、或超出 SVC_BIT_ALL：拒绝并返回 -1，状态不动。*/
int      svc_note(svc_status_t *s, uint32_t bit, int rv);
/* 登记一个"结果无从核实"的服务（接口是 void）*/
void     svc_unverified(svc_status_t *s, uint32_t bit);
int      svc_all_up(const svc_status_t *s);         /* 没有任何失败项（未核实的不算失败）*/
/* 位掩码 → 空格分隔的服务名（固定顺序）。返回写入的字符数（不含 NUL）；截断安全。*/
size_t   svc_names(uint32_t mask, char *out, size_t cap);
/* 汇总报告（每条日志各带完整的文本）：
 *   I 级 "services up: <已核实成功的名单>"——只有既没有失败也没有未核实项时才带 "(all up)"；
 *   有失败：一条 E 级 "services FAILED to start: <名单>"；
 *   每个未核实的服务各一条 W 级 "service start result not verifiable (void API): <名字>"。
 * 返回 failed：非 0 = 有服务没起来，调用方据此置 ALM_SERVICE。log 可为 NULL。*/
uint32_t svc_report(const svc_status_t *s, svc_log_fn log, void *arg);

#ifdef __cplusplus
}
#endif
#endif /* TCS_SVC_STATUS_H */
