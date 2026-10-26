/**
 * m207_service.c — M207 串口服务实现（ESP32-S3 主控侧；R1b，任务书 v11.3）
 *
 * 架构（第八章预算）：
 *   - 单任务 m207_svc 钉核 1、50ms 节流拍（vTaskDelayUntil）；TX 仲裁每拍至多 1 帧
 *     （重发 > 队列帧 > 轮询帧）→ 50ms 铁律结构性满足（正典 §4）。
 *   - RX=IDF 驱动 RX 环形缓冲（1024B，ISR 自动搬运）+ 事件队列；每拍无阻塞清空
 *     （57600 满速率 288B/拍 ≪ 环形容量）。TX=驱动 TX 环（256B），写不阻塞拍内
 *     → 拍 WCET ≤1ms 预算不靠阻塞发送。
 *   - 写路径：设 → CMD:REPLY=1/8 → 等≥50ms（下一拍出队）→ 查 → 回显比对
 *     （容差 0.5×10^-decimals）→ 不一致/超时重发 1 次（重试预算=1/事务）→ NAK 记录
 *     （正典 §2/§4）。
 *   - 轮询：500ms 交替 TC1/TC2 TCACTTEMP?；应答窗 100ms；5 连败离线
 *     （F5：ALM_M207 置位+停收新写+清队列/挂起；SAFE 输出由 control R1e 裁决）；
 *     恢复：1s 探测，复联后重同步（TCSW?/TCADJTEMP?+按需 TCSW=0，E-08 预研）。
 *   - H6.7 合理性滤波：|Δ|>10°C/s 判不可信→按失败计数（post_pure.c 纯函数）。
 *   - 自测：m207_lb_boottest=[M207LB]（内部回环全套+物理短接探测）；
 *     CONFIG_TCS_M207_SELFLOOP 构建附带应答器注入全路径取证=[M207SL]。
 */
#include "m207_service.h"
#include "m207_proto.h"
#include "param_registry.h"
#include "control.h"
#include "post.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "M207";

/* ====== 硬件与节律常量（接线表 v2.0 / 第八章 / 正典）====== */
#define M207_UART        UART_NUM_2
#define M207_TXD_GPIO    21
#define M207_RXD_GPIO    47
#define M207_BAUD        57600
#define M207_RXBUF       1024
#define M207_TXBUF       256
#define M207_EVTQ_LEN    20

#define M207_TICK_MS     50u     /* 队列服务节拍 = 铁律间隔 */
#define M207_POLL_MS     500u    /* 轮询交替周期 */
#define M207_REPLY_TICKS 2u      /* 应答窗 100ms（2 拍）*/
#define M207_FAIL_LIMIT  5       /* 5 连败离线（F5）*/
#define M207_PROBE_TICKS 20u     /* 离线探测 1s */
#define M207_Q_LEN       16
#define M207_CMD_MAX     40
#define M207_PEND_MAX    4       /* 在途写确认事务（FIFO）*/
#define M207_TASK_PRIO   5
#define M207_TASK_CORE   1    /* 第八章：控制组钉核 1 */
#define M207_TASK_STACK  4096
#define M207_PERF_WIN    200u    /* perf 汇报窗 = 200 拍 = 10s */

/* ====== 队列消息（生产者：任意任务 API/内部；消费者：服务任务）====== */
typedef struct {
    uint8_t  verify;                 /* 1=需写后读回确认 */
    uint8_t  ch;                     /* 1/2 */
    uint8_t  decimals;
    float    value;                  /* 期望值（确认比对）*/
    char     cmd[M207_CMD_MAX];      /* 下行帧（含 \r）*/
    char     qry[M207_CMD_MAX];      /* 确认查询帧 */
    char     param[16];              /* 确认参数名 */
} m207_msg_t;

/* ====== 在途写确认事务（FIFO，串行化：模块应答与下行同序）====== */
typedef struct {
    uint8_t  in_use, phase;          /* phase: 1=等 CMD:REPLY 2=等查询回显 */
    uint8_t  ch, decimals, retries;
    uint8_t  resend_due;             /* 本拍仲裁须重发（set_cmd/qry_cmd 按 phase）*/
    float    value;
    char     param[16];
    char     set_cmd[M207_CMD_MAX];
    char     qry_cmd[M207_CMD_MAX];
    uint32_t deadline;               /* 绝对拍号 */
} m207_pend_t;

/* ====== 状态 ====== */
static QueueHandle_t s_q;            /* 下行命令队列 */
static QueueHandle_t s_evt_q;        /* UART 事件队列 */
static uint8_t  s_started;
static uint8_t  s_online;
static uint8_t  s_probing;           /* 0=常规轮询 FSM（含上电首联）1=离线探测 */
static uint8_t  s_ever_online;
static uint8_t  s_fail;
static uint8_t  s_waiting;           /* 1=轮询已发在等应答 */
static uint8_t  s_poll_ch = 1;       /* 1↔2 交替 */
static uint32_t s_tick;              /* 50ms 拍号 */
static uint32_t s_next_poll;         /* 下一轮询拍号 */
static uint32_t s_next_probe;        /* 离线探测拍号 */
static m207_lineasm_t s_asm;
static m207_pend_t s_pend[M207_PEND_MAX];

static float    s_temp[3];           /* [1]=tc1 [2]=tc2 TCACTTEMP */
static uint32_t s_temp_tick[3];
static uint8_t  s_have_temp[3];

static struct {
    uint16_t nak, verify_ok, resend, offline, wdrop, qdrop, unexpected;
    uint32_t tx, rx_lines, parse_err;
    uint32_t tick_max_us, fsm_max_us;    /* perf 窗口内最大值 */
} s_stat;

#if CONFIG_TCS_M207_SELFLOOP
/* ====== 应答器注入（仅取证构建；拦截服务下行帧，合成应答延迟到下一拍写出。
 * 真实模块应答必有处理延迟，0µs 贴写属非物理行为——且 hex 取证表明贴写 junction
 * 会绞掉自家回显的 \r（应答数据本身完好）；延迟应答即判别实验，也更贴近真机。）*/
