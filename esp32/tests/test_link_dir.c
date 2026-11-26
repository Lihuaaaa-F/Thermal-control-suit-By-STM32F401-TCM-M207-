/* test_link_dir.c — R5-01（P1，协议分不清方向）/ R5-10（屏写参数误杀）的 ESP32 侧主机用例。
 *
 * 被测对象是 components/tcs_link/link_task.c **本身**：原样编译，FreeRTOS / IDF 用
 * tests/host_stubs 里的桩，不抄一份逻辑出来测。字节从桩 UART 的 RX 进、帧从桩 UART 的 TX 出，
 * 中间走的就是固件里那条路：uart_read_bytes → comm_rx_feed_byte（方向闸门）→ on_frame /
 * comm_on_ack。任务函数只进入一次（与真机一样是 while(1)），用例是虚拟时钟上的一条时间线：
 * 动作按时刻挂进时钟推进点，到点执行；到终点从 esp_task_wdt_reset 跳出。
 *
 * esp32/main/main.c 不在主机编译范围，它的 esp_on_setparam / esp_on_cmd（写权威注册表 + 回 ACK）
 * 在这里由 cb_set / cb_cmd 等价复刻：计数 + 回同样格式的 ACK。
 *
 * 判别性（修复前红）：R5-01 的三条绕过（处理延迟出窗 / 回声 Len 被改 / ACK 回声清 pending）、
 * 屏方向的 0x11 被当权威写入、发送帧不带方向位，在 f2571e7 的"等长 + 发送窗"实现上都是绿的，
 * 这些用例在那份代码上必须变红（docs/results/S_G_l1_结果.md 记了修复前的输出）。
 * 修复前后的实际输出（未修代码 PASS=38 FAIL=32 → 修复后全过）记在 docs/results/S_G_l1_结果.md。
 *
 * 用法：由 esp32/tests/run_m207_tests.sh 的 linkdir 一行编译运行（MSVC cl /W4 /std:c11）。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>

/* 与 host_stubs/freertos/FreeRTOS.h 同一口径：被测文件按诊断档编译。这里先定义是为了让
 * link_task.h 里 link_loopback / link_mute 的声明在本文件也可见。*/
#ifndef CONFIG_TCS_DIAG
#define CONFIG_TCS_DIAG 1
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "link_task.h"
#include "proto.h"
#include "tcs_proto.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

#define STAT_WDIR(st)   ((st).wrongdir)
#define STAT_ARQOK(st)  ((st).arq_ok)

/* ======================= 桩：虚拟时钟 + 桩 UART + 日志捕获 ======================= */
#define HOST_LINK_TXBUF 1024          /* = link_task.c 的 LINK_TXBUF */

static jmp_buf          g_jb;
static TaskFunction_t   g_task;
static uint64_t         g_now_ms, g_end_ms;

#define RX_CAP  16384
#define TX_CAP  262144
#define LOG_CAP 262144
static uint8_t  g_rx[RX_CAP]; static size_t g_rx_n; static uint64_t g_rx_ready_ms;
static uint8_t  g_tx[TX_CAP]; static size_t g_tx_n;
static char     g_log[LOG_CAP]; static size_t g_log_n;
static int      g_loopback;
static uint64_t g_ring_empty_ms; static size_t g_ring_bytes;   /* 桩驱动 TX 环里那一帧的线时 */
static void   (*g_on_tx)(const uint8_t *frame, size_t n);

typedef void (*act_fn)(int ph);
typedef struct { uint64_t at; act_fn fn; int ph; } act_t;
static act_t g_act[512];
static int   g_nact, g_iact;

/* 一帧在线路上的时间（ms，向上取整）：230400 8N1，每字节 10 位 */
static uint64_t wire_ms(size_t n) { return (n * 125u + 2879u) / 2880u; }

static void at(uint64_t t_ms, act_fn fn, int ph)
{
    if (g_nact >= (int)(sizeof(g_act) / sizeof(g_act[0]))) { printf("FAIL: 动作表满\n"); exit(2); }
    g_act[g_nact].at = t_ms; g_act[g_nact].fn = fn; g_act[g_nact].ph = ph;
    g_nact++;
}

/* 虚拟时钟每推进一步：到点的动作按时刻先后执行（动作可以再挂新动作）*/
static void sim_pump(void)
{
    int guard = 0;
    while (g_iact < g_nact && guard++ < 100000) {
        int k = g_iact;
        for (int i = g_iact + 1; i < g_nact; i++) if (g_act[i].at < g_act[k].at) k = i;
        if (g_act[k].at > g_now_ms) break;
        act_fn fn = g_act[k].fn; int ph = g_act[k].ph;
        g_act[k] = g_act[g_iact];
        g_iact++;
        fn(ph);
    }
}

static void sim_until(uint64_t t_ms)
{
    g_end_ms = t_ms;
    if (setjmp(g_jb) == 0) g_task(NULL);     /* 任务函数只在这里进入一次 */
}

/* 把字节放进桩 UART 的 RX（= 线路上来了字节）；delay_ms = 最后一字节到达前要走的线时 */
static void rx_push(const uint8_t *d, size_t n, uint64_t delay_ms)
{
    if (g_rx_n + n > RX_CAP) { printf("FAIL: 桩 RX 溢出\n"); exit(2); }
    memcpy(g_rx + g_rx_n, d, n);
    if (g_rx_n == 0) g_rx_ready_ms = g_now_ms + delay_ms;
    g_rx_n += n;
}

/* —— FreeRTOS / IDF 桩的实现 —— */
int64_t esp_timer_get_time(void) { return (int64_t)g_now_ms * 1000; }

esp_err_t esp_task_wdt_reset(void)
{
    sim_pump();
    if (g_now_ms >= g_end_ms) longjmp(g_jb, 1);
    return ESP_OK;
}
esp_err_t esp_task_wdt_add(TaskHandle_t h) { (void)h; return ESP_OK; }

