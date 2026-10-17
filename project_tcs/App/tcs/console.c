/**
 * console.c — USART1@115200 控制台实现（任务书 §3.8）
 *
 * printf 重定向：_write → TX 环形缓冲（满则丢弃+计数），主循环 console_poll
 * 里唯一 HAL_UART_Transmit 消费点（铁律：ISR 不发送）。日志同时进 RTT。
 * CLI 行缓冲 \r 或 \n 结尾；命令表见 §3.8。
 */
#include "console.h"
#include "param_registry.h"
#include "control.h"
#include "m207.h"
#include "segger_rtt.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define CON_RX_SIZE   256u
#define CON_RX_MASK   (CON_RX_SIZE - 1u)
#define CON_TX_SIZE   2048u
#define CON_TX_MASK   (CON_TX_SIZE - 1u)
#define CON_LINE_MAX  96

static UART_HandleTypeDef *s_uart;
static uint8_t s_rx_byte;
static volatile uint16_t s_rx_head, s_rx_tail;
static uint8_t s_rx_ring[CON_RX_SIZE];

static volatile uint16_t s_tx_head, s_tx_tail;
static uint8_t s_tx_ring[CON_TX_SIZE];
static volatile uint32_t s_tx_drop_cnt;

static char s_line[CON_LINE_MAX];
static uint16_t s_line_len;

/* ====== TX 环（主循环消费；_write/tcs_log 生产，可在任意上下文）====== */
static void tx_push(const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint16_t next = (uint16_t)((s_tx_head + 1u) & CON_TX_MASK);
        if (next == s_tx_tail) { s_tx_drop_cnt++; break; }
        s_tx_ring[s_tx_head] = d[i];
        s_tx_head = next;
    }
}

static void tx_drain(void)
{
    if (s_tx_head == s_tx_tail) return;
    /* 连续段长度（环形）*/
    uint16_t n = (s_tx_head > s_tx_tail)
               ? (uint16_t)(s_tx_head - s_tx_tail)
               : (uint16_t)(CON_TX_SIZE - s_tx_tail);
    HAL_UART_Transmit(s_uart, &s_tx_ring[s_tx_tail], n, 10u);
    s_tx_tail = (uint16_t)((s_tx_tail + n) & CON_TX_MASK);
}

/* ====== 日志 / printf 重定向 ====== */
void tcs_log(const char *prefix, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    int n = snprintf(buf, sizeof(buf), "%s ", prefix);
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - (size_t)n - 1, fmt, ap);
    va_end(ap);
    size_t len = strlen(buf);
    if (len < sizeof(buf) - 2) { buf[len++] = '\n'; buf[len] = '\0'; }
    SEGGER_RTT_WriteString(buf);
    tx_push((const uint8_t *)buf, len);
}

int _write(int fd, char *ptr, int len)
{
    (void)fd;
    if (len > 0) tx_push((const uint8_t *)ptr, (size_t)len);
    return len;
}

/* ====== RX ====== */
void console_isr_push(uint8_t b)
{
    uint16_t next = (uint16_t)((s_rx_head + 1u) & CON_RX_MASK);
    if (next == s_rx_tail) return;
    s_rx_ring[s_rx_head] = b;
    s_rx_head = next;
}

void console_rx_complete(void)
{
    console_isr_push(s_rx_byte);
    HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1);
}

void console_uart_error_recover(void)
{
    if (__HAL_UART_GET_FLAG(s_uart, UART_FLAG_ORE))
        __HAL_UART_CLEAR_OREFLAG(s_uart);
    HAL_UART_AbortReceive(s_uart);          /* 防 BUSY 导致重挂失败 */
    HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1);
}

