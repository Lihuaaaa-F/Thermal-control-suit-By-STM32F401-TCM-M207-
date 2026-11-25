/**
 * link_task.c — H743↔ESP32 可靠链路任务（任务书 §4.2）
 *
 * UART1 @230400，TX=GPIO17（→H743 PA3 RX），RX=GPIO18（←H743 PD5 TX）。
 * UART 事件队列 → 任务内逐字节 comm_rx_feed_byte（GS §0E 模式：协议处理只在任务上下文）；
 * 100ms comm_tick（重传/断链）；发送经 comm txq，任务内 comm_poll_tx 串行消费。
 * comm 层 0x90 ACK 判定已由 H743 侧同源文件扩展（comm.c 同一份拷贝）。
 * 官方查证：检索组件库 2026-10-26 无官方件（双板私有协议，GS 血统）；UART 通道用官方 driver/uart，任务用官方 FreeRTOS；实现参考 IDF examples/system/freertos/（任务骨架模式）
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
#include "esp_task_wdt.h"

static const char *TAG = "tcs_link";

/* 与 H743 param_registry.h 冻结一致：0=f32 1=u8 2=u16（双端同源由单测保证）*/
#ifndef PARAM_TYPE_F32
#define PARAM_TYPE_F32   0
#define PARAM_TYPE_U8    1
#define PARAM_TYPE_U16   2
#define PARAM_TYPE_BTN   3   /* R3-D-1：与 param_registry.h 同步（BTN=动作项，不走 0x11/0x12）*/
#endif

#define LINK_UART       UART_NUM_1
#define LINK_TXD        17
#define LINK_RXD        18
#define LINK_BAUD       230400
#define LINK_RXBUF      2048
/* 2026-11-23 现场实测（阶段 0 基线）：TX 环原为 0（无环，阻塞直写），而
 * link_uart_send 用 uart_get_tx_buffer_free_size() 判"环是否满"——无 TX 环时它
 * 恒返回 0 → free>=n 永假 → 每一帧都被当"环满"丢弃，日志刷
 * "link tx ring full"。即自 316e5a9（11-15 非阻塞改造）起链路发送实际全灭，
 * 主机单测/门禁覆盖不到（IDF 驱动行为）。给驱动一个真实 TX 环，非阻塞判定才成立。*/
#define LINK_TXBUF      1024
#define LINK_EVT_QUEUE  20

static comm_link_t s_link;
static QueueHandle_t s_uart_queue;
static link_status_t s_status;
static volatile uint32_t s_rx_bytes;   /* UART1 收到的字节总数（真机取证）*/
static volatile uint32_t s_rx_frames;  /* 解出的完整帧数 */
static uint32_t s_proto_mismatch;      /* M11-F-09：STATUS 协议版本不符帧数 */
static link_cmd_cb_t s_cmd_cb;         /* v2：屏幕 0x15 命令回调 */
static link_reg_t s_reg[LINK_REGISTRY_MAX];
/* R1 P1-01：注册表接收进度（替代"种子 used 恒真"导致永不重发 0x13 的短路）*/
static uint16_t s_reg_expect;
static uint16_t s_reg_got;
static volatile uint8_t s_reg_done;
static link_param_change_t s_last_param;
static float s_param_val[LINK_REGISTRY_MAX];   /* 与 s_reg 同下标缓存 */
static uint8_t s_param_valid[LINK_REGISTRY_MAX];
static link_event_cb_t s_cb;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t s_real_status_ms;  /* M11-F-12：int64 拆两半读会撕裂，改 32 位毫秒（单写者+32 位对齐=原子）*/
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
/* R1 修复 P0-05 / AD-5：演示模式改为编译期可选（CONFIG_TCS_DEMO，默认 n）。
 * 生产固件不含演示状态机——断链只报"离线"，不再用合成温度冒充真值。      */