void vTaskDelay(TickType_t ticks) { g_now_ms += ticks; sim_pump(); }
void vTaskDelayUntil(TickType_t *last_wake, TickType_t inc)
{
    uint64_t wake = (uint64_t)*last_wake + inc;
    g_now_ms = (wake > g_now_ms) ? wake : g_now_ms;
    *last_wake = (TickType_t)g_now_ms;
    sim_pump();
}
TickType_t xTaskGetTickCount(void) { return (TickType_t)g_now_ms; }

static int g_task_fail;             /* P1-12 的另一半（i3）：让下一次 xTaskCreate 失败 */
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack,
                       void *arg, UBaseType_t prio, TaskHandle_t *handle)
{
    (void)name; (void)stack; (void)arg; (void)prio;
    if (g_task_fail) { g_task_fail = 0; return 0; }   /* 任务没建成（桩里没有 pdFAIL，按 FreeRTOS 的值回 0）*/
    g_task = fn;                 /* 不起线程：用例在 sim_until 里亲自驱动 */
    if (handle) *handle = (TaskHandle_t)1;
    return pdPASS;
}
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack,
                                   void *arg, UBaseType_t prio, TaskHandle_t *handle,
                                   BaseType_t core)
{
    (void)core;
    return xTaskCreate(fn, name, stack, arg, prio, handle);
}

BaseType_t xQueueReceive(QueueHandle_t q, void *buf, TickType_t timeout)
{
    uart_event_t *e = (uart_event_t *)buf;
    (void)q;
    sim_pump();
    if (g_rx_n && g_now_ms >= g_rx_ready_ms) { e->type = UART_DATA; e->size = g_rx_n; return pdTRUE; }
    uint64_t t = g_now_ms + timeout;
    if (g_rx_n && g_rx_ready_ms < t) t = g_rx_ready_ms;     /* 字节在线路上：等到它到 */
    g_now_ms = t;
    sim_pump();
    if (g_rx_n && g_now_ms >= g_rx_ready_ms) { e->type = UART_DATA; e->size = g_rx_n; return pdTRUE; }
    return pdFALSE;
}

esp_err_t uart_driver_install(uart_port_t n, int rx, int tx, int qs, QueueHandle_t *q, int f)
{
    (void)n; (void)rx; (void)tx; (void)qs; (void)f;
    if (q) *q = (QueueHandle_t)1;
    return ESP_OK;
}
esp_err_t uart_driver_delete(uart_port_t n) { (void)n; g_loopback = 0; return ESP_OK; }
esp_err_t uart_param_config(uart_port_t n, const uart_config_t *c) { (void)n; (void)c; return ESP_OK; }
esp_err_t uart_set_pin(uart_port_t n, int a, int b, int c, int d)
{
    (void)n; (void)a; (void)b; (void)c; (void)d; return ESP_OK;
}
esp_err_t uart_set_loop_back(uart_port_t n, bool on) { (void)n; g_loopback = on ? 1 : 0; return ESP_OK; }
esp_err_t uart_flush_input(uart_port_t n) { (void)n; g_rx_n = 0; return ESP_OK; }
esp_err_t uart_get_tx_buffer_free_size(uart_port_t n, size_t *size)
{
    (void)n;
    *size = (g_now_ms < g_ring_empty_ms) ? (size_t)(HOST_LINK_TXBUF - g_ring_bytes)
                                         : (size_t)HOST_LINK_TXBUF;
    return ESP_OK;
}
int uart_read_bytes(uart_port_t n, void *buf, uint32_t length, TickType_t ticks)
{
    (void)n; (void)ticks;
    size_t want = (size_t)length;
    size_t c = (g_rx_n < want) ? g_rx_n : want;
    memcpy(buf, g_rx, c);
    memmove(g_rx, g_rx + c, g_rx_n - c);
    g_rx_n -= c;
    return (int)c;
}
int uart_write_bytes(uart_port_t n, const void *src, size_t size)
{
    const uint8_t *d = (const uint8_t *)src;
    (void)n;
    if (g_tx_n + size > TX_CAP) { printf("FAIL: 桩 TX 记录溢出\n"); exit(2); }
    memcpy(g_tx + g_tx_n, d, size);
    g_tx_n += size;
    g_ring_bytes = size;
    g_ring_empty_ms = g_now_ms + wire_ms(size);
    if (g_loopback) rx_push(d, size, wire_ms(size));   /* linkloop 1：芯片内 TX 接 RX */
    if (g_on_tx) g_on_tx(d, size);
    return (int)size;
}

void host_stub_abort(const char *why) { printf("FAIL: 被测代码调用了 %s\n", why); exit(2); }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "ESP_OK(stub)"; }

/* 被测代码的日志整条落进缓冲区：用例按原文断言（如"回环期间不得出现 PARAM id="）*/
void host_log(char lvl, const char *tag, const char *fmt, ...)
{
    char buf[320];
    int n = snprintf(buf, sizeof(buf), "[%c] %s: ", lvl, tag ? tag : "?");
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
    va_end(ap);
    size_t len = strlen(buf);
    if (len < sizeof(buf) - 1) { buf[len++] = '\n'; buf[len] = '\0'; }
    if (g_log_n + len < LOG_CAP) { memcpy(g_log + g_log_n, buf, len); g_log_n += len; }
}

/* ======================= 用例自己的工具 ======================= */
static int      g_set_n, g_evt_n, g_cmd_n, g_ack_n;
static uint16_t g_set_id; static float g_set_v; static uint8_t g_set_seq, g_cmd_v;

typedef struct {
    uint32_t rx_f, dup, echo, retry, wdir, crc, arq_ok;
    int      set_n, evt_n, cmd_n, ack_n;
    size_t   log_n, tx_n;
} snap_t;
static snap_t snap(void)
{
    link_stats_t st;
    link_stats_get(&st);
    snap_t s;
    memset(&s, 0, sizeof(s));
    s.rx_f = st.rx_frames; s.dup = st.dup; s.echo = st.echo_drop; s.retry = st.retry;
    s.wdir = STAT_WDIR(st); s.crc = st.crc_err; s.arq_ok = STAT_ARQOK(st);
    s.set_n = g_set_n; s.evt_n = g_evt_n; s.cmd_n = g_cmd_n; s.ack_n = g_ack_n;
    s.log_n = g_log_n; s.tx_n = g_tx_n;
    return s;
}
static int log_count_since(size_t mark, const char *needle)
{
    int n = 0;
    const char *p = g_log + mark;
    while ((p = strstr(p, needle)) != NULL) { n++; p++; }
    return n;
}

