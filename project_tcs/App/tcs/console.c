/**
 * console.c — USART1@115200 控制台实现（任务书 §3.8）
 *
 * printf 重定向：_write → TX 环形缓冲（满则丢弃+计数），主循环 console_poll
 * 里唯一 HAL_UART_Transmit 消费点（铁律：ISR 不发送）。日志同时进 RTT。
 * CLI 行缓冲 \r 或 \n 结尾；命令表见 §3.8。
 */
#include "console.h"
#include "build_info.h"      /* 构建时间与提交号：CMake 生成在构建目录的 generated/ 下 */
#include "ft5446.h"
#include "param_registry.h"
#include "comm.h"       /* comm_link_t：stat 里报屏链回声 / 方向不符计数（R5-01）*/
#include "proto.h"
#include "tcs_proto.h"  /* TCS_PROTO_VER：版本串里的协议版本（2026-11-26 之前写死成 1）*/
#include "control.h"
#include "m207.h"
#include "link_snap.h"   /* S 线 i3（条目 11）：stat 的 vr=（版本拒收计数）*/
#include "link_send.h"   /* S 线 e1（X01-F-03）：stat 的参数写入应答结果计数（wbad/wclamp/wbusy）*/
#include "m207_txq.h"   /* M207_CMD_MAX：m207 send 的长度上限（M07-F-10）*/
#include "audit_ring.h" /* M07-F-08：关键命令的审计留痕环 */
#include "perf_hist.h"  /* M01-F-09：perf 读最近一个 1 秒窗口 */
#include "post.h"
#include "segger_rtt.h"
#include "ltdc.h"
#include "tcs_lvgl.h"   /* g_tap_inject：UI 单栈后的触摸注入钩子 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define CON_RX_SIZE   256u
#define CON_RX_MASK   (CON_RX_SIZE - 1u)
#define CON_TX_SIZE   2048u
#define CON_TX_MASK   (CON_TX_SIZE - 1u)
#define CON_LINE_MAX  96

/* 构建档位（R1 修复 P0-03 / X05-F-03）：0=PROD 1=BENCH 2=SIM。
 * 驱动/波形/普查类命令只存在于 BENCH 及以上；PROD 产物不编译其实现体（S10）。
 * 由 CMake 的 TCS_PROFILE 注入；未定义时按 PROD 处理（最严）。 */
#ifndef TCS_PROFILE
#define TCS_PROFILE 0
#endif

static UART_HandleTypeDef *s_uart;
static uint8_t s_rx_byte;
static volatile uint16_t s_rx_head, s_rx_tail;
static uint8_t s_rx_ring[CON_RX_SIZE];

static volatile uint16_t s_tx_head, s_tx_tail;
static uint8_t s_tx_ring[CON_TX_SIZE];
static volatile uint32_t s_tx_drop_cnt;

static char s_line[CON_LINE_MAX];
static uint16_t s_line_len;
static uint8_t  s_line_discard;   /* R1 修复 M07-F-06：超长行丢弃至行尾，不执行残片 */

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
    /* 分块发送：115200 下 10ms 只能发 ~115B，整段一次发会让 HAL 超时后
     * 仍推进 tail → 批量输出（p list 等）被静默截断。
     * 2026-10-30 真机实证：50 条参数表只出来 7~18 行。*/
    while (n) {
        uint16_t chunk = (n > 64u) ? 64u : n;
        if (HAL_UART_Transmit(s_uart, &s_tx_ring[s_tx_tail], chunk, 50u) != HAL_OK)
            break;                      /* 发送失败：保留 tail 下次重试 */
        s_tx_tail = (uint16_t)((s_tx_tail + chunk) & CON_TX_MASK);
        n = (uint16_t)(n - chunk);
        /* M03-F-04：环里连着有 2KB 时这一次要连发 32 块（约 180ms）——每发完一块看一眼屏上的急停入口。*/
        console_estop_yield();
    }
}

/* ====== 阻塞型命令让位给急停兜底入口（M03-F-04，2026-11-25，S-G l1）======
 * 主循环每 10ms 看一次屏上的急停入口（tcs_lvgl_estop_watch）。一条命令把主循环占住的时候
 * （pinscan 最长 1s、wdttest 5s、wave 250ms、p list 的串口排空…），这个入口在这段时间里没人看：
 * 审查条目 M03-F-04 测到急停被延迟 3–5s，而屏入口的门槛是 ≤500ms。
 * 这里让这些循环体每 CONSOLE_ESTOP_YIELD_MS 回到同一个入口。
 * 交付档里没有阻塞型台架命令（C01），但 p list 三档都在，对它同样适用。
 * 注意：阻塞期间 LVGL 没被轮询、心跳不前进——这时按在首页急停键上，estop_gate 会按"LVGL 卡死"
 * 立即触发、不弹确认。方向是安全的（宁停勿漏），而且这些命令只在台架及以上档。*/
#if TCS_USE_LVGL
#define CONSOLE_ESTOP_YIELD_MS  10u
void console_estop_yield(void)
{
    static uint32_t s_last;
    uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_last) < CONSOLE_ESTOP_YIELD_MS) return;
    s_last = now;
    tcs_lvgl_estop_watch();
}
#else
void console_estop_yield(void) { }        /* 不带屏的构建：没有屏入口 */
#endif

