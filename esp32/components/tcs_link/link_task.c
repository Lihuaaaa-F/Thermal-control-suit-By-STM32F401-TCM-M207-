/**
 * link_task.c — H743↔ESP32 可靠链路任务（任务书 §4.2）
 *
 * UART1 @230400，TX=GPIO17（→H743 PA3 RX），RX=GPIO18（←H743 PD5 TX）。
 * UART 事件队列 → 任务内逐字节 comm_rx_feed_byte（GS §0E 模式：协议处理只在任务上下文）；
 * 100ms comm_tick（重传/断链）；发送经 comm txq，任务内 comm_poll_tx 串行消费。
 * comm 层 0x90 ACK 判定已由 H743 侧同源文件扩展（comm.c 同一份拷贝）。
 */
#include "link_task.h"
#include "proto.h"
#include "comm.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include <math.h>
#include "esp_log.h"

static const char *TAG = "tcs_link";

/* 与 H743 param_registry.h 冻结一致：0=f32 1=u8 2=u16（双端同源由单测保证）*/
#ifndef PARAM_TYPE_F32
#define PARAM_TYPE_F32   0
#define PARAM_TYPE_U8    1
#define PARAM_TYPE_U16   2
#endif

#define LINK_UART       UART_NUM_1
#define LINK_TXD        17
#define LINK_RXD        18
#define LINK_BAUD       230400
#define LINK_RXBUF      2048
#define LINK_EVT_QUEUE  20

static comm_link_t s_link;
static QueueHandle_t s_uart_queue;
static link_status_t s_status;
static link_reg_t s_reg[LINK_REGISTRY_MAX];
static link_ack_t s_ack;
static link_param_change_t s_last_param;
static float s_param_val[LINK_REGISTRY_MAX];   /* 与 s_reg 同下标缓存 */
static uint8_t s_param_valid[LINK_REGISTRY_MAX];
static link_event_cb_t s_cb;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile int64_t s_real_status_us;   /* 仅真链路 0x10 更新（演示模式不碰，防自锁）*/
static void notify(void);                   /* 前向声明（demo 层早于定义使用）*/   /* 仅真链路 0x10 更新（演示模式不碰，防自锁）*/

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

/* ===================================================================== */
/* WP-F.2 演示模式：H743 离线时模拟状态源，网页全功能独立演示               */
/* 真链路 STATUS 一到即自动退出（link_alive 判定），无需任何配置。           */
/* ===================================================================== */
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
    s_last_param.id = id;
    s_last_param.v = v;
    s_last_param.us = esp_timer_get_time();
    for (int i = 0; i < LINK_REGISTRY_MAX; i++)
        if (s_reg[i].used && s_reg[i].e.id == id) {
            s_param_val[i] = v;
            s_param_valid[i] = 1;
            break;
        }
    notify();
}

static int demo_set_param(uint16_t id, float v)
{
    switch (id) {
    case 0x01: s_demo.tc1_set = v; break;
    case 0x02: s_demo.tc2_set = v; break;
    case 0x06: s_demo.mode = (uint8_t)v; break;
    case 0x08: s_demo.commissioned = (uint8_t)v; break;
    default: break;   /* 其余参数仅缓存 */
    }
    demo_push_param(id, v);
    ESP_LOGI(TAG, "demo set id=0x%02x v=%.2f", id, v);
    return 0;
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

    taskENTER_CRITICAL(&s_mux);
    s_status.valid = 1;
    s_status.seq = ++s_demo.seq;
    s_status.tc1_act = s_demo.tc1_act;
    s_status.tc2_act = s_demo.tc2_act;
    s_status.body = s_demo.body;
    s_status.tec1 = s_demo.tec1;
    s_status.tec2 = s_demo.tec2;
    s_status.fan = s_demo.fan;
    s_status.mode = s_demo.mode;
    s_status.alarm = s_demo.alarm;
    s_status.online = 0x03;   /* 演示：M207+体温在线 */
    s_status.last_status_us = now;
    taskEXIT_CRITICAL(&s_mux);
    notify();
}