/* 组一整帧：mt_wire = 线上消息号字节（消息号 | 方向位）*/
static size_t wire_frame(uint8_t mt_wire, uint8_t seq, const uint8_t *body, size_t blen,
                         uint8_t *out)
{
    uint8_t payload[MAX_PAYLOAD];
    payload[0] = mt_wire; payload[1] = seq;
    memcpy(&payload[2], body, blen);
    return proto_encode_frame(payload, blen + 2, out, MAX_FRAME);
}
/* 屏（H743）发来一帧：线上消息号字节不带方向位，按线时到达 */
static void peer_send(uint8_t mt, uint8_t seq, const uint8_t *body, size_t blen)
{
    uint8_t f[MAX_FRAME];
    size_t n = wire_frame(tcs_frame_mark(mt, TCS_SIDE_H743), seq, body, blen, f);
    rx_push(f, n, wire_ms(n));
}
static void param_body(uint8_t *out, uint16_t id, float v)
{
    tcs_param_pl_t p = { .id = id, .type = 0 /* PARAM_TYPE_F32 */, .value = tcs_f2b(v) };
    (void)tcs_pl_param(out, TCS_PARAM_PL_LEN, &p);
}

/* 从 TX 记录的 from 起解析下一帧（载荷 = MT+Seq+body）。没有完整帧返回 0。*/
static int tx_next(size_t *from, uint8_t *pl, size_t *plen)
{
    size_t i = *from;
    while (i + 5 <= g_tx_n) {
        if (g_tx[i] != START_BYTE0 || g_tx[i + 1] != START_BYTE1) { i++; continue; }
        size_t len = g_tx[i + 2];
        size_t total = len + 4;                             /* 起始符 2 + Len 1 + 载荷 + CRC 1 */
        if (i + total > g_tx_n) break;                       /* 还没写完 */
        if (len < 2 || proto_crc8(&g_tx[i + 2], 1 + len) != g_tx[i + 3 + len]) { i++; continue; }
        if (*plen < len) return 0;
        memcpy(pl, &g_tx[i + 3], len);
        *plen = len;
        *from = i + total;
        return 1;
    }
    *from = g_tx_n;
    return 0;
}
/* TX 记录 [from, g_tx_n) 里：消息号字节 == mt_wire（0xFF = 任意）且 seq == seq（0x100 = 任意）的帧数 */
static int tx_count(size_t from, int mt_wire, int seq)
{
    uint8_t pl[MAX_PAYLOAD]; size_t len; int n = 0;
    while (from < g_tx_n) {
        len = sizeof(pl);
        if (!tx_next(&from, pl, &len)) break;
        if (mt_wire != 0xFF && pl[0] != (uint8_t)mt_wire) continue;
        if (seq != 0x100 && pl[1] != (uint8_t)seq) continue;
        n++;
    }
    return n;
}

/* TX 记录 [from, g_tx_n) 里：去掉方向位后的消息号 == mt 的帧数（不看是谁的方向）*/
static int tx_count_mt(size_t from, uint8_t mt)
{
    uint8_t pl[MAX_PAYLOAD]; size_t len; int n = 0;
    while (from < g_tx_n) {
        len = sizeof(pl);
        if (!tx_next(&from, pl, &len)) break;
        if (tcs_frame_mt(pl[0]) == mt) n++;
    }
    return n;
}

/* —— main.c 的 esp_on_setparam / esp_on_cmd 等价物（写权威注册表 + 回 ACK）—— */
static void cb_set(uint16_t id, float v, uint8_t seq)
{
    g_set_n++; g_set_id = id; g_set_v = v; g_set_seq = seq;
    host_log('I', "TEST", "H743 SET id=0x%02x v=%.2f -> ack", (unsigned)id, (double)v);
    link_send_ack(seq, MT_TCS_SET_PARAM, TCS_ACK_OK, v);
    g_ack_n++;
}
static void cb_cmd(uint8_t cmd, uint8_t seq)
{
    g_cmd_n++; g_cmd_v = cmd;
    link_send_ack(seq, MT_TCS_CMD, TCS_ACK_OK, 0.0f);
    g_ack_n++;
}
static void cb_evt(void) { g_evt_n++; }

/* ======================= 各阶段 ======================= */
static snap_t S;                       /* 当前阶段的起点快照 */
static size_t S_tx, S_log;
static void begin(void) { S = snap(); S_tx = S.tx_n; S_log = S.log_n; }
#define DLOG(str)   log_count_since(S_log, (str))

enum { HOOK_NONE, HOOK_B, HOOK_CAPTURE, HOOK_CAP_ONLY, HOOK_CAP_LEN, HOOK_E };
static int      g_hook;
static uint64_t g_cap_ms, g_cap_echo_ms;     /* 本端帧出线时刻、它的回声到达时刻（虚拟时钟）*/
static uint8_t  g_cap[MAX_FRAME]; static size_t g_cap_n;
static uint8_t  g_e_seq; static uint8_t g_e_ack[MAX_FRAME]; static size_t g_e_ack_n;
static int      g_e_copies;
static uint64_t g_l_end;

/* Len 字段被改坏的回声：仍是 CRC 正确的合法帧，只是载荷多 1 字节 */
static void push_echo_len_plus1(const uint8_t *frame)
{
    uint8_t body[TCS_PARAM_PL_LEN + 1];
    memcpy(body, &frame[5], TCS_PARAM_PL_LEN);
    body[TCS_PARAM_PL_LEN] = 0x5Au;
    uint8_t f[MAX_FRAME];
    size_t n = wire_frame(frame[3], frame[4], body, sizeof(body), f);
    rx_push(f, n, wire_ms(n));
}

