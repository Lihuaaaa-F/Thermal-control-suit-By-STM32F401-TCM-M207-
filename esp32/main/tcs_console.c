/**
 * tcs_console.c — ESP32 console REPL 实现（R1e）
 *
 * `p` 子命令派发（任务书命名）：p list / p set <id|key> <v> / p save。
 * 其余命令直呼 param/control/m207/tmp117/fan 公共 API，不旁路任何写路径。
 * 官方查证：检索 components.espressif.com 2026-10-26，esp_console 即官方控制台件；REPL 用官方 esp_console（linenoise/argtable 官方件）；自研仅为命令表与派发；实现参考 IDF examples/system/console/basic
 */
#include "tcs_console.h"
#include "param_registry.h"
#include "nvs_store.h"
#include "control.h"
#include "control_service.h"
#include "m207_service.h"
#include "tmp117_service.h"
#include "fan_service.h"
#include "link_task.h"   /* M11-F-10：linkstat 打故障分桶 */
#include "driver/gpio.h"
#include "esp_timer.h"      /* R1 复核 R-13：cmd_gedge 用 esp_timer_get_time，必须显式包含 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
#include "driver/uart.h"   /* ltx 屏链自检 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/* ====== p 子命令（list 默认）====== */
static int cmd_p(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "save") == 0) {
        int rc = param_persist_now();
        printf("p save -> %d (0=committed 1=no dirty -1=fail)\n", rc);
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "set") == 0) {
        if (argc < 4) { printf("usage: p set <id|key> <val>\n"); return 1; }
        uint16_t id = (uint16_t)strtoul(argv[2], NULL, 0);
        if (id == 0) {
            const param_desc_t *d = param_find_by_key(argv[2]);
            if (!d) { printf("bad id/key\n"); return 1; }
            id = d->id;
        }
        float v = strtof(argv[3], NULL);
        int rc = param_set(id, v, PARAM_SRC_CONSOLE);
        printf("p set 0x%04X = %.3f -> rc=%d (0=ok 1=clamped -1=bad -2=constraint)\n",
               (unsigned)id, (double)param_get(id), rc);
        return 0;
    }
    /* list（默认）*/
    printf("%-7s %-14s %-6s %10s %9s %9s\n", "id", "key", "unit", "value", "min", "max");
    for (int i = 0; i < param_count(); i++) {
        const param_desc_t *d = param_at(i);
        printf("0x%04X %-14s %-6s %10.3f %9.2f %9.2f\n",
               (unsigned)d->id, d->key, d->unit, (double)d->value,
               (double)d->min, (double)d->max);
    }
    return 0;
}

#if CONFIG_TCS_DIAG
static int cmd_mode(int argc, char **argv)
{
    if (argc < 2) { printf("mode 0|1 (now %u)\n", (unsigned)g_tcs.mode); return 1; }
    long m = strtol(argv[1], NULL, 0);
    if (m != 0 && m != 1) { printf("mode 0|1\n"); return 1; }
    int rc = param_set(PARAM_ID_MODE, (float)m, PARAM_SRC_CONSOLE);
    printf("mode=%u rc=%d\n", (unsigned)m, rc);
    return 0;
}

static int cmd_tec(int argc, char **argv)
{
    if (argc < 3) { printf("tec <ch 1|2> <on 0|1>\n"); return 1; }
    int ch = atoi(argv[1]), on = atoi(argv[2]);
    if ((ch != 1 && ch != 2) || (on != 0 && on != 1)) { printf("tec <ch 1|2> <on 0|1>\n"); return 1; }
    control_manual_tec((uint8_t)ch, (uint8_t)on);
    printf("tec %d=%d (SAFE 期拒绝)\n", ch, on);
    return 0;
}

static int cmd_fan(int argc, char **argv)
{
    if (argc < 2) { printf("fan <0-100> (now %u%%)\n", (unsigned)fan_get_duty()); return 1; }
    int p = atoi(argv[1]);
    if (p < 0 || p > 100) { printf("fan 0-100\n"); return 1; }
    if (control_state() == CTRL_SAFE && p < 100) {
        printf("SAFE 期风扇锁全开：拒绝降档（先 ack 退出 SAFE）\n");
        return 1;
    }
    fan_set_duty((uint8_t)p);
    printf("fan duty=%u%%\n", (unsigned)fan_get_duty());
    return 0;
}
#endif /* CONFIG_TCS_DIAG：直接手动控制仅台架（PROD 由屏/网页经权威链）*/

