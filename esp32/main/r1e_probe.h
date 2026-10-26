/**
 * r1e_probe.h — R1e 自驱取证入口（CONFIG_TCS_R1E_PROBE=y 取证构建专用）
 */
#ifndef TCS_R1E_PROBE_H
#define TCS_R1E_PROBE_H

#ifdef __cplusplus
extern "C" {
#endif

void r1e_probe_run(void);   /* app_main 服务起齐后调用；阻塞式自驱三阶段 */

#ifdef __cplusplus
}
#endif
#endif /* TCS_R1E_PROBE_H */
