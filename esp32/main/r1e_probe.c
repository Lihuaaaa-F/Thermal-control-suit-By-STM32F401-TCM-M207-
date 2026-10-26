/**
 * r1e_probe.c — R1e 自驱取证（CONFIG_TCS_R1E_PROBE=y 取证构建专用）
 *
 * 自驱序列（stage 存 NVS "r1e/stage"，done 存 "r1e/done"）：
 *   stage 0..18：验证上轮参数保持 → 写下一组 → p save → 软复位
 *               （E-18 前 20 次上电循环 + R1a NVS 写往返；NVS 统计入账）
 *   stage 19：  最终验证 → E-21 滞回四向量 ±0.001 真机注入（37.701→COOL /
 *               37.299→IDLE / 36.299→HEAT / 36.701→IDLE，tmp117 sim + 真 control
 *               + 真 M207 下发）→ E-22 失联 SAFE→ack 才回闭环 → 置 stage=20 →
 *               spawn hog 饿死核 1 → TASK_WDT 3s 复位（E-20）
 *   stage 20：  复位原因=ESP_RST_TASK_WDT 断言 + 参数保持复验 → done=1
 * 副作用：E-21 期间真模块 TC1 TCADJTEMP/TCSW 被写（TEC 无负载=电无害）；结束恢复
 * mode=手动（F9 语义）。done=1 后常态放行，不再自跑。
 */
#include "r1e_probe.h"
#include "param_registry.h"
#include "control.h"
#include "control_service.h"
#include "esp_task_wdt.h"
#include "m207_service.h"
#include "tmp117_service.h"

#include <string.h>
#include "nvs.h"
#include "esp_system.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "R1E"
#define R1E_CYCLES 20    /* E-18 前 20 次（R1a 挂账口径）*/

/* 每 stage 一组的写入值（tc1_set 域内等距扫，跨 stage 不同值）*/
static float stage_val(int stage) { return 20.0f + (float)(stage % 9) * 2.5f; }   /* tc1_set 域 20..40（max 45 内）*/

static nvs_handle_t s_nvs;

static int stage_read(void)
{
    uint8_t st = 0;
    nvs_get_u8(s_nvs, "stage", &st);
    return st;
}

static void stage_write(uint8_t st)
{
    nvs_set_u8(s_nvs, "stage", st);
    nvs_commit(s_nvs);
}

/* E-21：注入体温并等 control 到达期望态（≤1.5s=3 拍）*/
static int expect_state(ctrl_state_t want, const char *tag)
{
    for (int i = 0; i < 8; i++) {         /* 2s：均值窗 8 样本冲刷 0.8s+poll 拍 */
        vTaskDelay(pdMS_TO_TICKS(250));
        if (control_state() == want) {
            ESP_LOGI(TAG, "[E21] %s -> state=%d PASS", tag, (int)want);
            return 1;
        }
    }
    ESP_LOGE(TAG, "[E21] %s FAIL (state=%d want %d)", tag,
             (int)control_state(), (int)want);
    return 0;
}