#if CONFIG_TCS_DEMO
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
    /* 复核 G3：与 link_last_param_copy()/link_get_registry() 的读共用 s_mux，
     * 否则 DEMO 下 httpd 任务可能读到"新 id + 旧值"。*/
    taskENTER_CRITICAL(&s_mux);
    s_last_param.id = id;
    s_last_param.v = v;
    s_last_param.us = esp_timer_get_time();
    for (int i = 0; i < LINK_REGISTRY_MAX; i++)
        if (s_reg[i].used && s_reg[i].e.id == id) {
            s_param_val[i] = v;
            s_param_valid[i] = 1;
            break;
        }
    taskEXIT_CRITICAL(&s_mux);
    notify();
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
    s_status.ver = TCS_PROTO_VER;    /* R3-E-6：演示态也要带版本，否则网页常驻版本黄条 */
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
#else  /* !CONFIG_TCS_DEMO：生产档没有演示状态机 */
#define s_demo_on 0                      /* 编译期常 0：所有演示分支被优化掉 */
static int demo_active(void) { return 0; }
#endif /* CONFIG_TCS_DEMO */

/* 演示模型同步（v2）：参数写入都落在 ESP32 的 param_set，main 的 on_broadcast
 * 调到本函数。tcs_link 不依赖 tcs_core（组件环），所以走注入而不是 param_get。*/
void link_demo_note_param(uint16_t id, float v)
{
#if CONFIG_TCS_DEMO
    if (!s_demo_on) return;
    switch (id) {
    case 0x01: s_demo.tc1_set = v; break;
    case 0x02: s_demo.tc2_set = v; break;
    case 0x06: s_demo.mode = (uint8_t)(v != 0.0f); break;
    case 0x08: s_demo.commissioned = (uint8_t)(v != 0.0f); break;   /* 复核 G2 */
    default: break;
    }
    demo_push_param(id, v);   /* 演示档：网页/屏都跟着变 */
#else
    (void)id; (void)v;
#endif
}


/* —— UART 发送钩子（comm 注入；comm_poll_tx 任务内串行调用）—— */
static uint32_t s_uart_txdrop;   /* 驱动 TX 环满丢弃（linkstat 可见）*/

/* 回声防护（2026-11-25 重做，R5-01 / R5 复核）：屏→ESP32 那根发送线接触不良时，本端会在自己发送
 * 期间从 RX 脚串扰收到自己刚发出去的帧。2026-11-23 现场一条被改写的回声被当成"屏的写入"落进
 * 权威注册表（40.1 → 钳到下限 5.0）。当时的防护是"发送窗内收到等长的帧就丢"，它放在
 * comm_rx_feed_byte 之后（ACK 回声在 comm 层内已经被消费）、只比长度不比方向，并且会误杀
 * 紧跟在本端发送之后到达的屏帧（R5-10）。
 * 现在帧里有方向位（protocol/tcs_proto.h「方向」）：本端发出的帧 bit6=1。回送帧在 comm 层的
 * 方向闸门里、任何处理（ACK、去重、交付、判活）之前丢弃，只看字节，不看长度、不看时刻。
 * 这里只剩两件事：把计数报出来；周期性告警提示查接线。*/

/* 链路诊断（只在 CONFIG_TCS_SIM / CONFIG_TCS_DIAG 构建里；PROD 没有下面任何符号）：
 *   link_mute_ms(N)   —— N 毫秒内既不发也不收（收到的字节直接丢弃）。不拔线就能诱导
 *                        "屏链路断开再恢复"，用来验证两端的恢复逻辑；物理层仍要人工拔线。
 *   link_loopback(1)  —— 芯片内部把 TX 接到 RX：本端发什么就收什么，是确定性的回声源，
 *                        用来在真机上验证方向闸门（echo 计数应增长，不得出现参数写入）。*/
#if CONFIG_TCS_SIM || CONFIG_TCS_DIAG
static volatile uint32_t s_mute_until_ms;
static volatile uint8_t  s_loopback;
static uint32_t s_mute_rxdrop;

static uint32_t diag_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000LL); }
uint8_t  link_muted(void)            { return (int32_t)(s_mute_until_ms - diag_now_ms()) > 0; }
void     link_mute_ms(uint32_t ms)   { s_mute_until_ms = diag_now_ms() + ms; }
uint32_t link_mute_rxdrop(void)      { return s_mute_rxdrop; }
esp_err_t link_loopback(uint8_t on)
{
    s_loopback = on ? 1u : 0u;
    return uart_set_loop_back(LINK_UART, on ? true : false);
}
#endif

