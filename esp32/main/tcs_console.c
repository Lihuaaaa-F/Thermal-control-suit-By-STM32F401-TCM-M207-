/**
 * tcs_console.c — ESP32 console REPL 实现（R1e）
 *
 * `p` 子命令派发（任务书命名）：p list / p set <id|key> <v> / p save。
 * 其余命令直呼 param/control/m207/tmp117/fan 公共 API，不旁路任何写路径。
 */
#include "tcs_console.h"
#include "param_registry.h"
#include "nvs_store.h"
#include "control.h"
#include "control_service.h"
#include "m207_service.h"
#include "tmp117_service.h"
#include "fan_service.h"
#include "driver/gpio.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "esp_system.h"
#include "esp_log.h"
#include "esp_console.h"
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
           "alarm=0x%04X armed=%u state=%d sim=%u heap=%u\n",
           (double)g_tcs.tc1_act, (double)g_tcs.tc2_act, (double)g_tcs.body_temp,
           (unsigned)g_tcs.body_sensor_ok, (unsigned)fan_get_duty(),
           (unsigned)g_tcs.mode, (unsigned)g_tcs.m207_online,
           (unsigned)g_tcs.alarm_bits, (unsigned)control_service_armed(),
           (int)control_state(), (unsigned)tmp117_sim_active(),
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

static int cmd_ledblk(int argc, char **argv)
{
    if (argc < 2) { printf("ledblk <gpio>（对任意脚发 WS2812 全黑帧）\n"); return 1; }
    int g = atoi(argv[1]);
    if (g < 0 || g > 48) { printf("gpio 0-48\n"); return 1; }
    fan_ws2812_black_frame(g);
    printf("ledblk %d sent (WS2812 black frame)\n", g);
    return 0;
}

static int cmd_ledblink(int argc, char **argv)
{
    if (argc < 2) { printf("ledblink <gpio>（低/高各 300ms 闪 3 次，用眼睛定位灯脚）\n"); return 1; }
    int g = atoi(argv[1]);
    if (g < 0 || g > 48) { printf("gpio 0-48\n"); return 1; }
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
    if (g < 0 || g > 48) { printf("gpio 0-48\n"); return 1; }
    fan_ws2812_ledtest(g);
    printf("ledtest %d done\n", g);
    return 0;
}

static int cmd_reset(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("soft reset in 1s\n");
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
        { .command="mode", .help="mode 0|1（手动/自动）", .hint="<0|1>",
          .func=cmd_mode, .argtable=NULL },
        { .command="tec", .help="tec <ch 1|2> <on 0|1>", .hint="<ch> <on>",
          .func=cmd_tec, .argtable=NULL },
        { .command="fan", .help="fan <0-100>（占空比%，低=开）", .hint="<pct>",
          .func=cmd_fan, .argtable=NULL },
        { .command="body", .help="body <°C|off>（体温注入，E-21/22）", .hint="<v|off>",
          .func=cmd_body, .argtable=NULL },
        { .command="estop", .help="急停", .hint=NULL,
          .func=cmd_estop, .argtable=NULL },
        { .command="ack", .help="报警人工确认清除", .hint=NULL,
          .func=cmd_ack, .argtable=NULL },
        { .command="status", .help="状态一行", .hint=NULL,
          .func=cmd_status, .argtable=NULL },
        { .command="m207", .help="m207 raw <帧>（直查/注入；自动补 \r）", .hint="raw <frame>",
          .func=cmd_m207, .argtable=NULL },
        { .command="ledblk", .help="ledblk <gpio>（WS2812 全黑帧）", .hint="<gpio>",
          .func=cmd_ledblk, .argtable=NULL },
        { .command="ledblink", .help="ledblink <gpio>（闪 3 次定位灯脚）", .hint="<gpio>",
          .func=cmd_ledblink, .argtable=NULL },
        { .command="ledtest", .help="ledtest <gpio>（红闪→黑，可视自证）", .hint="<gpio>",
          .func=cmd_ledtest, .argtable=NULL },
        { .command="perf", .help="服务统计+vTaskList", .hint=NULL,
          .func=cmd_perf, .argtable=NULL },
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
