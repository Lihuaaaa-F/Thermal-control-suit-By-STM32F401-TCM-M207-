/* test_m207_txq.c — M07-F-10（2026-11-25，S-G l1）：M207 下发指令队列不截断过长指令；
 * m207_send_raw 的返回值说清楚"有没有入队"。
 *
 * 背景：`m207 send <超过 22 字符的指令>` 原来回 rv=0（读起来像"已发送"），指令实际被 24 字节的
 * 下发队列用 strncpy 截成另一条指令。队列与 m207_send_raw 原先都在 m207.c 的
 * "#ifndef TCS_UNIT_TEST" 段里，主机单测编译不到——这个缺陷一直没有判别性用例。
 *
 * 判别性：文件里照抄了修复前的入队函数（old_txq_push），同一组输入在它上面"接受并截断"，
 * 在新实现上"拒绝、队列不变"。被测实现：App/tcs/m207_txq.c 与 m207.c 纯逻辑段的 m207_send_raw。
 */
#include <stdio.h>
#include <string.h>
#include "m207.h"
#include "m207_txq.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* ---- 修复前的入队函数（m207.c 原实现，逐字照抄；只用于对照）---- */
#define OLD_TXQ_DEPTH 8u
#define OLD_TXQ_LEN   24u
typedef struct { char slot[OLD_TXQ_DEPTH][OLD_TXQ_LEN]; unsigned head, tail; } old_txq_t;
static int old_txq_push(old_txq_t *q, const char *cmd)
{
    unsigned next = (q->head + 1u) % OLD_TXQ_DEPTH;
    if (next == q->tail) return -1;         /* 满 */
    strncpy(q->slot[q->head], cmd, OLD_TXQ_LEN - 1);
    q->slot[q->head][OLD_TXQ_LEN - 1] = '\0';
    q->head = next;
    return 0;
}

/* 几何不变：每格 24 字节、深度 8（环形留一格判满，实际最多 7 条）——与修复前相同 */
_Static_assert(M207_CMD_MAX == 23u, "command slot length unchanged");
_Static_assert(M207_CMD_MAX + 1u == 24u, "queue slot is 24 bytes, same as before");
_Static_assert(M207_TXQ_DEPTH == OLD_TXQ_DEPTH, "queue depth unchanged");

/* n 个字符的原始指令（不含 \r）："TC1:X" + (n-5) 个 'A' */
static char g_raw[64];
static const char *mkraw(int n)
{
    memset(g_raw, 'A', (size_t)n);
    memcpy(g_raw, "TC1:X", 5);
    g_raw[n] = '\0';
    return g_raw;
}
static char g_cmd[96];
static const char *with_cr(const char *raw)      /* raw + "\r" */
{
    snprintf(g_cmd, sizeof(g_cmd), "%s\r", raw);
    return g_cmd;
}