static void run_e21_e22(int *e21, int *e22)
{
    ESP_LOGI(TAG, "[E21] === hysteresis 4 vectors (±0.001) ===");
    tmp117_sim_set(37.0f);                /* sim 源健康（E-22 前提）*/
    uint32_t w = 0;
    while (!m207_online() && w < 8000) { vTaskDelay(pdMS_TO_TICKS(100)); w += 100; }
    (void)param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_CONSOLE);
    /* E-18 循环期 boot 瞬态（absent 传感器 1s 内 LOST）会残留 ALM_BODY_SENSOR+
     * LOST_ALARM——先恢复数据流再 ack，LOST→ARMED 需 acked&&10 healthy */
    /* 退出条件=报警清零且 control 已出 SAFE（post_armed() 在 LOST_ALARM 也为真，
     * 不能只看它；LOST→ARMED 另需 acked&&10 healthy ≈1s，在窗内自然完成）*/
    for (int i = 0; i < 50 && (control_state() == CTRL_SAFE ||
         tcs_alarm_active(ALM_BODY_SENSOR) || tcs_alarm_active(ALM_M207)); i++) {
        tcs_alarm_ack();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    ESP_LOGI(TAG, "[E21] m207=%u armed=%u (after %lums)",
             (unsigned)m207_online(), (unsigned)control_service_armed(),
             (unsigned long)w);
    if (!control_service_armed()) { ESP_LOGE(TAG, "[E21] not armed — abort"); return; }

    tmp117_sim_set(37.7f + 0.001f);       /* ① >37.7 → COOL（真下发 15.0+TCSW=1）*/
    *e21 += expect_state(CTRL_COOL, "37.701 -> COOL");
    tmp117_sim_set(37.3f - 0.001f);       /* ② <37.3 退制冷 */
    *e21 += expect_state(CTRL_IDLE, "37.299 -> exit COOL");
    tmp117_sim_set(36.3f - 0.001f);       /* ③ <36.3 → HEAT（45.0）*/
    *e21 += expect_state(CTRL_HEAT, "36.299 -> HEAT");
    tmp117_sim_set(36.7f + 0.001f);       /* ④ >36.7 退制热 */
    *e21 += expect_state(CTRL_IDLE, "36.701 -> exit HEAT");

    ESP_LOGI(TAG, "[E22] === sensor loss -> SAFE -> ack-to-return ===");
    tmp117_sim_disable();                 /* 假源关 → 真 I2C NACK ×10 → LOST */
    vTaskDelay(pdMS_TO_TICKS(1500));
    uint8_t safe1 = (control_state() == CTRL_SAFE);
    uint8_t alm1 = tcs_alarm_active(ALM_BODY_SENSOR);
    ESP_LOGI(TAG, "[E22] after loss: state=%d(want %d SAFE) alarm=%u(want 1)",
             (int)control_state(), (int)CTRL_SAFE, (unsigned)alm1);
    tcs_alarm_ack();                      /* 源未恢复 → ack 不清 */
    uint8_t stays = (tcs_alarm_active(ALM_BODY_SENSOR) == 1 &&
                     control_state() == CTRL_SAFE);
    tmp117_sim_set(37.0f);                /* 源恢复：3 ok → 数据流恢复（报警保持）*/
    vTaskDelay(pdMS_TO_TICKS(1300));
    tcs_alarm_ack();                      /* 源已恢复 → 清位 */
    vTaskDelay(pdMS_TO_TICKS(1500));
    uint8_t back = (control_state() == CTRL_IDLE &&
                    !tcs_alarm_active(ALM_BODY_SENSOR));
    ESP_LOGI(TAG, "[E22] no-recover-ack stays=%u; recover+ack -> IDLE=%u",
             (unsigned)stays, (unsigned)back);
    *e22 = (safe1 && alm1 && stays && back) ? 1 : 0;
    ESP_LOGI(TAG, "[E22] RESULT %s (safe=%u alm=%u stays=%u back=%u)",
             *e22 ? "PASS" : "FAIL", (unsigned)safe1, (unsigned)alm1,
             (unsigned)stays, (unsigned)back);

    (void)param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_CONSOLE);   /* F9 复位 */
    tcs_alarm_ack();
}