/* ====== 命令执行 ====== */
static void cmd_exec(char *line)
{
    /* 去行尾空白 */
    size_t n = strlen(line);
    while (n && (line[n-1] == ' ' || line[n-1] == '\t')) line[--n] = '\0';
    if (n == 0) return;

    char *argv[6];
    int argc = 0;
    char *save = NULL;
    for (char *tok = strtok_r(line, " \t", &save);
         tok && argc < 6; tok = strtok_r(NULL, " \t", &save))
        argv[argc++] = tok;

    /* --- ver --- */
    if (strcmp(argv[0], "ver") == 0) {
        printf("tcs v%lu.%lu.%lu build=%s %s params=v1(7+0x100resv) proto=v%u\n",
               (unsigned long)(g_tcs.fw_ver >> 16) & 0xFFu,
               (unsigned long)(g_tcs.fw_ver >> 8) & 0xFFu,
               (unsigned long)(g_tcs.fw_ver) & 0xFFu,
               __DATE__, __TIME__, 1);
        return;
    }
    /* --- stat --- */
    if (strcmp(argv[0], "stat") == 0) {
        printf("tc1=%.1f tc2=%.1f body=%.1f tec1=%u tec2=%u fan=%u mode=%u "
               "alarm=0x%04x m207=%u sensor=%u esp=%u ctrl=%d drops=%lu\n",
               (double)g_tcs.tc1_act, (double)g_tcs.tc2_act, (double)g_tcs.body_temp,
               g_tcs.tec1_on, g_tcs.tec2_on, g_tcs.fan_on, g_tcs.mode,
               g_tcs.alarm_bits, g_tcs.m207_online, g_tcs.body_sensor_ok,
               g_tcs.esp_link_ok, (int)control_state(),
               (unsigned long)s_tx_drop_cnt);
        return;
    }
    /* --- p ... --- */
    if (strcmp(argv[0], "p") == 0 && argc >= 2) {
        if (strcmp(argv[1], "list") == 0) {
            for (int i = 0; i < param_count(); i++) {
                const param_desc_t *p = param_at(i);
                printf("0x%02x %s %.2f [%.1f,%.1f] 0x%02x\n",
                       p->id, p->key, (double)p->value,
                       (double)p->min, (double)p->max, p->flags);
            }
            return;
        }
        if (strcmp(argv[1], "set") == 0 && argc == 4) {
            uint16_t id = (uint16_t)strtoul(argv[2], NULL, 0);
            float v = strtof(argv[3], NULL);
            int rv = param_set(id, v, PARAM_SRC_CONSOLE);
            if (rv == 0)      printf("ok %.2f\n", (double)param_get(id));
            else if (rv == 1) printf("clamped %.2f\n", (double)param_get(id));
            else if (rv == -2) printf("rejected (body_cold<body_hot)\n");
            else              printf("bad id\n");
            return;
        }
        if (strcmp(argv[1], "save") == 0) {
            int rv = param_persist_now();
            printf("save rv=%d\n", rv);
            return;
        }
        if (strcmp(argv[1], "defaults") == 0) {
            if (argc == 3 && argv[2][0] == 'Y') {
                param_load_defaults();
                (void)param_persist_now();
                printf("defaults restored+saved\n");
            } else {
                printf("confirm: p defaults Y\n");
            }
            return;
        }
    }
    /* --- 控制命令（与 UI 同路径）--- */
    if (strcmp(argv[0], "mode") == 0 && argc == 2) {
        int rv = param_set(PARAM_ID_MODE, strtof(argv[1], NULL), PARAM_SRC_CONSOLE);
        printf("mode rv=%d m=%u\n", rv, g_tcs.mode);
        return;
    }
    if (strcmp(argv[0], "tec") == 0 && argc == 3) {
        int ch = atoi(argv[1]);
        int on = atoi(argv[2]);
        if (ch != 1 && ch != 2) { printf("ch 1|2\n"); return; }
        control_manual_tec((uint8_t)ch, on ? 1u : 0u);
        printf("tec%u=%u\n", (unsigned)ch, (unsigned)(on ? 1u : 0u));
        return;
    }
    if (strcmp(argv[0], "fan") == 0 && argc == 2) {
        control_manual_fan(atoi(argv[1]) ? 1u : 0u);
        printf("fan=%u\n", g_tcs.fan_on);
        return;
    }
    if (strcmp(argv[0], "estop") == 0) {
        tcs_emergency_stop();
        printf("ESTOP done alarm=0x%04x\n", g_tcs.alarm_bits);
        return;
    }
    if (strcmp(argv[0], "ack") == 0) {
        tcs_alarm_ack();
        printf("ack alarm=0x%04x\n", g_tcs.alarm_bits);
        return;
    }
    /* --- m207 send <raw> --- */
    if (strcmp(argv[0], "m207") == 0 && argc >= 3 && strcmp(argv[1], "send") == 0) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "%s\r", argv[2]);
        printf("m207 rv=%d\n", m207_send_raw(cmd));
        return;
    }
    /* --- touch cal a b c d（R4 现场重校接口）--- */
    if (strcmp(argv[0], "touch") == 0 && argc == 6 && strcmp(argv[1], "cal") == 0) {
        extern void ft5446_set_calibration(float a, float b, float c, float d);
        ft5446_set_calibration(strtof(argv[2], NULL), strtof(argv[3], NULL),
                               strtof(argv[4], NULL), strtof(argv[5], NULL));
        printf("touch cal set\n");
        return;
    }
    /* --- wdttest：死循环 3s 不喂狗 → IWDG 复位（M3/F5 实验命令）--- */
    if (strcmp(argv[0], "wdttest") == 0) {
        tcs_log("[SYSTEM]", "IWDG test: busy-loop 35s");
        uint32_t t0 = HAL_GetTick();
        while (HAL_GetTick() - t0 < 35000u) { }
        return;
    }

    printf("?\n");
}