/* ====== 日志 / printf 重定向 ====== */
void tcs_log(const char *prefix, const char *fmt, ...)
{
    char buf[160];
    va_list ap;
    int n = snprintf(buf, sizeof(buf), "%s ", prefix);
    /* R1 修复 M07-F-09：snprintf 截断/失败时 n 可能 ≥sizeof(buf) 或 <0，buf+n 会越界。
     * 截断就从头覆写，宁可丢 prefix 也不能越界。*/
    if (n < 0 || (size_t)n >= sizeof(buf)) n = 0;
    va_start(ap, fmt);
    vsnprintf(buf + n, sizeof(buf) - (size_t)n, fmt, ap);
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
 * 主机 probe-rs 写 RAM，布局 v2（M07-F-07；tools/mb_console.py 同提交对齐）：
 *   +0  magic (u32, 'CMST')  ← 最后写：写完即代表本条记录完整
 *   +4  len   (u32)          ← cmd 有效字节数 1..96
 *   +8  crc16 (u32, 低 16 位有效) ← CRC-16/CCITT-FALSE(init 0xFFFF) 覆盖 cmd[0..len)
 *   +12 cmd[96]
 * 消费时先校验 len 与 CRC 再执行；不合法只计数不执行（坏记录绝不能被当成命令）。
 * 校验通过后 s_mb_seq++，主机轮询该值确认消费。 */
#define MB_MAGIC_RUN  0x54534D43u   /* 'CMST' */
#define MB_CMD_MAX    96
typedef struct {
    volatile uint32_t magic;
    volatile uint32_t len;
    volatile uint32_t crc16;
    char cmd[MB_CMD_MAX];
} mb_mailbox_t;
static mb_mailbox_t s_mb;
static volatile uint32_t s_mb_seq;
static uint32_t s_mb_bad;           /* 被拒记录数（拒绝时打 [MB] reject，累计计数）*/

/* CRC-16/CCITT-FALSE：poly 0x1021 / init 0xFFFF / 不反射 / 末不异或。
 * 已知向量 "123456789" → 0x29B1（mb_console.py 内有同值断言）。*/
static uint16_t mb_crc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0xFFFFu;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int b = 0; b < 8; b++) {
            uint16_t sh = (uint16_t)(crc << 1);
            if (crc & 0x8000u) sh ^= 0x1021u;
            crc = sh;
        }
    }
    return crc;
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

/* ====== 审计留痕（M07-F-08，2026-11-25，S-G l1）======
 * 改参数 / 改模式 / 驱动引脚 / 急停与确认 / 仿真注入 / 复位这类命令，执行后记进一块**独立的**
 * 512 字节环形缓冲（不走控制台 TX 环，也不占 RTT 缓冲），同时往 RTT 打一行 [AUDIT]。
 * 判定与环在 audit_ring.c（纯逻辑，主机可测）；这里只做接入。三档都保留，`audit` 命令只读回放。*/
static audit_ring_t s_audit;
static char s_audit_text[AUDIT_CMD_MAX + 1u];   /* 本条命令的留痕文本；空串 = 这条不留 */
static int  s_cmd_rv;                            /* 本条命令的返回值：未知命令 -1，p set / m207 send 取其实际返回值 */

/* 命令执行完之后调：把这条（如果要留）写进环并打到 RTT */
static void audit_commit(void)
{
    uint32_t now = HAL_GetTick();
    char ln[96];
    if (!s_audit_text[0]) return;
    (void)audit_ring_add(&s_audit, now, s_audit_text, s_cmd_rv);
    snprintf(ln, sizeof(ln), "[AUDIT] %lu %s rv=%d\n",
             (unsigned long)now, s_audit_text, s_cmd_rv);
    SEGGER_RTT_WriteString(ln);
    s_audit_text[0] = '\0';
}