static struct {
    uint8_t silent;        /* 1=应答静默（拔线仿真）*/
    uint8_t wrong_echo;    /* 0=关 1=下一次查询回显错值 2=恒错值 */
    uint8_t tcsw[3];
    float   set1, act1;
    char    pend_line[48]; /* 延迟到下一拍写出的应答行 */
    uint8_t pend_len;
} s_rsp;
static void m207_rsp_on_tx(const char *cmd);
static void m207_rsp_flush(void);      /* 服务任务每拍开头调用 */
void m207_rsp_fault(int on) { s_rsp.silent = on ? 1u : 0u; }
void m207_rsp_wrong(int mode) { s_rsp.wrong_echo = (uint8_t)mode; }
#endif

static void m207_tx_now(const char *cmd, size_t len)
{
    uart_write_bytes(M207_UART, cmd, len);
    s_stat.tx++;
#if CONFIG_TCS_M207_SELFLOOP
    m207_rsp_on_tx(cmd);
#endif
}

static int m207_enqueue(const m207_msg_t *msg)
{
    if (!s_started || !s_q) return -1;
    if (xQueueSend(s_q, msg, 0) != pdTRUE) return -1;
    return 0;
}

static void m207_enqueue_simple(const char *cmd)
{
    m207_msg_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.cmd, cmd, sizeof(m.cmd) - 1);
    (void)m207_enqueue(&m);
}

/* ====== 在途确认事务 ====== */
static m207_pend_t *pend_alloc(void)
{
    for (int i = 0; i < M207_PEND_MAX; i++)
        if (!s_pend[i].in_use) return &s_pend[i];
    return NULL;
}

static m207_pend_t *pend_find(uint8_t phase, uint8_t ch, const char *param)
{
    for (int i = 0; i < M207_PEND_MAX; i++) {
        m207_pend_t *p = &s_pend[i];
        if (p->in_use && p->phase == phase && p->ch == ch &&
            strcmp(p->param, param) == 0) return p;
    }
    return NULL;
}

static m207_pend_t *pend_find_any_phase1(void)
{
    for (int i = 0; i < M207_PEND_MAX; i++)
        if (s_pend[i].in_use && s_pend[i].phase == 1) return &s_pend[i];
    return NULL;
}

static void pend_release(m207_pend_t *p) { memset(p, 0, sizeof(*p)); }

static float pend_tol(uint8_t decimals)
{
    float t = 0.5f;
    while (decimals--) t *= 0.1f;
    return t + 1e-4f;                    /* 0.5×10^-dec + 浮点余量 */
}

static void pend_nak(m207_pend_t *p, const char *why, int code)
{
    s_stat.nak++;
    ESP_LOGW(TAG, "set NAK ch=%u %s set=%.3f (%s code=%d) nak_total=%u",
             (unsigned)p->ch, p->param, (double)p->value, why, code,
             (unsigned)s_stat.nak);
    pend_release(p);
}

/* 重试预算=1/事务：标记本拍仲裁重发（不直接 TX——保 50ms 铁律单帧/拍）*/
static void pend_mark_resend(m207_pend_t *p, uint8_t phase, const char *why)
{
    if (p->retries) { pend_nak(p, why, 0); return; }
    p->retries = 1;
    p->phase = phase;                    /* 1=重发设定 2=重发查询 */
    p->resend_due = 1;
    s_stat.resend++;
    ESP_LOGW(TAG, "%s ch=%u %s → resend #1", why, (unsigned)p->ch, p->param);
}

/* ====== 在线/离线沿 ====== */
static void m207_go_online(const char *why)
{
    s_fail = 0;
    s_probing = 0;
    if (s_online) return;
    s_online = 1;
    g_tcs.m207_online = 1;
    ESP_LOGI(TAG, "online (%s)", why);
    /* 复联重同步（E-08 预研）：状态查询 + SAFE 语义保持 */
    m207_enqueue_simple("TC1:TCSW?\r");
    m207_enqueue_simple("TC2:TCSW?\r");
    m207_enqueue_simple("TC1:TCADJTEMP?\r");
    m207_enqueue_simple("TC2:TCADJTEMP?\r");
    if (!g_tcs.tec1_on) m207_enqueue_simple("TC1:TCSW=0\r");
    if (!g_tcs.tec2_on) m207_enqueue_simple("TC2:TCSW=0\r");
}

static void m207_go_offline(void)
{
    s_online = 0;
    g_tcs.m207_online = 0;
    s_probing = 1;                       /* 转入 1s 探测（一次性沿；报警粘滞）*/
    s_stat.offline++;
    s_waiting = 0;
    tcs_alarm_raise(ALM_M207);           /* F5：粘滞至人工 ack（tcs_alarm_ack）*/
    ESP_LOGW(TAG, "OFFLINE (%u fails) → ALM_M207, writes halted, SAFE by control",
             (unsigned)s_fail);
    s_fail = 0;
    /* 清队列 + 失败全部在途事务 */
    m207_msg_t m;
    while (xQueueReceive(s_q, &m, 0) == pdTRUE) s_stat.qdrop++;
    for (int i = 0; i < M207_PEND_MAX; i++)
        if (s_pend[i].in_use) pend_nak(&s_pend[i], "offline", 0);
    s_next_probe = s_tick + M207_PROBE_TICKS;
}

