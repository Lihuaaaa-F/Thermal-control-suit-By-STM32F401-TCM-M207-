/**
 * m207_txq.c — M207 下发指令队列。见 m207_txq.h。
 */
#include "m207_txq.h"
#include <string.h>

void m207_txq_reset(m207_txq_t *q)
{
    q->head = 0;
    q->tail = 0;
}

uint8_t m207_txq_count(const m207_txq_t *q)
{
    /* 环形留一格判满，head 可以绕到 tail 前面：先加一圈再取模，读的人不用想提升规则。
     * （直接写 (head - tail) % DEPTH 在这里是等价的——DEPTH 是 2 的幂、运算是无符号的；
     * 但若有人把 DEPTH 改成非 2 的幂，或先转成 uint8_t 再相减，就会得到 250 这样的数：
     * tests/test_m207_txq.c 的回绕用例与变异 Q3 盯着这一点。）*/
    return (uint8_t)((q->head + M207_TXQ_DEPTH - q->tail) % M207_TXQ_DEPTH);
}

const char *m207_txq_peek(const m207_txq_t *q)
{
    if (q->head == q->tail) return 0;
    return q->slot[q->tail];
}

void m207_txq_pop(m207_txq_t *q)
{
    if (q->head == q->tail) return;
    q->tail = (uint8_t)((q->tail + 1u) % M207_TXQ_DEPTH);
}

int m207_txq_push(m207_txq_t *q, const char *cmd)
{
    if (!cmd || !*cmd) return -1;
    /* M07-F-10：先量长度。超过一格能放下的最长指令就拒绝——原来的写法是
     * strncpy(dst, cmd, 23) 再补 NUL，长指令被静默截成另一条指令发出去。*/
    size_t n = strlen(cmd);
    if (n > M207_CMD_MAX) return -2;
    uint8_t next = (uint8_t)((q->head + 1u) % M207_TXQ_DEPTH);
    if (next == q->tail) return -1;            /* 满 */
    memcpy(q->slot[q->head], cmd, n + 1u);
    q->head = next;
    return 0;
}
