/**
 * tcs_bootrec.h — 上次运行记录：落点与刷新（X04-F-03 / R5-13）
 *
 * 纯逻辑在 components/tcs_core/boot_record.c（主机单测 tests/test_tcs_core.c）；这里只管：
 *   - 记录放在 RTC 慢速存储的 NOINIT 段；
 *   - app_main 第一件事读复位原因、抄出上一轮、写入本轮、打印 [SYSTEM] 启动行；
 *   - 报告 flash 里是否留着上一次异常的 core dump（能解出任务名 / PC 就一并打印）；
 *   - 1Hz 把运行时长 / 报警位 / 锁存 / 控制状态刷回记录（纯 RAM 写，无磨损）；
 *   - 给 console 的 status 一行提供"本次 / 上次"字段。
 */
#ifndef TCS_BOOTREC_H
#define TCS_BOOTREC_H

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* app_main 的第一件事。必须在任何可能动 RTC NOINIT 段的初始化之前调用。*/
void tcs_bootrec_boot(void);

/* 起 1Hz 刷新定时器。失败原因经返回值交给调用方汇总（P1-12：svc_mask + ALM_SERVICE）。*/
esp_err_t tcs_bootrec_start_refresh(void);

/* 把"本次 / 上次"字段追加到 status 一行（键=值，空格分隔）。返回写入的字符数。*/
size_t tcs_bootrec_status(char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* TCS_BOOTREC_H */