#if CONFIG_TCS_SIM
static int cmd_body(int argc, char **argv)
{
    if (argc < 2) { printf("body <°C|off> (sim=%u)\n", (unsigned)tmp117_sim_active()); return 1; }
    if (strcmp(argv[1], "off") == 0) {
        tmp117_sim_disable();
        printf("body sim OFF (real I2C path)\n");
    } else {
        float v = strtof(argv[1], NULL);
        tmp117_sim_set(v);
        printf("body sim %.2f\n", (double)v);
    }
    return 0;
}
#endif /* CONFIG_TCS_SIM：R1 修复 P0-06，PROD 不编译注入命令 */

static int cmd_estop(int argc, char **argv)
{
    (void)argc; (void)argv;
    tcs_emergency_stop();
    printf("ESTOP: TCSW x0 + fan on + mode=manual + ALM_ESTOP (ack to clear)\n");
    return 0;
}

static int cmd_ack(int argc, char **argv)
{
    (void)argc; (void)argv;
    tcs_alarm_ack();
    printf("ack: alarm=0x%04X armed=%u state=%d\n",
           (unsigned)g_tcs.alarm_bits, (unsigned)control_service_armed(),
           (int)control_state());
    return 0;
}

static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("tc1=%.2f tc2=%.2f body=%.2f(ok=%u) fan=%u%% mode=%u m207=%u "
           "alarm=0x%04X armed=%u state=%d sim=%u latch=0x%02X heap=%u\n",
           (double)g_tcs.tc1_act, (double)g_tcs.tc2_act, (double)g_tcs.body_temp,
           (unsigned)g_tcs.body_sensor_ok, (unsigned)fan_get_duty(),
           (unsigned)g_tcs.mode, (unsigned)g_tcs.m207_online,
           (unsigned)g_tcs.alarm_bits, (unsigned)control_service_armed(),
           (int)control_state(), (unsigned)tmp117_sim_active(),
           (unsigned)control_safety_latched(),      /* R-10：现场能看出"为什么还锁着" */
           (unsigned)esp_get_minimum_free_heap_size());
    return 0;
}

static int cmd_perf(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("[perf] m207 nak=%u vok=%u rsend=%u off=%u wdrop=%u rx?unexpected | "
           "ctrl pollmax=%luus polls=%lu armed=%u\n",
           (unsigned)m207_stats_nak(), (unsigned)m207_stats_verify_ok(),
           (unsigned)m207_stats_resend(), (unsigned)m207_stats_offline(),
           (unsigned)m207_stats_wdrop(),
           (unsigned long)control_service_poll_max_us(),
           (unsigned long)control_service_polls(),
           (unsigned)control_service_armed());
    static char lst[2048];            /* 输出缓冲（~16 任务×64B）*/
    vTaskGetRunTimeStats(lst);        /* 首份运行时统计表（第八章 测量门）*/
    printf("Task            Abs Time     CPU%%\n%s", lst);
    vTaskList(lst);
    printf("Task            State Prio  Stack  Num\n%s", lst);
    return 0;
}

#if CONFIG_TCS_DIAG
static int cmd_m207(int argc, char **argv)
{
    if (argc < 3 || strcmp(argv[1], "raw") != 0) {
        printf("usage: m207 raw <MODULE:PARAM...>  (例: m207 raw TC1:TCERRORSTATUS?)\n");
        return 1;
    }
    int rc = m207_send_raw(argv[2]);
    printf("m207 raw '%s' -> rc=%d (0=已入队；应答行见下方 reply/CMD:REPLY 日志)\n",
           argv[2], rc);
    return 0;
}
#endif /* CONFIG_TCS_DIAG：R1 复核 R-01，PROD 不得保留可直写 TCSW 的通道 */

/* 引脚白名单（R1 修复 M08-F-02）：原实现放开 GPIO0–48，`ledblink 26` 会对
 * Flash(26–32)/Octal PSRAM(33–37) 做 gpio_config。
 * R1 复核 R-02：42 是风扇 PWM（LEDC 通道），gpio_config 会把 pad 从 LEDC 摘下且不复原，
 * 之后风扇恒 OFF（含 SAFE 态）——所以白名单只留 38/48 两个板载 RGB 脚。*/
static int gpio_allowed(int g)
{
    return (g == 38 || g == 48);
}

static int cmd_ledblk(int argc, char **argv)
{
    if (argc < 2) { printf("ledblk <gpio>（对白名单脚发 WS2812 全黑帧）\n"); return 1; }
    int g = atoi(argv[1]);
    if (!gpio_allowed(g)) { printf("gpio not allowed (38/48)\n"); return 1; }
    fan_ws2812_black_frame(g);
    printf("ledblk %d sent (WS2812 black frame)\n", g);
    return 0;
}