static void tx_hook(const uint8_t *f, size_t n)
{
    if (n < 5) return;
    uint8_t mt = tcs_frame_mt(f[3]);
    if (g_hook == HOOK_B && mt == MT_TCS_PARAM) {
        /* R5-10：本端镜像帧刚出线，屏的写参数帧（等长，载荷同为 9 字节）同一毫秒到达 */
        uint8_t body[TCS_PARAM_PL_LEN];
        g_hook = HOOK_NONE;
        param_body(body, 0x03, 38.0f);
        peer_send(MT_TCS_SET_PARAM, 0x21, body, sizeof(body));
    } else if ((g_hook == HOOK_CAPTURE || g_hook == HOOK_CAP_ONLY || g_hook == HOOK_CAP_LEN) &&
               mt == MT_TCS_PARAM) {
        int what = g_hook;
        g_hook = HOOK_NONE;
        memcpy(g_cap, f, n); g_cap_n = n; g_cap_ms = g_now_ms;
        g_cap_echo_ms = g_now_ms + wire_ms(n);
        if (what == HOOK_CAPTURE) rx_push(f, n, wire_ms(n));   /* 线时之后原样回来（立即回送）*/
        if (what == HOOK_CAP_LEN) push_echo_len_plus1(f);      /* 线时之后回来时 Len 已被改 */
    } else if (g_hook == HOOK_E && mt == MT_TCS_GET_REGISTRY) {
        /* (c)：造一帧"自己的 0x90 应答"，它确认的 (seq, mt) 恰好就是这帧等待确认的出站帧 */
        g_e_seq = f[4];
        link_send_ack(g_e_seq, MT_TCS_GET_REGISTRY, TCS_ACK_OK, 0.0f);
        g_ack_n++;
    } else if (g_hook == HOOK_E && mt == MT_TCS_ACK) {
        g_hook = HOOK_NONE;
        memcpy(g_e_ack, f, n); g_e_ack_n = n;
    }
}

/* ---- 上线：屏发来 STATUS，并 ACK 掉握手期连发的 5 帧 0x13（seq 0..4）---- */
static void act_linkup(int ph)
{
    (void)ph;
    tcs_status_pl_t st; memset(&st, 0, sizeof(st));
    st.ver = TCS_PROTO_VER; st.seq = 1; st.tc1_act = 25.5f; st.online = TCS_ONLINE_M207;
    uint8_t spl[TCS_STATUS_PL_LEN]; (void)tcs_pl_status(spl, sizeof(spl), &st);
    peer_send(MT_TCS_STATUS, 0x40, spl, sizeof(spl));
    for (uint8_t s = 0; s < 5; s++) {
        uint8_t a[TCS_ACK_PL_LEN];
        tcs_ack_pl_t ack = { .acked_seq = s, .acked_mt = MT_TCS_GET_REGISTRY, .status = TCS_ACK_OK, .ret = 0.0f };
        (void)tcs_pl_ack(a, sizeof(a), &ack);
        peer_send(MT_TCS_ACK, (uint8_t)(0x10 + s), a, sizeof(a));
    }
}

/* ---- B：屏写参数 0x12 紧跟本端发送之后到达，必须被接受（R5-10 时间窗误杀）---- */
static void act_b0(int ph)
{
    (void)ph;
    begin();
    g_hook = HOOK_B;
    CHECK(link_send_param(0x05, 0.2f) == 0, "B: link_send_param 入队");
}
static void act_b1(int ph)
{
    (void)ph;
    snap_t s = snap();
    printf("  [B] 屏 0x12 紧跟本端 0x11 之后到达：rx_f+%lu echo+%lu 权威写入回调+%d\n",
           (unsigned long)(s.rx_f - S.rx_f), (unsigned long)(s.echo - S.echo), s.set_n - S.set_n);
    CHECK(g_hook == HOOK_NONE, "B: 本端镜像帧已发出、屏帧已注入");
    CHECK(s.set_n - S.set_n == 1, "B: 屏的写参数被接受");
    CHECK(g_set_id == 0x03 && g_set_v > 37.99f && g_set_v < 38.01f, "B: 落下的是屏写的值");
    CHECK(tx_count_mt(S_tx, MT_TCS_ACK) == 1, "B: 回了一个 ACK");
    CHECK(s.echo - S.echo == 0, "B: 屏帧没有被当成回声");
}

/* ---- A：(a) 自己发出的参数镜像被回送。A1/A2 同一帧回送两次；A3 另一帧的回声第一次到就晚了；
 *          A4 第三帧的回声一回来 Len 就不对 ---- */
