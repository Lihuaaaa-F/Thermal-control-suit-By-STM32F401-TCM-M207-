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
#include "esp_netif.h"
#include "esp_app_desc.h"
#include "softap.h"

#define TAG "SNAP"

#define SNAP_PERIOD_MS  200u

static TaskHandle_t s_task;
static uint32_t s_enc_max_us;
static uint32_t s_snap_cnt, s_delta_cnt;
static uint32_t s_send_drop;        /* M08-F-06：组帧失败或发送失败 */
static uint32_t s_send_skip;        /* 演示模式抑制（预期内，单列不混入 drop）*/
static uint32_t s_seq;
static tcs_delta_src_t s_last;      /* 上次已发状态（差分基准）*/
static uint8_t s_last_valid;
static portMUX_TYPE s_snap_mux = portMUX_INITIALIZER_UNLOCKED;
static uint32_t s_mirror_tick;      /* 镜像泵节拍 */
static uint16_t s_mirror_idx;       /* 镜像泵游标 */

/* M08-F-05 / R3-B-1/B-2：一个任务周期内所有"同帧"字段必须同源。
 * 0x20/0x21 曾逐字段读 g_tcs、设定值又各自 param_get 一次——控制任务刚好在两次读之间
 * 完成一拍更新，就会出现"新设定 + 旧实测"这种同帧不同时刻的组合（屏端据此判越界/绘图）。
 * 现在整个周期只取一次采样（g_tcs 整块 + 两路设定 + fresh_ms + badge），两帧都从它组装。
 * 临界区说明（R3-B-3）：这里只挡本核抢占（本核上若有 g_tcs 写者），跨核写者不参与，
 * 仍可能在一次结构体赋值内部交错；彻底修法要让所有写者共用一把 spinlock，改动面大于收益，
 * 按设计只收敛读侧并如实记录残余风险。*/
typedef struct {
    tcs_state_t st;
    float    tc1_set, tc2_set;
    uint16_t fresh_ms;
    uint8_t  badge;
} snap_cycle_t;

static void snap_sample(snap_cycle_t *c)
{
    taskENTER_CRITICAL(&s_snap_mux);
    c->st = g_tcs;                                   /* 约 28B */
    c->tc1_set = param_get(PARAM_ID_TC1_SET);
    c->tc2_set = param_get(PARAM_ID_TC2_SET);
    c->fresh_ms = (uint16_t)m207_temp_age_ms();
    c->badge = (uint8_t)post_arm_state();
    taskEXIT_CRITICAL(&s_snap_mux);
}

static void build_delta_src(tcs_delta_src_t *d, const snap_cycle_t *c)
{
    d->tc1_set = c->tc1_set;
    d->tc2_set = c->tc2_set;
    d->body    = c->st.body_temp;
    d->tec     = (uint8_t)((c->st.tec1_on ? 1 : 0) | (c->st.tec2_on ? 2 : 0));
    d->fan     = c->st.fan_on;
    d->mode    = c->st.mode;
    d->alarm   = c->st.alarm_bits;
    d->badge   = c->badge;
}

/* 现场取证（见 tcs_snap.h 的 tcs_snap_mute）：静默期内 snap_send 一帧都不发。
 * 截止时刻只有一个自然对齐的 32 位量，命令任务写、快照任务读，各自只访问一次，不用加锁。*/
static volatile uint32_t s_mute_until_ms;   /* 0 = 不静默 */

static int snap_muted(void)
{
    return s_mute_until_ms &&
           (int32_t)(s_mute_until_ms - (uint32_t)(esp_timer_get_time() / 1000)) > 0;
}

int tcs_snap_mute(uint32_t seconds)
{
    s_mute_until_ms = seconds ? (uint32_t)(esp_timer_get_time() / 1000) + seconds * 1000u : 0u;
    return 0;
}

/* M08-F-06：以前 `link_send_frame_mt(...); cnt++;` 把失败也记成发出去了，
 * 断链/组帧失败在日志里看不出来。这里按返回值分账：成功才计发送数。*/
static void snap_send(uint8_t mt, const uint8_t *pl, size_t n, uint32_t *ok)
{
    if (snap_muted()) { s_send_skip++; return; }   /* 取证静默：按预期内抑制计数 */
    if (!n) { s_send_drop++; return; }             /* 载荷编码失败 */
    int rv = link_send_frame_mt(mt, pl, n);
    if (rv == 0)      (*ok)++;
    else if (rv > 0)  s_send_skip++;               /* 演示模式抑制 */
    else              s_send_drop++;
}

