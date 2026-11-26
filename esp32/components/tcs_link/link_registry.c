/**
 * link_registry.c — 注册表镜像（M11-F-15 拆分，S 线 e1）
 *
 * 从 link_task.c 拆出的第 3 块：内置种子表（H743 参数表冻结副本 + 0x100 假 M207 参数）、
 * 0x14 条目的落地与"全量收齐"判定、0x13 请求、参数值缓存，以及"最近一次屏写参数"
 * （网页 pv 推送源）。
 */
#include "link_internal.h"
#include <string.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "tcs_link";

static link_reg_t s_reg[LINK_REGISTRY_MAX];
/* R1 P1-01：注册表接收进度（替代"种子 used 恒真导致永不重发 0x13"的短路）*/
static uint16_t s_reg_expect;
static uint16_t s_reg_got;
static volatile uint8_t s_reg_done;
static link_param_change_t s_last_param;
static float s_param_val[LINK_REGISTRY_MAX];   /* 与 s_reg 同下标缓存 */
static uint8_t s_param_valid[LINK_REGISTRY_MAX];

/* ===================================================================== */
/* WP-F.2/F3：内置注册表快照（H743 参数表冻结副本 + 0x100 假 M207 参数）    */
/* 真链路 0x14 帧到达后逐条覆盖同 id 条目；0x100 段 H743 不发，纯演示。      */
/* ===================================================================== */
typedef struct {
    uint16_t id; const char *key, *name, *unit;
    uint8_t type, flags; float min, max, step, def;
} reg_seed_t;

static const reg_seed_t s_reg_seed[] = {
    /* 与 H743 param_registry.c v1 表逐项一致（id 永久锚点，双端同源）*/
    { 0x01, "tc1_set",        "贴身水箱设定温度", "°C", 0, 0x1f, 5.0f, 45.0f, 0.1f,  25.0f },
    { 0x02, "tc2_set",        "环境水箱设定温度", "°C", 0, 0x1f, 5.0f, 45.0f, 0.1f,  25.0f },
    { 0x03, "body_hot",       "体温过热阈值",    "°C", 0, 0x0f, 36.0f, 40.0f, 0.1f, 37.5f },
    { 0x04, "body_cold",      "体温过冷阈值",    "°C", 0, 0x0f, 35.0f, 39.0f, 0.1f, 36.5f },
    { 0x05, "hyst",           "滞回回差",        "°C", 0, 0x0f, 0.1f,  1.0f,  0.1f,  0.2f },
    { 0x06, "mode",           "控制模式",        "",   1, 0x0f, 0.0f,  1.0f,  1.0f,  0.0f },
    { 0x07, "tc_set_confirm", "高温二次确认阈值", "°C", 0, 0x0d, 38.0f, 45.0f, 0.5f, 40.0f },
    { 0x08, "commissioned",   "布防",            "",   1, 0x0f, 0.0f,  1.0f,  1.0f,  0.0f },
    /* WP-F.3：假 M207 参数（0x100 段）——网页零改动自动出现控件（G4 前半提前）*/
    { 0x100, "pid_kp",        "M207 PID 比例",   "",   0, 0x04, 0.0f, 10.0f, 0.1f,  1.0f },
    { 0x101, "pid_ki",        "M207 PID 积分",   "",   0, 0x04, 0.0f,  1.0f, 0.01f, 0.1f },
};

static void registry_seed(void)
{
    for (size_t i = 0; i < sizeof(s_reg_seed) / sizeof(s_reg_seed[0]); i++) {
        const reg_seed_t *sd = &s_reg_seed[i];
        if (i >= LINK_REGISTRY_MAX) break;
        tcs_reg_entry_t *e = &s_reg[i].e;
        memset(&s_reg[i], 0, sizeof(s_reg[i]));
        s_reg[i].used = 1;
        e->id = sd->id; e->type = sd->type; e->flags = sd->flags;
        e->min = sd->min; e->max = sd->max; e->step = sd->step;
        strncpy(e->key,  sd->key,  sizeof(e->key) - 1);
        strncpy(e->name, sd->name, sizeof(e->name) - 1);
        strncpy(e->unit, sd->unit, sizeof(e->unit) - 1);
        s_param_val[i] = sd->def;
        s_param_valid[i] = 1;
    }
    ESP_LOGI(TAG, "registry seeded: %u entries (H743 0x14 will override)",
             (unsigned)(sizeof(s_reg_seed) / sizeof(s_reg_seed[0])));
}

