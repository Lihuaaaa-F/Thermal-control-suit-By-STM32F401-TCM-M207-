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
#include "ltdc.h"
#include "tcs_ui.h"
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
    if (len > 0) {
        tx_push((const uint8_t *)ptr, (size_t)len);
        SEGGER_RTT_Write(ptr, (unsigned)len);   /* printf 镜像 RTT（无 COM 线观测）*/
    }
    return len;
}

/* ====== 主机邮箱命令注入（WP-D：无 COM/USB-TTL 时的控制台入口）======
 * 主机 probe-rs write RAM：先写 s_mb_line（含 '\0'），再写 s_mb_magic=MB_MAGIC_RUN；
 * console_poll 检测到即执行并 s_mb_seq++。读 s_mb_seq 确认消费。 */
#define MB_MAGIC_RUN  0x54534D43u   /* 'CMST' */
static volatile uint32_t s_mb_magic;
static volatile uint32_t s_mb_seq;
static char s_mb_line[CON_LINE_MAX];

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
        printf("tcs v%lu.%lu.%lu build=%s %s params=v1(8+0x100resv) proto=v%u\n",
               (unsigned long)(g_tcs.fw_ver >> 16) & 0xFFu,
               (unsigned long)(g_tcs.fw_ver >> 8) & 0xFFu,
               (unsigned long)(g_tcs.fw_ver) & 0xFFu,
               __DATE__, __TIME__, 1);
        return;
    }
    /* --- stat --- */
    if (strcmp(argv[0], "stat") == 0) {
        printf("tc1=%.1f tc2=%.1f body=%.1f tec1=%u tec2=%u fan=%u mode=%u "
               "alarm=0x%04x m207=%u sensor=%u esp=%u ctrl=%d arm=%u loop=%lu drops=%lu\n",
               (double)g_tcs.tc1_act, (double)g_tcs.tc2_act, (double)g_tcs.body_temp,
               g_tcs.tec1_on, g_tcs.tec2_on, g_tcs.fan_on, g_tcs.mode,
               g_tcs.alarm_bits, g_tcs.m207_online, g_tcs.body_sensor_ok,
               g_tcs.esp_link_ok, (int)control_state(),
               (unsigned)param_get(PARAM_ID_COMMISSIONED),
               (unsigned long)g_stat_loop_passes,
               (unsigned long)s_tx_drop_cnt);
        return;
    }
    /* --- perf：DWT 实测（WP-C.5）--- */
    if (strcmp(argv[0], "perf") == 0) {
        uint32_t up_s = HAL_GetTick() / 1000u;
        uint32_t rate = up_s ? (g_stat_loop_passes / up_s) : 0;
        printf("work avg=%luus max=%luus ovf=%lu | ui_full=%lu.%lu ms | "
               "touch max=%luus | loop=%lu rate=%lu/s up=%lus\n",
               (unsigned long)g_stat_work_us_avg, (unsigned long)g_stat_work_us_max,
               (unsigned long)g_stat_tick_ovf,
               (unsigned long)(g_stat_ui_ms_x10 / 10u),
               (unsigned long)(g_stat_ui_ms_x10 % 10u),
               (unsigned long)g_stat_touch_us_max,
               (unsigned long)g_stat_loop_passes, (unsigned long)rate,
               (unsigned long)up_s);
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
    /* --- wdttest：死循环不喂狗 → IWDG 2s 复位（WP-C.1 实验命令）--- */
    if (strcmp(argv[0], "wdttest") == 0) {
        tcs_log("[SYSTEM]", "IWDG test: busy-loop 5s (timeout=2s)");
        uint32_t t0 = HAL_GetTick();
        while (HAL_GetTick() - t0 < 5000u) { }
        return;
    }
    /* --- scrtest：屏诊断（WP-A.2d 噪声二分：纯 DMA2D 清屏/标定文本/触摸计数）--- */
    if (strcmp(argv[0], "scrtest") == 0 && argc >= 2) {
        if (strcmp(argv[1], "clear") == 0) {
            tcs_ui_screen_clear(RGB565(0x10, 0x14, 0x18));
            printf("scrtest clear done taps=%lu\n", (unsigned long)tcs_ui_tap_count());
        } else if (strcmp(argv[1], "fill") == 0 && argc == 3) {
            uint16_t c = (uint16_t)strtoul(argv[2], NULL, 16);
            tcs_ui_screen_clear(c);
            printf("scrtest fill 0x%04x done taps=%lu\n", c, (unsigned long)tcs_ui_tap_count());
        } else if (strcmp(argv[1], "text") == 0) {
            tcs_ui_screen_text();
            printf("scrtest text done taps=%lu\n", (unsigned long)tcs_ui_tap_count());
        } else if (strcmp(argv[1], "home") == 0) {
            tcs_ui_force_home();
            printf("scrtest home done taps=%lu\n", (unsigned long)tcs_ui_tap_count());
        } else if (strcmp(argv[1], "tune") == 0) {
            tcs_ui_force_tune();
            printf("scrtest tune done taps=%lu\n", (unsigned long)tcs_ui_tap_count());
        } else if (strcmp(argv[1], "stress") == 0 && argc == 4) {
            int mode = atoi(argv[2]);
            int n = atoi(argv[3]);
            tcs_ui_screen_stress(mode, n);
            printf("scrtest stress mode=%d n=%d done\n", mode, n);
        } else {
            printf("usage: scrtest clear|fill <565hex>|text|home|stress <mode> <n>\n");
        }
        return;
    }

#ifdef TCS_SIM
    /* --- sim：板内仿真注入（WP-G；仅 TCS_SIM 构建）--- */
    if (strcmp(argv[0], "sim") == 0 && argc >= 2) {
        if (strcmp(argv[1], "body") == 0 && argc == 3) {
            extern void tmp117_sim_set(float v);
            extern void tmp117_sim_disable(void);
            if (strcmp(argv[2], "off") == 0) tmp117_sim_disable();
            else tmp117_sim_set(strtof(argv[2], NULL));
            printf("sim body %s\n", argv[2]);
        } else if (strcmp(argv[1], "m207") == 0 && argc == 3) {
            if (strcmp(argv[2], "fault") == 0) {
                extern void m207_sim_fault(void);
                m207_sim_fault();
            } else if (strcmp(argv[2], "recover") == 0) {
                extern void m207_sim_recover(void);
                m207_sim_recover();
            } else if (strcmp(argv[2], "wrongecho") == 0) {
                extern void m207_sim_wrong_echo(void);
                m207_sim_wrong_echo();
            }
            printf("sim m207 %s\n", argv[2]);
        } else if (strcmp(argv[1], "touch") == 0 && argc == 4) {
            extern void tcs_ui_inject_tap(int x, int y);
            tcs_ui_inject_tap(atoi(argv[2]), atoi(argv[3]));
            printf("sim touch %s %s\n", argv[2], argv[3]);
        } else if (strcmp(argv[1], "arm") == 0) {
            (void)param_set(PARAM_ID_COMMISSIONED, 1.0f, PARAM_SRC_CONSOLE);
            printf("sim armed=%u\n", (unsigned)param_get(PARAM_ID_COMMISSIONED));
        } else {
            printf("usage: sim body <val|off>|m207 <fault|recover|wrongecho>|"
                   "touch <x> <y>|arm\n");
        }
        return;
    }
#endif
    printf("?\n");
}

