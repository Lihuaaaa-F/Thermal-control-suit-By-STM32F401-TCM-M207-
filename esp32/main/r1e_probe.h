/**
 * r1e_probe.h — R1e 自驱取证入口（CONFIG_TCS_R1E_PROBE=y 取证构建专用）
 */
#ifndef TCS_R1E_PROBE_H
#define TCS_R1E_PROBE_H

#include <stdint.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_TCS_R1E_PROBE   /* 与实现、调用三处同一个条件（M09-F-12 / X05-F-05）*/
void r1e_probe_run(void);   /* app_main 服务起齐后调用；阻塞式自驱三阶段 */
#endif

#ifdef __cplusplus
}
#endif
#endif /* TCS_R1E_PROBE_H */
