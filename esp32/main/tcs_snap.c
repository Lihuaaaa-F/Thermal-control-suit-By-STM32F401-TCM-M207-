/**
 * tcs_snap.c — R2 链路快照发送实现
 *
 * 数据源：g_tcs（双实际/tec/fan/mode/alarm/online）+ param（双设定）+
 * m207_temp_age_ms（新鲜度）+ post_arm_state（badge）。
 * 0x21 触发：与上次发送的差异位图非零即发（同拍先 0x21 后 0x20）。
 */
#include "tcs_snap.h"
#include "tcs_proto.h"
#include "link_task.h"
#include "param_registry.h"
#include "m207_service.h"
#include "post.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

#define TAG "SNAP"

#define SNAP_PERIOD_MS  200u

static TaskHandle_t s_task;
static uint32_t s_enc_max_us;
static uint32_t s_snap_cnt, s_delta_cnt;
static uint32_t s_seq;
static tcs_delta_src_t s_last;      /* 上次已发状态（差分基准）*/
static uint8_t s_last_valid;

static void build_delta_src(tcs_delta_src_t *d)
{
    d->tc1_set = param_get(PARAM_ID_TC1_SET);
    d->tc2_set = param_get(PARAM_ID_TC2_SET);
    d->body    = g_tcs.body_temp;
    d->tec     = (uint8_t)((g_tcs.tec1_on ? 1 : 0) | (g_tcs.tec2_on ? 2 : 0));
    d->fan     = g_tcs.fan_on;
    d->mode    = g_tcs.mode;
    d->alarm   = g_tcs.alarm_bits;
    d->badge   = (uint8_t)post_arm_state();
}

static void snap_task(void *arg)
{
    esp_task_wdt_add(NULL);   /* R1 P1-07：关键任务纳入 TWDT（原来只有 ctrl_svc）*/
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t p_win = 0;
    for (;;) {
        esp_task_wdt_reset();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SNAP_PERIOD_MS));
        int32_t t0 = (int32_t)esp_timer_get_time();

        /* ① 0x21 增量：与上次差异位图（首拍全量 mask 不发 0x21，由 0x20 承载）*/
        tcs_delta_src_t now;
        build_delta_src(&now);
        if (s_last_valid) {
            uint16_t mask = 0;
            if (now.tc1_set != s_last.tc1_set) mask |= TCS_D_TC1_SET;
            if (now.tc2_set != s_last.tc2_set) mask |= TCS_D_TC2_SET;
            if (now.body    != s_last.body)    mask |= TCS_D_BODY;
            if (now.tec     != s_last.tec)     mask |= TCS_D_TEC;
            if (now.fan     != s_last.fan)     mask |= TCS_D_FAN;
            if (now.mode    != s_last.mode)    mask |= TCS_D_MODE;
            if (now.alarm   != s_last.alarm)   mask |= TCS_D_ALARM;
            if (now.badge   != s_last.badge)   mask |= TCS_D_BADGE;
            if (mask) {
                uint8_t pl[24];
                size_t n = tcs_pl_delta(pl, sizeof(pl), mask, &now);
                if (n) { link_send_frame_mt(MT_TCS_DELTA, pl, n); s_delta_cnt++; }
            }
        }
        s_last = now;
        s_last_valid = 1;

        /* ② 0x20 全量 */
        tcs_snap_pl_t s = {
            .ver = TCS_PROTO_VER,
            .seq = ++s_seq,
            .ms  = (uint32_t)(t0 / 1000),
            .tc1_act = g_tcs.tc1_act,
            .tc2_act = g_tcs.tc2_act,
            .body    = g_tcs.body_temp,
            .tc1_set = now.tc1_set,
            .tc2_set = now.tc2_set,
            .tec1 = g_tcs.tec1_on, .tec2 = g_tcs.tec2_on,
            .fan = g_tcs.fan_on, .mode = g_tcs.mode,
            .alarm = g_tcs.alarm_bits,
            .online = (uint8_t)((g_tcs.m207_online ? TCS_ONLINE_M207 : 0) |
                                (g_tcs.body_sensor_ok ? TCS_ONLINE_BODY_SENSOR : 0)),
            .fresh_ms = (uint16_t)m207_temp_age_ms(),
            .badge = (uint8_t)post_arm_state(),
            .rfu = 0,
        };
        uint8_t pl[TCS_SNAP_PL_LEN];
        size_t n = tcs_pl_snapshot(pl, sizeof(pl), &s);
        if (n) link_send_frame_mt(MT_TCS_SNAPSHOT, pl, n);
        s_snap_cnt++;

        int32_t dt = (int32_t)esp_timer_get_time() - t0;
        if (dt > 0 && (uint32_t)dt > s_enc_max_us) s_enc_max_us = (uint32_t)dt;

        if (++p_win >= 50) {               /* 10s perf 行（第八章预算 500µs）*/
            p_win = 0;
            ESP_LOGI(TAG, "[SNAPP] enc_max=%luus (budget 500) snap=%lu delta=%lu",
                     (unsigned long)s_enc_max_us, (unsigned long)s_snap_cnt,
                     (unsigned long)s_delta_cnt);
        }
    }
}

uint32_t tcs_snap_enc_max_us(void) { return s_enc_max_us; }

int tcs_snap_start(void)
{
    if (s_task) return -1;
    if (xTaskCreatePinnedToCore(snap_task, "tcs_snap", 3072, NULL,
                                3, &s_task, 0) != pdPASS) return -1;
    ESP_LOGI(TAG, "snapshot sender up: 0x20@200ms + 0x21 delta, core=0");
    return 0;
}
