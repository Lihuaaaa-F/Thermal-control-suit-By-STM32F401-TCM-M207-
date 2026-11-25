/**
 * tmp117_core.c — TMP117 采样服务的决策逻辑（S-G e2：自主机不可编译的 tmp117_service.c 抽取）
 *
 * 这里只有一拍的决策，没有 RTOS / IDF 调用（见 tmp117_core.h）。I2C 经注入的出入口访问，
 * 每拍至多一次 I2C 事务（第八章预算 500µs）。
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

static const tmp117_cfg_t s_default_cfg = {
    .sample_ms      = 100u,
    .lost_ms        = 1000u,    /* 失联降频：错满 10 次后 1s 试联（F4 恢复检测 ≤1s 首样）*/
    .id_retry_ms    = 5000u,
    .cfg_check_ms   = 1000u,
    .cfg_rewrite_ms = 1000u,
};

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

/* ====== 门③：单样本接受——H6.7 合理性（25~45°C 且 |Δ|≤2°C/s）====== */
static int sample_accept(const tmp117_core_t *c, float cv, uint32_t now_ms)
{
    /* R1 修复 M13-F-03：首样本也过 25–45°C 范围检查（dt=0 → 只查范围，不查变化率）*/
    if (!c->have_valid) return post_body_sanity_ok(cv, cv, 0.0f);
    /* M13-F-04：dt 用真实毫秒。失联降频到 1s 后一拍是 1s，按拍数算却只当 0.1s——
     * 恢复期的变化率判据被收紧 10 倍，物理上合理的首样会被当 implausible 丢掉并再计一次错。 */
    float dt = (float)(now_ms - c->last_valid_ms) / 1000.0f;
    return post_body_sanity_ok(c->last_valid_c, cv, dt);
}

static uint16_t be16(const uint8_t b[2]) { return (uint16_t)(((uint16_t)b[0] << 8) | b[1]); }

void tmp117_core_init(tmp117_core_t *c, const tmp117_io_t *io, const tmp117_cfg_t *cfg)
{
    memset(c, 0, sizeof(*c));
    if (io) c->io = *io;
    c->cfg = cfg ? *cfg : s_default_cfg;
    tmp117_health_init(&c->health);
    tmp117_avg_init(&c->avg);
    c->started = 1;
    /* id_ok=0、cfg_ok=0、cfg_written=0：第一拍核对身份，第二拍写配置，第三拍回读核对 */
}

