/**
 * tmp117_pure.h — TMP117 纯逻辑（ESP32 侧复用件；R1c，任务书 v11.3 第十一章）
 *
 * 健康机/换算逐字提取自 project_tcs/App/tcs/tmp117.c+h 纯函数区——Q5 单向同步：
 * 改健康机逻辑只许改 project_tcs 源侧再复制过来；R4 H743 瘦身后源侧删除即终局。
 * 提取时刻源 md5（2026-10-26）：tmp117.c `0018aa234ee6d75b36b206d4094253a5`
 * / tmp117.h `1c1b5ad62b54e59042ac1c5cc0b97a75`（docs/results/R1.md R1c 节）。
 * 滑动均值窗口为本侧新增纯函数（H743 为 poll 内联逻辑，语义一致：FIFO 窗口 8）。
 */
#ifndef TCS_TMP117_PURE_H
#define TCS_TMP117_PURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TMP117_LSB_C        0.0078125f
#define TMP117_AVG_WIN      8
#define TMP117_ERR_TRIP     10     /* 连续 10 错 → 掉线 */
#define TMP117_OK_RUN       3      /* 连续 3 成功 → 恢复数据 */

/* ====== 纯逻辑 1：raw→°C（project_tcs tmp117.c 逐字）====== */
float tmp117_raw_to_c(int16_t raw);

/* 健康计数器（每次采样结果喂一步；返回 body_sensor_ok 当前值 0/1）
 * alarm_set/alarm_clr_req 由 control 侧实现注入：掉线沿置 ALM_BODY_SENSOR；恢复沿只清 ok 不清报警位 */
typedef struct {
    uint8_t ok;             /* 当前 body_sensor_ok */
    uint8_t err_run;        /* 连续错误计数 */
    uint8_t ok_run;         /* 连续成功计数 */
} tmp117_health_t;

void tmp117_health_init(tmp117_health_t *h);
/* step=1 成功 / 0 错误。alarm_raise 在掉线沿被调一次。 */
void tmp117_health_step(tmp117_health_t *h, int step, void (*alarm_raise)(void));

/* ====== 纯逻辑 2：滑动均值窗口（本侧新增；语义=H743 poll 内联逻辑）====== */
typedef struct {
    float win[TMP117_AVG_WIN];
    int   idx;
    int   fill;
} tmp117_avg_t;

void  tmp117_avg_init(tmp117_avg_t *a);
void  tmp117_avg_push(tmp117_avg_t *a, float v);
float tmp117_avg_mean(const tmp117_avg_t *a);

#ifdef __cplusplus
}
#endif
#endif /* TCS_TMP117_PURE_H */