static void link_uart_send(const uint8_t *d, size_t n)
{
    /* R1 P1-07：非阻塞写——TX 环满时丢弃该帧（comm 层 ARQ 会重传），
     * 不让 uart_write_bytes 阻塞把 link 任务拖死触发 TWDT。*/
#if CONFIG_TCS_SIM || CONFIG_TCS_DIAG
    if (link_muted()) return;             /* 诊断静默期：不上线。no-ARQ 帧丢弃，ARQ 帧由 comm 层超时重传 */
#endif
    size_t free_sz = 0;
    if (uart_get_tx_buffer_free_size(LINK_UART, &free_sz) == ESP_OK && free_sz >= n) {
        uart_write_bytes(LINK_UART, d, n);     /* 帧已由 comm 层带上方向位 */
    } else {
        s_uart_txdrop++;
        if ((s_uart_txdrop % 50u) == 1u) ESP_LOGW(TAG, "link tx ring full, dropped %lu frames", (unsigned long)s_uart_txdrop);
    }
}

static uint32_t link_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

/* FreeRTOS 无全局 __disable_irq；临界区保护 tx 队列索引读改写 */
static void link_crit_enter(void) { taskENTER_CRITICAL(&s_mux); }
static void link_crit_exit(void)  { taskEXIT_CRITICAL(&s_mux); }

/* comm_tick 判 ARQ 全败（链路死）时调用：作废注册表收齐标记，重连后重新全量请求。
 * 只做这一件事——链路丢失按用户裁决「只报告」，不置任何锁存。*/
static void link_on_down(void)
{
    s_reg_done = 0;
    s_reg_got = 0;
    ESP_LOGW(TAG, "link down (ARQ) -> registry resync armed");
}

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

static link_setparam_cb_t s_setparam_cb;

void link_on_setparam(link_setparam_cb_t cb)
{
    s_setparam_cb = cb;
}