static void act_a0(int ph) { (void)ph; begin(); g_hook = HOOK_CAPTURE; CHECK(link_send_param(0x01, 40.0f) == 0, "A: 镜像入队"); }
static void act_a1(int ph)
{
    (void)ph;
    snap_t s = snap();
    printf("  [A1] 立即回送：echo+%lu 权威写入回调+%d\n", (unsigned long)(s.echo - S.echo), s.set_n - S.set_n);
    CHECK(g_cap_n == 13 && tcs_frame_mt(g_cap[3]) == MT_TCS_PARAM, "A: 抓到本端发出的镜像帧");
    CHECK(s.echo - S.echo == 1, "A1: 立即回送的镜像计入 echo");
    CHECK(s.set_n - S.set_n == 0, "A1: 没有写权威注册表");
    CHECK(s.evt_n - S.evt_n == 0, "A1: 没有触发推送");
    CHECK(DLOG("PARAM id=") == 0, "A1: 没有 PARAM 写入日志");
    g_cap_echo_ms = g_now_ms + wire_ms(g_cap_n);
    rx_push(g_cap, g_cap_n, wire_ms(g_cap_n));         /* 同一帧 300ms 后再次回送 */
}
static void act_a2(int ph)
{
    (void)ph;
    snap_t s = snap();
    printf("  [A2] 发出 %llu ms 后同一帧再次回送：echo+%lu dup+%lu 权威写入回调+%d\n",
           (unsigned long long)(g_cap_echo_ms - g_cap_ms), (unsigned long)(s.echo - S.echo),
           (unsigned long)(s.dup - S.dup), s.set_n - S.set_n);
    CHECK(s.echo - S.echo == 2, "A2: 再次回送仍按方向计入 echo");
    CHECK(s.dup - S.dup == 0, "A2: 回声不进去重（不计 dup）");
    CHECK(tx_count_mt(S_tx, MT_TCS_ACK) == 0, "A2: 回声不引发 ACK");
    CHECK(s.set_n - S.set_n == 0 && s.evt_n - S.evt_n == 0, "A2: 没有权威写入、没有推送");
    CHECK(DLOG("PARAM id=") == 0, "A2: 没有 PARAM 写入日志");
}
static void act_a3(int ph) { (void)ph; g_hook = HOOK_CAP_ONLY; CHECK(link_send_param(0x02, 41.0f) == 0, "A: 第二帧镜像入队"); }
static void act_a4(int ph)
{
    (void)ph;
    snap_t s = snap();
    CHECK(g_cap_n == 13, "A: 抓到第二帧镜像");
    CHECK(s.echo - S.echo == 2, "A3: 回声还没到，echo 不变");
    g_cap_echo_ms = g_now_ms + wire_ms(g_cap_n);
    rx_push(g_cap, g_cap_n, wire_ms(g_cap_n));         /* 这一帧的回声：线时过了 300ms 才回来 */
}
static void act_a5(int ph)
{
    (void)ph;
    snap_t s = snap();
    printf("  [A3] 回声第一次到就晚了 %llu ms：echo+%lu 权威写入回调+%d\n",
           (unsigned long long)(g_cap_echo_ms - g_cap_ms), (unsigned long)(s.echo - S.echo), s.set_n - S.set_n);
    CHECK(g_cap_echo_ms - g_cap_ms > 100u, "A3: 回送时刻远在旧实现的发送窗之外（旧窗约 5ms）");
    CHECK(s.set_n - S.set_n == 0, "A3: 晚到的回声没有写权威注册表（旧实现的绕过 1）");
    CHECK(s.evt_n - S.evt_n == 0, "A3: 没有触发推送");
    CHECK(DLOG("PARAM id=") == 0, "A3: 没有 PARAM 写入日志");
    CHECK(s.echo - S.echo == 3, "A3: 晚到的回声也计入 echo");
    g_hook = HOOK_CAP_LEN;
    CHECK(link_send_param(0x03, 42.0f) == 0, "A: 第三帧镜像入队");
}
static void act_a6(int ph)
{
    (void)ph;
    snap_t s = snap();
    printf("  [A4] 回声 Len 被改（多 1 字节，CRC 正确），发出 %llu ms 后到：echo+%lu 权威写入回调+%d\n",
           (unsigned long long)(g_cap_echo_ms - g_cap_ms), (unsigned long)(s.echo - S.echo), s.set_n - S.set_n);
    CHECK(g_hook == HOOK_NONE && g_cap_n == 13, "A: 第三帧镜像已发出、改过 Len 的回声已注入");
    CHECK(g_cap_echo_ms - g_cap_ms <= 5u, "A4: 回声是在旧实现的发送窗之内到的");
    CHECK(s.set_n - S.set_n == 0, "A4: Len 被改的回声没有写权威注册表（旧实现的绕过 2）");
    CHECK(s.evt_n - S.evt_n == 0, "A4: 没有触发推送");
    CHECK(DLOG("PARAM id=") == 0, "A4: 没有 PARAM 写入日志");
    CHECK(s.echo - S.echo == 4, "A4: 也按方向计入 echo");
}

/* ---- D：本端会发的每一种帧被回送，都不触及任何处理 ---- */
static void act_d0(int ph)
{
    (void)ph;
    begin();
    uint8_t f[MAX_FRAME]; size_t n;
    tcs_snap_pl_t z; memset(&z, 0, sizeof(z)); z.ver = TCS_PROTO_VER;
    uint8_t spl[TCS_SNAP_PL_LEN]; (void)tcs_pl_snapshot(spl, sizeof(spl), &z);
    uint8_t body[TCS_PARAM_PL_LEN]; param_body(body, 0x01, 30.0f);
    const uint8_t *info = (const uint8_t *)"ssid=x";
    /* 0x13 / 0x20 / 0x21 / 0x22 / 0x90 / 0x10（真链路上本端不发 0x10，但方向位说了就算本端）*/
    n = wire_frame(0x40u | MT_TCS_GET_REGISTRY, 0x71, NULL, 0, f);                  rx_push(f, n, 20);
    n = wire_frame(0x40u | MT_TCS_SNAPSHOT,     0x72, spl, sizeof(spl), f);         rx_push(f, n, 20);
    n = wire_frame(0x40u | MT_TCS_DELTA,        0x73, body, 2, f);                  rx_push(f, n, 20);
    n = wire_frame(0x40u | MT_TCS_INFO,         0x74, info, 6, f);                  rx_push(f, n, 20);
    n = wire_frame(0x40u | MT_TCS_ACK,          0x75, body, TCS_PARAM_PL_LEN, f);  rx_push(f, n, 20);
    n = wire_frame(0x40u | MT_TCS_STATUS,       0x76, spl, TCS_STATUS_PL_LEN, f);  rx_push(f, n, 20);
}
static void act_d1(int ph)
{
    (void)ph;
    snap_t s = snap();
    printf("  [D] 本端 6 种帧的回送：echo+%lu rx_f+%lu dup+%lu alive=%d\n",
           (unsigned long)(s.echo - S.echo), (unsigned long)(s.rx_f - S.rx_f),
           (unsigned long)(s.dup - S.dup), (int)link_alive());
    CHECK(s.echo - S.echo == 6, "D: 六种本端帧的回送全部计入 echo");
    CHECK(s.rx_f - S.rx_f == 0, "D: 没有一帧回声被当成对端帧");
    CHECK(!link_alive(), "D: 回声不刷新链路判活（含 0x10）");
    CHECK(s.dup - S.dup == 0, "D: 回声不进去重");
    CHECK(s.evt_n - S.evt_n == 0, "D: 没有触发推送");
}

