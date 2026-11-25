/**
 * tmp117_core.c — TMP117 采样服务的决策逻辑（S-G e2：自主机不可编译的 tmp117_service.c 抽取）
 *
 * 这里只有一拍的决策，没有 RTOS / IDF 调用（见 tmp117_core.h）。I2C 经注入的出入口访问。
 * 掉线沿（H743 同语义）：布防门=WP-A.5——未布防只记沿不报警；布防后 ALM_BODY_SENSOR 粘滞。
 *
 * 官方查证：本文件不调用任何外设或库（主机可编译）；I2C 通道由 tmp117_service.c 用官方
 * driver/i2c_master 提供。组件库无 TMP117 官方件（查证记录见 tmp117_service.c 文件头）；
 * 寄存器语义取自 TI 数据手册 SNOSD2D（见 tmp117_pure.h 配置寄存器一节）。
 */
#include "tmp117_core.h"

#include <string.h>

#include "param_registry.h"     /* g_tcs */
#include "control.h"            /* tcs_alarm_raise / ALM_BODY_SENSOR */
#include "post.h"               /* H6.7 post_body_sanity_ok */

#ifdef ESP_PLATFORM
#include "esp_log.h"
static const char *TAG = "TMP117";
#define T117_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define T117_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define T117_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#else
#define T117_LOGE(...) do {} while (0)
#define T117_LOGW(...) do {} while (0)
#define T117_LOGI(...) do {} while (0)
#endif

#define TMP117_SAMPLE_MS      100u
#define TMP117_LOST_PERIOD_MS 1000u   /* 失联降频：错满 10 次后 1s 试联 */

#if TCS_SIM_ENABLED
#define SIM_ACTIVE(c) ((c)->sim_on)
#else
#define SIM_ACTIVE(c) (0u)            /* 编译期常 0：分支与注入实现一起消失 */
#endif

/* ====== 掉线沿 ====== */
static tmp117_core_t *s_ticking;     /* 当前正在步进的核心（健康机的回调不带上下文）*/
static void on_sensor_lost(void)
{
    if (s_ticking) s_ticking->lost_edges++;   /* R1 修复 M13-F-07：计数不受布防门控 */
    if (!post_armed()) {
        T117_LOGW("LOST (unarmed, log only)");
        return;
    }
    tcs_alarm_raise(ALM_BODY_SENSOR);
    T117_LOGW("LOST (%u err-run) -> ALM_BODY_SENSOR (sticky)", TMP117_ERR_TRIP);
}

/* ====== 单样本接受：H6.7 合理性（25~45°C 且 |Δ|≤2°C/s）====== */
static int sample_accept(tmp117_core_t *c, float cv, uint32_t tick_now)
{
    /* R1 修复 M13-F-03：首样本也过 25–45°C 范围检查（dt=0 → 只查范围，不查变化率）*/
    if (!c->have_valid) return post_body_sanity_ok(cv, cv, 0.0f);
    float dt = (float)(tick_now - c->last_valid_tick) * (TMP117_SAMPLE_MS / 1000.0f);
    return post_body_sanity_ok(c->last_valid_c, cv, dt);
}

void tmp117_core_init(tmp117_core_t *c, const tmp117_io_t *io)
{
    memset(c, 0, sizeof(*c));
    if (io) c->io = *io;
    tmp117_health_init(&c->health);
    tmp117_avg_init(&c->avg);
    c->started = 1;
}

int tmp117_core_tick(tmp117_core_t *c, uint32_t now_ms)
{
    const tmp117_io_t *io = &c->io;
    uint8_t buf[2];
    int ok = 0;
    (void)now_ms;
    s_ticking = c;
    c->tick++;

#if TCS_SIM_ENABLED
    if (c->sim_on) {
        tmp117_avg_push(&c->avg, c->sim_val);
        g_tcs.body_temp = tmp117_avg_mean(&c->avg);
        c->last_valid_c = c->sim_val;
        c->last_valid_tick = c->tick;
        c->have_valid = 1;
        c->stat_samples++;
        ok = 1;
    } else
#endif
    if (c->started && io->read_reg) {
        if (io->read_reg(io->ctx, TMP117_REG_TEMP, buf) == 0) {
            int16_t raw = (int16_t)(((uint16_t)buf[0] << 8) | buf[1]);
            float cv = tmp117_raw_to_c(raw);
            if (sample_accept(c, cv, c->tick)) {
                tmp117_avg_push(&c->avg, cv);
                g_tcs.body_temp = tmp117_avg_mean(&c->avg);
                c->last_valid_c = cv;
                c->last_valid_tick = c->tick;
                c->have_valid = 1;
                c->stat_samples++;
                ok = 1;
            } else {
                c->stat_err++;
                T117_LOGW("implausible sample %.2f dropped (H6.7)", (double)cv);
            }
        } else {
            c->stat_err++;
        }
    } else if (c->started) {
        c->stat_err++;
    }
    {
        uint8_t ok_before = c->health.ok;
        tmp117_health_step(&c->health, ok, on_sensor_lost);
        /* M06-F-06：状态真值 = 健康机 ok 且已有有效样本。
         * 原来只在"掉线沿/恢复沿"写，上电初值 body_sensor_ok=1 + body_temp=0.0，
         * auto 模式首个 500ms 评估就按"体温 0°C"开始加热。*/
        g_tcs.body_sensor_ok = (uint8_t)(c->health.ok && c->have_valid);
        if (ok_before && !c->health.ok) {
            /* 掉线沿：真值已由上句统一写，这里只补一条日志 */
            T117_LOGW("LOST (data flow stopped; alarm raised by control)");
        } else if (!ok_before && c->health.ok) {
            /* 恢复沿：数据流回来了。通信类故障的锁存由 control 在源连续健康 2s 后自动清
             * （U 线），不需要人工确认；急停/超温才需要。*/
            c->recover_edges++;
            T117_LOGW("RECOVERED (data flow back; control auto-recovers after 2s healthy)");
        }
    }
    s_ticking = NULL;
    return ok;
}

uint32_t tmp117_core_period_ms(const tmp117_core_t *c)
{
    /* 失联降频：错满 TRIP 且非 sim → 1s 试联（F4 恢复检测 ≤1s 首样，3 成 ≤1.3s）*/
    return (c->health.err_run >= TMP117_ERR_TRIP && !SIM_ACTIVE(c))
           ? TMP117_LOST_PERIOD_MS : TMP117_SAMPLE_MS;
}

uint8_t tmp117_core_ok(const tmp117_core_t *c) { return c->health.ok; }

/* 状态行永远可用：注入关断的构建里恒返回 0（不进 #if，否则外壳在交付档编译不过）*/
uint8_t tmp117_core_sim_active(const tmp117_core_t *c) { return (uint8_t)SIM_ACTIVE(c); }

#if TCS_SIM_ENABLED
void tmp117_core_sim_set(tmp117_core_t *c, float v)
{
    c->sim_val = v;
    c->sim_on = 1;
    T117_LOGW("[SIM] body temp forced %.2f", (double)v);
}
void tmp117_core_sim_disable(tmp117_core_t *c)
{
    c->sim_on = 0;
    T117_LOGW("[SIM] body temp OFF (real I2C path; absent=loss path)");
}
#endif