/* R2：回 ACK（H743 侧 comm 层按 (seq,mt) 匹配 pending；fire-and-forget 入队）*/
void link_send_ack(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret)
{
    uint8_t pl[TCS_ACK_PL_LEN];
    tcs_ack_pl_t a = { acked_seq, acked_mt, status, ret };
    size_t n = tcs_pl_ack(pl, sizeof(pl), &a);
    uint8_t frame[MAX_FRAME];
    size_t fn = mt_frame(MT_TCS_ACK, comm_next_seq(&s_link), pl, n,
                         frame, sizeof(frame));
    if (fn) comm_txq_push(&s_link, frame, fn);
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
        /* M11-F-09：版本不符不能静默吞掉——留痕 + 计数，网页用真实 ver 显示黄条
         * （原 ws_status_json 硬编码 ver=1，双方版本漂移永远看不见）。
         * 不新增 ALM_PROTO 报警位：ALM 是发给屏的线格式契约，屏侧文案/字模未同步前
         * 加位只会得到"未知位"；版本差的既定 UX 就是网页顶部黄条。*/
        if (s.ver != TCS_PROTO_VER) {
            s_proto_mismatch++;
            if (s_proto_mismatch <= 3 || (s_proto_mismatch % 100u) == 0)
                ESP_LOGW(TAG, "STATUS ver=%u != local %u (count=%lu)",
                         (unsigned)s.ver, (unsigned)TCS_PROTO_VER,
                         (unsigned long)s_proto_mismatch);
        }
        taskENTER_CRITICAL(&s_mux);
        s_status.valid = 1;
        s_status.ver = s.ver;
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
        s_real_status_ms = (uint32_t)(esp_timer_get_time() / 1000LL);
        taskEXIT_CRITICAL(&s_mux);
        notify();
        break;
    }
    case MT_TCS_REGISTRY_ENTRY: {
        tcs_reg_entry_t e;
        if (tcs_scan_registry_entry(body, blen, &e) != 0) break;
        if (e.idx < LINK_REGISTRY_MAX) {
            /* M11-F-07：s_reg 被 link_get_registry()/link_param_value() 并发读，
             * 写入必须整体进同一临界区（原实现逐字段写，读侧可能读到"新 id + 旧名字"）。*/
            uint16_t got = 0, expect = 0;
            uint8_t done = 0;
            taskENTER_CRITICAL(&s_mux);
            s_reg[e.idx].used = 1;
            s_reg[e.idx].e = e;
            if (e.idx == 0) { s_reg_expect = e.count; s_reg_got = 0; }
            if ((uint16_t)(e.idx + 1u) > s_reg_got) s_reg_got = (uint16_t)(e.idx + 1u);
            if (!s_reg_done && s_reg_expect && s_reg_got >= s_reg_expect) {
                s_reg_done = 1;
                done = 1;
            }
            got = s_reg_got; expect = s_reg_expect;
            taskEXIT_CRITICAL(&s_mux);
            if (done) ESP_LOGI(TAG, "registry complete %u/%u", (unsigned)got, (unsigned)expect);
            ESP_LOGI(TAG, "REG[%u/%u] 0x%02x %s", e.idx + 1, e.count, e.id, e.key);
            notify();
        }
        break;
    }
    case MT_TCS_SET_PARAM: {         /* 屏写参数（R5-10 起所有参数都走这一条，等 ACK）→ 本端权威注册表 → M207 verify */
        tcs_param_pl_t p;
        uint8_t seq = payload[1];
        if (tcs_scan_param(body, blen, &p) != 0) {
            link_send_ack(seq, mt, TCS_ACK_BAD_ID, 0.0f);
            break;
        }
        float v = (p.type == PARAM_TYPE_F32) ? tcs_b2f(p.value) : (float)p.value;
        /* 这里也是 web 的 pv 推送源之一：记"最近一次屏的参数写入"（R5-01 之前 0x11 的
         * 广播也走这段，本端镜像回声因此能冒充屏的写入；现在 0x11 是主控专用，进不了这里）。*/
        taskENTER_CRITICAL(&s_mux);
        s_last_param.id = p.id;
        s_last_param.v = v;
        s_last_param.us = esp_timer_get_time();
        taskEXIT_CRITICAL(&s_mux);
        /* 破组件环（tcs_core→tcs_link）：应用层注册处理器落权威注册表并回 ACK */
        if (s_setparam_cb) {
            s_setparam_cb(p.id, v, seq);
        } else {
            link_send_ack(seq, mt, TCS_ACK_BUSY, 0.0f);
        }
        notify();
        break;
    }
    case MT_TCS_CMD: {               /* v2 反向：屏幕急停/确认 → 本端执行 */
        uint8_t seq = payload[1];
        if (blen < 1) { link_send_ack(seq, mt, TCS_ACK_BAD_ID, 0.0f); break; }
        uint8_t cmd = body[0];
        if (s_cmd_cb) s_cmd_cb(cmd, seq);
        else link_send_ack(seq, mt, TCS_ACK_BUSY, 0.0f);
        break;
    }
    default:
        break;   /* 0x90 已被 comm 层消费；0x11/0x20/0x21/0x22 是本端专用（方向闸门在 comm 层就丢了）；0x13 仅本端发起 */
    }
}

/* —— 上行 API —— */

/* 返回值（M08-F-06）：0=已入队，1=演示模式抑制（无真链路，属正常），-1=组帧失败 */
int link_send_frame_mt(uint8_t mt, const uint8_t *pl, size_t n)
{
#if CONFIG_TCS_DEMO
    if (s_demo_on) return 1;                       /* 演示模式不发（本地闭环）*/
#endif
    uint8_t frame[MAX_FRAME];
    size_t fn = mt_frame(mt, comm_next_seq(&s_link), pl, n, frame, sizeof(frame));
    if (!fn) return -1;
    /* R1 P1-02：0x20/0x21 免 ARQ。屏端本来就不回 ACK，走 ARQ 会让 8 槽 pending
     * 被快照长期占满、每帧重传 4 次，并把参数写/命令帧挤掉。*/
    if (mt == MT_TCS_SNAPSHOT || mt == MT_TCS_DELTA) {
        comm_send_noarq(&s_link, frame, fn);
    } else {
        comm_send(&s_link, frame, fn, mt);
    }
    return 0;
}

/* v2 补口（2026-11-16）：ESP32→H743 参数镜像。R5-01（2026-11-25）起 0x11 只在这个方向使用：
 * 屏写参数一律走 0x12（等 ACK），ESP32 收到 0x11 一律按方向表丢弃，不存在"把 0x11 当成屏的
 * 写入"的代码路径——2026-11-23 现场那条 tc1_set 40.1→5.0 的路（本端镜像回声冒充屏的写入）由此关掉。
 * 背景：网页/控制台改的非设定值参数（body_hot/body_cold/hyst/tc_set_confirm/0x1xx…）需要
 * 有通道回屏，否则屏上显示旧值，用户在屏上再按 +/- 就会用旧值覆盖网页的新值（静默数据丢失）。
 * H743 侧 param_mirror_apply 静默落表、不回推，故不会成环。*/