void link_registry_init(void)
{
    memset(s_reg, 0, sizeof(s_reg));
    registry_seed();                        /* WP-F.2/F3：内置快照先行 */
}

void link_registry_progress_reset(void)
{
    s_reg_done = 0;
    s_reg_got = 0;
}

uint8_t link_registry_done(void) { return s_reg_done; }

void link_registry_apply_entry(tcs_reg_entry_t *e)
{
    if (e->idx >= LINK_REGISTRY_MAX) return;
    /* M11-F-07：s_reg 被 link_get_registry()/link_param_value() 并发读，
     * 写入必须整体进同一临界区（原实现逐字段写，读侧可能读到"新 id + 旧名字"）。*/
    uint16_t got = 0, expect = 0;
    uint8_t done = 0;
    link_internal_lock();
    s_reg[e->idx].used = 1;
    s_reg[e->idx].e = *e;
    if (e->idx == 0) { s_reg_expect = e->count; s_reg_got = 0; }
    if ((uint16_t)(e->idx + 1u) > s_reg_got) s_reg_got = (uint16_t)(e->idx + 1u);
    if (!s_reg_done && s_reg_expect && s_reg_got >= s_reg_expect) {
        s_reg_done = 1;
        done = 1;
    }
    got = s_reg_got; expect = s_reg_expect;
    link_internal_unlock();
    if (done) ESP_LOGI(TAG, "registry complete %u/%u", (unsigned)got, (unsigned)expect);
    ESP_LOGI(TAG, "REG[%u/%u] 0x%02x %s", e->idx + 1, e->count, e->id, e->key);
    link_notify_event();
}

void link_registry_note_param(uint16_t id, float v)
{
    /* 复核 G3：与 link_last_param_copy()/link_get_registry() 的读共用一把锁，
     * 否则 DEMO 下 httpd 任务可能读到"新 id + 旧值"。*/
    link_internal_lock();
    s_last_param.id = id;
    s_last_param.v = v;
    s_last_param.us = esp_timer_get_time();
    for (int i = 0; i < LINK_REGISTRY_MAX; i++)
        if (s_reg[i].used && s_reg[i].e.id == id) {
            s_param_val[i] = v;
            s_param_valid[i] = 1;
            break;
        }
    link_internal_unlock();
}

int link_get_registry(link_reg_t *out, int max)
{
    link_internal_lock();
    int n = 0;
    for (int i = 0; i < LINK_REGISTRY_MAX && n < max; i++)
        if (s_reg[i].used) out[n++] = s_reg[i];
    link_internal_unlock();
    return n;
}

void link_request_registry(void)
{
    uint8_t frame[MAX_FRAME];
    size_t fn = link_frame_build(MT_TCS_GET_REGISTRY, comm_next_seq(link_ctx()), NULL, 0,
                                 frame, sizeof(frame));
    if (fn) comm_send(link_ctx(), frame, fn, MT_TCS_GET_REGISTRY, NULL);
}

void link_last_param_copy(link_param_change_t *out)
{
    if (!out) return;
    link_internal_lock();
    *out = s_last_param;
    link_internal_unlock();
}

float link_param_value(uint16_t id)
{
    float v = NAN; uint8_t ok = 0;
    link_internal_lock();
    for (int i = 0; i < LINK_REGISTRY_MAX; i++) {
        if (s_reg[i].used && s_reg[i].e.id == id) {
            v = s_param_val[i];
            ok = s_param_valid[i];
            break;
        }
    }
    link_internal_unlock();
    return ok ? v : NAN;
}
