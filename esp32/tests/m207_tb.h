/**
 * m207_tb.h — M207 服务内核的主机测试台（头文件内实现；每个测试文件自包含一份，不改产线代码）
 *
 * 做三件事：
 *   1) 发送出口：接住内核发出的每一帧（去 \r 记入 TX 记录），转给假模块；
 *   2) 假模块（对端行为模型的 C 精简版；完整字典见 project_tcs/tools/m207_sim.py）：
 *      查询回值 / 设定回 CMD:REPLY=1 / 保存回 8 / 越界回 4 / 未知参数回 2 / 传感器开路回 2；
 *      可拔线（静默）、单通道开路、错值回显、设定被拒、应答丢失、塞任意字节；
 *   3) 逐拍驱动：tb_tick() = m207_core_tick_begin → 送达到期应答 → m207_core_rx → tick_end。
 *      虚拟时钟每拍 +50ms；应答默认在发出后的下一拍送达（与真机回环应答器一致）。
 *
 * 断言宏 TB_CHECK 与其它单测同款（打印 FAIL 行，返回码非 0）。
 */
#ifndef M207_TB_H
#define M207_TB_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "m207_service.h"
#include "m207_core.h"
#include "param_registry.h"
#include "control.h"

static int g_pass, g_fail;
#define TB_CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

#define TB_TICK_MS 50u
#define TB_TXCAP   16384

typedef struct { char f[64]; uint32_t tick; } tb_tx_t;

typedef struct { char name[16]; float v; } tb_kv_t;

static struct {
    uint32_t now_ms, tick;
    /* TX 记录 */
    tb_tx_t tx[TB_TXCAP];
    int     ntx;
    int     tx_full_every;      /* !=0：每 N 帧让发送出口报"环满"（丢帧注入）*/
    int     tx_dropped;
    /* 待送达的应答：due_tick 到期后进入 rx 字节流 */
    struct { uint32_t due; char s[160]; } pend[64];
    int npend;
    unsigned char rx[65536];
    int rxh, rxt;
    /* 假模块 */
    struct {
        int   present;          /* 0=拔线：全静默 */
        int   open[3];          /* 通道传感器开路：TCACTTEMP? → CMD:REPLY=2 */
        int   silent;           /* 1=应答静默（拔线仿真的另一种开关）*/
        float adj[3];           /* TCADJTEMP */
        int   sw[3];            /* TCSW */
        int   sw_stuck[3];      /* 1=该通道"关不动"：设定照回 REPLY=1，回读仍是原值 */
        float act[3];           /* TCACTTEMP 基值 */
        float ramp[3];          /* 每次应答的增量（造渐变到超温区，过的了合理性滤波）*/
        int   n[3];             /* 基值之外的小抖动计数 */
        int   wrong_echo;       /* 1=下一次 TCADJTEMP? 回显 +0.7 后清零；2=恒错 */
        int   reject_set;       /* 1=设定一律回 CMD:REPLY=4 */
        int   drop_pct;         /* 应答丢弃概率%（确定性序列）*/
        tb_kv_t kv[64];         /* 其它参数：<名> → 最近设定值 */
        int   nkv;
        int   nreply;
    } m;
    /* selfloop 应答器语义：应答在"下一拍开头"才写出（m207_rsp_flush）*/
} TB;

static int tb_rnd(void)                    /* 确定性：线性同余，不用库 rand */
{
    static unsigned s = 0x5EED2026u;
    s = s * 1103515245u + 12345u;
    return (int)((s >> 16) & 0x7FFFu);
}

static void tb_reset(void)
{
    memset(&TB, 0, sizeof TB);
    TB.m.present = 1;
    TB.m.act[1] = 25.0f; TB.m.act[2] = 24.0f;
    TB.m.adj[1] = 25.0f; TB.m.adj[2] = 25.0f;
    control_init(NULL);
}

/* ---- 假模块：把一行应答排进送达队列（默认发出后下一拍送达）---- */
static void tb_reply(const char *s)
{
    if (TB.npend >= 64) return;
    TB.pend[TB.npend].due = TB.tick + 1u;
    snprintf(TB.pend[TB.npend].s, sizeof TB.pend[TB.npend].s, "%s", s);
    TB.npend++;
}

/* 直接塞任意字节进 RX 流（下一拍读到）：噪声、截断行、超长行 */
static void tb_rx_inject(const char *s)
{
    for (const char *p = s; *p; p++) {
        TB.rx[TB.rxh] = (unsigned char)*p;
        TB.rxh = (TB.rxh + 1) % (int)sizeof TB.rx;
    }
}