/* ====== 命令执行 ====== */
static void cmd_exec(char *line)
{
    /* 去行尾空白 */
    size_t n = strlen(line);
    while (n && (line[n-1] == ' ' || line[n-1] == '\t')) line[--n] = '\0';
    if (n == 0) return;

    /* M07-F-08：strtok_r 会把 line 就地切碎，留痕用的原文要在它之前抄一份 */
    char raw[CON_LINE_MAX];
    strncpy(raw, line, sizeof(raw) - 1);
    raw[sizeof(raw) - 1] = '\0';

    char *argv[6];
    int argc = 0;
    char *save = NULL;
    for (char *tok = strtok_r(line, " \t", &save);
         tok && argc < 6; tok = strtok_r(NULL, " \t", &save))
        argv[argc++] = tok;

    /* M07-F-08：关键命令留痕。这里只判断并抄原文；写环在执行之后（audit_commit），带返回值。
     * 交付档上被档位挡掉、但仍在审计表里的关键命令（p defaults / p save）会留一条 rv=-1——
     * 有人在交付固件上试过什么，事后看得到。台架专用的命令（pindrv / mode …）在交付档不进审计表
     * （命令名不能出现在交付档产物里），试敲它们不留痕。*/
    s_cmd_rv = 0;
    s_audit_text[0] = '\0';
    if (audit_is_critical(argv[0], argc >= 2 ? argv[1] : NULL))
        audit_cmd_text(s_audit_text, sizeof(s_audit_text), raw);

    /* --- ver --- */
    if (strcmp(argv[0], "ver") == 0) {
        /* 构建时间与提交号来自 CMake 生成的 build_info.h（可复现构建，审查条目 X05-F-06）：
         * 时间是提交时间（或 SOURCE_DATE_EPOCH），不是按下编译键的时间；工作树有未提交改动时提交号带 -dirty。
         * 协议版本取 TCS_PROTO_VER：2026-11-26 之前这里写死成 1，协议抬到 2 之后版本串还在报 1（D7 第 ⓪ 步发现）。 */
        printf("tcs v%lu.%lu.%lu build=%s git=%s params=v1(8+0x100resv) proto=v%u\n",
               (unsigned long)(g_tcs.fw_ver >> 16) & 0xFFu,
               (unsigned long)(g_tcs.fw_ver >> 8) & 0xFFu,
               (unsigned long)(g_tcs.fw_ver) & 0xFFu,
               TCS_BUILD_STAMP, TCS_BUILD_GIT, (unsigned)TCS_PROTO_VER);
        return;
    }
    /* --- audit：审计留痕回放（只读；三档都在，M07-F-08）---
     * 环里是从旧到新的关键命令（时间戳 ms + 命令原文 + 返回值），最多 16 条；total 是开机以来的总条数。*/
    if (strcmp(argv[0], "audit") == 0) {
        static char buf[AUDIT_SLOT_BYTES * AUDIT_SLOT_COUNT + 1u];   /* 不占主栈：513 字节 */
        int shown = audit_ring_format(&s_audit, buf, sizeof(buf));
        printf("audit: total=%lu kept=%u shown=%d\n",
               (unsigned long)s_audit.total, (unsigned)s_audit.count, shown);
        printf("%s", buf);
        tx_drain();                          /* 16 行约 0.6KB：一次排掉，免得和后面的输出挤 TX 环 */
        return;
    }
    /* --- fault：重打上一次 HardFault 现场（启动那一行只有 RTT 开着才看得到；2026-11-26，S 线 C06）--- */
    if (strcmp(argv[0], "fault") == 0) { console_print_fault_record(); return; }
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
        /* 屏链诊断（2026-10-30）：RX 字节总量+快照帧计数（ltx 自检核对；全 0=方向不通）
         * echo / wdir（2026-11-25，R5-01）：屏发出的帧被回送、按方向位丢弃的帧数（只在接线/串扰
         * 异常时增长）；方向位说是主控、但主控不会发的消息号（坏帧或两端版本不匹配）。*/
        extern volatile uint32_t g_dbg_lrx_total;
        extern volatile uint32_t g_dbg_snap;
        extern volatile uint32_t g_dbg_snap_gap_max;
        extern volatile uint32_t g_dbg_snap_gap_last;
        extern comm_link_t g_link;
        /* arq / txfail（M04-F-06 / X03-F-09）：comm 层 link_ok；没能上线的发送次数（应与 uartdrop 同源、恒 0）*/
        /* vr（S 线 i3，条目 11）：被版本校验拒掉的快照数——>0 且屏上一直"连接中断"时，
         * 多半是两端协议版本不配对（主控 OTA 升过、屏还没烧）。*/
        /* ackbad / wbad / wclamp / wbusy（S 线 e1，X01-F-03）：0x90 应答里 status != OK 的计数；
         * 其中按参数写入分类的三种（BAD_ID / CLAMP / BUSY）单独列出——非 0 说明屏写参数
         * 被主控拒绝或限幅，`[WARN] param write rejected ...` 那条日志里有 id 与值。*/
        uint32_t wbad = 0, wclamp = 0, wbusy = 0;
        link_send_ack_stats(&wbad, &wclamp, &wbusy);
        /* fresh / bdg / stl（S 线 e1，X01-F-04）：主控快照里的 M207 温度数据龄、布防角标，
         * 以及"现在算不算陈旧"（fresh_ms 0xFFFF / 超 3s，或快照超过 2s 没来）。*/
        extern void tcs_link_fresh_copy(tcs_link_fresh_t *out);
        tcs_link_fresh_t fr; tcs_link_fresh_copy(&fr);
        uint32_t snap_age = 0, snap_seq = 0; uint8_t snap_have = 0;
        tcs_link_snap_info(&snap_age, &snap_seq, &snap_have);
        printf("lrx=%lu snap=%lu gapmax=%lu gaplast=%lu echo=%lu wdir=%lu arq=%u txfail=%lu vr=%lu "
               "ackbad=%lu wbad=%lu wclamp=%lu wbusy=%lu fresh=%u bdg=%u stl=%u\n",
               (unsigned long)g_dbg_lrx_total,
               (unsigned long)g_dbg_snap,
               (unsigned long)g_dbg_snap_gap_max,
               (unsigned long)g_dbg_snap_gap_last,
               (unsigned long)g_link.echo_cnt, (unsigned long)g_link.wrongdir_cnt,
               (unsigned)g_link.link_ok, (unsigned long)g_link.tx_fail_cnt,
               (unsigned long)tcs_link_snap_ver_rejects(),
               (unsigned long)g_link.ack_nonok_cnt,
               (unsigned long)wbad, (unsigned long)wclamp, (unsigned long)wbusy,
               (unsigned)fr.fresh_ms, (unsigned)fr.badge,
               (unsigned)tcs_link_stale_at(fr.fresh_ms, snap_age, fr.have));
        return;
    }
