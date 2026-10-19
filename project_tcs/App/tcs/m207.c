/**
 * m207.c — M207 温控模块驱动实现（任务书 §3.4）
 *
 * RX 通路：USART3 RX 中断单字节 → 512B 环形缓冲（SPSC，head 仅 ISR 写）
 *         → 主循环取字节拼行（\r 结束，行长上限 64）→ m207_parse_line。
 * ORE 溢出：HAL_UART_ErrorCallback 先清标志再重置接收（F401 踩坑防御保留）。
 * TX：主循环阻塞 HAL_UART_Transmit（铁律：禁止 ISR 内发送）。
 */
#include "m207.h"
#include "post.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ===================================================================== */
/*                       纯逻辑（PC 单测覆盖）                             */
/* ===================================================================== */

void m207_lineasm_reset(m207_lineasm_t *la)
{
    la->len = 0;
    la->buf[0] = '\0';
}

int m207_lineasm_feed(m207_lineasm_t *la, uint8_t b, char *line, size_t cap)
{
    if (b == '\r' || b == '\n') {
        if (la->len == 0) return 0;            /* 空行忽略（连续 \r\n）*/
        size_t n = la->len;
        if (n >= cap) n = cap - 1;
        memcpy(line, la->buf, n);
        line[n] = '\0';
        la->len = 0;
        la->buf[0] = '\0';
        return 1;
    }
    if (la->len >= M207_LINE_MAX) {            /* 超长：丢弃至行尾，防溢出 */
        return 0;
    }
    la->buf[la->len++] = (char)b;
    la->buf[la->len] = '\0';
    return 0;
}

int m207_parse_line(const char *line, m207_reply_t *out)
{
    if (!line || !out) return 0;
    size_t len = strlen(line);
    if (len == 0 || len > M207_LINE_MAX) return 0;

    /* 小写自动大写到本地副本（F401 同款归一化）*/
    char up[M207_LINE_MAX + 1];
    for (size_t i = 0; i < len; i++) {
        char c = line[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 0x20);
        up[i] = c;
    }
    up[len] = '\0';

    /* F401 ProtocolAnalysis 逻辑：扫 ':'、'='、'?'，取最后出现位置 */
    int colon = -1, eq = -1, qm = -1;
    for (size_t i = 0; i < len; i++) {
        if (up[i] == ':') colon = (int)i;
        else if (up[i] == '=') eq = (int)i;
        else if (up[i] == '?') qm = (int)i;
    }
    if (colon <= 0) return 0;                  /* 缺冒号（或冒号在首位）*/
    if (eq < 0 && qm < 0) return 0;            /* 缺等号/问号 */

    /* CMD:REPLY=N（现行M207协议_v1.md §2/§3）：设/存/错的统一应答。
     * 正典约定不是回显参数名——解析器按此分路（WP-P2 板无关部分）。*/
    if (up[0] == 'C' && up[1] == 'M' && up[2] == 'D' && up[3] == ':'
        && eq >= 0 && strncmp(&up[4], "REPLY", 5) == 0) {
        const char *vs = &up[eq + 1];
        if (*vs == '\0') return 0;
        char *end = NULL;
        long code = strtol(vs, &end, 10);
        if (end == vs || code < 0 || code > 255) return 0;
        out->module = M207_MOD_NONE;
        out->param[0] = '\0';
        out->kind = M207_KIND_ERRCODE;
        out->value = 0.0f;
        out->errcode = (uint8_t)code;
        return 1;
    }

    /* 模块名 TC1/TC2，其它（如 TC3/空）→ module=NONE 且解析失败 */
    int mlen = colon;
    if (mlen != 3 || up[0] != 'T' || up[1] != 'C') return 0;
    if (up[2] != '1' && up[2] != '2') return 0;
    out->module = (up[2] == '1') ? M207_MOD_TC1 : M207_MOD_TC2;

    /* 参数名 = colon+1 .. 分隔符前 */
    int sep = (eq >= 0) ? eq : qm;
    int plen = sep - colon - 1;
    if (plen <= 0 || plen >= (int)sizeof(out->param)) return 0;
    memcpy(out->param, &up[colon + 1], (size_t)plen);
    out->param[plen] = '\0';
    out->kind = (eq >= 0) ? M207_KIND_REPLY : M207_KIND_POLL;
    out->value = 0.0f;

    if (eq >= 0) {
        const char *vs = &up[eq + 1];
        if (*vs == '\0') return 0;             /* '=' 后空值 */
        char *end = NULL;
        float v = strtof(vs, &end);
        if (end == vs) return 0;               /* 非数值 */
        out->value = v;
    }
    return 1;
}

