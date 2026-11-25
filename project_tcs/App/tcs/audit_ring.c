/**
 * audit_ring.c — 关键控制台命令的审计留痕环。见 audit_ring.h。
 */
#include "audit_ring.h"
#include <string.h>

_Static_assert(sizeof(audit_slot_t) * AUDIT_SLOT_COUNT == 512u, "audit ring must be 512 bytes");

void audit_ring_init(audit_ring_t *r)
{
    memset(r, 0, sizeof(*r));
}

void audit_cmd_text(char *out, size_t cap, const char *line)
{
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!line || cap == 1) return;
    size_t o = 0, i = 0;
    int gap = 0;
    while (line[i]) {
        char c = line[i++];
        if (c == ' ' || c == '\t') { gap = 1; continue; }
        if (gap && o) {                      /* 词与词之间只留一个空格 */
            if (o + 1u >= cap) break;
            out[o++] = ' ';
        }
        gap = 0;
        if (o + 1u >= cap) {                /* 这一个字符放不下（要给 NUL 留位置）：截断 */
            if (cap >= 3) { out[o - 1u] = '~'; out[o] = '\0'; }
            return;
        }
        out[o++] = c;
    }
    out[o] = '\0';
}

int audit_is_critical(const char *cmd, const char *sub)
{
    static const char *const whole[] = {
        "estop", "ack",                       /* 急停与确认 */
        "reset",                              /* 复位 */
#if TCS_PROFILE >= 1
        /* 台架及以上才有的命令：交付档不编译它们，命令名也不进交付档的产物
         * （verify_prod_clean.py 按产物查台架命令名）。*/
        "mode", "tec", "fan",                 /* 改模式、驱动执行器 */
        "pindrv", "pinrel",                   /* 驱动引脚 */
        "m207",                               /* m207 send：直写模块指令 */
        "sim",                                /* 仿真注入（含 sim arm = 布防）*/
        "wdttest",                            /* 看门狗测试 */
#endif
        NULL,
    };
    if (!cmd || !*cmd) return 0;
    for (int i = 0; whole[i]; i++)
        if (strcmp(cmd, whole[i]) == 0) return 1;
    if (strcmp(cmd, "p") == 0) {              /* p：只有写操作留痕（p list 不留）*/
        return sub && *sub && strcmp(sub, "list") != 0;
    }
    if (strcmp(cmd, "touch") == 0) {          /* touch：只有标定会改状态 */
        return sub && strcmp(sub, "cal") == 0;
    }
    return 0;
}

int audit_ring_add(audit_ring_t *r, uint32_t ts_ms, const char *cmd, int rv)
{
    if (!r || !cmd || !*cmd) return 0;
    audit_slot_t *s = &r->slot[r->head];
    s->ts = ts_ms;
    s->rv = (int8_t)((rv > 127) ? 127 : (rv < -128) ? -128 : rv);
    strncpy(s->cmd, cmd, AUDIT_CMD_MAX);
    s->cmd[AUDIT_CMD_MAX] = '\0';
    r->head = (uint8_t)((r->head + 1u) % AUDIT_SLOT_COUNT);
    if (r->count < AUDIT_SLOT_COUNT) r->count++;
    r->total++;
    return 1;
}

/* 把非负整数写成十进制到 out，返回字符数。out 至少 12 字节。*/
static unsigned put_uint(char *out, uint32_t a)
{
    char d[12];
    unsigned n = 0, o = 0;
    if (a == 0) d[n++] = '0';
    while (a) { d[n++] = (char)('0' + (a % 10u)); a /= 10u; }
    while (n) out[o++] = d[--n];
    return o;
}

/* 带符号（记录里的返回值是 int8_t）：负数前面加 '-'。out 至少 12 字节。*/
static unsigned put_int(char *out, int v)
{
    unsigned o = 0;
    if (v < 0) { out[o++] = '-'; v = -v; }
    o += put_uint(out + o, (uint32_t)v);
    return o;
}

int audit_ring_format(const audit_ring_t *r, char *out, size_t cap)
{
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    if (!r || cap == 1) return 0;
    size_t o = 0;
    unsigned first = (r->count < AUDIT_SLOT_COUNT) ? 0u
                  : (unsigned)r->head;       /* 满了：最旧的一条就在 head 上 */
    unsigned n = 0;
    for (; n < r->count; n++) {
        const audit_slot_t *s = &r->slot[(first + n) % AUDIT_SLOT_COUNT];
        char num[12], rv[12];
        unsigned dn = put_uint(num, s->ts);  /* 无符号：开机超过 24.8 天也不是负数 */
        unsigned rn = put_int(rv, s->rv);
        size_t clen = strlen(s->cmd);
        if (dn + 1u + clen + 4u + rn + 1u > cap - 1u - o) break;   /* 放不下整条就停，不写半条 */
        memcpy(out + o, num, dn); o += dn;
        out[o++] = ' ';
        memcpy(out + o, s->cmd, clen); o += clen;
        memcpy(out + o, " rv=", 4u); o += 4u;
        memcpy(out + o, rv, rn); o += rn;
        out[o++] = '\n';
    }
    out[o] = '\0';
    return (int)n;
}