/* ====== 应答行处理 ====== */
static void m207_handle_line(const char *line)
{
    m207_reply_t r;
    if (!m207_parse_line(line, &r)) {
        s_stat.parse_err++;
        ESP_LOGI(TAG, "parse-err line: '%s'", line);   /* 异常路径，非逐拍 */
        return;
    }
    s_stat.rx_lines++;

    if (r.kind == M207_KIND_ERRCODE) {
        m207_pend_t *p = pend_find_any_phase1();
        ESP_LOGD(TAG, "[RXE] errcode=%u pend=%s", (unsigned)r.errcode,
                 p ? (p->phase == 1 ? "phase1" : "phase2") : "none"); /* 拍内不做控制台 IO */
        if (p) {
            if (r.errcode == 1 || r.errcode == 8) {
                p->phase = 2;            /* 设定已受理 */
                p->resend_due = 1;       /* 下一拍仲裁发确认查询（≥50ms 铁律）*/
                p->deadline = s_tick + M207_REPLY_TICKS;
            } else {
                pend_nak(p, "module rejected", (int)r.errcode);  /* 协议拒绝，重发无益 */
            }
        } else if (s_waiting && r.errcode != 1 && r.errcode != 8) {
            s_fail++;                    /* 轮询得到错误应答 = 失败 */
            s_waiting = 0;
        } else {
            s_stat.unexpected++;         /* 无事务 ACK（如上电 TCSW=0）*/
        }
        return;
    }
    if (r.module == M207_MOD_NONE) return;

    /* 写路径确认：phase2 事务的查询回显。只认 '=' 应答（KIND_REPLY）——
     * 回环下自家查询行（'?'，KIND_POLL，value=0）不得参与比对。 */
    m207_pend_t *p = (r.kind == M207_KIND_REPLY) ?
                     pend_find(2, r.module, r.param) : NULL;
    if (p) {
        float diff = r.value - p->value;
        if (diff < 0) diff = -diff;
        if (diff <= pend_tol(p->decimals)) {
            s_stat.verify_ok++;
            ESP_LOGI(TAG, "verify OK ch=%u %s=%.3f (want %.3f)",
                     (unsigned)r.module, r.param, (double)r.value, (double)p->value);
            pend_release(p);
        } else {
            pend_mark_resend(p, 1, "echo mismatch");
        }
    }

    /* 缓存更新 */
    if (r.kind == M207_KIND_REPLY && strcmp(r.param, "TCACTTEMP") == 0) {
        if (s_have_temp[r.module]) {
            float dt = (float)(s_tick - s_temp_tick[r.module]) *
                       (M207_TICK_MS / 1000.0f);
            if (!post_m207_sanity_ok(s_temp[r.module], r.value, dt)) {
                ESP_LOGW(TAG, "implausible ch=%u %.1f→%.1f (dt=%.2fs) → count fail",
                         (unsigned)r.module, (double)s_temp[r.module],
                         (double)r.value, (double)dt);
                s_fail++;
                s_waiting = 0;
                return;                  /* 不采纳（H6.7）*/
            }
        }
        s_temp[r.module] = r.value;
        s_temp_tick[r.module] = s_tick;
        s_have_temp[r.module] = 1;
        if (r.module == 1) g_tcs.tc1_act = r.value;
        else               g_tcs.tc2_act = r.value;
        m207_go_online(s_ever_online ? "recover" : "first");
        s_ever_online = 1;
        s_waiting = 0;
    } else if (r.kind == M207_KIND_REPLY && strcmp(r.param, "TCSW") == 0) {
        if (r.module == 1) g_tcs.tec1_on = (r.value != 0.0f);
        else               g_tcs.tec2_on = (r.value != 0.0f);
    } else if (r.kind == M207_KIND_REPLY && strcmp(r.param, "TCADJTEMP") == 0) {
        ESP_LOGI(TAG, "module TCADJTEMP ch=%u = %.2f (cache mirror at R5)",
                 (unsigned)r.module, (double)r.value);
    } else if (r.kind == M207_KIND_REPLY) {
        /* 非缓存参数的应答（TCERRORSTATUS/TCOTPSTATUS/SYSMODEL 类）——
         * 原样留痕（R5 字典镜像接入前的通用观测口）*/
        ESP_LOGI(TAG, "reply ch=%u %s=%.3f", (unsigned)r.module, r.param, (double)r.value);
    }
}

/* ====== RX 清空（事件驱动 + 拍内兜底）====== */
static void m207_rx_drain(void)
{
    uint8_t buf[256];
    char line[M207_LINE_MAX + 1];
    for (;;) {
        int n = uart_read_bytes(M207_UART, buf, sizeof(buf), 0);
        if (n <= 0) break;
#if CONFIG_TCS_M207_RAWLOG
        {
            char hx[3 * 40 + 8];
            int o = 0;
            for (int i = 0; i < n && i < 40; i++)
                o += snprintf(hx + o, sizeof(hx) - o, "%02X ", buf[i]);
            ESP_LOGI("M207R", "rx %dB: %s", n, hx);
        }
#endif
        for (int i = 0; i < n; i++)
            if (m207_lineasm_feed(&s_asm, buf[i], line, sizeof(line)))
                m207_handle_line(line);
    }
}

/* ====== 轮询/恢复状态机（返回本拍要发的轮询帧或 NULL）======
 * 上电=常规轮询 FSM（H743 同语义）：500ms 交替、应答窗超时计败、5 连败→
 * go_offline（ALM_M207 一次性沿）→转 1s 探测；探测成功→go_online("recover")。 */
static const char *m207_fsm_poll(void)
{
    static char cmd[24];
    if (!s_probing) {
        if (s_waiting) {
            if (s_tick >= s_next_poll + M207_REPLY_TICKS) {
                s_fail++;                /* 应答窗超时 */
                s_waiting = 0;
                if (s_fail >= M207_FAIL_LIMIT) { m207_go_offline(); return NULL; }
            }
            return NULL;
        }
        if (s_tick >= s_next_poll) {
            s_next_poll = s_tick + M207_POLL_MS / M207_TICK_MS;
            snprintf(cmd, sizeof(cmd), "TC%u:TCACTTEMP?\r", (unsigned)s_poll_ch);
            s_poll_ch = (s_poll_ch == 1) ? 2u : 1u;
            s_waiting = 1;
            return cmd;
        }
    } else {
        /* 离线/恢复：1s 探测 TC1（失败不再计数——报警沿已锁定，恢复由成功沿驱动）*/
        if (s_tick >= s_next_probe) {
            s_next_probe = s_tick + M207_PROBE_TICKS;
            snprintf(cmd, sizeof(cmd), "TC1:TCACTTEMP?\r");
            return cmd;
        }
    }
    return NULL;
}

static const char *m207_fsm_poll_timed(void)
{
    int32_t t0 = (int32_t)esp_timer_get_time();
    const char *r = m207_fsm_poll();
    int32_t dt = (int32_t)esp_timer_get_time() - t0;
    if (dt > 0 && (uint32_t)dt > s_stat.fsm_max_us) s_stat.fsm_max_us = (uint32_t)dt;
    return r;
}

/* ====== 服务任务（钉核 1；50ms 节拍；事件驱动 RX）====== */
static void m207_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    uart_event_t ev;
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(M207_TICK_MS));
        s_tick++;
#if CONFIG_TCS_M207_SELFLOOP
        m207_rsp_flush();                /* 延迟应答：上一拍拦截的应答本拍写出 */