static int demo_active(void)
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

/* —— UART 发送钩子（comm 注入；comm_poll_tx 任务内串行调用）—— */
static void link_uart_send(const uint8_t *d, size_t n)
{
    uart_write_bytes(LINK_UART, d, n);
}

static uint32_t link_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

/* FreeRTOS 无全局 __disable_irq；临界区保护 tx 队列索引读改写 */
static void link_crit_enter(void) { taskENTER_CRITICAL(&s_mux); }
static void link_crit_exit(void)  { taskEXIT_CRITICAL(&s_mux); }

/* —— 组帧（MT+Seq+body → 整帧）—— */
static size_t mt_frame(uint8_t mt, uint8_t seq, const uint8_t *body, size_t blen,
                       uint8_t *out, size_t cap)
{
    uint8_t payload[MAX_PAYLOAD];
    if (blen + 2 > MAX_PAYLOAD) return 0;
    payload[0] = mt;
    payload[1] = seq;
    memcpy(&payload[2], body, blen);
    return proto_encode_frame(payload, blen + 2, out, cap);
}

static void notify(void)
{
    if (s_cb) s_cb();
}

/* —— 帧分发（任务上下文）—— */
static void on_frame(const uint8_t *payload, size_t len)
{
    if (len < 2) return;
    uint8_t mt = payload[0];
    const uint8_t *body = &payload[2];
    size_t blen = len - 2;

    switch (mt) {
    case MT_TCS_STATUS: {
        tcs_status_pl_t s;
        if (tcs_scan_status(body, blen, &s) != 0) break;
        taskENTER_CRITICAL(&s_mux);
        s_status.valid = 1;
        s_status.seq = s.seq;
        s_status.tc1_act = s.tc1_act;
        s_status.tc2_act = s.tc2_act;
        s_status.body = s.body;
        s_status.tec1 = s.tec1;
        s_status.tec2 = s.tec2;
        s_status.fan = s.fan;
        s_status.mode = s.mode;
        s_status.alarm = s.alarm;
        s_status.online = s.online;
        s_status.last_status_us = esp_timer_get_time();
        s_real_status_us = esp_timer_get_time();
        taskEXIT_CRITICAL(&s_mux);
        notify();
        break;
    }
    case MT_TCS_PARAM: {
        tcs_param_pl_t p;
        if (tcs_scan_param(body, blen, &p) != 0) break;
        float v = (p.type == PARAM_TYPE_F32) ? tcs_b2f(p.value) : (float)p.value;
        ESP_LOGI(TAG, "PARAM id=0x%02x v=%.2f", p.id, v);
        taskENTER_CRITICAL(&s_mux);
        s_last_param.id = p.id;
        s_last_param.v = v;
        s_last_param.us = esp_timer_get_time();
        for (int i = 0; i < LINK_REGISTRY_MAX; i++) {
            if (s_reg[i].used && s_reg[i].e.id == p.id) {
                s_param_val[i] = v;
                s_param_valid[i] = 1;
                break;
            }
        }
        taskEXIT_CRITICAL(&s_mux);
        notify();
        break;
    }
    case MT_TCS_REGISTRY_ENTRY: {
        tcs_reg_entry_t e;
        if (tcs_scan_registry_entry(body, blen, &e) != 0) break;
        if (e.idx < LINK_REGISTRY_MAX) {
            s_reg[e.idx].used = 1;
            s_reg[e.idx].e = e;
            ESP_LOGI(TAG, "REG[%u/%u] 0x%02x %s", e.idx + 1, e.count, e.id, e.key);
            notify();
        }
        break;
    }
    default:
        break;   /* 0x90 已被 comm 层消费；0x12/0x13/0x15 不会从 H743 发来 */
    }
}