#if TCS_PROFILE >= 1
    /* --- txwave：PD5 发送波形自证（64B 0x55 + 边沿计数）--- */
    if (strcmp(argv[0], "txwave") == 0) {
        extern void link_tx_wave_capture(void);
        extern volatile uint32_t g_txwave_n;
        link_tx_wave_capture();
        printf("txwave n=%lu\n", (unsigned long)g_txwave_n);
        return;
    }
    /* --- pindrv/pinrel：引脚探针（BENCH 档；安全引脚黑名单）---
     * P0-03：原实现无白名单，`pindrv A 1 0` 可把 PA1(KEY0 急停键) 改推挽拉低，
     * 使 kill_key_poll 恒读 0、急停键失效。此处拒绝一切安全相关引脚，
     * 并按 S1 改用 HAL 时钟宏（原 RCC->AHB4ENR 裸写）。*/
    if ((strcmp(argv[0], "pindrv") == 0 && argc == 4) ||
        (strcmp(argv[0], "pinrel") == 0 && argc == 3)) {
        GPIO_TypeDef *ports[11] = { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF,
                                    GPIOG, GPIOH, GPIOI, GPIOJ, GPIOK };
        char pc = argv[1][0];
        char up = (pc >= 'a' && pc <= 'z') ? (char)(pc - 32) : pc;
        int pi = (pc >= 'a' && pc <= 'k') ? (pc - 'a') : (pc - 'A');
        int pn = atoi(argv[2]);
        if (pi < 0 || pi > 10 || pn < 0 || pn > 15) { printf("bad port/pin\n"); return; }
        static const struct { char port; uint8_t pin; const char *why; } deny[] = {
            { 'A',  1, "KEY0 estop" }, { 'A',  3, "esp link RX" },
            { 'D',  5, "esp link TX" },
            /* R1 复核 R-11：PB10/PB11 是 USART3(M207)，PB8/PB9 才是 I2C1(TMP117)。*/
            { 'B', 10, "m207 uart TX" }, { 'B', 11, "m207 uart RX" },
            { 'B',  8, "tmp117 i2c scl" }, { 'B', 9, "tmp117 i2c sda" },
            { 'B', 14, "fan" },
        };
        for (unsigned k = 0; k < sizeof(deny) / sizeof(deny[0]); k++) {
            if (up == deny[k].port && pn == deny[k].pin) {
                printf("pindrv: P%c%d safety-critical (%s), refused\n",
                       deny[k].port, pn, deny[k].why);
                return;
            }
        }
        switch (pi) {
        case 0: __HAL_RCC_GPIOA_CLK_ENABLE(); break;
        case 1: __HAL_RCC_GPIOB_CLK_ENABLE(); break;
        case 2: __HAL_RCC_GPIOC_CLK_ENABLE(); break;
        case 3: __HAL_RCC_GPIOD_CLK_ENABLE(); break;
        case 4: __HAL_RCC_GPIOE_CLK_ENABLE(); break;
        case 5: __HAL_RCC_GPIOF_CLK_ENABLE(); break;
        case 6: __HAL_RCC_GPIOG_CLK_ENABLE(); break;
        case 7: __HAL_RCC_GPIOH_CLK_ENABLE(); break;
        case 8: __HAL_RCC_GPIOI_CLK_ENABLE(); break;
        case 9: __HAL_RCC_GPIOJ_CLK_ENABLE(); break;
        default: __HAL_RCC_GPIOK_CLK_ENABLE(); break;
        }
        GPIO_InitTypeDef gi = {0};
        gi.Pin = (uint32_t)1u << pn;
        gi.Speed = GPIO_SPEED_FREQ_LOW;
        gi.Pull = GPIO_NOPULL;
        gi.Mode = (strcmp(argv[0], "pindrv") == 0) ? GPIO_MODE_OUTPUT_PP : GPIO_MODE_INPUT;
        HAL_GPIO_Init(ports[pi], &gi);
        if (strcmp(argv[0], "pindrv") == 0)
            HAL_GPIO_WritePin(ports[pi], gi.Pin, atoi(argv[3]) ? GPIO_PIN_SET : GPIO_PIN_RESET);
        printf("%s P%c%d%s\n", argv[0], (char)('A' + pi), pn,
               (strcmp(argv[0], "pindrv") == 0) ? (atoi(argv[3]) ? "=1" : "=0") : "=in");
        return;
    }
#endif /* TCS_PROFILE >= 1: txwave / pindrv / pinrel */
    /* --- tbl：注册表序列化自检明细（POST TABLE 项排障）--- */
    if (strcmp(argv[0], "tbl") == 0) {
        uint8_t img[EE_IMG_SIZE];
        int rv = param_serialize(img);
        extern uint8_t proto_crc8(const uint8_t *data, size_t len);
        int npersist = 0;
        for (int i = 0; i < param_count(); i++) {
            const param_desc_t *p = param_at(i);
            if (p && (p->flags & PF_PERSIST)) npersist++;
        }
        printf("tbl serialize rv=%d count=%d npersist=%d crc=%02x calc=%02x\n",
               rv, (rv == 0) ? (int)img[2] : -1, npersist,
               (rv == 0) ? img[EE_CRC_ADDR] : 0,
               (rv == 0) ? (int)proto_crc8(img, EE_CRC_ADDR) : 0);
        for (int i = 0; i < param_count(); i++) {
            const param_desc_t *p = param_at(i);
            if (!p) { printf("bad idx %d null\n", i); break; }
            if (!(p->min < p->max))
                printf("bad range 0x%04x %s min=%.2f max=%.2f\n",
                       p->id, p->key, (double)p->min, (double)p->max);
            if (!(p->value >= p->min && p->value <= p->max))
                printf("bad value 0x%04x %s v=%.2f [%.2f,%.2f]\n",
                       p->id, p->key, (double)p->value,
                       (double)p->min, (double)p->max);
        }
        return;
    }