/* ====== 主循环节拍 ====== */
void console_poll(void)
{
    tx_drain();

    /* 主机邮箱命令注入（probe-rs 写 RAM 触发，先于 UART 行处理）*/
    if (s_mb_magic == MB_MAGIC_RUN) {
        s_mb_magic = 0;
        s_mb_line[CON_LINE_MAX - 1] = '\0';
        cmd_exec(s_mb_line);
        s_mb_seq++;
        tcs_log("[MB]", "done seq=%lu", (unsigned long)(s_mb_seq + 1));
    }

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

/* ====== 复位源解析（§3.9：复位后首行日志）======
 * WP-C.4：IWDG1RSTF 优先于 PIN——IWDG 复位时 NRST 也被拉低，PINRSTF 同样置位，
 * 按 PIN 优先会误报。其余位照常见义。 */
void console_print_reset_source(void)
{
    uint32_t rs = RCC->RSR;
    const char *s = "unknown";
    if (rs & RCC_RSR_IWDG1RSTF)      s = "IWDG";
    else if (rs & RCC_RSR_WWDG1RSTF) s = "WWDG";
    else if (rs & RCC_RSR_SFTRSTF)   s = "SOFT";
    else if (rs & RCC_RSR_BORRSTF)   s = "BOR";
    else if (rs & RCC_RSR_PINRSTF)   s = "PIN";
    else if (rs & RCC_RSR_LPWRRSTF)  s = "LPWR";
    tcs_log("[SYSTEM]", "boot reset-src=%s (RSR=0x%08lx)", s, (unsigned long)rs);
    __HAL_RCC_CLEAR_RESET_FLAGS();
}
