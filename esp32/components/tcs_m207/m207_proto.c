/**
 * m207_proto.c — M207 协议纯函数实现（ESP32 侧复用件）
 *
 * 逐字提取自 project_tcs/App/tcs/m207.c 纯逻辑区（原 19-122 行），Q5 单向同步：
 * 改协议逻辑只许改 project_tcs 源侧再复制过来。提取时刻源 md5 见 docs/results/R1.md。
 */
#include "m207_proto.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

void m207_lineasm_reset(m207_lineasm_t *la)
{
    la->len = 0;
    la->buf[0] = '\0';
}

int m207_lineasm_feed(m207_lineasm_t *la, uint8_t b, char *line, size_t cap)
{
    if (b == '\r' || b == '\n') {
        if (la->len == 0) return 0;            /* 空行忽略（连续 \r\n）*/
        size_t n = la->len;
        if (n >= cap) n = cap - 1;
        memcpy(line, la->buf, n);
        line[n] = '\0';
        la->len = 0;
        la->buf[0] = '\0';
        return 1;
    }
    if (la->len >= M207_LINE_MAX) {            /* 超长：丢弃至行尾，防溢出 */
        return 0;
    }
    la->buf[la->len++] = (char)b;
    la->buf[la->len] = '\0';
    return 0;
}

int m207_parse_line(const char *line, m207_reply_t *out)
{
    if (!line || !out) return 0;
    size_t len = strlen(line);
    if (len == 0 || len > M207_LINE_MAX) return 0;

    /* 小写自动大写到本地副本（F401 同款归一化）*/
    char up[M207_LINE_MAX + 1];
    for (size_t i = 0; i < len; i++) {
        char c = line[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 0x20);
        up[i] = c;
    }
    up[len] = '\0';

    /* F401 ProtocolAnalysis 逻辑：扫 ':'、'='、'?'，取最后出现位置 */
    int colon = -1, eq = -1, qm = -1;
    for (size_t i = 0; i < len; i++) {
        if (up[i] == ':') colon = (int)i;
        else if (up[i] == '=') eq = (int)i;
        else if (up[i] == '?') qm = (int)i;
    }
    if (colon <= 0) return 0;                  /* 缺冒号（或冒号在首位）*/
    if (eq < 0 && qm < 0) return 0;            /* 缺等号/问号 */

    /* CMD:REPLY=N（现行M207协议_v1.md §2/§3）：设/存/错的统一应答。
     * 正典约定不是回显参数名——解析器按此分路（WP-P2 板无关部分）。*/
    if (up[0] == 'C' && up[1] == 'M' && up[2] == 'D' && up[3] == ':'
        && eq >= 0 && strncmp(&up[4], "REPLY", 5) == 0) {
        const char *vs = &up[eq + 1];
        if (*vs == '\0') return 0;
        char *end = NULL;
        long code = strtol(vs, &end, 10);
        if (end == vs || code < 0 || code > 255) return 0;
        out->module = M207_MOD_NONE;
        out->param[0] = '\0';
        out->kind = M207_KIND_ERRCODE;
        out->value = 0.0f;
        out->errcode = (uint8_t)code;
        return 1;
    }

    /* 模块名 TC1/TC2，其它（如 TC3/空）→ module=NONE 且解析失败 */
    int mlen = colon;
    if (mlen != 3 || up[0] != 'T' || up[1] != 'C') return 0;
    if (up[2] != '1' && up[2] != '2') return 0;
    out->module = (up[2] == '1') ? M207_MOD_TC1 : M207_MOD_TC2;

    /* 参数名 = colon+1 .. 分隔符前 */
    int sep = (eq >= 0) ? eq : qm;
    int plen = sep - colon - 1;
    if (plen <= 0 || plen >= (int)sizeof(out->param)) return 0;
    memcpy(out->param, &up[colon + 1], (size_t)plen);
    out->param[plen] = '\0';
    out->kind = (eq >= 0) ? M207_KIND_REPLY : M207_KIND_POLL;
    out->value = 0.0f;

    if (eq >= 0) {
        const char *vs = &up[eq + 1];
        if (*vs == '\0') return 0;             /* '=' 后空值 */
        char *end = NULL;
        float v = strtof(vs, &end);
        if (end == vs) return 0;               /* 非数值 */
        out->value = v;
    }
    return 1;
}

/* ====== H10 映射表模板格式化（纯函数）====== */
int m207_map_format(const m207_map_entry_t *e, float v, char *out, size_t cap)
{
    if (!e || !out || e->ch < 1 || e->ch > 2 || e->cmd[0] == '\0') return -1;
    if (e->decimals > 6) return -1;
    int n = snprintf(out, cap, "TC%u:%s=%.*f\r",
                     (unsigned)e->ch, e->cmd, (int)e->decimals, (double)v);
    if (n < 0 || (size_t)n >= cap) return -1;
    return 0;
}

/* ====== 轮询等待期 ERRCODE 处置分类（纯函数；轴分离：协议错≠通信失败）====== */
m207_pollerr_act_t m207_classify_poll_errcode(uint8_t errcode, int waiting,
                                              const char *wait_param)
{
    if (!waiting) return M207_POLLERR_UNEXPECTED;
    if (errcode == 2 && wait_param && strcmp(wait_param, "TCACTTEMP") == 0)
        return M207_POLLERR_SENSOR_OPEN;
    return M207_POLLERR_LINK_ALIVE;   /* 含 1/8：查询不该回 SET/SAVE ACK，同留痕 */
}
