/**
 * tmp117.c — TMP117 体温采样实现（任务书 §3.5）
 *
 * 硬件 I2C1（PB8/PB9 AF4 @100kHz，飞线外接 4.7kΩ 上拉——接线表 #5/#6）。
 * HAL_I2C_Mem_Read 读 0x00 寄存器 2B 大端；滑动均值窗口 8；健康判定见头文件。
 */
#include "tmp117.h"

/* ====== 纯逻辑 ====== */
float tmp117_raw_to_c(int16_t raw)
{
    return (float)raw * TMP117_LSB_C;
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

/* ====== 目标机驱动 ====== */
#ifndef TCS_UNIT_TEST

#include "stm32h7xx_hal.h"
#include "param_registry.h"     /* g_tcs */
#include "control.h"            /* tcs_alarm_raise */
#include "segger_rtt.h"

static I2C_HandleTypeDef *s_i2c;
static tmp117_health_t s_health;
static float  s_win[TMP117_AVG_WIN];
static int    s_win_idx;
static int    s_win_fill;
static uint32_t s_last_ms;

static void on_sensor_lost(void)
{
    tcs_alarm_raise(ALM_BODY_SENSOR);
    SEGGER_RTT_WriteString("[ERROR] TMP117 LOST -> ALM_BODY_SENSOR\n");
}

void tmp117_init(I2C_HandleTypeDef *hi2c)
{
    s_i2c = hi2c;
    tmp117_health_init(&s_health);
    memset(s_win, 0, sizeof(s_win));
    s_win_idx = 0;
    s_win_fill = 0;
    s_last_ms = 0;
    g_tcs.body_sensor_ok = 1;
    g_tcs.body_temp = 0.0f;
}

void tmp117_poll(uint32_t now_ms)
{
    if (now_ms - s_last_ms < TMP117_SAMPLE_MS) return;
    s_last_ms = now_ms;

    uint8_t buf[2];
    int ok = 0;
    if (HAL_I2C_Mem_Read(s_i2c, (uint16_t)(TMP117_ADDR7 << 1), TMP117_REG_TEMP,
                         I2C_MEMADD_SIZE_8BIT, buf, 2, 20u) == HAL_OK) {
        int16_t raw = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
        float c = tmp117_raw_to_c(raw);
        s_win[s_win_idx] = c;
        s_win_idx = (s_win_idx + 1) % TMP117_AVG_WIN;
        if (s_win_fill < TMP117_AVG_WIN) s_win_fill++;
        /* 滑动均值 */
        float sum = 0.0f;
        for (int i = 0; i < s_win_fill; i++) sum += s_win[i];
        g_tcs.body_temp = sum / (float)s_win_fill;
        ok = 1;
    }
    tmp117_health_step(&s_health, ok, on_sensor_lost);
    g_tcs.body_sensor_ok = s_health.ok;
}

#endif /* !TCS_UNIT_TEST */