#endif
        int32_t t0 = (int32_t)esp_timer_get_time();

        /* 1) RX：先清事件（UART_DATA→读环形），防 50ms 窗积压 */
        while (xQueueReceive(s_evt_q, &ev, 0) == pdTRUE) {
            if (ev.type == UART_DATA) {
                m207_rx_drain();
            } else if (ev.type == UART_FIFO_OVF || ev.type == UART_BUFFER_FULL) {
                ESP_LOGW(TAG, "uart overflow ev=%d → flush", (int)ev.type);
                uart_flush_input(M207_UART);
            }
        }
        m207_rx_drain();                 /* 拍内兜底（事件丢失也无损）*/

        /* 2) 在途事务超时检查（重试只标记，TX 交给本拍仲裁）*/
        for (int i = 0; i < M207_PEND_MAX; i++) {
            m207_pend_t *p = &s_pend[i];
            if (!p->in_use || p->resend_due) continue;
            if ((int32_t)(s_tick - p->deadline) >= 0) {
                if (p->phase == 1) pend_mark_resend(p, 1, "ack timeout");
                else               pend_mark_resend(p, 2, "query echo lost");
            }
        }

        /* 3) TX 仲裁：每拍至多 1 帧（重发 > 队列帧 > 轮询帧）*/
        m207_pend_t *rp = NULL;
        for (int i = 0; i < M207_PEND_MAX; i++)
            if (s_pend[i].in_use && s_pend[i].resend_due) { rp = &s_pend[i]; break; }
        if (rp) {
            const char *c = (rp->phase == 1) ? rp->set_cmd : rp->qry_cmd;
            m207_tx_now(c, strlen(c));
            rp->resend_due = 0;
            rp->deadline = s_tick + M207_REPLY_TICKS;
        } else {
            m207_msg_t m;
            if (xQueueReceive(s_q, &m, 0) == pdTRUE) {
                m207_tx_now(m.cmd, strlen(m.cmd));
                if (m.verify) {
                    m207_pend_t *p = pend_alloc();
                    if (p) {
                        p->in_use = 1; p->phase = 1;
                        p->ch = m.ch; p->decimals = m.decimals; p->retries = 0;
                        p->value = m.value;
                        strncpy(p->param, m.param, sizeof(p->param) - 1);
                        strncpy(p->set_cmd, m.cmd, sizeof(p->set_cmd) - 1);
                        strncpy(p->qry_cmd, m.qry, sizeof(p->qry_cmd) - 1);
                        p->deadline = s_tick + M207_REPLY_TICKS;
                    } else {
                        ESP_LOGW(TAG, "pend overflow: %s sent unverified", m.cmd);
                    }
                }
            } else {
                const char *poll = m207_fsm_poll_timed();
                if (poll) m207_tx_now(poll, strlen(poll));
            }
        }

        /* 4) perf 统计（第八章：队列服务+节流 ≤1ms/拍，轮询状态机 ≤0.3ms）*/
        int32_t dt = (int32_t)esp_timer_get_time() - t0;
        if (dt > 0 && (uint32_t)dt > s_stat.tick_max_us) s_stat.tick_max_us = (uint32_t)dt;
        if ((s_tick % M207_PERF_WIN) == 0) {
            UBaseType_t stk = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG,
                     "[M207P] tick_max=%luous (fsm_max=%luous; budget 1000/300) "
                     "tx=%lu rxline=%lu perr=%lu nak=%u vok=%u rsend=%u off=%u "
                     "qdrop=%u wdrop=%u stk=%uW",
                     (unsigned long)s_stat.tick_max_us,
                     (unsigned long)s_stat.fsm_max_us,
                     (unsigned long)s_stat.tx, (unsigned long)s_stat.rx_lines,
                     (unsigned long)s_stat.parse_err, (unsigned)s_stat.nak,
                     (unsigned)s_stat.verify_ok, (unsigned)s_stat.resend,
                     (unsigned)s_stat.offline, (unsigned)s_stat.qdrop,
                     (unsigned)s_stat.wdrop, (unsigned)stk);
            s_stat.tick_max_us = 0;
            s_stat.fsm_max_us = 0;
        }
    }
}