#if TCS_PROFILE >= 1
    /* --- wave：PA3 板载逻辑分析仪（50ms 边沿时间戳进 RAM，探针直读）--- */
    if (strcmp(argv[0], "wave") == 0) {
        extern void link_wave_capture(void);
        extern volatile uint16_t g_wave_n;
        extern volatile uint8_t  g_wave_lvl0;
        link_wave_capture();
        printf("wave n=%u lvl0=%u\n", (unsigned)g_wave_n, (unsigned)g_wave_lvl0);
        return;
    }
    /* --- rxraw：链路 RX 原始 64 字节环形快照（板间线取证）--- */
    if (strcmp(argv[0], "rxraw") == 0) {
        extern volatile uint8_t  g_dbg_rx_ring[64];
        extern volatile uint32_t g_dbg_rx_pos;
        extern volatile uint32_t g_dbg_lrx_total;
        uint32_t pos = g_dbg_rx_pos;
        printf("rxraw lrx=%lu pos=%lu\n", (unsigned long)g_dbg_lrx_total,
               (unsigned long)pos);
        for (uint32_t i = 0; i < 8u; i++) {
            uint32_t o = i * 8u;
            printf("  %02x %02x %02x %02x %02x %02x %02x %02x\n",
                   g_dbg_rx_ring[(pos + o + 0u) & 63u], g_dbg_rx_ring[(pos + o + 1u) & 63u], g_dbg_rx_ring[(pos + o + 2u) & 63u], g_dbg_rx_ring[(pos + o + 3u) & 63u], g_dbg_rx_ring[(pos + o + 4u) & 63u], g_dbg_rx_ring[(pos + o + 5u) & 63u], g_dbg_rx_ring[(pos + o + 6u) & 63u], g_dbg_rx_ring[(pos + o + 7u) & 63u]);
        }
        return;
    }
    /* --- pinscan：全端口 GPIO 边沿普查（BENCH 档；屏蔽 MHz 级显示总线）---
     * R1：原实现 3s 忙等且循环内 iwdg_refresh()——既阻塞急停键轮询，又让看门狗假活。
     * 现改为：上限 1s（< IWDG 2s，不再需要喂狗）、去掉 iwdg_refresh、时钟走 HAL 宏。
     * M03-F-04（2026-11-25）：循环体内每 10ms 经 console_estop_yield() 回到屏上的急停入口——
     * 原来这 1s 里屏上的急停键没人看。让位期间要读触摸控制器（I2C4：PD12=SCL / PD13=SDA），
     * 这两脚上会出现 I2C 边沿：从普查里屏蔽掉，否则每次都报出假边沿。读 I2C 的那约 1ms 里
     * 别的边沿数不到（每 10ms 一段，约占一成），这是 pinscan 的已知盲区。*/
    if (strcmp(argv[0], "pinscan") == 0) {
        int secs = (argc >= 2) ? atoi(argv[1]) : 1;
        if (secs < 1) secs = 1;
        if (secs > 1) secs = 1;          /* 硬上限 1s：不跨 IWDG 窗口 */
        __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE();
        __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOD_CLK_ENABLE();
        __HAL_RCC_GPIOE_CLK_ENABLE(); __HAL_RCC_GPIOF_CLK_ENABLE();
        __HAL_RCC_GPIOG_CLK_ENABLE(); __HAL_RCC_GPIOH_CLK_ENABLE();
        __HAL_RCC_GPIOI_CLK_ENABLE(); __HAL_RCC_GPIOJ_CLK_ENABLE();
        __HAL_RCC_GPIOK_CLK_ENABLE();
        GPIO_TypeDef *ports[11] = { GPIOA, GPIOB, GPIOC, GPIOD, GPIOE, GPIOF,
                                    GPIOG, GPIOH, GPIOI, GPIOJ, GPIOK };
        static uint16_t last[11];
        static uint32_t edge[11][16];
        const uint16_t touch_i2c = (uint16_t)(GPIO_PIN_12 | GPIO_PIN_13);   /* PD12 / PD13 */
        for (int i = 0; i < 11; i++) {
            last[i] = (uint16_t)ports[i]->IDR;
            if (i == 3) last[i] &= (uint16_t)~touch_i2c;
            for (int b = 0; b < 16; b++) edge[i][b] = 0u;
        }
        uint32_t t0 = DWT->CYCCNT;
        uint32_t dur = (uint32_t)secs * (SystemCoreClock / 1u);
        while ((DWT->CYCCNT - t0) < dur) {
            console_estop_yield();
            for (int i = 0; i < 11; i++) {
                uint16_t cur = (uint16_t)ports[i]->IDR;
                if (i == 3) cur &= (uint16_t)~touch_i2c;
                uint16_t ch = (uint16_t)(cur ^ last[i]);
                if (ch) {
                    last[i] = cur;
                    for (int b = 0; b < 16; b++)
                        if (ch & (1u << b)) edge[i][b]++;
                }
            }
        }
        printf("pinscan %ds edges(2..400 shown; PD12/PD13 masked, estop watch runs every 10ms):\n", secs);
        for (int i = 0; i < 11; i++)
            for (int b = 0; b < 16; b++)
                if (edge[i][b] >= 2u && edge[i][b] <= 400u)
                    printf("  P%c%d %lu\n", (char)('A' + i), b, (unsigned long)edge[i][b]);
        return;
    }
