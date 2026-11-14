/**
 * tmp117.c — TMP117 体温采样实现（任务书 §3.5）
 *
 * 硬件 I2C1（PB8/PB9 AF4 @100kHz，飞线外接 4.7kΩ 上拉——接线表 #5/#6）。
 * HAL_I2C_Mem_Read 读 0x00 寄存器 2B 大端；滑动均值窗口 8；健康判定见头文件。
 * TCS_SIM 构建：假体温源（tmp117_sim_set/off）驱动 FSM/报警全路径（WP-G）。
 */
#include "tmp117.h"
#include "post.h"               /* H6.7 合理性滤波 post_body_sanity_ok */
#include <stdio.h>              /* snprintf：F14 浮点日志预格式化 */

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

#ifdef TCS_SIM
/* WP-G 板内仿真：假体温源（sim body <val> / sim body off）*/
static float   s_sim_body = 36.8f;
static uint8_t s_sim_on = 1;
void tmp117_sim_set(float v)
{
    s_sim_body = v;
    s_sim_on = 1;
    /* F14：RTT printf 无 %f（自研与 SEGGER 官方实现皆然），浮点必须 snprintf 预格式化 */
    char b[96];
    snprintf(b, sizeof b, "[SIM] body temp forced %.1f\n", (double)v);
    SEGGER_RTT_WriteString(b);
}
void tmp117_sim_disable(void)
{
    s_sim_on = 0;
    SEGGER_RTT_WriteString("[SIM] body temp sim OFF (sensor-loss path)\n");
}
#endif

static I2C_HandleTypeDef *s_i2c;
static tmp117_health_t s_health;
static float  s_win[TMP117_AVG_WIN];
static int    s_win_idx;
static int    s_win_fill;
static uint32_t s_last_ms;
static float    s_last_valid_c;     /* H6.7：上一有效样本（合理性滤波基准）*/
static uint32_t s_last_valid_ms;
static uint8_t  s_have_valid;

static void on_sensor_lost(void)
{
    /* H6 动态布防：布防态由 post 三态机裁决（0x08=auto/强制布防→armed）*/
    if (!post_armed()) {
        SEGGER_RTT_WriteString("[WARN] TMP117 LOST (unarmed, log only)\n");
        return;
    }
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
    s_have_valid = 0;
    s_last_valid_c = 0.0f;
    s_last_valid_ms = 0;
    g_tcs.body_sensor_ok = 1;
    g_tcs.body_temp = 0.0f;
}

int tmp117_read_devid(uint16_t *id)
{
    uint8_t buf[2];
    if (HAL_I2C_Mem_Read(s_i2c, (uint16_t)(TMP117_ADDR7 << 1), TMP117_REG_DEVID,
                         I2C_MEMADD_SIZE_8BIT, buf, 2, 20u) != HAL_OK)
        return -1;
    *id = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    return 0;
}

/* 单样本接受：I2C 成功 + H6.7 合理性（25~45°C 且 |Δ|≤2°C/s）。不合理按通信错误计。*/
static int sample_accept(float c, uint32_t now_ms)
{
    /* R1 修复 M06-F-09：首样本也要过 25–45°C 范围检查（原实现直接放行，与 post.h 注释相反）。
     * dt=0 → post_body_sanity_ok 只查范围、不查变化率。*/
    if (!s_have_valid) return post_body_sanity_ok(c, c, 0.0f);
    float dt = (float)(now_ms - s_last_valid_ms) / 1000.0f;
    return post_body_sanity_ok(s_last_valid_c, c, dt);
}

void tmp117_poll(uint32_t now_ms)
{
    if (now_ms - s_last_ms < TMP117_SAMPLE_MS) return;
    s_last_ms = now_ms;

#ifdef TCS_SIM
    if (s_sim_on) {                       /* 假体温：直接进滑动均值，恒 ok */
        s_win[s_win_idx] = s_sim_body;
        s_win_idx = (s_win_idx + 1) % TMP117_AVG_WIN;
        if (s_win_fill < TMP117_AVG_WIN) s_win_fill++;
        float sum = 0.0f;
        for (int i = 0; i < s_win_fill; i++) sum += s_win[i];
        g_tcs.body_temp = sum / (float)s_win_fill;
        tmp117_health_step(&s_health, 1, on_sensor_lost);
        g_tcs.body_sensor_ok = s_health.ok;
        return;
    }
#endif

    uint8_t buf[2];
    int ok = 0;
    if (HAL_I2C_Mem_Read(s_i2c, (uint16_t)(TMP117_ADDR7 << 1), TMP117_REG_TEMP,
                         I2C_MEMADD_SIZE_8BIT, buf, 2, 20u) == HAL_OK) {
        int16_t raw = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
        float c = tmp117_raw_to_c(raw);
        if (sample_accept(c, now_ms)) {   /* H6.7：不合理样本按错误计，不入窗口 */
            s_win[s_win_idx] = c;
            s_win_idx = (s_win_idx + 1) % TMP117_AVG_WIN;
            if (s_win_fill < TMP117_AVG_WIN) s_win_fill++;
            /* 滑动均值 */
            float sum = 0.0f;
            for (int i = 0; i < s_win_fill; i++) sum += s_win[i];
            g_tcs.body_temp = sum / (float)s_win_fill;
            s_last_valid_c = c;
            s_last_valid_ms = now_ms;
            s_have_valid = 1;
            ok = 1;
        } else {
            char b[96];         /* F14：同上，%f 预格式化 */
            snprintf(b, sizeof b, "[WARN] tmp117 implausible sample %.2f dropped\n",
                     (double)c);
            SEGGER_RTT_WriteString(b);
        }
    }
    tmp117_health_step(&s_health, ok, on_sensor_lost);
    g_tcs.body_sensor_ok = s_health.ok;
}

#endif /* !TCS_UNIT_TEST */
