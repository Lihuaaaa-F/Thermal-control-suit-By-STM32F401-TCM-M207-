/**
 * console.c — USART1@115200 控制台实现（任务书 §3.8）
 *
 * printf 重定向：_write → TX 环形缓冲（满则丢弃+计数），主循环 console_poll
 * 里唯一 HAL_UART_Transmit 消费点（铁律：ISR 不发送）。日志同时进 RTT。
 * CLI 行缓冲 \r 或 \n 结尾；命令表见 §3.8。
 */
#include "console.h"
#include "ft5446.h"
#include "param_registry.h"
#include "proto.h"
#include "control.h"
#include "m207.h"
#include "post.h"
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

/* ====== touch log 分块 dump（WP-M0）======
 * 2 行/拍 ≈ 8KB/s < UART 11.5KB/s，防 TX 环（2KB）溢出丢行；
 * ftlog_at 老→新序输出，环形覆盖由 count/at 抽象消化。*/
static uint8_t s_dump_on;
static uint32_t s_dump_idx;
static void touch_log_dump_poll(void)
{
    if (!s_dump_on) return;
    extern uint32_t ftlog_count(void);
    extern const ftlog_ent_t *ftlog_at(uint32_t idx);
    extern uint16_t ft5446_fw_version(void);
    if (s_dump_idx == 0) {
        printf("# ft5446 ver=0x%04x map=GS_SWAP_V1\n", ft5446_fw_version());
        printf("seq,t_ms,n,xr,yr,ev,id,src\n");
    }
    uint32_t total = ftlog_count();
    for (int i = 0; i < 2 && s_dump_idx < total; i++, s_dump_idx++) {
        const ftlog_ent_t *e = ftlog_at(s_dump_idx);
        printf("%lu,%lu,%u,%d,%d,0x%02x,%u,%u\n",
               (unsigned long)s_dump_idx, (unsigned long)e->t_ms, e->n,
               (int)e->xr, (int)e->yr, e->ev, e->id, e->src & 1u);
    }
    if (s_dump_idx >= total) {
        printf("touch log dump done (%lu lines)\n", (unsigned long)total);
        s_dump_on = 0;
        s_dump_idx = 0;
    }
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
               "alarm=0x%04x m207=%u sensor=%u esp=%u ctrl=%d arm=%u(0x08=%u) "
               "loop=%lu drops=%lu\n",
               (double)g_tcs.tc1_act, (double)g_tcs.tc2_act, (double)g_tcs.body_temp,
               g_tcs.tec1_on, g_tcs.tec2_on, g_tcs.fan_on, g_tcs.mode,
               g_tcs.alarm_bits, g_tcs.m207_online, g_tcs.body_sensor_ok,
               g_tcs.esp_link_ok, (int)control_state(),
               (unsigned)post_arm_state(),
               (unsigned)param_get(PARAM_ID_COMMISSIONED),
               (unsigned long)g_stat_loop_passes,
               (unsigned long)s_tx_drop_cnt);
        return;
    }
    /* --- ee：EEPROM 镜像现场重读+校验（诊断）--- */
    if (strcmp(argv[0], "ee") == 0) {
        extern int eeprom_ready(void);
        extern int eeprom_read_image(uint8_t img[EE_IMG_SIZE]);
        uint8_t img[EE_IMG_SIZE];
        if (!eeprom_ready()) { printf("ee offline\n"); return; }
        if (eeprom_read_image(img) != 0) { printf("ee read fail\n"); return; }
        uint8_t crc = proto_crc8(img, EE_CRC_ADDR);
        printf("ee magic=%02x ver=%02x count=%d crc=%02x(calc %02x) f6=%02x %s\n",
               img[0], img[1], img[2], img[EE_CRC_ADDR], crc, img[EE_SELFTST_ADDR],
               (img[0] == EE_MAGIC_V2 && img[1] == EE_LAYOUT_VER && img[EE_CRC_ADDR] == crc)
                   ? "VALID" : "INVALID");
        return;
    }
    /* --- post：六项自检结果 + 布防三态（H6）--- */
    if (strcmp(argv[0], "post") == 0) {
        if (argc >= 2 && strcmp(argv[1], "badge") == 0) {
            extern void post_badge_reshow(uint32_t ms);
            uint32_t ms = (argc >= 3) ? (uint32_t)strtoul(argv[2], NULL, 0) : 0u;
            post_badge_reshow(ms);
            printf("post badge reshow %lu ms\n",
                   (unsigned long)(ms ? ms : 3000u));
            return;
        }
        static const char *item_name[POST_ITEM_N] = {
            "TMP117", "M207", "EEPROM", "TABLE", "SCRTOUCH", "VER"
        };
        static const char *arm_name[3] = { "UNARMED_ABSENT", "ARMED_ONLINE", "LOST_ALARM" };
        printf("done=%u pass=%u result=0x%02x decided=0x%02x arm=%s badge=%u\n",
               post_done(), post_all_pass(),
               (unsigned)post_result_mask(), (unsigned)post_done_mask(),
               arm_name[(unsigned)post_arm_state() < 3 ? (unsigned)post_arm_state() : 0],
               post_badge_state());
        for (int i = 0; i < POST_ITEM_N; i++) {
            printf("  %-8s %s\n", item_name[i],
                   !post_item_decided((uint8_t)i) ? "WAIT" :
                   post_item_ok((uint8_t)i) ? "PASS" : "FAIL");
        }
        return;
    }
    /* --- perf：DWT 实测（WP-C.5）--- */
    if (strcmp(argv[0], "perf") == 0) {
        uint32_t up_s = HAL_GetTick() / 1000u;
        uint32_t rate = up_s ? (g_stat_loop_passes / up_s) : 0;
        /* v9.5 K3：hero/page/flips/patches 四计数入 perf（预算表回填口）*/
        printf("work avg=%luus max=%luus ovf=%lu | ui_full=%lu.%lu ms | "
               "touch max=%luus gap=%lums | loop=%lu rate=%lu/s | "
               "hero_max=%luus page=%luus flips=%lu patches=%lu "
               "touch_hz=%lu | up=%lus\n",
               (unsigned long)g_stat_work_us_avg, (unsigned long)g_stat_work_us_max,
               (unsigned long)g_stat_tick_ovf,
               (unsigned long)(g_stat_ui_ms_x10 / 10u),
               (unsigned long)(g_stat_ui_ms_x10 % 10u),
               (unsigned long)g_stat_touch_us_max,
               (unsigned long)g_stat_touch_gap_ms_max,
               (unsigned long)g_stat_loop_passes, (unsigned long)rate,
               (unsigned long)g_stat_hero_us_max, (unsigned long)g_stat_full_us_last,
               (unsigned long)g_stat_flip_cnt, (unsigned long)g_stat_patch_cnt,
               (unsigned long)g_stat_touch_hz, (unsigned long)up_s);
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
    /* --- touch log on|off|clear|stat|dump（WP-M0：幽灵/真击分布取证）--- */
    if (strcmp(argv[0], "touch") == 0 && argc >= 2 && strcmp(argv[1], "log") == 0) {
        extern void ftlog_clear(void);
        extern void ftlog_enable(uint8_t on);
        extern uint32_t ftlog_count(void);
        extern uint16_t ft5446_fw_version(void);
        extern volatile uint32_t g_ft5446_badsample_drops;
        extern volatile uint32_t g_ft5446_nak_cnt;
        extern volatile uint32_t g_ft5446_read_cnt;
        extern volatile uint32_t g_ts_ghost_sessions;
        extern volatile uint32_t g_ts_multipoint_drops;
        extern volatile uint32_t g_ts_badsample_drops;
        extern volatile uint32_t g_ts_jump_drops;
        extern volatile uint32_t g_ts_glitch_resume;
        if (argc == 3 && strcmp(argv[2], "on") == 0) {
            ftlog_enable(1);
            printf("touch log on\n");
        } else if (argc == 3 && strcmp(argv[2], "off") == 0) {
            ftlog_enable(0);
            printf("touch log off\n");
        } else if (argc == 3 && strcmp(argv[2], "clear") == 0) {
            ftlog_clear();
            printf("touch log clear\n");
        } else if (argc == 3 && strcmp(argv[2], "stat") == 0) {
            /* T-11：IC 固件版本入日志头 */
            printf("# ft5446 ver=0x%04x ready=%d\n",
                   ft5446_fw_version(), touch_ready());
            printf("log count=%lu\n", (unsigned long)ftlog_count());
            printf("drv badsample=%lu nak=%lu read=%lu nak_pct=%lu%%\n",
                   (unsigned long)g_ft5446_badsample_drops,
                   (unsigned long)g_ft5446_nak_cnt,
                   (unsigned long)g_ft5446_read_cnt,
                   (unsigned long)(g_ft5446_read_cnt
                                   ? (g_ft5446_nak_cnt * 100u / g_ft5446_read_cnt) : 0u));
            {
                char dbg[128];
                extern void tcs_ui_touch_debug(char *out, int cap);
                tcs_ui_touch_debug(dbg, (int)sizeof(dbg));
                printf("%s\n", dbg);
            }
            printf("sess ghost=%lu multipoint=%lu badsample=%lu jump=%lu glitch_resume=%lu\n",
                   (unsigned long)g_ts_ghost_sessions,
                   (unsigned long)g_ts_multipoint_drops,
                   (unsigned long)g_ts_badsample_drops,
                   (unsigned long)g_ts_jump_drops,
                   (unsigned long)g_ts_glitch_resume);
        } else if (argc == 3 && strcmp(argv[2], "dump") == 0) {
            s_dump_on = 1;   /* console_poll 分块出 CSV（2 行/拍，防 TX 环溢出丢数据）*/
        } else {
            printf("usage: touch log on|off|clear|stat|dump\n");
        }
        return;
    }
    /* --- touch diag：配置寄存器回读（M0/M1 数据通路健康判据）--- */
    if (strcmp(argv[0], "touch") == 0 && argc == 2 && strcmp(argv[1], "diag") == 0) {
        extern void ft5446_diag_readback(void);
        ft5446_diag_readback();
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
            } else if (strcmp(argv[2], "drop") == 0 && argc == 4) {
                extern void m207_sim_drop(uint8_t pct);
                m207_sim_drop((uint8_t)atoi(argv[3]));
            }
            printf("sim m207 %s\n", argv[2]);
        } else if (strcmp(argv[1], "touch") == 0 && argc == 4) {
            extern void tcs_ui_inject_tap(int x, int y);
            tcs_ui_inject_tap(atoi(argv[2]), atoi(argv[3]));
            printf("sim touch %s %s\n", argv[2], argv[3]);
        } else if (strcmp(argv[1], "touchh") == 0 && argc == 4) {
            extern void tcs_ui_inject_touch(int down, int x, int y);
            tcs_ui_inject_touch(1, atoi(argv[2]), atoi(argv[3]));
            printf("sim touchh %s %s (hold)\n", argv[2], argv[3]);
        } else if (strcmp(argv[1], "touchr") == 0) {
            extern void tcs_ui_inject_touch(int down, int x, int y);
            tcs_ui_inject_touch(0, 0, 0);
            printf("sim touchr (release)\n");
        } else if (strcmp(argv[1], "touchraw") == 0 && argc == 4) {
            /* T-13：raw 级注入——携带 raw 坐标走完整变换链（映射→会话机→UI）*/
            extern void ft5446_raw_inject(int down, uint16_t xr, uint16_t yr);
            ft5446_raw_inject(1, (uint16_t)strtoul(argv[2], NULL, 0),
                              (uint16_t)strtoul(argv[3], NULL, 0));
            printf("sim touchraw %s %s\n", argv[2], argv[3]);
        } else if (strcmp(argv[1], "touchrawr") == 0) {
            extern void ft5446_raw_inject(int down, uint16_t xr, uint16_t yr);
            ft5446_raw_inject(0, 0, 0);
            printf("sim touchrawr (release)\n");
        } else if (strcmp(argv[1], "arm") == 0) {
            (void)param_set(PARAM_ID_COMMISSIONED, 1.0f, PARAM_SRC_CONSOLE);
            printf("sim armed=%u\n", (unsigned)param_get(PARAM_ID_COMMISSIONED));
        } else if (strcmp(argv[1], "post") == 0 && argc == 4 &&
                   strcmp(argv[2], "fail") == 0) {
            /* 强制单项 FAIL（角标 ✗黄 + auto 不布防演示/取证）*/
            extern void post_sim_fail_item(uint8_t item);
            int it = atoi(argv[3]);
            post_sim_fail_item((uint8_t)it);
            printf("sim post item %d forced FAIL\n", it);
        } else {
            printf("usage: sim body <val|off>|m207 <fault|recover|wrongecho>|"
                   "touch <x> <y>|arm|post fail <0-5>\n");
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
    touch_log_dump_poll();   /* WP-M0：分块 CSV 导出（每拍续传）*/

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