/* —— 上行 API —— */
int link_set_param(uint16_t id, float v)
{
    if (s_demo_on) return demo_set_param(id, v);   /* WP-F.2：演示模式本地闭环 */
    uint8_t pl[TCS_PARAM_PL_LEN];
    tcs_param_pl_t p = { .id = id, .type = PARAM_TYPE_F32, .value = tcs_f2b(v) };
    size_t n = tcs_pl_param(pl, sizeof(pl), &p);
    uint8_t frame[MAX_FRAME];
    size_t fn = mt_frame(MT_TCS_SET_PARAM, comm_next_seq(&s_link), pl, n,
                         frame, sizeof(frame));
    if (!fn) return -1;
    comm_send(&s_link, frame, fn, MT_TCS_SET_PARAM);
    return 0;
}

int link_send_frame_mt(uint8_t mt, const uint8_t *pl, size_t n)
{
    if (s_demo_on) return 0;                       /* 演示模式不发（本地闭环）*/
    uint8_t frame[MAX_FRAME];
    size_t fn = mt_frame(mt, comm_next_seq(&s_link), pl, n, frame, sizeof(frame));
    if (!fn) return -1;
    comm_send(&s_link, frame, fn, mt);
    return 0;
}

int link_send_cmd(uint8_t cmd)
{
    if (s_demo_on) return demo_cmd(cmd);           /* WP-F.2：演示模式本地闭环 */
    uint8_t frame[MAX_FRAME];
    size_t fn = mt_frame(MT_TCS_CMD, comm_next_seq(&s_link), &cmd, 1,
                         frame, sizeof(frame));
    if (!fn) return -1;
    comm_send(&s_link, frame, fn, MT_TCS_CMD);
    return 0;
}

int link_get_registry(link_reg_t *out, int max)
{
    taskENTER_CRITICAL(&s_mux);
    int n = 0;
    for (int i = 0; i < LINK_REGISTRY_MAX && n < max; i++)
        if (s_reg[i].used) out[n++] = s_reg[i];
    taskEXIT_CRITICAL(&s_mux);
    return n;
}

void link_request_registry(void)
{
    uint8_t frame[MAX_FRAME];
    size_t fn = mt_frame(MT_TCS_GET_REGISTRY, comm_next_seq(&s_link), NULL, 0,
                         frame, sizeof(frame));
    if (fn) comm_send(&s_link, frame, fn, MT_TCS_GET_REGISTRY);
}

bool link_alive(void)
{
    /* 只看真链路心跳（demo_step 不更新 s_real_status_us，防演示模式自锁）*/
    int64_t last = s_real_status_us;
    return last != 0 && (esp_timer_get_time() - last) < 3000000LL;
}

const link_status_t *link_status(void) { return &s_status; }
const link_param_change_t *link_last_param(void) { return &s_last_param; }

float link_param_value(uint16_t id)
{
    taskENTER_CRITICAL(&s_mux);
    float v = 0.0f;
    uint8_t ok = 0;
    for (int i = 0; i < LINK_REGISTRY_MAX; i++) {
        if (s_reg[i].used && s_reg[i].e.id == id) {
            v = s_param_val[i];
            ok = s_param_valid[i];
            break;
        }
    }
    taskEXIT_CRITICAL(&s_mux);
    return ok ? v : NAN;
}
const link_ack_t *link_last_ack(void)  { return &s_ack; }
void link_on_event(link_event_cb_t cb) { s_cb = cb; }