/* ====== H10 映射表模板格式化（纯函数）====== */
int m207_map_format(const m207_map_entry_t *e, float v, char *out, size_t cap)
{
    if (!e || !out || e->ch < 1 || e->ch > 2 || e->cmd[0] == '\0') return -1;
    if (e->decimals > 6) return -1;
    int n = snprintf(out, cap, "TC%u:%s=%.*f\r",
                     (unsigned)e->ch, e->cmd, (int)e->decimals, (double)v);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

/* ===================================================================== */
/*                          目标机驱动                                    */
/* ===================================================================== */
#ifndef TCS_UNIT_TEST

#include "stm32h7xx_hal.h"
#include "segger_rtt.h"
#include "param_registry.h"
#include "console.h"        /* tcs_log（TCS_SIM 应答器日志）*/

#define M207_RING_SIZE   512u
#define M207_RING_MASK   (M207_RING_SIZE - 1u)
#define M207_TXQ_DEPTH   8u
#define M207_TXQ_LEN     24u
#define M207_POLL_MS     500u       /* 轮询时刻（通道交替）*/
#define M207_REPLY_TMO   100u       /* WAIT_REPLY 超时 */
#define M207_FAIL_LIMIT  5         /* ≈2.5s（2 通道交替 500ms）*/
#define M207_RECOVER_MS  1000u
#define M207_SET_TOL     0.05f

static UART_HandleTypeDef *s_huart;
static uint8_t s_rx_byte;                      /* HAL_UART_Receive_IT 落点 */
static volatile uint16_t s_ring_head;          /* 仅 ISR 写 */
static volatile uint16_t s_ring_tail;          /* 仅主循环写 */
static uint8_t s_ring[M207_RING_SIZE];
static m207_lineasm_t s_asm;
static m207_state_t s_state;
static uint32_t s_last_poll_ms;
static uint32_t s_wait_deadline;
static uint32_t s_recover_last_ms;
static uint8_t  s_fail_cnt;
static uint8_t  s_poll_ch = 1;                 /* 1↔2 交替 */
static uint8_t  s_online;
static float    s_last_temp[3];                /* [1]=tc1 [2]=tc2 */
static uint32_t s_last_temp_ms[3];             /* H6.7：上回值时刻（合理性滤波）*/
static uint8_t  s_have_temp[3];

/* 待确认下发（回读校验）：每通道 1 条 */
static m207_setpend_t s_pend[2];

/* ====== H10 参数映射表（占位；M207 文档到达=填表+向量，扩展成本一行）======
 * 0x100/0x101 为假参数占位（PIDKP/PIDKI 非真指令名），验证通用通道
 * 下发→回读确认→NAK 重发全链；真参数到达后替换 cmd 字段即可。 */
static const m207_map_entry_t s_map[] = {
    /* H10 全表（v1.2 §2.5；cmd=《现行M207协议_v1.md》字典参数名，TC1/TC2 双通道）
     * decimals=下发小数位（回读容差 0.5×10^-decimals）。R5 接真实传输。 */
    /* TC1 */
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
    /* TC2 */
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
    /* 系统/PCB（PCB 模块无通道概念，ch=1 占位仅用于模板 TC 前缀不适用——
     * PCB:* 类下发由 R5 按 module 字段扩展，本表 ch 仅 TC 类有效）*/
    { 0x120, 1, "PCBOTPHT",     1 },
    { 0x121, 1, "PCBOTPLT",     1 },
    { 0x122, 1, "PCBOTPHHYS",   1 },
    { 0x126, 1, "COMADDR",      0 },
};
#define M207_MAP_N  (sizeof(s_map) / sizeof(s_map[0]))

/* TX 命令队列（SPSC：生产者全在主循环上下文，消费者 m207_poll）*/
static char    s_txq[M207_TXQ_DEPTH][M207_TXQ_LEN];
static uint8_t s_txq_head, s_txq_tail;

void m207_isr_push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_ring_head + 1u) & M207_RING_MASK);
    if (next == s_ring_tail) return;           /* 满：丢最新（轮询型数据，旧值无害）*/
    s_ring[s_ring_head] = b;
    s_ring_head = next;
}

static uint16_t m207_ring_pop(uint8_t *b)
{
    if (s_ring_head == s_ring_tail) return 0;
    *b = s_ring[s_ring_tail];
    s_ring_tail = (uint16_t)((s_ring_tail + 1u) & M207_RING_MASK);
    return 1;
}

