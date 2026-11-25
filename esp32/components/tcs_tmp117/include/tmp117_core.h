/**
 * tmp117_core.h — TMP117 采样服务的决策逻辑内核（主机可编译；S-G e2 自 tmp117_service.c 抽取）
 *
 * 一拍的完整决策——读器件、样本合理性、均值窗、健康机、掉线/恢复沿、g_tcs 镜像——都在这里；
 * I2C 出入口经 tmp117_io_t 注入，所以主机上可以用一个假器件逐拍驱动
 * （esp32/tests/test_tmp117_core.c）。tmp117_service.c 只剩 IDF 外壳：I2C 总线、采样任务、注入命令。
 * 对外 API 仍声明在 tmp117_service.h，签名未变。
 */
#ifndef TCS_TMP117_CORE_H
#define TCS_TMP117_CORE_H

#include <stdint.h>
#include "tmp117_pure.h"

#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* 注入能力开关（R1 修复 P0-06 / AD-5）：只有 SIM 或 R1e 取证构建才编译注入实现。
 * 生产固件里 s_sim_on 分支与注入函数都不存在——一条命令注入假体温的通道被移除。 */
#if CONFIG_TCS_SIM || CONFIG_TCS_R1E_PROBE
#define TCS_SIM_ENABLED 1
#else
#define TCS_SIM_ENABLED 0
#endif

/* ====== I2C 出入口（服务提供；主机测试给假器件）====== */
typedef struct {
    int (*read_reg)(void *ctx, uint8_t reg, uint8_t out[2]);   /* 读 16 位寄存器（大端）；0=成功 */
    int (*write_reg)(void *ctx, uint8_t reg, uint16_t val);    /* 写 16 位寄存器；0=成功 */
    void *ctx;
} tmp117_io_t;

typedef struct {
    tmp117_io_t     io;
    tmp117_health_t health;         /* 健康机：连续 10 错掉线、连续 3 成恢复 */
    tmp117_avg_t    avg;            /* 滑动均值窗 8 */
    float    last_valid_c;          /* 上个有效样本（合理性滤波基准）*/
    uint32_t last_valid_tick;       /* 上个有效样本的拍号 */
    uint8_t  have_valid;
    uint8_t  started;
    uint32_t tick;                  /* 采样拍计数 */
    uint32_t stat_err;              /* 累计通信/合理性错误 */
    uint32_t stat_samples;          /* 累计入窗样本 */
    uint32_t lost_edges, recover_edges;
#if TCS_SIM_ENABLED
    volatile float   sim_val;
    volatile uint8_t sim_on;
#endif
} tmp117_core_t;

/* 上电初始化（状态全清；io 须常驻）。不碰器件。 */
void tmp117_core_init(tmp117_core_t *c, const tmp117_io_t *io);

/* 一个采样拍：读一次温度、过合理性、入窗、步进健康机、写 g_tcs.body_temp / body_sensor_ok。
 * now_ms=真实毫秒时基。返回 1=本拍样本被采纳，0=本拍按错误计。 */
int tmp117_core_tick(tmp117_core_t *c, uint32_t now_ms);

/* 下一拍的周期（ms）：失联后降到 1s 试联。 */
uint32_t tmp117_core_period_ms(const tmp117_core_t *c);

uint8_t tmp117_core_ok(const tmp117_core_t *c);     /* 健康机 ok 镜像 */
uint8_t tmp117_core_sim_active(const tmp117_core_t *c);   /* 任何构建都可调：注入关断时恒 0 */

#if TCS_SIM_ENABLED
void tmp117_core_sim_set(tmp117_core_t *c, float v);
void tmp117_core_sim_disable(tmp117_core_t *c);
#endif

#ifdef __cplusplus
}
#endif
#endif /* TCS_TMP117_CORE_H */