/* ====== 公共 API ====== */
int m207_service_start(void)
{
    if (s_started) return 0;
    uart_config_t cfg = {
        .baud_rate = M207_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(M207_UART, M207_RXBUF, M207_TXBUF,
                                        M207_EVTQ_LEN, &s_evt_q, 0));
    ESP_ERROR_CHECK(uart_param_config(M207_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(M207_UART, M207_TXD_GPIO, M207_RXD_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* 首写病理吸收（hex 取证 2026-10-26：装驱动后首个 TX 前导坏字节+丢帧尾 \r，
     * 后续写全部字节完好）。空行先导帧消耗之；空行对真机模块无副作用（正典 §1 空行非法即忽略）。*/
    uart_write_bytes(M207_UART, "\r", 1);
    uart_wait_tx_done(M207_UART, pdMS_TO_TICKS(50));
    uart_flush_input(M207_UART);

    s_q = xQueueCreate(M207_Q_LEN, sizeof(m207_msg_t));
    if (!s_q) return -1;
    m207_lineasm_reset(&s_asm);
    memset(s_pend, 0, sizeof(s_pend));
    memset(&s_stat, 0, sizeof(s_stat));
    s_started = 1;

    /* F9 上电安全默认：双通道 TCSW=0（模块若在场，先钳到安全侧）*/
    m207_enqueue_simple("TC1:TCSW=0\r");
    m207_enqueue_simple("TC2:TCSW=0\r");

    if (xTaskCreatePinnedToCore(m207_task, "m207_svc", M207_TASK_STACK, NULL,
                                M207_TASK_PRIO, NULL, M207_TASK_CORE) != pdPASS)
        return -1;
    ESP_LOGI(TAG, "service started uart%d %d baud tx=%d rx=%d core=%d",
             (int)M207_UART, M207_BAUD, M207_TXD_GPIO, M207_RXD_GPIO,
             M207_TASK_CORE);
    return 0;
}

int m207_set_temp(uint8_t ch, float v)
{
    if (ch != 1 && ch != 2) return -1;
    if (v < -20.0f || v > 80.0f) return -1;
    if (!s_started || !s_online) { s_stat.wdrop++; return -2; }
    m207_msg_t m;
    memset(&m, 0, sizeof(m));
    m.verify = 1; m.ch = ch; m.decimals = 1; m.value = v;
    strcpy(m.param, "TCADJTEMP");
    snprintf(m.cmd, sizeof(m.cmd), "TC%u:TCADJTEMP=%.1f\r", (unsigned)ch, (double)v);
    snprintf(m.qry, sizeof(m.qry), "TC%u:TCADJTEMP?\r", (unsigned)ch);
    return m207_enqueue(&m);
}

int m207_tec(uint8_t ch, uint8_t on)
{
    if (ch != 1 && ch != 2) return -1;
    if (!s_started || !s_online) { s_stat.wdrop++; return -2; }
    m207_msg_t m;
    memset(&m, 0, sizeof(m));
    m.verify = 1; m.ch = ch; m.decimals = 0; m.value = on ? 1.0f : 0.0f;
    strcpy(m.param, "TCSW");
    snprintf(m.cmd, sizeof(m.cmd), "TC%u:TCSW=%u\r", (unsigned)ch, (unsigned)(on ? 1u : 0u));
    snprintf(m.qry, sizeof(m.qry), "TC%u:TCSW?\r", (unsigned)ch);
    return m207_enqueue(&m);
}

/* ====== H10 映射表（逐字复制自 project_tcs m207.c s_map；R5 接注册表镜像）====== */
static const m207_map_entry_t s_map[] = {
    { 0x100, 1, "TCPIDP",       3 },
    { 0x101, 1, "TCPIDTI",      2 },
    { 0x102, 1, "TCPIDTD",      3 },
    { 0x103, 1, "TCOTPHT",      1 },
    { 0x104, 1, "TCOTPLT",      1 },
    { 0x105, 1, "TCOTPHHYS",    1 },
    { 0x106, 1, "TCOTPMODE",    0 },
    { 0x107, 1, "TCOCPADJTHR",  2 },
    { 0x108, 1, "TCOVPADJTHR",  2 },
    { 0x109, 1, "TCRTOCPHT",    1 },
    { 0x10A, 1, "TCRTOCPLT",    1 },
    { 0x10B, 1, "TCMINTEMP",    1 },
    { 0x10C, 1, "TCMAXTEMP",    1 },
    { 0x10D, 1, "TCMAXV",       2 },
    { 0x10E, 1, "TCCOOLVRATIO", 2 },
    { 0x10F, 1, "TCSTEPTEMP",   3 },
    { 0x110, 2, "TCPIDP",       3 },
    { 0x111, 2, "TCPIDTI",      2 },
    { 0x112, 2, "TCPIDTD",      3 },
    { 0x113, 2, "TCOTPHT",      1 },
    { 0x114, 2, "TCOTPLT",      1 },
    { 0x115, 2, "TCOTPHHYS",    1 },
    { 0x116, 2, "TCOTPMODE",    0 },
    { 0x117, 2, "TCOCPADJTHR",  2 },
    { 0x118, 2, "TCOVPADJTHR",  2 },
    { 0x119, 2, "TCRTOCPHT",    1 },
    { 0x11A, 2, "TCRTOCPLT",    1 },
    { 0x11B, 2, "TCMINTEMP",    1 },
    { 0x11C, 2, "TCMAXTEMP",    1 },
    { 0x11D, 2, "TCMAXV",       2 },
    { 0x11E, 2, "TCCOOLVRATIO", 2 },
    { 0x11F, 2, "TCSTEPTEMP",   3 },
    /* 系统/PCB 模块无通道概念；PCB:* 下发由 R5 按 module 字段扩展 */
    { 0x120, 1, "PCBOTPHT",     1 },
    { 0x121, 1, "PCBOTPLT",     1 },
    { 0x122, 1, "PCBOTPHHYS",   1 },
    { 0x126, 1, "COMADDR",      0 },
};
#define M207_MAP_N  (sizeof(s_map) / sizeof(s_map[0]))

int m207_map_set(uint16_t pid, float v)
{
    if (!s_started || !s_online) { s_stat.wdrop++; return -2; }
    for (size_t i = 0; i < M207_MAP_N; i++) {
        const m207_map_entry_t *e = &s_map[i];
        if (e->pid != pid) continue;
        m207_msg_t m;
        memset(&m, 0, sizeof(m));
        m.verify = 1; m.ch = e->ch; m.decimals = e->decimals; m.value = v;
        strncpy(m.param, e->cmd, sizeof(m.param) - 1);
        if (m207_map_format(e, v, m.cmd, sizeof(m.cmd)) != 0) return -1;
        snprintf(m.qry, sizeof(m.qry), "TC%u:%s?\r", (unsigned)e->ch, e->cmd);
        return m207_enqueue(&m);
    }
    return -1;                           /* 未映射参数 */
}

int m207_send_raw(const char *cmd)
{
    if (!cmd || !*cmd || strlen(cmd) >= M207_CMD_MAX) return -1;
    if (!s_started || !s_online) { s_stat.wdrop++; return -2; }
    m207_msg_t m;
    memset(&m, 0, sizeof(m));
    strncpy(m.cmd, cmd, sizeof(m.cmd) - 1);
    return m207_enqueue(&m);
}

uint8_t m207_online(void) { return s_online; }
float m207_last_temp(uint8_t ch) { return (ch == 1 || ch == 2) ? s_temp[ch] : 0.0f; }
uint16_t m207_stats_nak(void) { return s_stat.nak; }
uint16_t m207_stats_verify_ok(void) { return s_stat.verify_ok; }
uint16_t m207_stats_resend(void) { return s_stat.resend; }
uint16_t m207_stats_offline(void) { return s_stat.offline; }
uint16_t m207_stats_wdrop(void) { return s_stat.wdrop; }
uint8_t m207_alarm_active(void) { return tcs_alarm_active(ALM_M207); }

void m207_perf_reset(void)
{
    s_stat.tick_max_us = 0;
    s_stat.fsm_max_us = 0;
}

/* ====== 钩子适配器（param_registry PF_M207 写入口）====== */
void m207_hook_set_temp(uint8_t ch, float v) { (void)m207_set_temp(ch, v); }
void m207_hook_map_set(uint16_t pid, float v) { (void)m207_map_set(pid, v); }

/* ===================================================================== */
/*                 上电回环自测（独占 UART2，[M207LB]）                    */
/* ===================================================================== */

static const char *const s_lb_frames[] = {
    "TC1:TCACTTEMP=25.5",
    "TC2:TCACTTEMP=-10.05",
    "TC1:TCRTOCPHT=1.0",
    "CMD:REPLY=0", "CMD:REPLY=1", "CMD:REPLY=2", "CMD:REPLY=3",
    "CMD:REPLY=4", "CMD:REPLY=5", "CMD:REPLY=6", "CMD:REPLY=7",
    "CMD:REPLY=8",
};
#define M207_LB_N (sizeof(s_lb_frames) / sizeof(s_lb_frames[0]))

/* 对每帧断言：解析语义正确（V-5 全表锚定含 0..8）*/
static int lb_frame_pass(const char *f)
{
    m207_reply_t r;
    if (!m207_parse_line(f, &r)) return 0;
    if (strncmp(f, "CMD:REPLY=", 10) == 0)
        return r.kind == M207_KIND_ERRCODE && r.errcode == (uint8_t)atoi(&f[10]);
    if (r.kind != M207_KIND_REPLY || r.module == M207_MOD_NONE) return 0;
    if (strcmp(f, "TC1:TCACTTEMP=25.5") == 0)
        return r.module == M207_MOD_TC1 && r.value > 25.49f && r.value < 25.51f;
    if (strcmp(f, "TC2:TCACTTEMP=-10.05") == 0)
        return r.module == M207_MOD_TC2 && r.value < -10.04f && r.value > -10.06f;
    return r.param[0] != '\0';           /* 其余：参数名非空即过 */
}

static void lb_run_suite(const char *tag)
{
    int pass = 0, fail = 0;
    char line[M207_LINE_MAX + 1];
    uint8_t rxb[128];
    for (size_t i = 0; i < M207_LB_N; i++) {
        char tx[M207_LINE_MAX + 2];
        snprintf(tx, sizeof(tx), "%s\r", s_lb_frames[i]);
        uart_write_bytes(M207_UART, tx, strlen(tx));
        vTaskDelay(pdMS_TO_TICKS(20));
        int got = 0, n;
        m207_lineasm_reset(&s_asm);
        while ((n = uart_read_bytes(M207_UART, rxb, sizeof(rxb), 0)) > 0) {
            for (int k = 0; k < n; k++)
                if (m207_lineasm_feed(&s_asm, rxb[k], line, sizeof(line)))
                    got = 1;
        }
        line[sizeof(line) - 1] = '\0';
        if (got && strcmp(line, s_lb_frames[i]) == 0 && lb_frame_pass(s_lb_frames[i])) {
            pass++;
            ESP_LOGI(TAG, "[M207LB] %s %u/%u byte-identical+parse PASS",
                     tag, (unsigned)(i + 1), (unsigned)M207_LB_N);
        } else {
            fail++;
            ESP_LOGE(TAG, "[M207LB] %s %u/%u FAIL (got='%s')",
                     tag, (unsigned)(i + 1), (unsigned)M207_LB_N, line);
        }
    }
    ESP_LOGI(TAG, "[M207LB] RESULT %s pass=%d fail=%d %s",
             tag, pass, fail, fail ? "FAIL" : "PASS");
}

void m207_lb_boottest(void)
{
    QueueHandle_t q = NULL;
    uart_config_t cfg = {
        .baud_rate = M207_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(M207_UART, 2048, 512, 8, &q, 0) != ESP_OK) {
        ESP_LOGE(TAG, "[M207LB] driver install FAIL");
        return;
    }
    uart_param_config(M207_UART, &cfg);
    uart_set_pin(M207_UART, M207_TXD_GPIO, M207_RXD_GPIO,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* A) 内部回环全套（真 UART 外设/真驱动/真帧；WP-F.1 同款，无杜邦线）*/
    uart_set_loop_back(M207_UART, true);
    /* 先导帧：吸收"驱动安装+回环使能后首个 TX 不排空"的一次性损耗
     * （2026-10-26 实测：无先导时第 1 帧恒丢、后续全过；空行不产生解析帧）*/
    uart_write_bytes(M207_UART, "\r", 1);
    vTaskDelay(pdMS_TO_TICKS(60));
    uart_flush_input(M207_UART);
    vTaskDelay(pdMS_TO_TICKS(10));
    lb_run_suite("int");

    /* B) 物理短接探测：GPIO21↔47 杜邦在位则自动跑物理全套 */
    uart_set_loop_back(M207_UART, false);
    uart_flush_input(M207_UART);
    const char *probe = "TC1:TCACTTEMP=25.5\r";
    uart_write_bytes(M207_UART, probe, strlen(probe));
    vTaskDelay(pdMS_TO_TICKS(60));
    uint8_t echo[64];
    int n = uart_read_bytes(M207_UART, echo, sizeof(echo) - 1, 0);
    if (n >= (int)strlen(probe) - 1 && memcmp(echo, probe, (size_t)n) == 0) {
        ESP_LOGI(TAG, "[M207LB] physical jumper 21↔47 detected → full suite");
        lb_run_suite("phy");
    } else {
        ESP_LOGI(TAG, "[M207LB] physical probe: no echo (%dB) — GPIO21↔47 未短接，"
                      "物理回环挂账（接线表 v2.0 收尾项）", n);
    }

    uart_set_loop_back(M207_UART, false);
    uart_driver_delete(M207_UART);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/* ===================================================================== */
/*            R1b 取证专用：应答器注入全路径（CONFIG_TCS_M207_SELFLOOP）  */
/* ===================================================================== */
#if CONFIG_TCS_M207_SELFLOOP

/* 应答器：拦截服务 TX（m207_tx_now 调用），按正典 §2 合成应答。
 * 派发用参数名前缀+终结符直比（不用 sscanf 字面量——尾部字面量失配不影响
 * 已完成的 %d 赋值计数，==1 恒真会把一切命令派进第一分支，2026-10-26 实测踩坑）。
 * 查询→TCx:*=值；设定→CMD:REPLY=1（正典：设定不回显参数名！）；
 * wrong_echo=1 单次错值（重发路径）/2 恒错值（NAK 路径）；
 * silent=1 应答静默（拔线仿真 → 5 连败离线）。 */
static void rsp_stage(const char *s, size_t n)
{
    if (n >= sizeof(s_rsp.pend_line)) n = sizeof(s_rsp.pend_line) - 1;
    memcpy(s_rsp.pend_line, s, n);
    s_rsp.pend_len = (uint8_t)n;
}

static void m207_rsp_flush(void)
{
    if (s_rsp.pend_len) {
        uart_write_bytes(M207_UART, s_rsp.pend_line, s_rsp.pend_len);
        s_rsp.pend_len = 0;
    }
}

static void m207_rsp_on_tx(const char *cmd)
{
    if (s_rsp.silent) return;
    if (s_rsp.act1 == 0.0f) s_rsp.act1 = 25.0f;
    if (!(cmd[0] == 'T' && cmd[1] == 'C' &&
          (cmd[2] == '1' || cmd[2] == '2') && cmd[3] == ':')) return;
    int ch = cmd[2] - '0';
    const char *p = &cmd[4];                 /* 参数段起点 */
    char line[48];
    if (strncmp(p, "TCACTTEMP?", 10) == 0) {
        float sp = s_rsp.set1 ? s_rsp.set1 : 25.0f;
        s_rsp.act1 += (sp - s_rsp.act1) * 0.15f;
        snprintf(line, sizeof(line), "TC%d:TCACTTEMP=%.1f\r", ch, (double)s_rsp.act1);
        rsp_stage(line, strlen(line));
    } else if (strncmp(p, "TCADJTEMP?", 10) == 0) {
        float echo = s_rsp.set1;
        if (s_rsp.wrong_echo) {
            echo += 0.5f;
            if (s_rsp.wrong_echo == 1) s_rsp.wrong_echo = 0;
        }
        snprintf(line, sizeof(line), "TC%d:TCADJTEMP=%.2f\r", ch, (double)echo);
        rsp_stage(line, strlen(line));
    } else if (strncmp(p, "TCADJTEMP=", 10) == 0) {
        s_rsp.set1 = strtof(p + 10, NULL);   /* strtof：IDF newlib 下比 sscanf %%f 可靠 */
        ESP_LOGI(TAG, "[RSP] set ch=%d v=%.2f -> REPLY=1", ch, (double)s_rsp.set1);
        rsp_stage("CMD:REPLY=1\r", 12);
    } else if (strncmp(p, "TCSW=", 5) == 0) {
        if (ch == 1 || ch == 2) s_rsp.tcsw[ch] = (uint8_t)atoi(p + 5);
        rsp_stage("CMD:REPLY=1\r", 12);
    } else if (strncmp(p, "TCSW?", 5) == 0) {
        snprintf(line, sizeof(line), "TC%d:TCSW=%u\r", ch,
                 (unsigned)((ch == 1 || ch == 2) ? s_rsp.tcsw[ch] : 0u));
        rsp_stage(line, strlen(line));
    }
}

static int s_sl_pass, s_sl_fail;
#define SL_CHECK(cond, name) do { \
    if (cond) { s_sl_pass++; ESP_LOGI(TAG, "[M207SL] %s PASS", name); } \
    else      { s_sl_fail++; ESP_LOGE(TAG, "[M207SL] %s FAIL", name); } \
} while (0)

static void sl_wait_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void m207_selfloop_run(void)
{
    ESP_LOGI(TAG, "[M207SL] === selfloop evidence start (uart internal loopback) ===");
    m207_rsp_fault(0);
    m207_rsp_wrong(0);
    uart_set_loop_back(M207_UART, true);

    /* 1) 在线检测（轮询→应答→online 沿）*/
    uint32_t wait = 0;
    while (!m207_online() && wait < 4000) { sl_wait_ms(100); wait += 100; }
    SL_CHECK(m207_online(), "1 online-first");
    tcs_alarm_ack();   /* 清启动期无模块离线沿：保证 6/7 断言的是本序列产生的沿 */

    /* 2) 温度缓存路径 */
    sl_wait_ms(1500);
    SL_CHECK(s_have_temp[1] && s_have_temp[2], "2 temp-cache tc1/tc2");

    /* 3) 写后读回：一次成功 */
    uint16_t vok0 = m207_stats_verify_ok();
    (void)m207_set_temp(1, 30.5f);
    wait = 0;
    while (m207_stats_verify_ok() == vok0 && wait < 2000) { sl_wait_ms(100); wait += 100; }
    SL_CHECK(m207_stats_verify_ok() == vok0 + 1, "3 verify-ok set 30.5");

    /* 4) 错值回显 → 自动重发 1 次 → 成功 */
    uint16_t rs0 = m207_stats_resend();
    m207_rsp_wrong(1);
    (void)m207_set_temp(1, 31.0f);
    wait = 0;
    while (m207_stats_resend() == rs0 && wait < 2500) { sl_wait_ms(100); wait += 100; }
    sl_wait_ms(1500);
    SL_CHECK(m207_stats_resend() == rs0 + 1 &&
             m207_stats_verify_ok() == vok0 + 2, "4 mismatch-resend-then-ok");

    /* 5) 恒错回显 → 重发仍错 → NAK 记录 */
    uint16_t nak0 = m207_stats_nak();
    m207_rsp_wrong(2);
    (void)m207_set_temp(1, 32.0f);
    wait = 0;
    while (m207_stats_nak() == nak0 && wait < 3000) { sl_wait_ms(100); wait += 100; }
    m207_rsp_wrong(0);
    SL_CHECK(m207_stats_nak() == nak0 + 1, "5 nak-after-retry");

    /* 6) 拔线仿真 → 5 连败离线 + ALM_M207（F5）*/
    uint16_t off0 = m207_stats_offline();
    m207_rsp_fault(1);
    wait = 0;
    while (m207_stats_offline() == off0 && wait < 6000) { sl_wait_ms(100); wait += 100; }
    SL_CHECK(m207_stats_offline() == off0 + 1 && !m207_online() &&
             m207_alarm_active(), "6 offline-5fails-ALM_M207");

    /* 7) 复线恢复 → 探测上线 + 报警粘滞 */
    m207_rsp_fault(0);
    wait = 0;
    while (!m207_online() && wait < 5000) { sl_wait_ms(100); wait += 100; }
    SL_CHECK(m207_online() && m207_alarm_active(), "7 recover-alarm-sticky");

    /* 8) 人工 ack → 报警清除（F5：复联+ack）*/
    tcs_alarm_ack();
    SL_CHECK(!m207_alarm_active(), "8 ack-clears-ALM_M207");

    /* 9) 停写语义：离线时写请求被拒（-2）*/
    m207_rsp_fault(1);
    wait = 0;
    while (m207_stats_offline() == off0 + 1 && wait < 6000) { sl_wait_ms(100); wait += 100; }
    int rc = m207_set_temp(2, 25.0f);
    SL_CHECK(rc == -2, "9 write-rejected-while-offline");

    /* 收场：应答器退场 + 回环关闭（真机无模块 → 回到预期离线态）*/
    m207_rsp_fault(1);
    uart_set_loop_back(M207_UART, false);
    ESP_LOGI(TAG, "[M207SL] RESULT pass=%d fail=%d %s", s_sl_pass, s_sl_fail,
             s_sl_fail ? "FAIL" : "PASS");
    ESP_LOGI(TAG, "[M207SL] === selfloop evidence end ===");
}
#endif /* CONFIG_TCS_M207_SELFLOOP */

/* ===================================================================== */
/*      R1b 真机取证（CONFIG_TCS_M207_REALPROBE；M5-M7 接通后清账用）     */
/* ===================================================================== */
#if CONFIG_TCS_M207_REALPROBE
#include "driver/gpio.h"

/* P-2 软件子集：RS232 静态判活。
 * ① 真模块上电时其 TX 恒发 mark(负压)，经 MAX3232 后 TTL 侧(GPIO47)必读高——
 *    读低即模块 TX 未到达（未供电/断路；注意：即便 M5/M6 对调，对端有电也读高）。
 * ② 二级判别（内部上拉）：GPIO47 读低时挂 45k 上拉复测——仍低=MAX3232 在主动驱动
 *    低电平（=有电但 RIN 悬空，M6 支路未真正接到有电模块）；变高=MAX3232 输出
 *    高阻（=MAX3232 未上电/损坏，查 M3 3V3）。
 * GPIO21 为 TX 脚（输入缓冲未开），软件读数无效——TX 判活由模块是否应答决定。 */
static void realprobe_p2_levels(void)
{
    int rx_hi = 0, rx_pu_hi = 0;
    for (int i = 0; i < 100; i++) {
        rx_hi += gpio_get_level(M207_RXD_GPIO);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    gpio_pullup_en(M207_RXD_GPIO);
    vTaskDelay(pdMS_TO_TICKS(5));
    for (int i = 0; i < 100; i++) {
        rx_pu_hi += gpio_get_level(M207_RXD_GPIO);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    const char *v = (rx_hi >= 95) ?
        "RS232 对端在线（模块供电+M6 支路通）——若仍无应答再查 TX 侧/M5" :
        (rx_hi <= 5 && rx_pu_hi <= 5) ?
        "MAX3232 有电且输出恒低=RIN 悬空：模块未供电，或 M6/M7 未真正接通到模块引脚" :
        (rx_hi <= 5 && rx_pu_hi >= 95) ?
        "MAX3232 输出高阻=其自身未上电/损坏：查 M3（ESP32 3V3→MAX3232 VCC）" :
        "RXD 抖动——接触不良";
    ESP_LOGI(TAG, "[P2] GPIO47(RX) idle high=%u/100；挂 45k 上拉后 high=%u/100 → %s",
             (unsigned)rx_hi, (unsigned)rx_pu_hi, v);
}

void m207_realprobe_run(void)
{
    ESP_LOGI(TAG, "[RP] === real-machine probe start ===");
    realprobe_p2_levels();

    uint32_t w = 0;
    while (!m207_online() && w < 6000) { vTaskDelay(pdMS_TO_TICKS(100)); w += 100; }
    ESP_LOGI(TAG, "[RP] online=%u (after %lums)", m207_online(), (unsigned long)w);
    if (!m207_online()) {
        ESP_LOGW(TAG, "[RP] 模块无应答——按 [P2] 结论查供电/接线/R-K1 一次对调法");
        return;
    }

    /* 故障码读取（用户问询：模块报错灯亮）：位语义见 tc_hlp.xml——
     * 8 位，正常=1 故障=0：bit0 外部/bit1 过压/bit2 过流/bit3 过温/
     * bit4 传感器开路/bit5 负载短路/bit6 负载开路；全正常=127。
     * STATUS=当前态，LOG=上电以来锁存。 */
    (void)m207_send_raw("TC1:TCERRORSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC2:TCERRORSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC1:TCERRORLOG?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC1:TCOTPSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC1:TCOCPSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC1:TCRTOCPSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("PCB:TMACTTEMP?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("PCB:TMOTPSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* C3 变体 + C1 别名 + V-5 ERRORCODE 注入（只读/安全值；TCSW 全程 0）*/
    (void)m207_send_raw("SYSTEM:SYSMODEL?");    /* C3 型号变体裁定 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCADJTEMP?");
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCADJUSTTEMP?");   /* C1 别名裁定 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("FOO:BAR=1");           /* V-5 期望 err=0 未知模块 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:NOTEXIST?");       /* 期望 err=2 未知参数 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("GARBAGE");             /* 期望 err=6/0 语法 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCACTTEMP=999");   /* 期望 err=3/2/4 只读拒绝 */
    vTaskDelay(pdMS_TO_TICKS(800));

    /* COMPUSHLIST 自发现试跑（正典 §7；行超 64B 由 RAWLOG hex 兜底）*/
    (void)m207_send_raw("PCRS232:COMPUSHLIST=1");
    vTaskDelay(pdMS_TO_TICKS(4000));

    /* E-23 提前：节流压测 10 连写（R1b 形态；期望 0 丢 0 NAK 全确认）*/
    uint16_t v0 = m207_stats_verify_ok(), n0 = m207_stats_nak(), d0 = m207_stats_wdrop();
    for (int i = 0; i < 10; i++) {
        int rc = m207_set_temp(1, 25.0f + (i % 2));
        if (rc) ESP_LOGW(TAG, "[RP] burst enqueue rc=%d i=%d", rc, i);
    }
    vTaskDelay(pdMS_TO_TICKS(4000));
    ESP_LOGI(TAG, "[RP] burst: vok+%u nak+%u wdrop+%u (expect 10/0/0)",
             (unsigned)(m207_stats_verify_ok() - v0),
             (unsigned)(m207_stats_nak() - n0),
             (unsigned)(m207_stats_wdrop() - d0));
    ESP_LOGI(TAG, "[RP] safe state: tec1=%u tec2=%u (expect 0/0)", 
             g_tcs.tec1_on, g_tcs.tec2_on);

    /* E-06 前半：双通道真值 60s 录帧（每 5s 一行；值差/合理性由 H6.7 服务侧把关）*/
    for (int t = 5; t <= 60; t += 5) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "[E06] t=%ds tc1=%.2f tc2=%.2f online=%u nak=%u",
                 t, (double)m207_last_temp(1), (double)m207_last_temp(2),
                 m207_online(), m207_stats_nak());
    }
    ESP_LOGI(TAG, "[RP] === realprobe end ===");
}
#endif /* CONFIG_TCS_M207_REALPROBE */