#endif /* TCS_PROFILE >= 1: wave / rxraw / pinscan */
#if TCS_PROFILE >= 1
    /* --- ee：EEPROM 镜像现场重读+校验（诊断）。只在台架及以上（S-G l1，2026-11-25）：
     * 交付档的屏不初始化本地 EEPROM（参数真值在主控），这条命令在交付档上恒回 "ee offline"，
     * 是误导——现在交付档回 "?"，实现体不编译。--- */
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
#endif /* TCS_PROFILE >= 1: ee */
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
        /* 2026-11-15 UI 单栈：旧 tcs_ui 的 hero/page/flips/patches/touch_hz 计数随栈删除；
         * LVGL 渲染耗时看 TL-8 直方图（g_tl8，probe-rs 直读）。此处只保留主循环/触摸/工作耗时。*/
        printf("work avg=%luus max=%luus ovf=%lu | "
               "touch max=%luus | loop=%lu rate=%lu/s | up=%lus\n",
               (unsigned long)g_stat_work_us_avg, (unsigned long)g_stat_work_us_max,
               (unsigned long)g_stat_tick_ovf,
               (unsigned long)g_stat_touch_us_max,
               (unsigned long)g_stat_loop_passes, (unsigned long)rate,
               (unsigned long)up_s);
        /* M01-F-09：最近一个**已经结束**的 1 秒窗口的分布。上面的 max 是开机以来的终身值——
         * 一个几小时前的尖峰会一直占着它；这里看的是"现在"：P50 / P99（按桶下界报，桶宽 2 倍）、
         * 窗口最大值、超预算（8ms）的拍数。开机的第一秒还没有结束的窗口，n=0。*/
        {
            const perf_hist_t *w = tcs_perf_last_window();
            printf("win1s: n=%lu p50=%luus p99=%luus max=%luus over=%lu(budget %luus)\n",
                   (unsigned long)w->n, (unsigned long)perf_hist_pctl(w, 50u),
                   (unsigned long)perf_hist_pctl(w, 99u), (unsigned long)w->max,
                   (unsigned long)w->over, (unsigned long)w->over_thresh);
        }
        return;
    }
    /* --- p ... --- */
    if (strcmp(argv[0], "p") == 0 && argc >= 2) {
        if (strcmp(argv[1], "list") == 0) {
            /* 50 条 ≈2KB 输出 > TX 环 2KB → 边打印边排空（防尾部被丢弃）
             * M03-F-04：排空是阻塞的串口发送（整张表约 180ms），每排一次就回到屏上的急停入口看一眼。*/
            for (int i = 0; i < param_count(); i++) {
                const param_desc_t *p = param_at(i);
                printf("0x%02x %s %.2f [%.1f,%.1f] 0x%02x\n",
                       p->id, p->key, (double)p->value,
                       (double)p->min, (double)p->max, p->flags);
                if ((i & 7) == 7) { console_estop_yield(); tx_drain(); }
            }
            console_estop_yield();
            tx_drain();
            return;
        }
        if (strcmp(argv[1], "set") == 0 && argc == 4) {
            uint16_t id = (uint16_t)strtoul(argv[2], NULL, 0);
            float v = strtof(argv[3], NULL);
#if TCS_PROFILE < 1
            /* r7f（独立复核 R7-12，2026-11-26）：交付档没有 mode / tec / fan（见下面 TCS_PROFILE >= 1 那一段），
             * 但邮箱命令 p set 0x06 / 0x09 / 0x0A 与它们等效（控制模式、两路手动输出）——交付固件里不该有这条路。
             * 执行器类参数一律拒；非执行器参数照旧（屏幕调参页本来就能写，不新增能力）。
             * 判据在 param_registry.c（纯逻辑，主机用例 tests/test_param_registry.c）；口径同步在
             * tools/site/verify_prod_clean.py（交付档产物必须有这句拒绝文本、台架档必须没有——整段只在这里编译）。*/
            if (param_mailbox_blocked(id, 1)) {
                s_cmd_rv = -2;              /* M07-F-08：留痕用 */
                printf("rejected (prod: mode / TEC outputs are not writable from this mailbox)\n");
                return;
            }
#endif /* TCS_PROFILE < 1: 交付档邮箱 p set 的执行器参数拒绝表 */
            int rv = param_set(id, v, PARAM_SRC_CONSOLE);
            s_cmd_rv = rv;                  /* M07-F-08：留痕用 */
            if (rv == 0)      printf("ok %.2f\n", (double)param_get(id));
            else if (rv == 1) printf("clamped %.2f\n", (double)param_get(id));
            else if (rv == -2) printf("rejected (body_cold<body_hot)\n");
            else              printf("bad id\n");
            return;
        }
#if TCS_PROFILE >= 1
        /* p save / p defaults：只在台架及以上（S-G l1，2026-11-25，交付固件上机时发现）。
         * 交付档的屏是纯显示终端：参数真值在主控（NVS），本机既不读也不写参数区。
         * 原来这两条在交付档也在——`p defaults Y` 回 "defaults restored+saved rv=1"，
         * 实际只把屏本地的参数镜像重置了一下（下一帧快照 / 镜像就盖回去），保存必然失败；
         * `p save` 回 "save rv=1"，其实什么都没保存。回复是误导性的，交付档现在回 "?"。
         * 实现体与声明之外的调用点（param_persist_now / param_persist_all）仍被注册表自己引用，
         * 这里只是不让交付档够得着它们。要"经链路让主控恢复默认"是另一件事：需要新的协议消息、
         * 主控侧处理与二次确认，不在本批次。*/
        if (strcmp(argv[1], "save") == 0) {
            int rv = param_persist_now();
            printf("save rv=%d\n", rv);
            return;
        }
        if (strcmp(argv[1], "defaults") == 0) {
            if (argc == 3 && argv[2][0] == 'Y') {
                param_load_defaults();
                int rv = param_persist_all();   /* M05-F-02：load 会清脏，必须显式置脏再写 */
                printf("defaults restored+saved rv=%d\n", rv);
            } else {
                printf("confirm: p defaults Y\n");
            }
            return;
        }
#endif /* TCS_PROFILE >= 1: p save / p defaults */
    }
