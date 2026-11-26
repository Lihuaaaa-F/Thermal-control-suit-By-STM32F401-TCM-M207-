/**
 * link_demo.c — 演示模式（M11-F-15 拆分，S 线 e1）
 *
 * 从 link_task.c 拆出的第 4 块。R1 修复 P0-05 / AD-5：演示模式是编译期可选的
 * （CONFIG_TCS_DEMO，默认 n）。生产固件不含演示状态机——断链只报"离线"，不再用合成温度
 * 冒充真值。整段（含状态变量与步进函数）都在 #if 里，PROD 档连符号都不存在。
 */
#include "link_internal.h"
#include <string.h>
#include <math.h>
#include "esp_timer.h"
#include "esp_log.h"

#if CONFIG_TCS_DEMO
static const char *TAG = "tcs_link";
static int64_t s_boot_us;
static volatile uint8_t s_demo_on;

/* 演示状态（独立于真实链路缓存）*/
static struct {
    float tc1_act, tc2_act, body;
    uint8_t tec1, tec2, fan, mode, commissioned, estop;
    uint16_t alarm;
    uint32_t seq;
    float tc1_set, tc2_set;
} s_demo = { .tc1_set = 25, .tc2_set = 25, .body = 36.8f };

static void demo_push_param(uint16_t id, float v)
{
    link_registry_note_param(id, v);   /* 与网页/注册表读共用一把锁（link_registry.c）*/
    link_notify_event();
}

static int demo_cmd(uint8_t cmd)
{
    switch (cmd) {
    case 1: s_demo.tec1 = 1; break;
    case 2: s_demo.tec1 = 0; break;
    case 3: s_demo.tec2 = 1; break;
    case 4: s_demo.tec2 = 0; break;
    case 5: s_demo.fan = !s_demo.fan; break;
    case 6: s_demo.mode = 1; break;
    case 7: s_demo.mode = 0; break;
    case 8: /* 急停：与 H743 tcs_emergency_stop 同语义 */
        s_demo.estop = 1; s_demo.alarm |= 0x0004;
        s_demo.mode = 0; s_demo.tec1 = s_demo.tec2 = 0; s_demo.fan = 1;
        break;
    case 9: /* 人工确认恢复 */
        s_demo.estop = 0; s_demo.alarm &= ~0x0004u;
        break;
    default: return -1;
    }
    ESP_LOGI(TAG, "demo cmd=%u fan=%u mode=%u alarm=0x%04x",
             cmd, s_demo.fan, s_demo.mode, s_demo.alarm);
    return 0;
}

/* 500ms 演示步进：一阶趋近设定值 + 微幅正弦扰动，量级贴近真机 */
static void demo_step(void)
{
    int64_t now = esp_timer_get_time();
    float t = (float)(now / 1000000LL);
    if (!s_demo.estop && s_demo.mode) {
        s_demo.tc1_act += (s_demo.tc1_set - s_demo.tc1_act) * 0.08f;
        s_demo.tc2_act += (s_demo.tc2_set - s_demo.tc2_act) * 0.06f;
        s_demo.tc1_act += 0.03f * sinf(t / 7.0f);
        s_demo.tc2_act += 0.02f * sinf(t / 11.0f + 1.0f);
        s_demo.tec1 = (s_demo.tc1_act < s_demo.tc1_set - 0.2f) ? 1u
                    : (s_demo.tc1_act > s_demo.tc1_set + 0.2f) ? 0u : s_demo.tec1;
    } else {
        s_demo.tc1_act += (25.0f - s_demo.tc1_act) * 0.01f;   /* 回环境温度 */
        s_demo.tc2_act += (25.0f - s_demo.tc2_act) * 0.01f;
    }
    s_demo.body = 36.8f + 0.35f * sinf(t / 23.0f);

    link_internal_lock();
    {
        link_status_t *st = link_status_buf();
        st->valid = 1;
        st->seq = ++s_demo.seq;
        st->tc1_act = s_demo.tc1_act;
        st->tc2_act = s_demo.tc2_act;
        st->body = s_demo.body;
        st->tec1 = s_demo.tec1;
        st->tec2 = s_demo.tec2;
        st->fan = s_demo.fan;
        st->mode = s_demo.mode;
        st->alarm = s_demo.alarm;
        st->online = 0x03;   /* 演示：M207+体温在线 */
        st->ver = TCS_PROTO_VER;    /* R3-E-6：演示态也要带版本，否则网页常驻版本黄条 */
        st->last_status_us = now;
    }
    link_internal_unlock();
    link_notify_event();
}

int link_demo_takeover(void)
{
    /* 上电 5s 宽限后：真链路 3s 无 STATUS → 演示模式接管 */
    if (link_alive()) {
        if (s_demo_on) ESP_LOGI(TAG, "demo mode OFF (real link alive)");
        s_demo_on = 0;
        s_boot_us = 0;
        return 0;
    }
    if (esp_timer_get_time() - s_boot_us > 8000000LL) {
        if (!s_demo_on) ESP_LOGI(TAG, "demo mode ON (no H743 link, web standalone demo)");
        s_demo_on = 1;
        return 1;
    }
    return 0;
}

/* 演示模型的参数同步（由 link_demo_note_param 调用）*/
static void demo_note_param(uint16_t id, float v)
{
    if (!s_demo_on) return;
    switch (id) {
    case 0x01: s_demo.tc1_set = v; break;
    case 0x02: s_demo.tc2_set = v; break;
    case 0x06: s_demo.mode = (uint8_t)(v != 0.0f); break;
    case 0x08: s_demo.commissioned = (uint8_t)(v != 0.0f); break;   /* 复核 G2 */
    default: break;
    }
    demo_push_param(id, v);   /* 演示档：网页/屏都跟着变 */
}

void link_demo_boot(void)
{
    s_boot_us = esp_timer_get_time();       /* 演示宽限起点；生产档无此变量 */
}

int link_demo_suppressed(void) { return s_demo_on ? 1 : 0; }
int link_demo_cmd(uint8_t cmd) { return demo_cmd(cmd); }
void link_demo_step(void)      { demo_step(); }

#else  /* !CONFIG_TCS_DEMO：生产档没有演示状态机 */

int  link_demo_takeover(void)   { return 0; }
int  link_demo_suppressed(void) { return 0; }
int  link_demo_cmd(uint8_t cmd) { (void)cmd; return -1; }
void link_demo_step(void)       { }
void link_demo_boot(void)       { }
static void demo_note_param(uint16_t id, float v) { (void)id; (void)v; }

#endif /* CONFIG_TCS_DEMO */

/* 演示模型同步（v2）：参数写入都落在 ESP32 的 param_set，main 的 on_broadcast
 * 调到本函数。tcs_link 不依赖 tcs_core（组件环），所以走注入而不是 param_get。*/
void link_demo_note_param(uint16_t id, float v)
{
    demo_note_param(id, v);
}

uint8_t link_demo_running(void)
{
#if CONFIG_TCS_DEMO
    return link_demo_suppressed();
#else
    return 0;
#endif
}
