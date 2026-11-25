/**
 * tmp117_core.h — TMP117 采样服务的决策逻辑内核（主机可编译；S-G e2 自 tmp117_service.c 抽取）
 *
 * 一拍的完整决策——身份核对、配置写入与周期回读、读温度、样本合理性、均值窗、健康机、
 * 掉线/恢复沿、g_tcs 镜像——都在这里；I2C 出入口经 tmp117_io_t 注入，所以主机上可以用一个
 * 假器件逐拍驱动（esp32/tests/test_tmp117_core.c）。tmp117_service.c 只剩 IDF 外壳：
 * I2C 总线、采样任务、注入命令、上电自测。对外 API 仍声明在 tmp117_service.h，签名未变。
 *
 * 采样的三道门（任一道没过，这一拍就没有新样本、按错误步进健康机）：
 *   ① 身份：器件 ID 寄存器（0x0F）必须是 0x0117。没核对通过之前不读它的温度、不往里写一个字节
 *      （M13-F-08：0x48 上错装了别的器件时，它的寄存器 0x00 可能正好是 25–45°C）。
 *   ② 配置：写一次配置寄存器（连续转换、CONV=125 ms、AVG=8）并回读核对；之后约每秒回读一次
 *      代替当拍的温度读（M13-F-02：器件出厂默认转换周期是 1 s；配置若被改成关断，
 *      温度寄存器冻结而 I2C 照常应答，读数永远"健康"）。
 *   ③ 合理性：25–45°C 且 |Δ|≤2°C/s，dt 按真实时间算（M13-F-04）。
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

/* ====== 节律（毫秒；传 NULL 用默认值）====== */
typedef struct {
    uint32_t sample_ms;        /* 常规采样周期（100）*/
    uint32_t lost_ms;          /* 失联降频后的周期（1000）*/
    uint32_t id_retry_ms;      /* 器件在总线上但身份不符时的复核间隔（5000）；器件不在则每拍都查 */
    uint32_t cfg_check_ms;     /* 配置回读校验周期（1000）：到点的那一拍读配置、不读温度 */
    uint32_t cfg_rewrite_ms;   /* 配置不符时两次重写之间的最短间隔（1000）*/
} tmp117_cfg_t;

typedef struct {
    tmp117_io_t     io;
    tmp117_cfg_t    cfg;
    tmp117_health_t health;         /* 健康机：连续 10 错掉线、连续 3 成恢复 */
    tmp117_avg_t    avg;            /* 滑动均值窗 8 */
    float    last_valid_c;          /* 上个有效样本（合理性滤波基准）*/
    uint32_t last_valid_ms;         /* 上个有效样本的真实毫秒时刻 */
    uint8_t  have_valid;
    uint8_t  started;
    uint32_t tick;                  /* 采样拍计数（只作计数，不再当时基）*/
    /* 门①：身份 */
    uint8_t  id_ok;                 /* DEVID 已核对 = 0x0117 */
    uint8_t  id_seen;               /* 1=器件在但身份不符（按 id_retry_ms 限速复核）*/
    uint32_t last_id_ms;
    uint32_t id_fail_cnt;
    /* 门②：配置 */
    uint8_t  cfg_ok;                /* 最近一次回读与期望一致 */
    uint8_t  cfg_written;           /* 0=需要（重）写配置：首次、写失败、或回读不符且限速已到 */
    uint32_t last_cfg_ms;           /* 上次配置校验通过的时刻 */
    uint32_t last_rewrite_ms;       /* 上次写配置的时刻 */
    uint32_t cfg_bad_cnt;
    /* 统计 */
    uint32_t stat_err;              /* 累计通信/合理性错误 */
    uint32_t stat_samples;          /* 累计入窗样本 */
    uint32_t lost_edges, recover_edges;
#if TCS_SIM_ENABLED
    volatile float   sim_val;
    volatile uint8_t sim_on;
#endif
} tmp117_core_t;

/* 上电初始化（状态全清；io 须常驻）。不碰器件——身份核对与配置在第一拍里做。 */
void tmp117_core_init(tmp117_core_t *c, const tmp117_io_t *io, const tmp117_cfg_t *cfg);

/* 一个采样拍。now_ms=真实毫秒时基。返回 1=本拍样本被采纳，0=本拍按错误步进健康机。 */
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