/* ---- E：(c) 自己的 0x90 应答回送，(seq, mt) 与等待确认的出站帧相同 ---- */
static void act_e0(int ph) { (void)ph; begin(); g_hook = HOOK_E; link_request_registry(); }
static void act_e1(int ph)
{
    (void)ph;
    g_e_copies = tx_count(S_tx, 0xFF, g_e_seq);
    CHECK(g_e_ack_n == 13 && tcs_frame_mt(g_e_ack[3]) == MT_TCS_ACK, "E: 抓到本端发出的 ACK");
    CHECK(g_e_copies == 1, "E: 出站帧已发出一次");
    rx_push(g_e_ack, g_e_ack_n, 80);                 /* 回声晚到 80ms：远在旧实现的发送窗之外 */
}
static void act_e2(int ph)
{
    (void)ph;
    snap_t s = snap();
    int copies = tx_count(S_tx, 0xFF, g_e_seq);
    printf("  [E] ACK 回声（晚到 80ms）之后 800ms：出站帧累计发出 %d 次，echo+%lu\n",
           copies, (unsigned long)(s.echo - S.echo));
    CHECK(copies >= 2, "E: ACK 回声没有清掉等待确认状态（出站帧仍在重传）");
    CHECK(s.echo - S.echo == 1, "E: ACK 回声计入 echo");
}
static void act_e3(int ph)
{
    (void)ph;
    uint8_t a[TCS_ACK_PL_LEN];
    tcs_ack_pl_t ack = { .acked_seq = g_e_seq, .acked_mt = MT_TCS_GET_REGISTRY, .status = TCS_ACK_OK, .ret = 0.0f };
    (void)tcs_pl_ack(a, sizeof(a), &ack);
    peer_send(MT_TCS_ACK, 0x30, a, sizeof(a));       /* 对端方向的 ACK：必须照常确认 */
}
static void act_e4(int ph)
{
    (void)ph;
    int copies = tx_count(S_tx, 0xFF, g_e_seq);
    printf("  [E] 对端 ACK 到达 700ms 后：出站帧累计发出 %d 次\n", copies);
    CHECK(copies >= 2 && copies <= 4, "E: 对端方向的 ACK 照常清掉等待确认状态（此后不再重传）");
}

/* ---- F：方向表——屏方向的 0x11 不是"屏的写入"（事故路径，不靠回声）---- */
static void act_f0(int ph)
{
    (void)ph;
    begin();
    uint8_t body[TCS_PARAM_PL_LEN];
    param_body(body, 0x01, 5.0f);                    /* 2026-11-23 现场被写进去的那个值 */
    peer_send(MT_TCS_PARAM, 0x31, body, sizeof(body));
}
static void act_f1(int ph)
{
    (void)ph;
    snap_t s = snap();
    printf("  [F] 屏方向的 0x11（tc1_set=5.0）：权威写入回调+%d wdir+%lu rx_f+%lu\n",
           s.set_n - S.set_n, (unsigned long)(s.wdir - S.wdir), (unsigned long)(s.rx_f - S.rx_f));
    CHECK(s.set_n - S.set_n == 0, "F: 屏方向的 0x11 不得写权威注册表");
    CHECK(DLOG("PARAM id=") == 0, "F: 没有 PARAM 写入日志");
    CHECK(s.evt_n - S.evt_n == 0, "F: 没有触发推送");
    CHECK(s.rx_f - S.rx_f == 0, "F: 没有被当成对端帧交付");
    CHECK(s.wdir - S.wdir == 1, "F: 按「方向表不允许」丢弃并计数");
    CHECK(s.echo - S.echo == 0, "F: 它不是回声，不进 echo");
}

/* ---- G：ACK 确认了一个本端从不会发的消息号（本端发出的 ACK 回声即使方向位被改坏，
 *          它确认的也是对端发来的消息号，过不了方向表）---- */
static void act_g0(int ph)
{
    (void)ph;
    begin();
    uint8_t a[TCS_ACK_PL_LEN];
    tcs_ack_pl_t ack = { .acked_seq = g_e_seq, .acked_mt = MT_TCS_SET_PARAM, .status = TCS_ACK_OK, .ret = 0.0f };
    (void)tcs_pl_ack(a, sizeof(a), &ack);
    peer_send(MT_TCS_ACK, 0x32, a, sizeof(a));       /* 「屏」确认收到了本端的 0x12——本端不发 0x12 */
}
static void act_g1(int ph)
{
    (void)ph;
    snap_t s = snap();
    printf("  [G] ACK 确认了本端不会发的 0x12：wdir+%lu echo+%lu rx_f+%lu\n",
           (unsigned long)(s.wdir - S.wdir), (unsigned long)(s.echo - S.echo), (unsigned long)(s.rx_f - S.rx_f));
    CHECK(s.rx_f - S.rx_f == 0, "G: 没有交付");
    CHECK(s.evt_n - S.evt_n == 0, "G: 没有触发推送");
    CHECK(s.wdir - S.wdir == 1, "G: 按「方向表不允许」丢弃并计数");
    CHECK(s.echo - S.echo == 0, "G: 不进 echo");
}