int main(void)
{
    m207_txq_t q;
    const char *c;

    CHECK(strlen(mkraw(22)) == 22, "前置：22 字符的输入");
    CHECK(strlen(mkraw(23)) == 23, "前置：23 字符的输入");
    CHECK(strlen(mkraw(30)) == 30, "前置：30 字符的输入");

    /* ---- 修复前的行为（对照）：30 字符的指令被接受，截成 23 字符的半截指令 ---- */
    {
        old_txq_t o;
        memset(&o, 0, sizeof(o));
        c = with_cr(mkraw(30));                    /* 31 字符 */
        CHECK(strlen(c) == 31, "前置：31 字符（含 CR）");
        CHECK(old_txq_push(&o, c) == 0, "修复前：30 字符指令被接受（rv=0，读起来像已发送）");
        CHECK(strlen(o.slot[0]) == 23 && strncmp(o.slot[0], c, 23) == 0 && o.slot[0][22] != '\r',
              "修复前：被截成 23 字符的半截指令（结尾不再是 CR）");
    }

    /* ---- 修复后：过长的指令不入队、不截断 ---- */
    m207_txq_reset(&q);
    c = with_cr(mkraw(30));
    CHECK(m207_txq_push(&q, c) == -2, "过长：返回 -2");
    CHECK(m207_txq_count(&q) == 0 && m207_txq_peek(&q) == 0, "过长：队列不变，没有半截指令");

    /* ---- 上限的两端：恰好 23 字符（22 + CR）可以；24 字符不可以 ---- */
    c = with_cr(mkraw(22));
    CHECK(strlen(c) == 23 && m207_txq_push(&q, c) == 0, "恰好 23 字符（22 + CR）入队");
    CHECK(m207_txq_count(&q) == 1 && strcmp(m207_txq_peek(&q), c) == 0, "入队的内容逐字节不变");
    CHECK(strlen(with_cr(mkraw(23))) == 24 && m207_txq_push(&q, with_cr(mkraw(23))) == -2,
          "24 字符（23 + CR）被拒绝");
    CHECK(m207_txq_count(&q) == 1, "被拒绝的指令没有排进去");

    /* ---- 空指令 / NULL ---- */
    CHECK(m207_txq_push(&q, "") == -1, "空指令返回 -1");
    CHECK(m207_send_raw(NULL) == -1, "m207_send_raw(NULL) 返回 -1");
    CHECK(m207_send_raw("") == -1, "m207_send_raw(\"\") 返回 -1");

    /* ---- 队列满：环形留一格判满，实际最多 7 条（与修复前相同）；满了返回 -1 ---- */
    m207_txq_reset(&q);
    {
        int i, full_at = 0;
        for (i = 0; i < (int)M207_TXQ_DEPTH + 2; i++) {
            char k[32];
            snprintf(k, sizeof(k), "TC1:TCSW=%d\r", i & 1);
            if (m207_txq_push(&q, k) != 0) { full_at = i + 1; break; }
        }
        CHECK(full_at == (int)M207_TXQ_DEPTH, "队列满：第 8 条返回非 0（环形留一格，实际最多 7 条，与修复前相同）");
        CHECK(m207_txq_count(&q) == M207_TXQ_DEPTH - 1, "队列满：7 条");
        CHECK(m207_txq_push(&q, "TC1:TCSW=1\r") == -1, "队列满：返回的是 -1（满），不是 -2（过长）");
        /* 修复前的实现容量也是 7：第 8 条失败 */
        {
            old_txq_t o; int j, fa = 0;
            memset(&o, 0, sizeof(o));
            for (j = 0; j < 10; j++) if (old_txq_push(&o, "TC1:TCSW=0\r") != 0) { fa = j + 1; break; }
            CHECK(fa == (int)M207_TXQ_DEPTH, "修复前：容量同样是 7（第 8 条失败）");
        }
    }

    /* ---- 回绕：head 绕到 tail 前面之后，条数与队头仍然正确
     * （把条数写成"先转 uint8_t 再相减"会在这里得到 255——变异 Q3 盯着这一点）---- */
    m207_txq_reset(&q);
    {
        int i, bad = 0;
        for (i = 0; i < 7; i++) { char k[16]; snprintf(k, sizeof k, "A%d\r", i); if (m207_txq_push(&q, k) != 0) bad++; }
        bad += (m207_txq_count(&q) != 7);
        for (i = 0; i < 5; i++) m207_txq_pop(&q);
        bad += (m207_txq_count(&q) != 2);                 /* head = 7，tail = 5 */
        for (i = 0; i < 5; i++) { char k[16]; snprintf(k, sizeof k, "B%d\r", i); if (m207_txq_push(&q, k) != 0) bad++; }
        bad += (m207_txq_count(&q) != 7);                 /* head = 4，tail = 5：head < tail */
        bad += (strcmp(m207_txq_peek(&q), "A5\r") != 0);  /* 先进先出跨过回绕点 */
        m207_txq_pop(&q); m207_txq_pop(&q);
        bad += (strcmp(m207_txq_peek(&q), "B0\r") != 0 || m207_txq_count(&q) != 5);
        for (i = 0; i < 5; i++) m207_txq_pop(&q);
        bad += (m207_txq_count(&q) != 0 || m207_txq_peek(&q) != 0);
        CHECK(bad == 0, "回绕之后条数与队头都正确");
    }

    /* ---- 先进先出；取走后可以再入队 ---- */
    m207_txq_reset(&q);
    CHECK(m207_txq_push(&q, "TC1:TCSW=0\r") == 0 && m207_txq_push(&q, "TC2:TCSW=0\r") == 0 &&
          m207_txq_push(&q, "TC1:TCADJTEMP=25.0\r") == 0, "三条都入队");
    CHECK(strcmp(m207_txq_peek(&q), "TC1:TCSW=0\r") == 0, "队头是第一条");
    m207_txq_pop(&q);
    CHECK(strcmp(m207_txq_peek(&q), "TC2:TCSW=0\r") == 0 && m207_txq_count(&q) == 2, "取走一条后队头是第二条");
    m207_txq_pop(&q); m207_txq_pop(&q);
    CHECK(m207_txq_count(&q) == 0 && m207_txq_peek(&q) == 0, "全部取走后为空");
    m207_txq_pop(&q);
    CHECK(m207_txq_count(&q) == 0, "空队列再取不出错");
    CHECK(m207_txq_push(&q, "TC1:TCSW=1\r") == 0, "取空后可以再入队");
    m207_txq_reset(&q);
    CHECK(m207_txq_count(&q) == 0, "reset 清空");
    /* 绕回一圈之后下标不越界 */
    {
        int i, bad = 0;
        for (i = 0; i < 40; i++) {
            char k[32];
            snprintf(k, sizeof(k), "TC%u\r", (unsigned)(i & 1));
            if (m207_txq_push(&q, k) != 0) bad++;
            m207_txq_pop(&q);
        }
        CHECK(bad == 0 && m207_txq_count(&q) == 0, "绕回 40 次不出错");
    }

    /* ---- m207_send_raw：走模块级的队列（上电为空，本用例只在最后用一次）。
     * 返回值分三种：-2 过长（不入队）、0 已入队、-1 空指令 ---- */
    CHECK(m207_send_raw(with_cr(mkraw(30))) == -2, "m207_send_raw：过长返回 -2（不入队）");
    CHECK(m207_send_raw("TC1:TCSW=0\r") == 0, "m207_send_raw：正常返回 0 = 已入队");

    printf("test_m207_txq: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