/* —— 任务 —— */
static void link_task(void *arg)
{
    uart_event_t evt;
    uint8_t buf[256];
    uint32_t last_tick = 0;
    uint32_t last_reg_req = 0;

    ESP_LOGI(TAG, "link task up");
    /* 握手：要注册表全量（无 STATUS 前每 2s 重试一次）*/
    for (int i = 0; i < 5 && !s_status.valid; i++) {
        link_request_registry();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    while (1) {
        if (xQueueReceive(s_uart_queue, &evt, pdMS_TO_TICKS(50))) {
            if (evt.type == UART_DATA) {
                int n = uart_read_bytes(LINK_UART, buf, sizeof(buf), 0);
                for (int i = 0; i < n; i++) {
                    const uint8_t *pl; size_t plen; int dup;
                    if (comm_rx_feed_byte(&s_link, buf[i], &pl, &plen, &dup))
                        on_frame(pl, plen);
                }
            } else if (evt.type == UART_FIFO_OVF || evt.type == UART_BUFFER_FULL) {
                uart_flush_input(LINK_UART);
                ESP_LOGW(TAG, "uart overflow flushed");
            }
        }
        uint32_t now = link_now_ms();
        if (now - last_tick >= 100) {
            last_tick = now;
            comm_tick(&s_link);
            comm_poll_tx(&s_link);   /* 队列串行消费 */
        }
        if (now - last_reg_req >= 5000 && !s_reg[0].used) {
            last_reg_req = now;
            link_request_registry();
        }
        /* WP-F.2 演示模式：真链路离线时 500ms 模拟步进（0x10 快照同构）*/
        static uint32_t last_demo = 0;
        if (demo_active() && now - last_demo >= 500) {
            last_demo = now;
            demo_step();
        }
    }
}

void link_start(void)
{
    memset(&s_status, 0, sizeof(s_status));
    memset(s_reg, 0, sizeof(s_reg));
    registry_seed();                        /* WP-F.2/F3：内置快照先行 */
    s_boot_us = esp_timer_get_time();
    comm_init(&s_link, link_uart_send, link_now_ms, NULL, link_crit_enter, link_crit_exit);

    uart_config_t cfg = {
        .baud_rate = LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, LINK_RXBUF, 0, LINK_EVT_QUEUE,
                                        &s_uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, LINK_TXD, LINK_RXD,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreate(link_task, "tcs_link", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "started uart1 %d baud tx=%d rx=%d", LINK_BAUD, LINK_TXD, LINK_RXD);
}

/* ===================================================================== */
/* WP-F.1 真机自环：UART1 GPIO17(TX)→GPIO18(RX) 内部回环                  */
/* 真驱动/真帧/真协议栈：编解码回环、重复帧去重、坏 CRC 丢弃、ARQ 重传观测。 */
/* 在 link_start 之前调用（独占 UART1）；证据=[LPBK] 日志行。              */
/* ===================================================================== */
static void lp_send_fn(const uint8_t *d, size_t n)
{
    uart_write_bytes(LINK_UART, d, n);
}

static int lp_drain_rx(comm_link_t *lp, int *dups)
{
    uint8_t buf[256];
    int frames = 0;
    int n = uart_read_bytes(LINK_UART, buf, sizeof(buf), 0);
    for (int i = 0; i < n; i++) {
        const uint8_t *pl; size_t plen; int dup;
        if (comm_rx_feed_byte(lp, buf[i], &pl, &plen, &dup)) frames++;
        else if (dup) (*dups)++;
    }
    return frames;
}

void link_loopback_run(void)
{
    QueueHandle_t q = NULL;
    uart_config_t cfg = {
        .baud_rate = LINK_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(LINK_UART, 2048, 2048, 20, &q, 0);
    uart_param_config(LINK_UART, &cfg);
    uart_set_pin(LINK_UART, LINK_TXD, LINK_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_loop_back(LINK_UART, true);      /* ESP-IDF 内部回环：真机无杜邦线 */
    vTaskDelay(pdMS_TO_TICKS(20));

    static comm_link_t lp;   /* ~6KB，app_main 栈放不下（3.5K），必须 static */
    comm_init(&lp, lp_send_fn, link_now_ms, NULL, NULL, NULL);
    uint8_t frame[MAX_FRAME];
    uint8_t payload[MAX_PAYLOAD];
    int dups = 0, pass = 0, fail = 0;

    /* 1) 帧回环：STATUS 帧 TX→RX 原样解出 */
    tcs_status_pl_t st = { .ver = 1, .seq = 0x11223344, .tc1_act = 25.5f,
                           .tc2_act = 24.5f, .body = 36.8f, .tec1 = 0, .tec2 = 0,
                           .fan = 0, .mode = 0, .alarm = 0, .online = 3, .rfu = 0 };
    size_t pn = tcs_pl_status(payload + 2, sizeof(payload) - 2, &st);
    payload[0] = MT_TCS_STATUS; payload[1] = 0x5A;
    size_t fn = proto_encode_frame(payload, pn + 2, frame, sizeof(frame));
    uart_write_bytes(LINK_UART, frame, fn);
    vTaskDelay(pdMS_TO_TICKS(50));
    int got = lp_drain_rx(&lp, &dups);
    if (got == 1) { pass++; ESP_LOGI(TAG, "[LPBK] frame loopback PASS (1 frame decoded)"); }
    else { fail++; ESP_LOGE(TAG, "[LPBK] frame loopback FAIL (got %d)", got); }

    /* 2) 重复帧去重：同帧再发 → is_dup 命中 */
    uart_write_bytes(LINK_UART, frame, fn);
    vTaskDelay(pdMS_TO_TICKS(50));
    got = lp_drain_rx(&lp, &dups);
    if (got == 0 && dups >= 1) { pass++; ESP_LOGI(TAG, "[LPBK] dup dedup PASS (dup=%d)", dups); }
    else { fail++; ESP_LOGE(TAG, "[LPBK] dup dedup FAIL (frames=%d dups=%d)", got, dups); }

    /* 3) 坏 CRC 丢弃：翻转 1 字节 → 接收侧静默丢弃 */
    frame[fn - 1] ^= 0xFF;
    uart_write_bytes(LINK_UART, frame, fn);
    vTaskDelay(pdMS_TO_TICKS(50));
    got = lp_drain_rx(&lp, &dups);
    if (got == 0) { pass++; ESP_LOGI(TAG, "[LPBK] bad-crc drop PASS"); }
    else { fail++; ESP_LOGE(TAG, "[LPBK] bad-crc drop FAIL (got %d)", got); }

    /* 4) ARQ 重传观测：SET_PARAM 无 ACK，500ms 超时 ×3 重传 → 回环收到多份 */
    tcs_param_pl_t pm = { .id = 0x01, .type = PARAM_TYPE_F32, .value = tcs_f2b(30.5f) };
    size_t mn = tcs_pl_param(payload + 2, sizeof(payload) - 2, &pm);
    payload[0] = MT_TCS_SET_PARAM; payload[1] = 0xA7;
    size_t mfn = proto_encode_frame(payload, mn + 2, frame, sizeof(frame));
    comm_send(&lp, frame, mfn, MT_TCS_SET_PARAM);
    int copies = 0;
    for (int i = 0; i < 22; i++) {            /* 2.2s > 500ms×3+裕量 */
        vTaskDelay(pdMS_TO_TICKS(100));
        comm_tick(&lp);
        comm_poll_tx(&lp);
        copies += lp_drain_rx(&lp, &dups);
    }
    if (copies >= 3) { pass++; ESP_LOGI(TAG, "[LPBK] ARQ retransmit PASS (%d copies rx)", copies); }
    else { fail++; ESP_LOGE(TAG, "[LPBK] ARQ retransmit FAIL (%d copies)", copies); }

    uart_set_loop_back(LINK_UART, false);
    uart_driver_delete(LINK_UART);
    ESP_LOGI(TAG, "[LPBK] RESULT pass=%d fail=%d %s", pass, fail,
             fail ? "FAIL" : "PASS");
}