static int cmd_ledblink(int argc, char **argv)
{
    if (argc < 2) { printf("ledblink <gpio>（低/高各 300ms 闪 3 次，用眼睛定位灯脚）\n"); return 1; }
    int g = atoi(argv[1]);
    if (!gpio_allowed(g)) { printf("gpio not allowed (38/48)\n"); return 1; }
    gpio_config_t io = { .pin_bit_mask = 1ULL << g, .mode = GPIO_MODE_INPUT_OUTPUT,
                         .pull_up_en = 0, .pull_down_en = 0, .intr_type = GPIO_INTR_DISABLE };
    gpio_config(&io);
    for (int i = 0; i < 3; i++) {
        gpio_set_level(g, 0); vTaskDelay(pdMS_TO_TICKS(300));
        gpio_set_level(g, 1); vTaskDelay(pdMS_TO_TICKS(300));
    }
    printf("ledblink %d done (3 pulses)\n", g);
    return 0;
}

static int cmd_ledtest(int argc, char **argv)
{
    if (argc < 2) { printf("ledtest <gpio>（红闪 700ms 再灭，验证帧时序+引脚）\n"); return 1; }
    int g = atoi(argv[1]);
    if (!gpio_allowed(g)) { printf("gpio not allowed (38/48)\n"); return 1; }
    fan_ws2812_ledtest(g);
    printf("ledtest %d done\n", g);
    return 0;
}

/* 屏链物理自检（2026-10-30）：UART1 连发 100 个 0x55（H743 侧应收到 55 序列）*/
static int cmd_linkstat(int argc, char **argv)
{
    (void)argc; (void)argv;
    link_stats_t st;
    link_stats_get(&st);
    /* M11-F-10：一次打出全部故障桶（注入已知故障后应只有对应桶增长）*/
    printf("link rx_b=%lu rx_f=%lu crc=%lu len=%lu gap=%lu dup=%lu retry=%lu "
           "txqdrop=%lu uartdrop=%lu evict=%lu verg=%lu",
           (unsigned long)st.rx_bytes, (unsigned long)st.rx_frames,
           (unsigned long)st.crc_err, (unsigned long)st.len_err,
           (unsigned long)st.gap_resets, (unsigned long)st.dup,
           (unsigned long)st.retry, (unsigned long)st.txq_drop,
           (unsigned long)st.uart_txdrop,
           (unsigned long)st.pending_evict, (unsigned long)st.proto_mismatch);
    printf("\n");
    return 0;
}

static int cmd_gedge(int argc, char **argv)
{
    if (argc < 2) { printf("gedge <gpio> [ms]\n"); return 1; }
    int g = atoi(argv[1]);
    int ms = (argc > 2) ? atoi(argv[2]) : 1000;
    if (g < 0 || g > 48 || ms < 1 || ms > 5000) { printf("range: gpio0-48 ms1-5000\n"); return 1; }
    int last = gpio_get_level(g);
    int n = 0;
    int64_t t0 = esp_timer_get_time();
    while (esp_timer_get_time() - t0 < (int64_t)ms * 1000) {
        int v = gpio_get_level(g);
        if (v != last) { n++; last = v; }
    }
    printf("gedge gpio%d %dms edges=%d level=%d\n", g, ms, n, last);
    return 0;
}

static int cmd_linkbuf(int argc, char **argv)
{
    (void)argc; (void)argv;
    size_t n = 0;
    /* R1 修复 M08-F-14：删未用变量 st 与重复的第二次调用（ESP32 唯一构建告警）*/
    esp_err_t r = uart_get_buffered_data_len(UART_NUM_1, &n);
    printf("linkbuf buffered=%u err=%d\n", (unsigned)n, (int)r);
    return 0;
}