int link_send_param(uint16_t id, float v)
{
#if CONFIG_TCS_DEMO
    if (s_demo_on) return 1;          /* 演示模式无真链路（与 link_send_frame_mt 同规）*/
#endif
    uint8_t pl[TCS_PARAM_PL_LEN];
    tcs_param_pl_t p = { .id = id, .type = PARAM_TYPE_F32, .value = tcs_f2b(v) };
    size_t n = tcs_pl_param(pl, sizeof(pl), &p);
    if (!n) return -1;
    uint8_t frame[MAX_FRAME];
    size_t fn = mt_frame(MT_TCS_PARAM, comm_next_seq(&s_link), pl, n, frame, sizeof(frame));
    if (!fn) return -1;
    /* 首发就用 no-ARQ：0x11 是对端"变更即推"的单向帧，H743 侧收到不回的 ACK。
     * 用 comm_send 会登记 pending 却永远等不到确认 → 4 次重传后把链路判成断开。*/
    comm_send_noarq(&s_link, frame, fn);
    return 0;
}

/* U 线 W3：0x22 设备信息（热点名/密码/网址/固件版本）→ 屏的连接页。
 * 与 link_send_param 同规：单向周期帧，免 ARQ（屏侧不回 ACK）。载荷由调用方组好。*/
int link_send_info(const uint8_t *pl, size_t n)
{
#if CONFIG_TCS_DEMO
    if (s_demo_on) return 1;
#endif
    if (!pl || !n || n > TCS_INFO_MAX_LEN) return -1;
    uint8_t frame[MAX_FRAME];
    size_t fn = mt_frame(MT_TCS_INFO, comm_next_seq(&s_link), pl, n, frame, sizeof(frame));
    if (!fn) return -1;
    comm_send_noarq(&s_link, frame, fn);
    return 0;
}

int link_send_cmd(uint8_t cmd)
{
#if CONFIG_TCS_DEMO
    if (s_demo_on) return demo_cmd(cmd);           /* WP-F.2：演示模式本地闭环 */
#endif
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
    /* 只看真链路心跳（demo_step 不更新 s_real_status_ms，防演示模式自锁）*/
    uint32_t last = s_real_status_ms;
    uint32_t now  = (uint32_t)(esp_timer_get_time() / 1000LL);
    return last != 0 && (uint32_t)(now - last) < 3000u;
}

/* M11-F-12：整块拷贝返回，杜绝调用方拿着内部指针逐字段读（与 0x10 写入交错
 * 会拼出"新温度 + 旧报警"的混合状态）。*out 恒有定义，valid=0 表示尚无 0x10。*/
void link_status_copy(link_status_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_mux);
    *out = s_status;
    taskEXIT_CRITICAL(&s_mux);
}
void link_on_cmd(link_cmd_cb_t cb) { s_cmd_cb = cb; }
uint32_t link_rx_bytes(void) { return s_rx_bytes; }
uint32_t link_rx_frames(void) { return s_rx_frames; }