#ifdef TCS_SIM
/* ====== WP-G 板内仿真：M207 模拟应答器（无外线驱动状态机全路径）======
 * 拦截 TX 命令合成 RX 行入环形缓冲：查询→正弦水温；设定→回读确认
 * （sim m207 wrongecho 时首次故意错值 → 驱动走自动重发路径）；
 * sim m207 fault → 应答静默（拔线仿真，5 败→OFFLINE→RECOVER）。*/
static struct {
    uint8_t fault;
    uint8_t wrong_echo;
    float   setpoint[3];
    float   act[3];
    uint8_t drop_pct;      /* H11：回包丢弃率 0-100（间歇丢包注入）*/
} s_m207_sim;

static void sim_push_line(const char *line)
{
    if (s_m207_sim.drop_pct > 0 && (rand() % 100) < s_m207_sim.drop_pct)
        return;                       /* H11：间歇丢包（回包静默丢弃）*/
    for (const char *p = line; *p; p++) m207_isr_push((uint8_t)*p);
}

static void sim_respond(const char *cmd)
{
    int ch = 0;
    float v = 0;
    char line[40];
    if (!s_m207_sim.act[1] && !s_m207_sim.act[2]) {
        s_m207_sim.act[1] = 26.0f;
        s_m207_sim.act[2] = 25.0f;
    }
    if (sscanf(cmd, "TC%d:TCACTTEMP?", &ch) == 1) {
        /* 实际水温：向设定点一阶趋近 + 微幅正弦（画面/曲线有生命感）*/
        float sp = s_m207_sim.setpoint[ch] ? s_m207_sim.setpoint[ch] : 25.0f;
        s_m207_sim.act[ch] += (sp - s_m207_sim.act[ch]) * 0.15f;
        s_m207_sim.act[ch] += 0.05f * sinf((float)HAL_GetTick() / 7000.0f);
        snprintf(line, sizeof(line), "TC%d:TCACTTEMP=%.1f\r",
                 ch, (double)s_m207_sim.act[ch]);
        sim_push_line(line);
    } else if (sscanf(cmd, "TC%d:TCADJTEMP=%f", &ch, &v) == 2) {
        s_m207_sim.setpoint[ch] = v;
        float echo = s_m207_sim.wrong_echo ? (v + 0.3f) : v;
        s_m207_sim.wrong_echo = 0;
        snprintf(line, sizeof(line), "TC%d:TCADJTEMP=%.1f\r", ch, (double)echo);
        tcs_log("[SIM]", "echo %s (want %.1f)", line, (double)v);
        sim_push_line(line);
    } else if (sscanf(cmd, "TC%d:TCSW=%f", &ch, &v) == 2) {
        snprintf(line, sizeof(line), "TC%d:TCSW=%.0f\r", ch, (double)v);
        sim_push_line(line);
    }
}

void m207_sim_fault(void)
{
    s_m207_sim.fault = 1;
    tcs_log("[SIM]", "m207 responder SILENT (unplug sim)");
}

void m207_sim_recover(void)
{
    s_m207_sim.fault = 0;
    tcs_log("[SIM]", "m207 responder BACK online");
}

void m207_sim_wrong_echo(void)
{
    s_m207_sim.wrong_echo = 1;
    tcs_log("[SIM]", "next setpoint echo will be WRONG (resend path)");
}

void m207_sim_drop(uint8_t pct)
{
    s_m207_sim.drop_pct = (pct > 100) ? 100u : pct;
    tcs_log("[SIM]", "reply drop rate = %u%%", (unsigned)s_m207_sim.drop_pct);
}
#endif /* TCS_SIM */

static void m207_tx_raw(const char *cmd)
{
#ifdef TCS_SIM
    if (!s_m207_sim.fault) sim_respond(cmd);
#endif
    HAL_UART_Transmit(s_huart, (uint8_t *)cmd, (uint16_t)strlen(cmd), 20u);
}

static int m207_txq_push(const char *cmd)
{
    uint8_t next = (uint8_t)((s_txq_head + 1u) % M207_TXQ_DEPTH);
    if (next == s_txq_tail) return -1;         /* 满 */
    strncpy(s_txq[s_txq_head], cmd, M207_TXQ_LEN - 1);
    s_txq[s_txq_head][M207_TXQ_LEN - 1] = '\0';
    s_txq_head = next;
    return 0;
}