#if TCS_PROFILE >= 1
    /* --- 控制命令（与 UI 同路径；BENCH 档：手动直控会绕过网页/屏的权威链）--- */
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
#endif /* TCS_PROFILE >= 1: mode / tec / fan */
    /* estop 在 PROD 也保留：它是安全动作入口，不是调试能力 */
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
#if TCS_PROFILE >= 1
    /* --- m207 send <raw>（BENCH：可直写 TCSW 等执行器参数，PROD 必须不存在）---
     * M07-F-10（2026-11-25，S-G l1）：原来把 "%s\r" 截进 64 字节缓冲后交给 24 字节的下发队列，
     * 超过 22 字符的指令被队列静默截成另一条指令，回复却是 "m207 rv=0"（只表示入队成功，
     * 读起来像"已发送"）。现在：过长显式拒绝；回复按 m207_send_raw 的返回值说清楚
     * "只是入了队，还没发出"。*/
    if (strcmp(argv[0], "m207") == 0 && argc >= 3 && strcmp(argv[1], "send") == 0) {
        size_t rawlen = strlen(argv[2]);
        if (rawlen + 1u > M207_CMD_MAX) {        /* +1 = 结尾的 \r */
            printf("m207 send: too long (%u chars; max %u incl. CR) -> NOT sent\n",
                   (unsigned)rawlen, (unsigned)(M207_CMD_MAX - 1u));
            return;
        }
        char cmd[M207_CMD_MAX + 1u];
        snprintf(cmd, sizeof(cmd), "%s\r", argv[2]);
        int rv = m207_send_raw(cmd);
        s_cmd_rv = rv;                      /* M07-F-08：留痕用 */
        if (rv == 0)       printf("m207 send -> queued (not sent yet; one frame per 50ms; reply in log)\n");
        else if (rv == -1) printf("m207 send -> NOT queued (queue full or empty command)\n");
        else               printf("m207 send -> NOT queued (rv=%d)\n", rv);
        return;
    }
#endif /* TCS_PROFILE >= 1: m207 send */
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
#if TCS_PROFILE >= 1
    /* --- wdttest：死循环不喂狗 → IWDG 2s 复位（WP-C.1 实验命令；仅 BENCH）--- */
    if (strcmp(argv[0], "wdttest") == 0) {
        tcs_log("[SYSTEM]", "IWDG test: busy-loop 5s (timeout=2s)");
        uint32_t t0 = HAL_GetTick();
        /* M03-F-04：这 5 秒里屏上的急停入口照旧每 10ms 看一次（不喂狗——那才是这条命令要测的）。
         * 急停命令在下一次复位之前就能发给主控。*/
        while (HAL_GetTick() - t0 < 5000u) { console_estop_yield(); }
        return;
    }
#endif /* TCS_PROFILE >= 1: wdttest */

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
            /* 2026-11-15 UI 单栈：改走 LVGL 注入钩子 g_tap_inject（150ms 合成按压）*/
            g_tap_inject[2] = (uint32_t)atoi(argv[2]);
            g_tap_inject[3] = (uint32_t)atoi(argv[3]);
            g_tap_inject[1] = 1;
            g_tap_inject[0]++;
            printf("sim touch %s %s\n", argv[2], argv[3]);
        } else if (strcmp(argv[1], "touchh") == 0 && argc == 4) {
            /* 按住：mode=2 持续 PRESSED，直到 touchr */
            g_tap_inject[2] = (uint32_t)atoi(argv[2]);
            g_tap_inject[3] = (uint32_t)atoi(argv[3]);
            g_tap_inject[1] = 2;
            g_tap_inject[0]++;
            printf("sim touchh %s %s (hold)\n", argv[2], argv[3]);
        } else if (strcmp(argv[1], "touchr") == 0) {
            g_tap_inject[1] = 0;
            g_tap_inject[0]++;
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
    s_cmd_rv = -1;                          /* M07-F-08：没被任何分支接住 = 这条命令没执行 */
    printf("?\n");
}