void link_stats_get(link_stats_t *out)
{
    if (!out) return;
    out->rx_bytes = s_rx_bytes;
    out->rx_frames = s_rx_frames;
    out->crc_err = s_link.rx.n_crc_err;
    out->len_err = s_link.rx.n_len_err;
    out->gap_resets = s_link.rx_gap_resets;
    out->dup = s_link.dup_cnt;
    out->retry = s_link.retry_cnt;
    out->txq_drop = s_link.txq_drop;
    out->uart_txdrop = s_uart_txdrop;
    /* R5-01：回声 = 本端发出的帧被回送、按方向位丢弃的帧数（comm 层方向闸门）；
     * wdir = 方向位说是对端、但方向表不允许的消息号（不是回声：坏帧或版本不匹配的对端）。*/
    out->echo_drop = s_link.echo_cnt;
    out->wrongdir = s_link.wrongdir_cnt;
    out->tx_wrongdir = s_link.tx_wrongdir_cnt;
    out->arq_ok = (uint8_t)(s_link.link_ok ? 1u : 0u);
    out->echo_param = s_link.echo_by_mt[MT_TCS_PARAM];
    out->echo_snap  = (uint32_t)s_link.echo_by_mt[MT_TCS_SNAPSHOT] + s_link.echo_by_mt[MT_TCS_DELTA];
    out->echo_info  = s_link.echo_by_mt[MT_TCS_INFO];
    out->echo_ack   = s_link.echo_by_mt[MT_TCS_ACK];
    out->echo_getreg = s_link.echo_by_mt[MT_TCS_GET_REGISTRY];
    out->pending_evict = s_link.pending_evict;
    out->proto_mismatch = s_proto_mismatch;
}
void link_last_param_copy(link_param_change_t *out)
{
    if (!out) return;
    taskENTER_CRITICAL(&s_mux);
    *out = s_last_param;
    taskEXIT_CRITICAL(&s_mux);
}

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
void link_on_event(link_event_cb_t cb) { s_cb = cb; }

uint8_t link_demo_running(void)
{
#if CONFIG_TCS_DEMO
    return s_demo_on;
#else
    return 0;
#endif
}