/* 把热点名/密码/网址/固件版本组成 0x22 载荷并发给屏（格式见 protocol/tcs_proto.h）*/
static void push_info(void)
{
    char ssid[33] = {0}, pass[65] = {0}, url[40] = "http://192.168.4.1";
    (void)softap_credentials(ssid, sizeof ssid, pass, sizeof pass);   /* 没拿到=空串，屏显示"—" */
    esp_netif_t *nf = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip;
    if (nf && esp_netif_get_ip_info(nf, &ip) == ESP_OK)
        snprintf(url, sizeof url, "http://" IPSTR, IP2STR(&ip.ip));
    const char *keys[] = { "ssid", "pass", "url", "fw" };
    const char *vals[] = { ssid, pass, url, esp_app_get_description()->version };
    uint8_t pl[TCS_INFO_MAX_LEN + 1];
    size_t n = tcs_pl_info((char *)pl, sizeof pl, keys, vals, 4);
    if (n && !snap_muted()) (void)link_send_info(pl, n);
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

        /* ① 参数镜像泵（裁决 2026-11-23）：PROD 的 H743 不做本地持久化，
         * 启动是默认值，必须靠 0x11 镜像拿全量权威值。变更即推已覆盖"改一次推一次"，
         * 这里再做一轮慢速全量轮转（每 800ms 一条，51 条约 41s 走完），
         * 兜住：H743 重启、免 ARQ 丢帧、链路抖动期间漏掉的镜像。
         * 不在 link_alive() 判定里做——屏刚启动时 0x10 未到，会白等 3s。*/
        if ((s_mirror_tick++ % 4u) == 0u) {
            int n = param_count();
            if (n > 0) {
                if (s_mirror_idx >= (uint16_t)n) s_mirror_idx = 0;
                const param_desc_t *mp = param_at((int)s_mirror_idx++);
                if (mp && (mp->flags & PF_RW)) {
                    (void)link_send_param(mp->id, mp->value);
                }
            }
        }

        /* ①b 设备信息（U 线 W3）：每 16 拍（3.2s）推一次 0x22，免 ARQ。
         * 屏的连接页据此显示真实热点名/密码/网址并生成扫码连网的二维码；
         * 屏单独重启后最多 3.2s 就能拿到，不需要屏主动请求。*/
        if ((s_mirror_tick % 16u) == 8u) push_info();

        /* ② 本拍唯一采样（M08-F-05：两帧共用，避免同帧不同时刻）*/
        snap_cycle_t cy;
        snap_sample(&cy);

        /* ③ 0x21 增量：与上次差异位图（首拍全量 mask 不发 0x21，由 0x20 承载）*/
        tcs_delta_src_t now;
        build_delta_src(&now, &cy);
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
                snap_send(MT_TCS_DELTA, pl, n, &s_delta_cnt);
            }
        }

        /* ④ 0x20 全量（与 0x21 同源副本）*/
        tcs_snap_pl_t s = {
            .ver = TCS_PROTO_VER,
            .seq = ++s_seq,
            .ms  = (uint32_t)(t0 / 1000),
            .tc1_act = cy.st.tc1_act,
            .tc2_act = cy.st.tc2_act,
            .body    = cy.st.body_temp,
            .tc1_set = cy.tc1_set,
            .tc2_set = cy.tc2_set,
            .tec1 = cy.st.tec1_on, .tec2 = cy.st.tec2_on,
            .fan = cy.st.fan_on, .mode = cy.st.mode,
            .alarm = cy.st.alarm_bits,
            .online = (uint8_t)((cy.st.m207_online ? TCS_ONLINE_M207 : 0) |
                                (cy.st.body_sensor_ok ? TCS_ONLINE_BODY_SENSOR : 0)),
            .fresh_ms = cy.fresh_ms,
            .badge = cy.badge,
            .rfu = 0,
        };
        uint8_t pl[TCS_SNAP_PL_LEN];
        size_t n = tcs_pl_snapshot(pl, sizeof(pl), &s);
        snap_send(MT_TCS_SNAPSHOT, pl, n, &s_snap_cnt);

        /* 差分基准在本拍两帧都组装完之后前移 */
        s_last = now;
        s_last_valid = 1;

        int32_t dt = (int32_t)esp_timer_get_time() - t0;
        if (dt > 0 && (uint32_t)dt > s_enc_max_us) s_enc_max_us = (uint32_t)dt;

        if (++p_win >= 50) {               /* 10s perf 行（第八章预算 500µs）*/
            p_win = 0;
            ESP_LOGI(TAG, "[SNAPP] enc_max=%luus (budget 500) snap=%lu delta=%lu drop=%lu skip=%lu",
                     (unsigned long)s_enc_max_us, (unsigned long)s_snap_cnt,
                     (unsigned long)s_delta_cnt, (unsigned long)s_send_drop,
                     (unsigned long)s_send_skip);
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