/* ====== 主循环节拍 ====== */
void console_poll(void)
{
    tx_drain();

    /* RX 自愈：ORE 置位或 RXNEIE 失能（HAL 错误路径遗留）→ 恢复接收。
     * 症状：启动期线路扰动 → ORE → 中断被 HAL 关掉后控制台永久失聪。 */
    if (__HAL_UART_GET_FLAG(s_uart, UART_FLAG_ORE)
        || (s_uart->Instance->CR1 & USART_CR1_RXNEIE_RXFNEIE) == 0u) {
        console_uart_error_recover();
    }

    uint16_t guard = CON_RX_SIZE + 2u;
    uint8_t b;
    while (guard--) {
        if (s_rx_head == s_rx_tail) break;
        b = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1u) & CON_RX_MASK);
        if (b == '\r' || b == '\n') {
            if (s_line_len) {
                s_line[s_line_len] = '\0';
                cmd_exec(s_line);
                s_line_len = 0;
            }
        } else if (s_line_len < CON_LINE_MAX - 1) {
            s_line[s_line_len++] = (char)b;
        } else {
            s_line_len = 0;   /* 超长丢弃 */
        }
    }
}

void console_init(UART_HandleTypeDef *huart)
{
    s_uart = huart;
    s_rx_head = s_rx_tail = 0;
    s_tx_head = s_tx_tail = 0;
    s_tx_drop_cnt = 0;
    s_line_len = 0;
    HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1);
}

/* ====== 复位源解析（§3.9：复位后首行日志）====== */
void console_print_reset_source(void)
{
    uint32_t rs = RCC->RSR;
    const char *s = "unknown";
    if (rs & RCC_RSR_PINRSTF)        s = "PIN";
    else if (rs & RCC_RSR_BORRSTF)   s = "BOR";
    else if (rs & RCC_RSR_SFTRSTF)   s = "SOFT";
    else if (rs & RCC_RSR_IWDG1RSTF) s = "IWDG";
    else if (rs & RCC_RSR_WWDG1RSTF) s = "WWDG";
    else if (rs & RCC_RSR_LPWRRSTF)  s = "LPWR";
    tcs_log("[SYSTEM]", "boot reset-src=%s (RSR=0x%08lx)", s, (unsigned long)rs);
    __HAL_RCC_CLEAR_RESET_FLAGS();
}
