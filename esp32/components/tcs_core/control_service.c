/**
 * control_service.c — control FSM 服务任务实现（R1e）
 *
 * ops 装配在 main（fan→tcs_fan / tec→tcs_m207）；本文件只管节拍/wdt/布防步进/perf。
 * M207 上电宽限语义沿用 control.c（5s）；body 传感器宽限由布防机制天然承载
 * （UNARMED 期 sensor 异常只记沿不进 SAFE，H743 WP-A.5 同款）。
 */
#include "control_service.h"
#include "control.h"
#include "post.h"
#include "param_registry.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_log.h"

#define TAG "CTRL"

static TaskHandle_t s_task;
static uint8_t s_started;
static volatile uint8_t s_hang_test;   /* E-20：置 1 后本任务停喂狗（F12 注入口）*/
static uint32_t s_poll_max_us;      /* control_poll 段耗时峰值（预算 100µs）*/
static uint32_t s_polls;
static post_arm_ctx_t s_arm_ctx;    /* H6 布防三态机上下文 */

/* post 步进（H743 post_poll 272-288 行同语义；无 UI 角标——屏角标 R2 起）*/
static void post_step_100ms(void)
{
    static uint32_t last_step;
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    if (now - last_step < 100u) return;
    last_step = now;

    /* 健康/已确认都要把"探头开路"算进去：M207 在线但温度不可用不算健康 */
    uint8_t healthy = (g_tcs.body_sensor_ok && g_tcs.m207_online
                       && !g_tcs.m207_sensor_open) ? 1u : 0u;
    uint8_t acked = (!(g_tcs.alarm_bits & ALM_BODY_SENSOR) &&
                     !(g_tcs.alarm_bits & ALM_M207) &&
                     !(g_tcs.alarm_bits & ALM_SENSOR_OPEN)) ? 1u : 0u;
    uint8_t mode08 = (uint8_t)param_get(PARAM_ID_COMMISSIONED);
    uint8_t post_pass = healthy;   /* ESP32 偏差：无 POST 电池，pass:=双源健康（R1.md）*/
    post_arm_t prev = post_arm_state();
    post_arm_t st = post_arm_step(&s_arm_ctx, mode08, post_pass, healthy, acked);
    post_arm_commit(st);
    if (st == POST_ARM_ARMED_ONLINE && prev == POST_ARM_LOST_ALARM)
        ESP_LOGI(TAG, "arm LOST->ARMED (10 healthy samples)");
    else if (st == POST_ARM_LOST_ALARM && prev == POST_ARM_ARMED_ONLINE)
        ESP_LOGI(TAG, "arm ARMED->LOST (source lost)");
    else if (st == POST_ARM_ARMED_ONLINE && prev == POST_ARM_UNARMED_ABSENT)
        ESP_LOGI(TAG, "arm UNARMED->ARMED (sources healthy)");
}

static void control_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    esp_task_wdt_add(NULL);         /* 订阅 TWDT（3s；F3/F12）*/
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
        int32_t t0 = (int32_t)esp_timer_get_time();
        control_poll((uint32_t)(t0 / 1000));
        post_step_100ms();
        param_persist_flush((uint32_t)(t0 / 1000));   /* H743 同款 2s 防抖落盘 */
        int32_t dt = (int32_t)esp_timer_get_time() - t0;
        if (dt > 0 && (uint32_t)dt > s_poll_max_us) s_poll_max_us = (uint32_t)dt;
        s_polls++;
        if (!s_hang_test) esp_task_wdt_reset();   /* 500ms 间隙 <1s 铁律；E-20 停喂 */
    }
}

uint8_t control_service_armed(void) { return post_armed(); }
void control_service_hang_test(uint8_t on) { s_hang_test = on; }

int control_service_start(void)
{
    if (s_started) return -1;
    post_arm_init(&s_arm_ctx);
    if (xTaskCreatePinnedToCore(control_task, "ctrl_svc", 4096, NULL,
                                4, &s_task, 1) != pdPASS) return -1;
    s_started = 1;
    ESP_LOGI(TAG, "service started core=1 500ms poll + 100ms arm-step, wdt 3s");
    return 0;
}

/* perf 读侧（console perf 命令用）*/
uint32_t control_service_poll_max_us(void) { return s_poll_max_us; }
uint32_t control_service_polls(void) { return s_polls; }
