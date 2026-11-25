/* m207_core_host.c — 固件 M207 服务内核的主机桥（S-G e2：X06-F-05）
 *
 * 把 m207_core.c（交付固件用的同一份决策逻辑：队列、旁路、在途确认、轮询 / 探测状态机）编成
 * 主机可执行文件，经 stdin / stdout 让 Python 的对端模型（project_tcs/tools/m207_sim.py 的
 * M207Core：209 参字典、ERRORCODE 全表、! 保存、50ms 铁律）直接驱动。
 * 原来 test_m207_adv.py 的注入矩阵只证明 Python 模拟器自洽；现在同一套对端模型对着固件实现跑。
 *
 * 协议（一行一条）：
 *   init                      → rc 0
 *   api set_temp <ch> <v>     → rc <返回值>      api tec <ch> <on> → rc    api map_set <pid> <v> → rc
 *   api raw <帧>              → rc               api save_all → rc
 *   tick                      → 每发一帧先输出 "TX <帧>"，然后等对端回一行："R <应答行>" 或 "R"（静默）；
 *                                应答在下一拍开头进入 RX（真机回环应答器同款时序）。拍末输出：
 *                                "S <online> <alarm十六进制> <tc1> <tc2> <sensor_open> <tec1_on> <tec2_on>
 *                                   <nak> <vok> <resend> <offline> <unexpected> <qdrop> <superseded>
 *                                   <save_ok> <save_rej>"
 *   q                         → 退出
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m207_service.h"
#include "m207_core.h"
#include "param_registry.h"
#include "control.h"

static uint32_t s_now;                       /* 虚拟毫秒 */
static unsigned char s_rx[65536];
static size_t s_rxn;

static int host_tx(void *ctx, const char *frame, size_t len)
{
    char f[128], in[1024];
    (void)ctx;
    if (len && frame[len - 1] == '\r') len--;
    if (len > sizeof f - 1) len = sizeof f - 1;
    memcpy(f, frame, len);
    f[len] = '\0';
    printf("TX %s\n", f);
    fflush(stdout);
    if (!fgets(in, sizeof in, stdin)) { printf("S EOF\n"); fflush(stdout); exit(2); }
    in[strcspn(in, "\r\n")] = '\0';
    if (in[0] == 'R' && in[1] == ' ') {
        const char *p = in + 2;
        while (*p && s_rxn < sizeof s_rx - 2) s_rx[s_rxn++] = (unsigned char)*p++;
        s_rx[s_rxn++] = '\r';
    }
    return 0;
}

static int64_t host_now(void *ctx) { (void)ctx; return (int64_t)s_now * 1000; }

static int api(const char *name, int ch, double v)
{
    if (!strcmp(name, "set_temp")) return m207_set_temp((uint8_t)ch, (float)v);
    if (!strcmp(name, "tec"))      return m207_tec((uint8_t)ch, (uint8_t)(v != 0.0));
    if (!strcmp(name, "map_set"))  return m207_map_set((uint16_t)ch, (float)v);
    return -99;
}

int main(void)
{
    char line[1024];
    control_init(NULL);
    g_tcs.body_temp = 0.0f;
    g_tcs.body_sensor_ok = 0;
    m207_core_init(&(m207_core_io_t){ .tx = host_tx, .now_us = host_now });
    while (fgets(line, sizeof line, stdin)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (!strcmp(line, "q")) break;
        if (!strncmp(line, "api raw ", 8)) {
            printf("rc %d\n", m207_send_raw(line + 8));
        } else if (!strcmp(line, "api save_all")) {
            printf("rc %d\n", m207_save_all());
        } else if (!strncmp(line, "api ", 4)) {
            char n[32];
            int ch = 0;
            double v = 0.0;
            if (sscanf(line + 4, "%31s %d %lf", n, &ch, &v) >= 2) printf("rc %d\n", api(n, ch, v));
            else printf("rc -98\n");
        } else if (!strcmp(line, "init")) {
            printf("rc 0\n");
        } else if (!strcmp(line, "tick")) {
            m207_qstats_t st;
            m207_core_tick_begin(s_now);
            if (s_rxn) { m207_core_rx(s_rx, s_rxn, s_now); s_rxn = 0; }
            m207_core_tick_end((int64_t)s_now * 1000);
            s_now += 50u;
            m207_stats_queue(&st);
            printf("S %u %X %.3f %.3f %u %u %u %u %u %u %u %u %u %u %u %u\n",
                   (unsigned)m207_online(), (unsigned)g_tcs.alarm_bits,
                   (double)g_tcs.tc1_act, (double)g_tcs.tc2_act,
                   (unsigned)g_tcs.m207_sensor_open, (unsigned)g_tcs.tec1_on,
                   (unsigned)g_tcs.tec2_on,
                   (unsigned)m207_stats_nak(), (unsigned)m207_stats_verify_ok(),
                   (unsigned)m207_stats_resend(), (unsigned)m207_stats_offline(),
                   (unsigned)m207_stats_unexpected(), (unsigned)st.qdrop,
                   (unsigned)st.superseded, (unsigned)st.save_ok, (unsigned)st.save_rej);
        } else {
            printf("rc -97\n");
        }
        fflush(stdout);
    }
    return 0;
}

/* param_registry.c 的目标机桩 */
int eeprom_ready(void) { return 0; }
int eeprom_read_image(uint8_t *img) { (void)img; return -1; }
