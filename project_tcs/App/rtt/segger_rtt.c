/**
 * segger_rtt.c — SEGGER RTT 最小实现（probe-rs 兼容）
 *
 * 标准 SEGGER RTT 控制块格式：16B 魔数 "SEGGER RTT" + up/down buffer 描述符数组。
 * probe-rs attach 先找符号 _SEGGER_RTT，找不到再扫 RAM 找魔数。两者都保证。
 *
 * 只实现上行 channel 0，阻塞写入（满了丢最老数据，环形覆盖）。
 * 主循环调用，不在中断调（格式化耗时 + 非重入）。
 */
#include "segger_rtt.h"
#include <stdarg.h>
#include <string.h>

#define RTT_MAX_UP   1
#define RTT_MAX_DOWN 1
#define RTT_BUF_UP   1024
#define RTT_BUF_DOWN 16

typedef struct {
    const char          *sName;
    char                *pBuffer;
    unsigned             SizeOfBuffer;
    volatile unsigned    WrOff;
    volatile unsigned    RdOff;
    unsigned             Flags;
} rtt_buf_t;

typedef struct {
    char         acID[16];          /* "SEGGER RTT\0\0\0\0\0\0" */
    unsigned     MaxNumUpBuffers;
    unsigned     MaxNumDownBuffers;
    rtt_buf_t    aUp[RTT_MAX_UP];
    rtt_buf_t    aDown[RTT_MAX_DOWN];
} rtt_cb_t;

/* 上行缓冲（静态，避免动态分配）*/
static char rtt_up_buf[RTT_BUF_UP];
static char rtt_down_buf[RTT_BUF_DOWN];

/* RTT 控制块。__used 防 GC，命名 _SEGGER_RTT 让 probe-rs 符号查找命中。*/
__attribute__((used))
rtt_cb_t _SEGGER_RTT = {
    .acID              = "SEGGER RTT\0\0\0\0\0\0",
    .MaxNumUpBuffers   = RTT_MAX_UP,
    .MaxNumDownBuffers = RTT_MAX_DOWN,
    .aUp = {
        { "Terminal", rtt_up_buf, RTT_BUF_UP, 0, 0, 0 },
    },
    .aDown = {
        { "Terminal", rtt_down_buf, RTT_BUF_DOWN, 0, 0, 0 },
    },
};

static int rtt_inited = 0;

void SEGGER_RTT_Init(void)
{
    if (rtt_inited) return;
    /* 控制块已是静态初始化，这里只确保魔数完整（防被覆盖）*/
    _SEGGER_RTT.acID[0]  = 'S';
    _SEGGER_RTT.acID[1]  = 'E';
    _SEGGER_RTT.acID[2]  = 'G';
    _SEGGER_RTT.acID[3]  = 'G';
    _SEGGER_RTT.acID[4]  = 'E';
    _SEGGER_RTT.acID[5]  = 'R';
    _SEGGER_RTT.acID[6]  = ' ';
    _SEGGER_RTT.acID[7]  = 'R';
    _SEGGER_RTT.acID[8]  = 'T';
    _SEGGER_RTT.acID[9]  = 'T';
    _SEGGER_RTT.acID[10] = 0;
    rtt_inited = 1;
}

/* 原子地写一段到 up buffer 0（环形，满则覆盖未读的最老数据）*/
unsigned SEGGER_RTT_Write(const char *buf, unsigned len)
{
    if (!rtt_inited) SEGGER_RTT_Init();
    rtt_buf_t *b = &_SEGGER_RTT.aUp[0];
    unsigned wr = b->WrOff;
    for (unsigned i = 0; i < len; i++) {
        b->pBuffer[wr] = buf[i];
        wr++;
        if (wr >= b->SizeOfBuffer) wr = 0;
    }
    b->WrOff = wr;   /* 最后一次性更新 WrOff，host 读到的是完整段 */
    return len;
}

unsigned SEGGER_RTT_WriteString(const char *s)
{
    return SEGGER_RTT_Write(s, (unsigned)strlen(s));
}

/* —— 简化 printf —— */
static void put_str(const char *s, unsigned *len)
{
    *len += SEGGER_RTT_Write(s, (unsigned)strlen(s));
}

static void put_uint(unsigned long v, int base, int width, char pad, int upper)
{
    char tmp[16];
    int n = 0;
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    if (v == 0) tmp[n++] = '0';
    while (v && n < 15) { tmp[n++] = digits[v % (unsigned)base]; v /= (unsigned)base; }
    while (n < width) { tmp[n++] = pad; }
    /* 反向输出 */
    char out[16];
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    SEGGER_RTT_Write(out, (unsigned)n);
}

static void put_int(long v, int width, char pad)
{
    char tmp[16];
    int n = 0;
    int neg = 0;
    unsigned long uv;
    if (v < 0) { neg = 1; uv = (unsigned long)(-v); }
    else uv = (unsigned long)v;
    if (uv == 0) tmp[n++] = '0';
    while (uv && n < 15) { tmp[n++] = (char)('0' + uv % 10); uv /= 10; }
    if (neg) tmp[n++] = '-';
    while (n < width) tmp[n++] = pad;
    char out[16];
    for (int i = 0; i < n; i++) out[i] = tmp[n - 1 - i];
    SEGGER_RTT_Write(out, (unsigned)n);
}

void SEGGER_RTT_printf(unsigned channel, const char *fmt, ...)
{
    (void)channel;
    if (!rtt_inited) SEGGER_RTT_Init();
    va_list ap;
    va_start(ap, fmt);
    const char *p = fmt;
    while (*p) {
        if (*p != '%') {
            char c = *p++;
            SEGGER_RTT_Write(&c, 1);
            continue;
        }
        p++;  /* 跳过 % */
        char pad = ' ';
        int width = 0;
        if (*p == '0') { pad = '0'; p++; }
        while (*p >= '0' && *p <= '9') { width = width * 10 + (*p - '0'); p++; }
        switch (*p) {
        case 'd': case 'i': {
            long v = va_arg(ap, int);
            put_int(v, width, pad);
            break;
        }
        case 'u': {
            unsigned v = va_arg(ap, unsigned int);
            put_uint(v, 10, width, pad, 0);
            break;
        }
        case 'x': {
            unsigned v = va_arg(ap, unsigned int);
            put_uint(v, 16, width, pad, 0);
            break;
        }
        case 'X': {
            unsigned v = va_arg(ap, unsigned int);
            put_uint(v, 16, width, pad, 1);
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            unsigned dummy = 0;
            put_str(s ? s : "(null)", &dummy);
            break;
        }
        case 'c': {
            char c = (char)va_arg(ap, int);
            SEGGER_RTT_Write(&c, 1);
            break;
        }
        case '%': {
            char c = '%';
            SEGGER_RTT_Write(&c, 1);
            break;
        }
        default:
            /* 未知格式原样输出 */
            { char c = '%'; SEGGER_RTT_Write(&c, 1); char d = *p; if (d) SEGGER_RTT_Write(&d, 1); }
            break;
        }
        if (*p) p++;
    }
    va_end(ap);
}
