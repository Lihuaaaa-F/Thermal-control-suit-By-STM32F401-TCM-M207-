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

/* —— UART 发送钩子（comm 注入；comm_poll_tx 任务内串行调用）—— */
static void link_uart_send(const uint8_t *d, size_t n)
{
    /* R1 P1-07：非阻塞写——TX 环满时丢弃该帧（comm 层 ARQ 会重传），
     * 不让 uart_write_bytes 阻塞把 link 任务拖死触发 TWDT。*/
    size_t free_sz = 0;
    if (uart_get_tx_buffer_free_size(LINK_UART, &free_sz) == ESP_OK && free_sz >= n) {
        uart_write_bytes(LINK_UART, d, n);
    } else {
        static uint32_t s_txdrop;
        s_txdrop++;
        if ((s_txdrop % 50u) == 1u) ESP_LOGW(TAG, "link tx ring full, dropped %lu frames", (unsigned long)s_txdrop);
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
        /* v2 上行写路径：屏幕 0x11 广播（模式/阈值等非 PF_M207 参数）必须落入
         * 本端权威注册表，否则屏幕改动丢失并被快照回灌成旧值。
         * param_set 对等值短路 → 0x12 已写过的参数不会重复下发 M207。*/
        if (s_setparam_cb) s_setparam_cb(p.id, v, payload[1]);
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
    case MT_TCS_SET_PARAM: {         /* R2 协议换向：屏（H743）写参数 → 本端权威注册表 → M207 verify */
        tcs_param_pl_t p;
        uint8_t seq = payload[1];
        if (tcs_scan_param(body, blen, &p) != 0) {
            link_send_ack(seq, mt, TCS_ACK_BAD_ID, 0.0f);
            break;
        }
        float v = (p.type == PARAM_TYPE_F32) ? tcs_b2f(p.value) : (float)p.value;
        /* 破组件环（tcs_core→tcs_link）：应用层注册处理器落权威注册表并回 ACK */
        if (s_setparam_cb) {
            s_setparam_cb(p.id, v, seq);
        } else {
            link_send_ack(seq, mt, TCS_ACK_BUSY, 0.0f);
        }
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
        break;   /* 0x90 已被 comm 层消费；0x13 仅本端发起 */
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

/* v2 补口（2026-11-16）：ESP32→H743 参数镜像。
 * 0x11 原本只有 H743→ESP32 一个方向；v2 把权威放到 ESP32 后，网页/控制台改的
 * 非设定值参数（body_hot/body_cold/hyst/tc_set_confirm/0x1xx…）没有通道回屏，
 * 屏上显示旧值，用户在屏上再按 +/- 就会用旧值覆盖网页的新值（静默数据丢失）。
 * 0x11 的载荷 tcs_param_pl_t 本来就是"参数变更"，反向复用不改格式；
 * H743 侧 param_link_sync 静默落表、不回推，故不会成环。*/
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

/* —— 任务 —— */
static void link_task(void *arg)
{
    esp_task_wdt_add(NULL);   /* R1 P1-07：关键任务纳入 TWDT（原来只有 ctrl_svc）*/
    uart_event_t evt;
    uint8_t buf[256];
    uint32_t last_tick = 0;
    uint32_t last_reg_req = 0;

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
                if (n > 0) s_rx_bytes += (uint32_t)n;
                for (int i = 0; i < n; i++) {
                    const uint8_t *pl; size_t plen; int dup;
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
              link_crit_enter, link_crit_exit);

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