/* ---- H：(e) 对端方向来的每一种帧照常处理 ---- */
static void act_h0(int ph)
{
    (void)ph;
    begin();
    tcs_status_pl_t st; memset(&st, 0, sizeof(st));
    st.ver = TCS_PROTO_VER; st.seq = 2; st.tc1_act = 25.5f; st.online = TCS_ONLINE_M207;
    uint8_t spl[TCS_STATUS_PL_LEN]; (void)tcs_pl_status(spl, sizeof(spl), &st);
    peer_send(MT_TCS_STATUS, 0x41, spl, sizeof(spl));

    tcs_reg_entry_t e; memset(&e, 0, sizeof(e));
    e.count = 51; e.idx = 20; e.id = 0x77; e.type = 0; e.min = 0; e.max = 10; e.step = 1;
    strcpy(e.key, "t_link_dir"); strcpy(e.name, "link dir test"); strcpy(e.unit, "");
    uint8_t rp[200]; size_t rn = tcs_pl_registry_entry(rp, sizeof(rp), &e);
    peer_send(MT_TCS_REGISTRY_ENTRY, 0x42, rp, rn);

    uint8_t cmd = TCS_CMD_ESTOP;
    peer_send(MT_TCS_CMD, 0x43, &cmd, 1);
}
static void act_h1(int ph)
{
    (void)ph;
    snap_t s = snap();
    link_status_t ls; link_reg_t reg[LINK_REGISTRY_MAX];
    int found = 0, n = link_get_registry(reg, LINK_REGISTRY_MAX);
    for (int i = 0; i < n; i++) if (reg[i].e.id == 0x77 && strcmp(reg[i].e.key, "t_link_dir") == 0) found = 1;
    printf("  [H] 屏的 0x10/0x14/0x15：rx_f+%lu 命令回调+%d alive=%d 注册表条目=%d\n",
           (unsigned long)(s.rx_f - S.rx_f), s.cmd_n - S.cmd_n, (int)link_alive(), found);
    CHECK(link_alive(), "H: 0x10 判活");
    link_status_copy(&ls);
    CHECK(ls.valid && ls.seq == 2 && ls.tc1_act > 25.49f && ls.tc1_act < 25.51f, "H: 0x10 落表");
    CHECK(found, "H: 0x14 注册表条目落表");
    CHECK(s.cmd_n - S.cmd_n == 1 && g_cmd_v == TCS_CMD_ESTOP, "H: 0x15 急停命令到了执行回调");
    CHECK(tx_count_mt(S_tx, MT_TCS_ACK) == 1, "H: 0x15 回了 ACK");
    CHECK(s.rx_f - S.rx_f == 3, "H: 三帧全部交付");
}

/* ---- I：本端发出的每一帧都带本端方向位，消息号是方向表允许本端发的 ---- */
static void act_i0(int ph)
{
    (void)ph;
    begin();
    tcs_snap_pl_t z; memset(&z, 0, sizeof(z)); z.ver = TCS_PROTO_VER;
    uint8_t spl[TCS_SNAP_PL_LEN]; (void)tcs_pl_snapshot(spl, sizeof(spl), &z);
    tcs_delta_src_t d; memset(&d, 0, sizeof(d)); d.fan = 1;
    uint8_t dp[24]; size_t dn = tcs_pl_delta(dp, sizeof(dp), TCS_D_FAN, &d);
    CHECK(link_send_frame_mt(MT_TCS_SNAPSHOT, spl, sizeof(spl)) == 0, "I: 快照入队");
    CHECK(link_send_frame_mt(MT_TCS_DELTA, dp, dn) == 0, "I: 增量入队");
    CHECK(link_send_info((const uint8_t *)"fw=1", 4) == 0, "I: 设备信息入队");
}
static void act_i1(int ph)
{
    (void)ph;
    snap_t s = snap();
    int bad = 0, n = 0, i0 = 0;
    uint8_t pl[MAX_PAYLOAD]; size_t len;
    while ((size_t)i0 < s.tx_n) {
        size_t from = (size_t)i0;
        len = sizeof(pl);
        if (!tx_next(&from, pl, &len)) break;
        i0 = (int)from;
        n++;
        uint8_t mt = tcs_frame_mt(pl[0]);
        if (tcs_frame_sender(pl[0]) != TCS_SIDE_ESP32) bad++;
        if (mt != MT_TCS_PARAM && mt != MT_TCS_GET_REGISTRY && mt != MT_TCS_ACK &&
            mt != MT_TCS_SNAPSHOT && mt != MT_TCS_DELTA && mt != MT_TCS_INFO) bad++;
    }
    printf("  [I] 本端累计发出 %d 帧，其中不带方向位 / 方向表不允许本端发的：%d\n", n, bad);
    CHECK(tx_count(S_tx, (int)(0x40u | MT_TCS_SNAPSHOT), 0x100) == 1, "I: 0x20 带方向位");
    CHECK(tx_count(S_tx, (int)(0x40u | MT_TCS_DELTA), 0x100) == 1, "I: 0x21 带方向位");
    CHECK(tx_count(S_tx, (int)(0x40u | MT_TCS_INFO), 0x100) == 1, "I: 0x22 带方向位");
    CHECK(n > 10 && bad == 0, "I: 每一帧都带方向位，消息号都在 ESP32 一侧的方向表里");
}