void m207_init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_ring_head = s_ring_tail = 0;
    m207_lineasm_reset(&s_asm);
    s_state = M207_ST_IDLE;
    s_fail_cnt = 0;
    s_online = 0;
    s_last_temp[1] = s_last_temp[2] = 0.0f;
    s_last_temp_ms[1] = s_last_temp_ms[2] = 0;
    s_have_temp[1] = s_have_temp[2] = 0;
    memset(s_pend, 0, sizeof(s_pend));
    s_txq_head = s_txq_tail = 0;
    g_tcs.m207_online = 0;

    /* F9 上电安全：双通道 TCSW=0（TEC 保持停止）。离线时发不出去也无害。 */
    m207_txq_push("TC1:TCSW=0\r");
    m207_txq_push("TC2:TCSW=0\r");

    /* 启动 RX 单字节中断（错误恢复同款重启入口）*/
    HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1);
}

int m207_set_temp(uint8_t ch, float v)
{
    if (ch != 1 && ch != 2) return -1;
    if (v < -20.0f || v > 80.0f) return -1;
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "TC%u:TCADJTEMP=%.1f\r",
             (unsigned)ch, (double)v);
    if (m207_txq_push(cmd) != 0) return -1;
    /* 登记/覆盖同通道待确认 */
    m207_setpend_t *p = &s_pend[ch - 1];
    p->value = v;
    p->ch = ch;
    p->retries = 0;
    p->active = 1;
    strncpy(p->param, "TCADJTEMP", sizeof(p->param) - 1);
    p->param[sizeof(p->param) - 1] = '\0';
    p->decimals = 1;
    return 0;
}

int m207_tec(uint8_t ch, uint8_t on)
{
    if (ch != 1 && ch != 2) return -1;
    char cmd[24];
    snprintf(cmd, sizeof(cmd), "TC%u:TCSW=%u\r", (unsigned)ch, (unsigned)(on ? 1u : 0u));
    return m207_txq_push(cmd);
}

int m207_send_raw(const char *cmd)
{
    if (!cmd || !*cmd) return -1;
    return m207_txq_push(cmd);
}

/* H10：按映射表下发（下发→回读确认→不一致自动重发 1 次→NAK 告警）*/
int m207_map_set(uint16_t pid, float v)
{
    for (size_t i = 0; i < M207_MAP_N; i++) {
        const m207_map_entry_t *e = &s_map[i];
        if (e->pid != pid) continue;
        char cmd[24];
        if (m207_map_format(e, v, cmd, sizeof(cmd)) != 0) return -1;
        if (m207_txq_push(cmd) != 0) return -1;
        m207_setpend_t *p = &s_pend[e->ch - 1];
        p->value = v;
        p->ch = e->ch;
        p->retries = 0;
        p->active = 1;
        strncpy(p->param, e->cmd, sizeof(p->param) - 1);
        p->param[sizeof(p->param) - 1] = '\0';
        p->decimals = e->decimals;
        return 0;
    }
    return -1;   /* 未映射参数 */
}

float m207_last_temp(uint8_t ch)
{
    if (ch != 1 && ch != 2) return 0.0f;
    return s_last_temp[ch];
}

uint8_t m207_online(void) { return s_online; }

/* 回复行处理（含写路径确认）。now_ms 供 H6.7 合理性滤波取 Δt。*/
static void m207_handle_line(const char *line, uint32_t now_ms)
{
    m207_reply_t r;
    if (!m207_parse_line(line, &r)) return;
    if (r.module == M207_MOD_NONE) return;

    if (r.kind == M207_KIND_REPLY && strcmp(r.param, "TCACTTEMP") == 0) {
        /* H6.7 合理性滤波：|Δ|>10°C/s 判不可信 → 按失联路径（计数+1，不采纳）*/
        if (s_have_temp[r.module]) {
            float dt = (float)(now_ms - s_last_temp_ms[r.module]) / 1000.0f;
            if (!post_m207_sanity_ok(s_last_temp[r.module], r.value, dt)) {
                SEGGER_RTT_printf(0, "[WARN] m207 implausible ch=%u %.1f->%.1f (dt=%.1fs)\n",
                                  (unsigned)r.module, (double)s_last_temp[r.module],
                                  (double)r.value, (double)dt);
                if (s_fail_cnt < 255) s_fail_cnt++;
                return;
            }
        }
        s_last_temp[r.module] = r.value;
        s_last_temp_ms[r.module] = now_ms;
        s_have_temp[r.module] = 1;
        s_fail_cnt = 0;                        /* 任何成功回复清失败计数 */
        if (!s_online) {
            s_online = 1;
            g_tcs.m207_online = 1;
            SEGGER_RTT_WriteString("[NET] m207 online\n");
        }
        if (s_state == M207_ST_RECOVER) {      /* 探测成功 → 恢复 */
            s_state = M207_ST_IDLE;
            SEGGER_RTT_WriteString("[NET] m207 recovered\n");
        }
    }
    /* 写路径确认：该通道有待确认 && 收到 TCADJTEMP 回显 */
    m207_setpend_t *p = &s_pend[r.module - 1];
    if (p->active && r.kind == M207_KIND_REPLY && strcmp(r.param, "TCADJTEMP") == 0) {
        float diff = r.value - p->value;
        if (diff < 0) diff = -diff;
        if (diff < M207_SET_TOL) {
            p->active = 0;                     /* 确认成功 */
        } else if (p->retries == 0) {
            p->retries = 1;                    /* 自动重发 1 次 */
            char cmd[24];
            snprintf(cmd, sizeof(cmd), "TC%u:TCADJTEMP=%.1f\r",
                     (unsigned)r.module, (double)p->value);
            m207_txq_push(cmd);
        } else {
            p->active = 0;
            SEGGER_RTT_printf(0, "[WARN] m207 set NAK ch=%u set=%.1f echo=%.1f\n",
                              (unsigned)r.module, (double)p->value, (double)r.value);
        }
    }
}