static float tb_mod_get(const char *name, int ch)
{
    (void)ch;
    for (int i = 0; i < TB.m.nkv; i++)
        if (strcmp(TB.m.kv[i].name, name) == 0) return TB.m.kv[i].v;   /* 不分通道：同名同值 */
    return 0.0f;
}

static void tb_mod_set(const char *name, float v)
{
    for (int i = 0; i < TB.m.nkv; i++)
        if (strcmp(TB.m.kv[i].name, name) == 0) { TB.m.kv[i].v = v; return; }
    if (TB.m.nkv < 64) {
        snprintf(TB.m.kv[TB.m.nkv].name, sizeof TB.m.kv[TB.m.nkv].name, "%s", name);
        TB.m.kv[TB.m.nkv].v = v;
        TB.m.nkv++;
    }
}

/* 模块收到一帧（不含 \r）。应答按正典：查询回值、设定回 REPLY=1、保存回 8。 */
static void tb_mod_frame(const char *f)
{
    char line[200];
    if (!TB.m.present || TB.m.silent) return;
    TB.m.nreply++;
    if (TB.m.drop_pct && (tb_rnd() % 100) < TB.m.drop_pct) return;
    int ch = 0;
    const char *p = NULL;
    if (f[0] == 'T' && f[1] == 'C' && (f[2] == '1' || f[2] == '2') && f[3] == ':') {
        ch = f[2] - '0'; p = f + 4;
    } else if (!strncmp(f, "PCB:", 4)) {
        p = f + 4;                      /* 非温控模块：按参数帧应答（正典 §2.5）*/
    } else if (!strncmp(f, "PCRS232:", 8)) {
        p = f + 8;
    }
    if (!p || !*p) { tb_reply("CMD:REPLY=6\r"); return; }
    if (ch && !strcmp(p, "TCACTTEMP?")) {
        if (TB.m.open[ch]) { tb_reply("CMD:REPLY=2\r"); return; }
        TB.m.act[ch] += TB.m.ramp[ch];
        snprintf(line, sizeof line, "TC%d:TCACTTEMP=%.1f\r", ch,
                 (double)(TB.m.act[ch] + 0.1f * (float)(TB.m.n[ch]++ % 10)));
        tb_reply(line);
        return;
    }
    if (ch && !strcmp(p, "TCADJTEMP?")) {
        float echo = TB.m.adj[ch];
        if (TB.m.wrong_echo) {
            echo += 0.7f;
            if (TB.m.wrong_echo == 1) TB.m.wrong_echo = 0;
        }
        snprintf(line, sizeof line, "TC%d:TCADJTEMP=%.2f\r", ch, (double)echo);
        tb_reply(line);
        return;
    }
    if (ch && !strncmp(p, "TCADJTEMP=", 10)) {
        float v = (float)atof(p + 10);
        if (TB.m.reject_set || v > 80.0f || v < -20.0f) { tb_reply("CMD:REPLY=4\r"); return; }
        TB.m.adj[ch] = v;
        tb_reply("CMD:REPLY=1\r");
        return;
    }
    if (ch && !strncmp(p, "TCSW=", 5)) {
        if (TB.m.reject_set) { tb_reply("CMD:REPLY=4\r"); return; }
        if (!TB.m.sw_stuck[ch]) TB.m.sw[ch] = atoi(p + 5);
        tb_reply("CMD:REPLY=1\r");
        return;
    }
    if (ch && !strcmp(p, "TCSW?")) {
        snprintf(line, sizeof line, "TC%d:TCSW=%d\r", ch, TB.m.sw[ch]);
        tb_reply(line);
        return;
    }
    /* 其它参数（TC 通道或 PCB: 前缀）：<名>=<值> 设定 / <名>? 查询 / <名>! 保存 */
    size_t pl = strcspn(p, "=?!");
    char name[16];
    if (pl == 0 || pl >= sizeof name) { tb_reply("CMD:REPLY=6\r"); return; }
    memcpy(name, p, pl); name[pl] = '\0';
    if (p[pl] == '=' && p[pl + 1]) {
        if (TB.m.reject_set) { tb_reply("CMD:REPLY=4\r"); return; }
        tb_mod_set(name, (float)atof(p + pl + 1));
        tb_reply("CMD:REPLY=1\r");
    } else if (p[pl] == '?') {
        /* 回显保持原前缀（TC1:/TC2:/PCB:）：取本帧从开头到参数名之前的那一段 */
        snprintf(line, sizeof line, "%.*s%s=%.3f\r", (int)(p - f), f, name,
                 (double)tb_mod_get(name, ch));
        tb_reply(line);
    } else if (p[pl] == '!') {
        tb_reply("CMD:REPLY=8\r");
    } else {
        tb_reply("CMD:REPLY=2\r");
    }
}