/* ---- L：linkloop 1 的芯片内回环 20 s（真机验收的主机预演；节奏与 tcs_snap.c 相同）---- */
static void act_l_tick(int ph);
static void act_l0(int ph)
{
    (void)ph;
    begin();
    g_l_end = g_now_ms + 20000u;
    CHECK(link_loopback(1) == ESP_OK, "L: 打开芯片内回环");
    at(g_now_ms, act_l_tick, ph);
}
static void act_l_tick(int ph)
{
    tcs_snap_pl_t z; memset(&z, 0, sizeof(z)); z.ver = TCS_PROTO_VER; z.seq = (uint32_t)g_now_ms;
    uint8_t spl[TCS_SNAP_PL_LEN]; (void)tcs_pl_snapshot(spl, sizeof(spl), &z);
    (void)link_send_frame_mt(MT_TCS_SNAPSHOT, spl, sizeof(spl));           /* 0x20 每 200ms */
    if ((g_now_ms / 200u) % 4u == 0u) (void)link_send_param(0x01, 25.0f);   /* 镜像每 800ms */
    if ((g_now_ms % 3200u) < 200u) (void)link_send_info((const uint8_t *)"ssid=tcs", 8);   /* 0x22 每 3.2s */
    if (g_now_ms + 200u <= g_l_end) at(g_now_ms + 200u, act_l_tick, ph);
}
static void act_l1(int ph)
{
    (void)ph;
    snap_t s = snap();
    uint8_t pl[MAX_PAYLOAD]; size_t len; int sent = 0, by[8] = {0};
    size_t i = S_tx;
    while (i < s.tx_n) {
        len = sizeof(pl);
        if (!tx_next(&i, pl, &len)) break;
        sent++;
        if (pl[0] == (0x40u | MT_TCS_SNAPSHOT))     by[0]++;
        else if (pl[0] == (0x40u | MT_TCS_DELTA))   by[1]++;
        else if (pl[0] == (0x40u | MT_TCS_PARAM))   by[2]++;
        else if (pl[0] == (0x40u | MT_TCS_INFO))    by[3]++;
        else if (pl[0] == (0x40u | MT_TCS_GET_REGISTRY)) by[4]++;
        else if (pl[0] == (0x40u | MT_TCS_ACK))     by[5]++;
    }
    CHECK(link_loopback(0) == ESP_OK, "L: 关闭回环");
    if (getenv("TCS_LINKDIR_DEBUG")) {        /* 排查用：逐帧打印回环期间发出的（消息号字节，载荷长度）*/
        size_t j = S_tx; int k = 0;
        while (j < s.tx_n) {
            len = sizeof(pl);
            if (!tx_next(&j, pl, &len)) break;
            printf("    tx#%d mt=%02x len=%u\n", ++k, (unsigned)pl[0], (unsigned)len);
        }
    }
    printf("  [L] 回环 20s（t=%llu，TX %lu 字节）：本端发出 %d 帧（20=%d 21=%d 11=%d 22=%d 13=%d 90=%d）"
           "，echo+%lu rx_f+%lu dup+%lu retry+%lu 权威写入回调+%d arq_ok=%u\n",
           (unsigned long long)g_now_ms, (unsigned long)(s.tx_n - S_tx),
           sent, by[0], by[1], by[2], by[3], by[4], by[5],
           (unsigned long)(s.echo - S.echo), (unsigned long)(s.rx_f - S.rx_f),
           (unsigned long)(s.dup - S.dup), (unsigned long)(s.retry - S.retry),
           s.set_n - S.set_n, (unsigned)s.arq_ok);
    CHECK(sent > 120, "L: 回环期间本端在持续发帧");
    CHECK(s.echo - S.echo == (uint32_t)sent, "L: 每一帧都只让 echo 增长（echo 增量 == 发出帧数）");
    CHECK(s.rx_f - S.rx_f == 0, "L: 没有回声被当成对端帧");
    CHECK(s.dup - S.dup == 0, "L: dup 不增长");
    CHECK(DLOG("PARAM id=") == 0, "L: 没有 PARAM 写入日志");
    CHECK(s.set_n - S.set_n == 0 && s.evt_n - S.evt_n == 0, "L: 没有权威写入、没有推送");
    CHECK(!link_alive(), "L: 回声不判活");
    CHECK(s.arq_ok == 0, "L: ACK 回声不会把 link_ok 置真");
}

/* ---- M：M04-F-06 判活单一口径——ARQ 判死之后，对端的任何一帧合法帧都把 link_ok 置回真 ---- */
static void act_m0(int ph)
{
    (void)ph;
    tcs_status_pl_t st; memset(&st, 0, sizeof(st));
    st.ver = TCS_PROTO_VER; st.seq = 3;
    uint8_t spl[TCS_STATUS_PL_LEN]; (void)tcs_pl_status(spl, sizeof(spl), &st);
    peer_send(MT_TCS_STATUS, 0x50, spl, sizeof(spl));     /* 对端的 STATUS：免 ARQ，没有对应的 ACK */
}
static void act_m1(int ph)
{
    (void)ph;
    snap_t s = snap();
    printf("  [M] ARQ 判死之后对端 STATUS 到达：arq_ok=%u alive=%d\n", (unsigned)s.arq_ok, (int)link_alive());
    CHECK(link_alive(), "M: 对端 STATUS 判活");
    CHECK(s.arq_ok == 1, "M: 对端的非 ACK 帧把 link_ok 置回真（旧实现只能被 ACK 复位）");
}

/* ======================= main ======================= */
static int cmp_act(const void *a, const void *b)
{
    uint64_t x = ((const act_t *)a)->at, y = ((const act_t *)b)->at;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

int main(void)
{
    /* 开机自环自测（app_main 在 link_start 之前调它）：这里先看它在方向位改动之后的结果。*/
    size_t log0 = g_log_n;
    link_loopback_run();
    printf("[LPBK] 开机自环自测在主机上的结果：\n%s", g_log + log0);
    g_now_ms = 0; g_tx_n = 0; g_rx_n = 0;           /* 时间线从任务启动算起 */

    link_start();
    link_on_setparam(cb_set);
    link_on_cmd(cb_cmd);
    link_on_event(cb_evt);
    g_on_tx = tx_hook;

    /* 握手 0–2500ms：任务连发 5 帧 0x13（真机没有屏时就是这样）。3000ms 起屏上线。*/
    at(3000, act_linkup, 0);

    at(3600,  act_b0, 0);  at(4000,  act_b1, 0);
    at(4400,  act_a0, 0);  at(4700,  act_a1, 0);  at(4900, act_a2, 0);
    at(5200,  act_a3, 0);  at(5500,  act_a4, 0);  at(5700, act_a5, 0);
    at(6000,  act_a6, 0);
    at(6600,  act_d0, 0);  at(7100,  act_d1, 0);
    at(7400,  act_e0, 0);  at(7800,  act_e1, 0);  at(8600, act_e2, 0);
    at(8700,  act_e3, 0);  at(9400,  act_e4, 0);
    at(9700,  act_f0, 0);  at(10000, act_f1, 0);
    at(10400, act_g0, 0);  at(10700, act_g1, 0);
    at(11100, act_h0, 0);  at(11400, act_h1, 0);
    at(11800, act_i0, 0);  at(12300, act_i1, 0);
    at(12600, act_l0, 0);  at(32600, act_l1, 0);
    at(32900, act_m0, 0);  at(33200, act_m1, 0);

    qsort(g_act, (size_t)g_nact, sizeof(g_act[0]), cmp_act);
    sim_until(33400);

    /* P1-12 的另一半（S 线 i3）：link_start 把任务创建的结果带出来。原来是 void 接口，
     * 启动汇总只能把它记成"未核实"；任务没起来时屏链整个不存在，汇总里却看不出来。*/
    CHECK(link_start() == ESP_OK, "P1-12: link_start reports success when the task is created");
    g_task_fail = 1;
    CHECK(link_start() == ESP_ERR_NO_MEM, "P1-12: link_start reports a task that could not be created");

    printf("test_link_dir: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