/* —— 任务 —— */
static void link_task(void *arg)
{
    esp_task_wdt_add(NULL);   /* R1 P1-07：关键任务纳入 TWDT（原来只有 ctrl_svc）*/
    uart_event_t evt;
    uint8_t buf[256];
    uint32_t last_tick = 0;
    uint32_t last_reg_req = 0;
    uint32_t last_echo = 0;    /* R5-01：上一圈的本端回送帧数（只用来触发告警）*/

    ESP_LOGI(TAG, "link task up");
    /* 握手：要注册表全量（无 STATUS 前每 2s 重试一次）*/
    for (int i = 0; i < 5 && !s_status.valid; i++) {
        esp_task_wdt_reset();   /* 握手最长 2.5s，必须喂狗（TWDT 3s）*/
        link_request_registry();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    while (1) {
        esp_task_wdt_reset();
        if (xQueueReceive(s_uart_queue, &evt, pdMS_TO_TICKS(50))) {
            if (evt.type == UART_DATA) {
                int n = uart_read_bytes(LINK_UART, buf, sizeof(buf), 0);
#if CONFIG_TCS_SIM || CONFIG_TCS_DIAG
                if (link_muted()) { if (n > 0) s_mute_rxdrop += (uint32_t)n; n = 0; }
                #endif
                if (n > 0) s_rx_bytes += (uint32_t)n;
                for (int i = 0; i < n; i++) {
                    const uint8_t *pl; size_t plen; int dup;
                    /* R5-01：本端发出的帧被回送时 comm_rx_feed_byte 返回 0（方向闸门在 comm 层，
                     * ACK / 去重 / 交付之前），这里不需要、也不再有任何按长度或时刻的过滤。*/
                    if (comm_rx_feed_byte(&s_link, buf[i], &pl, &plen, &dup)) {
                        s_rx_frames++;
                        on_frame(pl, plen);
                    } else if (dup) {
                        /* R1 修复 M11-F-04：重复帧也回 ACK（幂等）。
                         * 原来丢弃且不回 ACK，发送端 ARQ 3 次后误判链路失败。*/
                        uint8_t ds, dm;
                        if (comm_last_dup_info(&s_link, &ds, &dm))
                            link_send_ack(ds, dm, TCS_ACK_OK, 0.0f);
                    }
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
        /* R5-01：本端回送帧只在接线/串扰异常时出现。每 20 帧告警一次，提示查接线。*/
        if (s_link.echo_cnt != last_echo) {
            last_echo = s_link.echo_cnt;
            if ((last_echo % 20u) == 1u)
                ESP_LOGW(TAG, "self-echo on RX dropped=%lu (check H743 TX wire / crosstalk)",
                         (unsigned long)last_echo);
        }
        if (!link_alive()) s_reg_done = 0;      /* 对端重启/断链 → 重新要全量 */
        if (now - last_reg_req >= 5000 && !s_reg_done) {
            last_reg_req = now;
            link_request_registry();
        }
#if CONFIG_TCS_DEMO
        /* WP-F.2 演示模式：真链路离线时 500ms 模拟步进（0x10 快照同构）*/
        static uint32_t last_demo = 0;
        if (demo_active() && now - last_demo >= 500) {
            last_demo = now;
            demo_step();
        }
#else
        /* 生产档：不做演示接管；断链由 link_alive()/快照 valid 位表达为"离线"。
         * 保留 s_demo_on 语义为 0（见上方宏），下游分支自然失效。 */
        (void)demo_active;
#endif
    }
}

void link_start(void)
{
    memset(&s_status, 0, sizeof(s_status));
    memset(s_reg, 0, sizeof(s_reg));
    registry_seed();                        /* WP-F.2/F3：内置快照先行 */
#if CONFIG_TCS_DEMO
    s_boot_us = esp_timer_get_time();       /* 演示宽限起点；生产档无此变量 */
#endif
    /* 2026-11-19：原来 on_link_down 传 NULL——ARQ 判死后没人记账，重连不重问注册表。
     * 现在断链即作废注册表收齐状态，重连后由 5s 请求/心跳自动补齐。*/
    comm_init(&s_link, link_uart_send, link_now_ms, link_on_down,
              link_crit_enter, link_crit_exit, TCS_SIDE_ESP32);

    uart_config_t cfg = {
        .baud_rate = LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, LINK_RXBUF, LINK_TXBUF, LINK_EVT_QUEUE,
                                        &s_uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, LINK_TXD, LINK_RXD,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    xTaskCreate(link_task, "tcs_link", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "started uart1 %d baud tx=%d rx=%d", LINK_BAUD, LINK_TXD, LINK_RXD);
}

/* ===================================================================== */
/* WP-F.1 真机自环自测：UART1 芯片内 TX→RX 回环，真驱动 / 真帧 / 真协议栈。
 * 在 link_start 之前调用（独占 UART1，~2.5s）；证据 = [LPBK] 日志行。
 *
 * 2026-11-25（R5-01）改为两个链路实例共用这一条回环线：A 扮演屏（H743），B 扮演主控
 * （ESP32，与真链路同身份）。线上的字节同时喂给两端，每一端只认对端发来的帧，自己发出的
 * 那一份被各自的方向闸门丢弃——这正是方向闸门在真链路要做的事。
 * 此前这里只有一个实例自问自答：第 4 项只数"交付"的份数，重传副本全被去重，恒为 1，
 * 每次开机都报 "[LPBK] ARQ retransmit FAIL"、"RESULT pass=3 fail=1 FAIL"。*/
/* ===================================================================== */
static comm_link_t s_lbk_a, s_lbk_b;

static void lbk_send_fn(const uint8_t *d, size_t n)
{
    uart_write_bytes(LINK_UART, d, n);
}

typedef struct { int deliv, dups; } lbk_rx_t;

/* 读回环线上的字节，逐字节同时喂给两端 */
static void lbk_drain(lbk_rx_t *a, lbk_rx_t *b)
{
    uint8_t buf[256];
    int n = uart_read_bytes(LINK_UART, buf, sizeof(buf), 0);
    for (int i = 0; i < n; i++) {
        const uint8_t *pl; size_t plen; int dup;
        if (comm_rx_feed_byte(&s_lbk_a, buf[i], &pl, &plen, &dup)) a->deliv++;
        else if (dup) a->dups++;
        if (comm_rx_feed_byte(&s_lbk_b, buf[i], &pl, &plen, &dup)) b->deliv++;
        else if (dup) b->dups++;
    }
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

    comm_init(&s_lbk_a, lbk_send_fn, link_now_ms, NULL, NULL, NULL, TCS_SIDE_H743);
    comm_init(&s_lbk_b, lbk_send_fn, link_now_ms, NULL, NULL, NULL, TCS_SIDE_ESP32);
    uint8_t frame[MAX_FRAME];
    uint8_t payload[MAX_PAYLOAD];
    lbk_rx_t ra = { 0, 0 }, rb = { 0, 0 };
    int pass = 0, fail = 0;

    /* 1) 帧回环：屏发 STATUS，TX→RX 原样解出 1 帧（主控端）；屏自己那一份被方向闸门丢弃 */
    tcs_status_pl_t st = { .ver = 1, .seq = 0x11223344, .tc1_act = 25.5f,
                           .tc2_act = 24.5f, .body = 36.8f, .tec1 = 0, .tec2 = 0,
                           .fan = 0, .mode = 0, .alarm = 0, .online = 3, .rfu = 0 };
    size_t pn = tcs_pl_status(payload + 2, sizeof(payload) - 2, &st);
    payload[0] = MT_TCS_STATUS; payload[1] = 0x5A;   /* 自环夹具：屏方向的帧，不带方向位 */
    size_t fn = proto_encode_frame(payload, pn + 2, frame, sizeof(frame));
    uart_write_bytes(LINK_UART, frame, fn);
    vTaskDelay(pdMS_TO_TICKS(50));
    lbk_drain(&ra, &rb);
    if (rb.deliv == 1 && ra.deliv == 0 && s_lbk_a.echo_cnt == 1) {
        pass++; ESP_LOGI(TAG, "[LPBK] frame loopback PASS (controller decoded 1 frame, screen dropped own echo)");
    } else {
        fail++; ESP_LOGE(TAG, "[LPBK] frame loopback FAIL (controller=%d screen=%d echo=%lu)",
                         rb.deliv, ra.deliv, (unsigned long)s_lbk_a.echo_cnt);
    }

    /* 2) 重复帧去重：同帧再发 → 主控端判重复，不交付 */
    uart_write_bytes(LINK_UART, frame, fn);
    vTaskDelay(pdMS_TO_TICKS(50));
    lbk_drain(&ra, &rb);
    if (rb.deliv == 1 && rb.dups >= 1) { pass++; ESP_LOGI(TAG, "[LPBK] dup dedup PASS (dup=%d)", rb.dups); }
    else { fail++; ESP_LOGE(TAG, "[LPBK] dup dedup FAIL (frames=%d dups=%d)", rb.deliv, rb.dups); }

    /* 3) 坏 CRC 丢弃：翻转 1 字节 → 两端都静默丢弃 */
    frame[fn - 1] ^= 0xFF;
    uart_write_bytes(LINK_UART, frame, fn);
    vTaskDelay(pdMS_TO_TICKS(50));
    lbk_drain(&ra, &rb);
    if (rb.deliv == 1 && ra.deliv == 0) { pass++; ESP_LOGI(TAG, "[LPBK] bad-crc drop PASS"); }
    else { fail++; ESP_LOGE(TAG, "[LPBK] bad-crc drop FAIL (controller=%d screen=%d)", rb.deliv, ra.deliv); }

    /* 4) ARQ 重传观测：主控发 0x13（屏不应答）→ 500ms 超时 ×3 重传 → 屏端收到 4 份
     *    （1 份交付 + 3 份被去重）；主控端自己发出的这 4 份一份也不得被当成屏帧。*/
    payload[0] = MT_TCS_GET_REGISTRY; payload[1] = 0xA7;
    fn = proto_encode_frame(payload, 2, frame, sizeof(frame));
    comm_send(&s_lbk_b, frame, fn, MT_TCS_GET_REGISTRY);
    lbk_rx_t ra0 = ra, rb0 = rb;
    for (int i = 0; i < 22; i++) {            /* 2.2s > 500ms×3+裕量 */
        vTaskDelay(pdMS_TO_TICKS(100));
        comm_tick(&s_lbk_b);
        comm_poll_tx(&s_lbk_b);
        lbk_drain(&ra, &rb);
    }
    int copies = (ra.deliv - ra0.deliv) + (ra.dups - ra0.dups);
    int self_got = rb.deliv - rb0.deliv;
    if (copies >= 3 && self_got == 0) {
        pass++;
        ESP_LOGI(TAG, "[LPBK] ARQ retransmit PASS (%d copies rx by screen: %d delivered + %d dedup'd)",
                 copies, ra.deliv - ra0.deliv, ra.dups - ra0.dups);
    } else {
        fail++;
        ESP_LOGE(TAG, "[LPBK] ARQ retransmit FAIL (copies=%d, controller took own frame %d times)",
                 copies, self_got);
    }

    uart_set_loop_back(LINK_UART, false);
    uart_driver_delete(LINK_UART);
    ESP_LOGI(TAG, "[LPBK] RESULT pass=%d fail=%d %s", pass, fail,
             fail ? "FAIL" : "PASS");
}
