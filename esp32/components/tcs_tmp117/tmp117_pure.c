/**
 * tmp117_pure.c — TMP117 纯逻辑实现（Q5：健康机/换算逐字提取自 project_tcs tmp117.c；
 * 均值窗口为本侧新增纯函数，语义与 H743 poll 内联一致）
 */
#include "tmp117_pure.h"

/* ====== 纯逻辑 1（project_tcs tmp117.c 逐字）====== */
float tmp117_raw_to_c(int16_t raw)
{
    return (float)raw * TMP117_LSB_C;
}

/* ====== 配置寄存器比对（M13-F-02；S-G e2 新增）：只比可读写字段 ====== */
uint8_t tmp117_cfg_matches(uint16_t cfg)
{
    return ((cfg & TMP117_CFG_RW_MASK) == (TMP117_CFG_WORD & TMP117_CFG_RW_MASK)) ? 1u : 0u;
}

void tmp117_health_init(tmp117_health_t *h)
{
    h->ok = 1;
    h->err_run = 0;
    h->ok_run = 0;
}

void tmp117_health_step(tmp117_health_t *h, int step, void (*alarm_raise)(void))
{
    if (step) {
        h->err_run = 0;
        if (h->ok_run < 255u) h->ok_run++;
        if (!h->ok && h->ok_run >= TMP117_OK_RUN) {
            /* 恢复：只恢复数据流；报警位保持，人工确认清除（F3）*/
            h->ok = 1;
        }
    } else {
        h->ok_run = 0;
        if (h->err_run < 255u) h->err_run++;
        if (h->ok && h->err_run >= TMP117_ERR_TRIP) {
            h->ok = 0;
            if (alarm_raise) alarm_raise();   /* 掉线沿：置 ALM_BODY_SENSOR + SAFE */
        }
    }
}

/* ====== 纯逻辑 2：滑动均值窗口（新增）====== */
void tmp117_avg_init(tmp117_avg_t *a)
{
    a->idx = 0;
    a->fill = 0;
}

void tmp117_avg_push(tmp117_avg_t *a, float v)
{
    a->win[a->idx] = v;
    a->idx = (a->idx + 1) % TMP117_AVG_WIN;
    if (a->fill < TMP117_AVG_WIN) a->fill++;
}

float tmp117_avg_mean(const tmp117_avg_t *a)
{
    if (a->fill == 0) return 0.0f;
    float sum = 0.0f;
    for (int i = 0; i < a->fill; i++) sum += a->win[i];
    return sum / (float)a->fill;
}