/* ---- 内核的发送出口 / 时钟 ---- */
static int tb_tx(void *ctx, const char *frame, size_t len)
{
    char f[64];
    (void)ctx;
    if (TB.tx_full_every && (TB.ntx % TB.tx_full_every) == (TB.tx_full_every - 1)) {
        TB.tx_dropped++;
        return -1;
    }
    if (len >= 2 && frame[len - 1] == '\r') len--;      /* 记录去 \r，比较用 */
    {
        size_t n = len < sizeof f - 1 ? len : sizeof f - 1;
        memcpy(f, frame, n);
        f[n] = '\0';
    }
    if (TB.ntx < TB_TXCAP) {
        memcpy(TB.tx[TB.ntx].f, f, strlen(f) + 1);
        TB.tx[TB.ntx].tick = TB.tick;
        TB.ntx++;
    }
    tb_mod_frame(f);
    return 0;
}
static int64_t tb_now_us(void *ctx)
{
    (void)ctx;
    return (int64_t)TB.now_ms * 1000;
}

static void tb_init(void)
{
    tb_reset();
    m207_core_init(&(m207_core_io_t){ .tx = tb_tx, .now_us = tb_now_us });
}

static void tb_tick(void)
{
    TB.tick++;
    TB.now_ms += TB_TICK_MS;
    m207_core_tick_begin(TB.now_ms);
    /* 上一拍排入的应答本拍送达（= 真机回环应答器的 m207_rsp_flush）*/
    for (int i = 0; i < TB.npend; ) {
        if (TB.pend[i].due <= TB.tick) {
            tb_rx_inject(TB.pend[i].s);
            TB.pend[i] = TB.pend[TB.npend - 1];
            TB.npend--;
        } else i++;
    }
    if (TB.rxh != TB.rxt) {
        int n = TB.rxh >= TB.rxt ? TB.rxh - TB.rxt : (int)sizeof TB.rx - TB.rxt;
        m207_core_rx(TB.rx + TB.rxt, (size_t)n, TB.now_ms);
        TB.rxt = TB.rxh;
    }
    m207_core_tick_end((int64_t)TB.now_ms * 1000);
}

static void tb_run(int n) { while (n-- > 0) tb_tick(); }

/* ---- TX 记录查询 ---- */
static int tb_tx_find(const char *frame)      /* 首次出现的下标；-1=没发过 */
{
    for (int i = 0; i < TB.ntx; i++)
        if (strcmp(TB.tx[i].f, frame) == 0) return i;
    return -1;
}
static int tb_tx_count(const char *frame)
{
    int c = 0;
    for (int i = 0; i < TB.ntx; i++)
        if (strcmp(TB.tx[i].f, frame) == 0) c++;
    return c;
}
static int tb_tx_since(int idx, const char *frame)   /* 自下标 idx 起首次出现的绝对下标 */
{
    for (int i = idx; i < TB.ntx; i++)
        if (strcmp(TB.tx[i].f, frame) == 0) return i;
    return -1;
}
/* 自 from 起、to 之前（不含）是否发过 frame：用于"下一帧就是它"这类顺序断言 */
static int tb_tx_at(int idx, const char *frame)
{
    return idx >= 0 && idx < TB.ntx && strcmp(TB.tx[idx].f, frame) == 0;
}

/* 上电到上线（最多 max_ticks 拍）；返回 1=已上线 */
static int tb_wait_online(int max_ticks)
{
    while (!m207_online() && max_ticks-- > 0) tb_tick();
    return m207_online();
}

static int tb_done(const char *name)
{
    printf("%s: PASS=%d FAIL=%d\n", name, g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/* param_registry.c 的目标机桩（主机用例不碰 EEPROM 镜像；与 test_control_esp32.c 同款）*/
int eeprom_ready(void) { return 0; }
int eeprom_read_image(uint8_t *img) { (void)img; return -1; }

#endif /* M207_TB_H */