int tmp117_core_tick(tmp117_core_t *c, uint32_t now_ms)
{
    const tmp117_io_t *io = &c->io;
    uint8_t buf[2];
    int ok = 0;
    uint8_t ok_before;

    s_ticking = c;
    c->tick++;

#if TCS_SIM_ENABLED
    if (c->sim_on) {                                      /* 假体温：直接进滑动均值，恒 ok */
        tmp117_avg_push(&c->avg, c->sim_val);
        g_tcs.body_temp = tmp117_avg_mean(&c->avg);
        c->last_valid_c = c->sim_val;
        c->last_valid_ms = now_ms;
        c->have_valid = 1;
        c->stat_samples++;
        ok = 1;
        goto decided;
    }
#endif
    if (!c->started || !io->read_reg || !io->write_reg) {
        c->stat_err++;
        goto decided;
    }

    /* ===== 门①：身份（M13-F-08）===== */
    if (!c->id_ok) {
        if (c->id_seen && (uint32_t)(now_ms - c->last_id_ms) < c->cfg.id_retry_ms)
            goto no_data;                     /* 器件在但身份不符：限速复核 */
        c->last_id_ms = now_ms;
        if (io->read_reg(io->ctx, TMP117_REG_DEVID, buf) != 0) {
            c->id_seen = 0;                   /* 器件不在：下一拍再查（接上后要能马上发现）*/
            goto comm_err;
        }
        if (be16(buf) != TMP117_DEVID_EXPECT) {
            c->id_seen = 1;
            c->id_fail_cnt++;
            if (c->id_fail_cnt == 1u || (c->id_fail_cnt % 60u) == 0u)
                T117_LOGE("DEVID=0x%04X, expected 0x0117: not a TMP117 — refusing to use it "
                          "as the body temperature source", be16(buf));
            goto no_data;
        }
        c->id_ok = 1;
        c->id_seen = 0;
        T117_LOGI("DEVID=0x%04X OK", be16(buf));
        goto no_data;
    }

    /* ===== 门②：配置（M13-F-02）===== */
    if (!c->cfg_ok) {
        if (!c->cfg_written) {
            if (io->write_reg(io->ctx, TMP117_REG_CONFIG, TMP117_CFG_WORD) != 0) {
                c->cfg_written = 0;
                goto comm_err;                 /* 写失败：下一拍重写 */
            }
            c->cfg_written = 1;
            c->last_rewrite_ms = now_ms;
            T117_LOGI("CONFIG := 0x%04X (continuous, CONV=125ms, AVG=8)", TMP117_CFG_WORD);
            goto no_data;                      /* 下一拍再回读核对；每拍仍只有一次事务 */
        }
        if (io->read_reg(io->ctx, TMP117_REG_CONFIG, buf) != 0) goto comm_err;
        if (!tmp117_cfg_matches(be16(buf))) {
            c->cfg_bad_cnt++;
            if ((uint32_t)(now_ms - c->last_rewrite_ms) >= c->cfg.cfg_rewrite_ms)
                c->cfg_written = 0;            /* 到点了：下一拍重写 */
            if (c->cfg_bad_cnt == 1u || (c->cfg_bad_cnt % 10u) == 0u)
                T117_LOGW("CONFIG read back 0x%04X, expected 0x%04X: not sampling until it matches",
                          be16(buf), TMP117_CFG_WORD);
            goto no_data;
        }
        c->cfg_ok = 1;
        c->last_cfg_ms = now_ms;
        T117_LOGI("CONFIG read back OK (0x%04X)", be16(buf));
        goto no_data;
    }
    if ((uint32_t)(now_ms - c->last_cfg_ms) >= c->cfg.cfg_check_ms) {
        /* 周期校验：这一拍读配置代替读温度。器件若被改成关断模式，温度寄存器冻结而
         * I2C 照常应答——只看读数永远发现不了。 */
        uint16_t v;
        c->last_cfg_ms = now_ms;
        if (io->read_reg(io->ctx, TMP117_REG_CONFIG, buf) != 0) goto comm_err;
        v = be16(buf);
        if (!tmp117_cfg_matches(v)) {
            c->cfg_ok = 0;
            c->cfg_bad_cnt++;
            if ((uint32_t)(now_ms - c->last_rewrite_ms) >= c->cfg.cfg_rewrite_ms)
                c->cfg_written = 0;
            T117_LOGW("CONFIG changed under us: 0x%04X (expected 0x%04X) -> stop sampling, rewrite",
                      v, TMP117_CFG_WORD);
            goto no_data;
        }
        ok = 1;                               /* 通信正常、模式正确：数据流健康，只是这一拍没有新样本 */
        goto decided;
    }

    /* ===== 读温度 → 门③ ===== */
    if (io->read_reg(io->ctx, TMP117_REG_TEMP, buf) != 0) goto comm_err;
    {
        float cv = tmp117_raw_to_c((int16_t)be16(buf));
        if (sample_accept(c, cv, now_ms)) {
            tmp117_avg_push(&c->avg, cv);
            g_tcs.body_temp = tmp117_avg_mean(&c->avg);
            c->last_valid_c = cv;
            c->last_valid_ms = now_ms;
            c->have_valid = 1;
            c->stat_samples++;
            ok = 1;
        } else {
            c->stat_err++;
            T117_LOGW("implausible sample %.2f dropped (H6.7)", (double)cv);
        }
    }
    goto decided;

comm_err:
    c->stat_err++;
    /* fall through */
no_data:
    ok = 0;

decided:
    ok_before = c->health.ok;
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
    s_ticking = NULL;
    return ok;
}

uint32_t tmp117_core_period_ms(const tmp117_core_t *c)
{
    /* 失联降频：错满 TRIP 且非 sim → 1s 试联 */
    return (c->health.err_run >= TMP117_ERR_TRIP && !SIM_ACTIVE(c))
           ? c->cfg.lost_ms : c->cfg.sample_ms;
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