/* ====== 主循环节拍 ====== */
void console_poll(void)
{
    tx_drain();
    touch_log_dump_poll();   /* WP-M0：分块 CSV 导出（每拍续传）*/

    /* 主机邮箱命令注入（probe-rs 写 RAM 触发，先于 UART 行处理）*/
    if (s_mb.magic == MB_MAGIC_RUN) {
        uint32_t len = s_mb.len;
        uint32_t crc = s_mb.crc16 & 0xFFFFu;
        uint8_t ok = (len >= 1u && len <= MB_CMD_MAX
                      && mb_crc16((const uint8_t *)s_mb.cmd, len) == (uint16_t)crc);
        s_mb.magic = 0;                 /* 先清：坏记录不能被下一拍重复处理 */
        if (!ok) {
            s_mb_bad++;
            printf("[MB] reject len=%lu crc=%04lx (bad=%lu)\n",
                   (unsigned long)len, (unsigned long)crc, (unsigned long)s_mb_bad);
        } else {
            /* 强制 NUL 结尾（len 由校验保证 ≤96；正好 96 时借用末字节）*/
            if (len >= MB_CMD_MAX) s_mb.cmd[MB_CMD_MAX - 1] = '\0';
            else                   s_mb.cmd[len] = '\0';
            cmd_exec(s_mb.cmd);
            audit_commit();                  /* M07-F-08 */
            s_mb_seq++;
            tcs_log("[MB]", "done seq=%lu", (unsigned long)(s_mb_seq + 1));
        }
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
            if (s_line_discard) {
                s_line_discard = 0;   /* R1：超长行的残片在这里丢弃，不执行 */
                s_line_len = 0;
            } else if (s_line_len) {
                s_line[s_line_len] = '\0';
                cmd_exec(s_line);
                audit_commit();              /* M07-F-08 */
                s_line_len = 0;
            }
        } else if (s_line_discard) {
            /* 丢弃至行尾（原实现清零后继续累积 → 执行尾部残片）*/
        } else if (s_line_len < CON_LINE_MAX - 1) {
            s_line[s_line_len++] = (char)b;
        } else {
            s_line_discard = 1;       /* 超长：整行作废 */
            s_line_len = 0;
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
    s_line_discard = 0;
    audit_ring_init(&s_audit);              /* M07-F-08：审计环上电清空 */
    s_audit_text[0] = '\0';
    HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1);
}

/* ====== 复位源解析（§3.9：复位后首行日志）======
 * WP-C.4：IWDG1RSTF 优先于 PIN——IWDG 复位时 NRST 也被拉低，PINRSTF 同样置位，
 * 按 PIN 优先会误报。其余位照常见义。 */
/* X04-F-03：上次 HardFault 现场（.noinit，复位存活）。只在发生过时打印，不主动清。
 * 2026-11-26（S 线 C06）：记录多留了栈指针、栈内的返回地址与 xPSR、异常状态和序号——三次受控触发读出的 PC 都是 0，
 * 没有这些判不出记录器哪一步不对。结构布局与 Core/Src/stm32h7xx_it.c 的 fault_rec_t 必须一致（改一处同步另一处）。*/
void console_print_fault_record(void)
{
    extern volatile struct {
        uint32_t magic, pc, lr, cfsr, hfsr, bfar;
        uint32_t sp, stk_lr, xpsr, icsr, shcsr, fpccr, seq;
    } g_fault_rec;
    if (g_fault_rec.magic != 0x54434641u) return;
    tcs_log("[FAULT]", "HardFault #%lu PC=0x%08lx EXC_RETURN=0x%08lx CFSR=0x%08lx HFSR=0x%08lx BFAR=0x%08lx",
            (unsigned long)g_fault_rec.seq, (unsigned long)g_fault_rec.pc, (unsigned long)g_fault_rec.lr,
            (unsigned long)g_fault_rec.cfsr, (unsigned long)g_fault_rec.hfsr,
            (unsigned long)g_fault_rec.bfar);
    tcs_log("[FAULT]", "  stacked SP=0x%08lx stk_lr=0x%08lx xPSR=0x%08lx ICSR=0x%08lx SHCSR=0x%08lx FPCCR=0x%08lx",
            (unsigned long)g_fault_rec.sp, (unsigned long)g_fault_rec.stk_lr, (unsigned long)g_fault_rec.xpsr,
            (unsigned long)g_fault_rec.icsr, (unsigned long)g_fault_rec.shcsr, (unsigned long)g_fault_rec.fpccr);

    /* S 线 i3（条目 9）：最近 8 条状态迁移（.noinit，软复位后还在；掉电丢）。
     * 与 HardFault 记录一起打：复位前走过哪些状态、当时报警位是什么。*/
    {
        const tcs_trace_t *t = control_trace();
        static const char *const nm[4] = { "IDLE", "COOL", "HEAT", "SAFE" };
        uint16_t n = tcs_trace_count(t);
        if (!n) {
            tcs_log("[TRACE]", "state trace: none (empty, or invalid contents after a cold power-up)");
        } else {
            tcs_log("[TRACE]", "state trace: %u entries (oldest first), seq=%lu",
                    (unsigned)n, (unsigned long)t->seq);
            for (uint16_t i = 0; i < n; i++) {
                const tcs_trace_ent_t *e = tcs_trace_at(t, i);
                if (!e) break;
                tcs_log("[TRACE]", "  %lu ms %s -> %s%s alarm=0x%04X",
                        (unsigned long)e->ms, nm[e->from & 3u], nm[e->to & 3u],
                        e->ev ? " (estop)" : "", (unsigned)e->alarm);
            }
        }
    }
}

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