void m207_poll(uint32_t now_ms)
{
    /* 1) TX 队列消费（一拍最多 1 帧，防挤占节拍；57600 下 15B≈2.6ms）*/
    if (s_txq_tail != s_txq_head) {
        m207_tx_raw(s_txq[s_txq_tail]);
        s_txq_tail = (uint8_t)((s_txq_tail + 1u) % M207_TXQ_DEPTH);
    }

    /* 2) RX：环形缓冲取字节拼行（0.3ms 预算内，环形至多清空 512B）*/
    uint8_t b;
    char line[M207_LINE_MAX + 1];
    uint16_t guard = M207_RING_SIZE + 2u;
    while (guard-- && m207_ring_pop(&b)) {
        if (m207_lineasm_feed(&s_asm, b, line, sizeof(line)))
            m207_handle_line(line, now_ms);
    }

    /* 3) 轮询状态机 */
    switch (s_state) {
    case M207_ST_IDLE:
        if (now_ms - s_last_poll_ms >= M207_POLL_MS) {
            s_last_poll_ms = now_ms;
            char cmd[24];
            snprintf(cmd, sizeof(cmd), "TC%u:TCACTTEMP?\r", (unsigned)s_poll_ch);
            m207_tx_raw(cmd);
            s_state = M207_ST_WAIT_REPLY;
            s_wait_deadline = now_ms + M207_REPLY_TMO;
            s_poll_ch = (s_poll_ch == 1) ? 2u : 1u;   /* 下轮换通道 */
        }
        break;
    case M207_ST_WAIT_REPLY:
        if ((int32_t)(now_ms - s_wait_deadline) >= 0) {
            /* 超时（回复已在 RX 路径处理成功情形）*/
            s_fail_cnt++;
            s_state = M207_ST_IDLE;
            if (s_fail_cnt >= M207_FAIL_LIMIT) {
                s_online = 0;
                g_tcs.m207_online = 0;
                SEGGER_RTT_WriteString("[ERROR] m207 OFFLINE (5 fails)\n");
                s_state = M207_ST_RECOVER;
                s_recover_last_ms = now_ms;
            }
        }
        break;
    case M207_ST_RECOVER:
        if (now_ms - s_recover_last_ms >= M207_RECOVER_MS) {
            s_recover_last_ms = now_ms;
            m207_tx_raw("TC1:TCACTTEMP?\r");   /* 每 1s 探测一次 */
        }
        /* 任一成功回复在 m207_handle_line 中迁移回 IDLE */
        break;
    default:
        s_state = M207_ST_IDLE;
        break;
    }
}

/* HAL 错误回调由 main.c 转发：ORE 先清标志再重置接收（F401 踩坑防御保留）*/
void m207_uart_error_recover(void)
{
    if (!s_huart) return;
    if (__HAL_UART_GET_FLAG(s_huart, UART_FLAG_ORE)) {
        __HAL_UART_CLEAR_OREFLAG(s_huart);
        SEGGER_RTT_WriteString("[WARN] m207 UART ORE cleared\n");
    }
    HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1);
}

/* RX 完成回调转发入口（main.c HAL_UART_RxCpltCallback 里按实例分发）*/
void m207_rx_complete(void)
{
    m207_isr_push(s_rx_byte);
    HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1);
}

#endif /* !TCS_UNIT_TEST */