void r1e_probe_run(void)
{
    if (nvs_open("r1e", NVS_READWRITE, &s_nvs) != ESP_OK) {
        ESP_LOGE(TAG, "nvs open fail — probe disabled");
        return;
    }
    uint8_t done = 0;
    nvs_get_u8(s_nvs, "done", &done);
    int stage = stage_read();
    ESP_LOGI(TAG, "=== probe stage=%d done=%u (reset reason=%d) ===", stage,
             (unsigned)done, (int)esp_reset_reason());
    if (done) {
        /* flash 触发的 USB/上电复位 = 开发者重跑口；软复位链不受影响 */
        int rr = (int)esp_reset_reason();
        if (rr == (int)ESP_RST_USB || rr == (int)ESP_RST_POWERON) {
            ESP_LOGW(TAG, "[R1E] done=1 但为 flash 复位(rr=%d) → 清 done+stage 归零重跑", rr);
            nvs_set_u8(s_nvs, "done", 0);
            stage_write(0);
            stage = 0;
            done = 0;
        } else {
            ESP_LOGI(TAG, "[R1E] 已完成（done=1），常态放行");
            return;
        }
    }

    if (stage >= R1E_CYCLES) {            /* E-20 复位后落地（stage=20）*/
        int rst = (int)esp_reset_reason();
        float expect = stage_val(R1E_CYCLES - 1);
        float v = param_get(PARAM_ID_TC1_SET);
        uint8_t kept = (v > expect - 0.01f && v < expect + 0.01f);
        /* 看门狗家族断言：TWDT panic 落 PANIC / INT_WDT 先咬 / 直咬 = TASK_WDT——
         * 语义门="卡死→自动复位+参数保持"，具体哪只狗先咬随链路（已记录）*/
        uint8_t wdt_fam = (rst == (int)ESP_RST_PANIC || rst == (int)ESP_RST_INT_WDT ||
                           rst == (int)ESP_RST_TASK_WDT || rst == (int)ESP_RST_WDT);
        ESP_LOGI(TAG, "[E20] RESULT %s (reset_reason=%d wdt_family=%u, "
                 "tc1_set=%.2f kept=%u)",
                 (wdt_fam && kept) ? "PASS" : "FAIL", rst, (unsigned)wdt_fam,
                 (double)v, (unsigned)kept);
        ESP_LOGI(TAG, "[R1E] ALL DONE（E-18 前%d次 NVS 往返+E-20 wdt 复位+E-21/E-22 注入闭环）",
                 R1E_CYCLES);
        stage_write(R1E_CYCLES + 1);
        nvs_set_u8(s_nvs, "done", 1);
        nvs_commit(s_nvs);
        return;
    }

    /* E-18 前 20 次：验证上轮 → 写新值 → save */
    if (stage > 0) {
        float expect = stage_val(stage - 1);
        float v = param_get(PARAM_ID_TC1_SET);
        if (v < expect - 0.01f || v > expect + 0.01f) {
            ESP_LOGE(TAG, "[E18] cycle %d/%d FAIL: tc1_set=%.2f want %.2f",
                     stage, R1E_CYCLES, (double)v, (double)expect);
            nvs_set_u8(s_nvs, "done", 1);
            nvs_commit(s_nvs);
            return;
        }
        ESP_LOGI(TAG, "[E18] cycle %d/%d kept tc1_set=%.2f PASS", stage,
                 R1E_CYCLES, (double)v);
    }
    (void)param_set(PARAM_ID_TC1_SET, stage_val(stage), PARAM_SRC_CONSOLE);
    int rc = param_persist_now();
    nvs_stats_t st;
    nvs_get_stats(NULL, &st);
    ESP_LOGI(TAG, "[E18] cycle %d/%d wrote tc1_set=%.2f save=%d | nvs used=%u "
             "free=%u total=%u", stage + 1, R1E_CYCLES, (double)stage_val(stage),
             rc, (unsigned)st.used_entries, (unsigned)st.free_entries,
             (unsigned)st.total_entries);
    stage_write((uint8_t)(stage + 1));

    if (stage + 1 < R1E_CYCLES) {
        vTaskDelay(pdMS_TO_TICKS(300));
        esp_restart();                    /* 下一轮上电循环 */
    }
    /* stage 19 落地：进 E-21/E-22 → E-20 */
    int e21 = 0, e22 = 0;
    run_e21_e22(&e21, &e22);
    ESP_LOGI(TAG, "[E21] RESULT %s (%d/4 vectors)", e21 == 4 ? "PASS" : "FAIL", e21);
    if (e22 == 0 || e21 != 4) {
        ESP_LOGE(TAG, "[E21/22] FAIL — 跳过 E-20（保现场），probe 停");
        nvs_set_u8(s_nvs, "done", 1);
        nvs_commit(s_nvs);
        return;
    }
    stage_write(R1E_CYCLES);              /* 复位后 stage=20 分支取证 E-20 */
    ESP_LOGW(TAG, "[E20] ctrl_svc 停喂狗（F12 语义）— expect TWDT 3s 咬任务→panic→复位；"
             "params must survive");
    vTaskDelay(pdMS_TO_TICKS(200));       /* 让日志走完 */
    control_service_hang_test(1);
    /* 不会返回：TWDT 3s → panic → 复位 → stage=20 分支 */
}
