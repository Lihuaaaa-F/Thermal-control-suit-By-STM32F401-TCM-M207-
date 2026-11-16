/**
 * m207_proto.c — M207 协议纯函数实现（ESP32 侧复用件）
 *
 * 逐字提取自 project_tcs/App/tcs/m207.c 纯逻辑区（原 19-122 行），Q5 单向同步：
 * 改协议逻辑只许改 project_tcs 源侧再复制过来。提取时刻源 md5 见 docs/results/R1.md。
 * 官方查证：检索组件库 2026-10-26 无官方件（M207 私有协议行文法）；Q5 逐字提取件（与 project_tcs/App/tcs/m207.c 纯逻辑区单向同步），字节级正确性由 PC 53+16 断言+真机九码实录+project_tcs/tools/proto_ref.py 双实现互证背书；实现参考：不适用（Q5 逐字提取件，参照物=H743 侧同源纯逻辑区+project_tcs/tools/proto_ref.py 双实现互证，非官方示例移植）
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

    /* 模块名前缀 → 枚举（M12-F-09：原按 `mlen==3` 判定，PCB/SYSTEM/PCRS232 这些
     * 同总线模块会被无差别丢掉，且模块名长度一变判定就全错）。冒号前必须与表项
     * 完全相等，未知模块整行忽略——不猜。*/
    static const struct { const char *name; uint8_t mod; } mods[] = {
        { "TC1",     M207_MOD_TC1     },
        { "TC2",     M207_MOD_TC2     },
        { "PCB",     M207_MOD_PCB     },
        { "SYSTEM",  M207_MOD_SYSTEM  },
        { "PCRS232", M207_MOD_PCRS232 },
    };
    uint8_t mod = M207_MOD_NONE;
    for (size_t k = 0; k < sizeof(mods) / sizeof(mods[0]); k++) {
        size_t nl = strlen(mods[k].name);
        if ((size_t)colon == nl && strncmp(up, mods[k].name, nl) == 0) {
            mod = mods[k].mod;
            break;
        }
    }
    if (mod == M207_MOD_NONE) return 0;
    out->module = mod;

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

/* 模块号 → 名字（日志/取证用；M12-F-09 与新枚举同源）*/
const char *m207_mod_name(uint8_t mod)
{
    switch (mod) {
    case M207_MOD_TC1:     return "TC1";
    case M207_MOD_TC2:     return "TC2";
    case M207_MOD_PCB:     return "PCB";
    case M207_MOD_SYSTEM:  return "SYSTEM";
    case M207_MOD_PCRS232: return "PCRS232";
    default:               return "?";
    }
}

/* ====== H10 映射表模板格式化（纯函数）====== */
int m207_map_format(const m207_map_entry_t *e, float v, char *out, size_t cap)
{
    if (!e || !out || e->cmd[0] == '\0' || e->decimals > 6) return -1;
    /* M12-F-05：前缀按模块归属。mod==0 → TC{ch}（TC1/TC2 通道参数）；
     * mod!=0 → 模块名（PCB/SYSTEM/PCRS232）。原来 0x120-0x126 用 TC1 前缀下发
     * PCB 参数，模块必拒，而网页 ACK 还报成功。*/
    char pfx[12];
    if (e->mod) {
        const char *nm = m207_mod_name(e->mod);
        if (nm[0] == '?') return -1;
        snprintf(pfx, sizeof pfx, "%s", nm);
    } else {
        if (e->ch < 1 || e->ch > 2) return -1;
        snprintf(pfx, sizeof pfx, "TC%u", (unsigned)e->ch);
    }
    int n = snprintf(out, cap, "%s:%s=%.*f\r",
                     pfx, e->cmd, (int)e->decimals, (double)v);
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