static int cmd_linkrepin(int argc, char **argv)
{
    (void)argc; (void)argv;
    esp_err_t r1 = uart_set_pin(UART_NUM_1, 17, 18, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_flush_input(UART_NUM_1);
    printf("linkrepin set_pin=%d flushed\n", (int)r1);
    return 0;
}

static int cmd_ltx(int argc, char **argv)
{
    (void)argc; (void)argv;
    uint8_t pat[100];
    memset(pat, 0x55, sizeof(pat));
    int n = uart_write_bytes(UART_NUM_1, pat, sizeof(pat));
    printf("ltx: wrote %d bytes 0x55 to UART1(GPIO17)\n", n);
    return 0;
}

static int cmd_reset(int argc, char **argv)
{
    (void)argc; (void)argv;
    /* R1 修复 M08-F-13：复位前强制落盘，2s 防抖窗口内的参数变更不再丢 */
    int rv = param_persist_now();
    printf("persist rv=%d; soft reset in 1s\n", rv);
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return 0;
}

int tcs_console_start(void)
{
    esp_console_repl_config_t repl_cfg = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_cfg.max_cmdline_length = 128;
    repl_cfg.prompt = "tcs";

    esp_console_dev_usb_serial_jtag_config_t jcfg =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    esp_console_repl_t *repl = NULL;
    esp_err_t err = esp_console_new_repl_usb_serial_jtag(&jcfg, &repl_cfg, &repl);
    if (err != ESP_OK) {
        ESP_LOGE("CON", "repl new fail %s", esp_err_to_name(err));
        return -1;
    }

    esp_console_register_help_command();
    const esp_console_cmd_t cmds[] = {
        { .command="p", .help="参数：p list(默认) | p set <id|key> <v> | p save",
          .hint="<list|set|save>", .func=cmd_p, .argtable=NULL },
#if CONFIG_TCS_DIAG
        /* 直接手动控制只在台架存在（与 H743 BENCH 档一致；PROD 由屏/网页经权威链操作）*/
        { .command="mode", .help="mode 0|1（手动/自动；台架）", .hint="<0|1>",
          .func=cmd_mode, .argtable=NULL },
        { .command="tec", .help="tec <ch 1|2> <on 0|1>（台架）", .hint="<ch> <on>",
          .func=cmd_tec, .argtable=NULL },
        { .command="fan", .help="fan <0-100>（占空比%，低=开；台架）", .hint="<pct>",
          .func=cmd_fan, .argtable=NULL },
#endif /* CONFIG_TCS_DIAG */
#if CONFIG_TCS_SIM
        { .command="body", .help="body <°C|off>（体温注入，E-21/22；仅 SIM 构建）", .hint="<v|off>",
          .func=cmd_body, .argtable=NULL },
#endif
        { .command="estop", .help="急停", .hint=NULL,
          .func=cmd_estop, .argtable=NULL },
        { .command="ack", .help="报警人工确认清除", .hint=NULL,
          .func=cmd_ack, .argtable=NULL },
        { .command="status", .help="状态一行", .hint=NULL,
          .func=cmd_status, .argtable=NULL },
#if CONFIG_TCS_DIAG
        { .command="m207", .help="m207 raw <帧>（台架直查/注入；自动补 \r）", .hint="raw <frame>",
          .func=cmd_m207, .argtable=NULL },
#endif
        { .command="ledblk", .help="ledblk <gpio>（WS2812 全黑帧）", .hint="<gpio>",
          .func=cmd_ledblk, .argtable=NULL },
        { .command="ledblink", .help="ledblink <gpio>（闪 3 次定位灯脚）", .hint="<gpio>",
          .func=cmd_ledblink, .argtable=NULL },
        { .command="ledtest", .help="ledtest <gpio>（红闪→黑，可视自证）", .hint="<gpio>",
          .func=cmd_ledtest, .argtable=NULL },
        { .command="perf", .help="服务统计+vTaskList", .hint=NULL,
          .func=cmd_perf, .argtable=NULL },
        { .command="linkstat", .help="链路 RX 字节/帧计数（真机方向判定）", .hint=NULL,
      .func=cmd_linkstat, .argtable=NULL },
    { .command="linkbuf", .help="UART1 驱动缓冲字节数", .hint=NULL,
      .func=cmd_linkbuf, .argtable=NULL },
    { .command="linkrepin", .help="重下发 UART1 引脚映射并清 RX", .hint=NULL,
      .func=cmd_linkrepin, .argtable=NULL },
    { .command="gedge", .help="gedge <gpio> [ms]（引脚边沿探测，不重配）", .hint="<gpio> [ms]",
      .func=cmd_gedge, .argtable=NULL },
    { .command="ltx", .help="屏链自检：UART1 连发 100B 0x55", .hint=NULL,
          .func=cmd_ltx, .argtable=NULL },
        { .command="reset", .help="软复位", .hint=NULL,
          .func=cmd_reset, .argtable=NULL },
    };
    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        err = esp_console_cmd_register(&cmds[i]);
        if (err != ESP_OK)
            ESP_LOGW("CON", "register %s fail %s", cmds[i].command, esp_err_to_name(err));
    }
    err = esp_console_start_repl(repl);
    if (err != ESP_OK) {
        ESP_LOGE("CON", "repl start fail %s", esp_err_to_name(err));
        return -1;
    }
    ESP_LOGI("CON", "console REPL up (USB-Serial-JTAG; help = commands)");
    return 0;
}
