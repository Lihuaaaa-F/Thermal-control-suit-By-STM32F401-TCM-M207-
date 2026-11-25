/**
 * m207_txq.h — M207 下发指令队列（纯逻辑，零 HAL，主机可测）
 *
 * M07-F-10（2026-11-25，S-G l1）：原来队列、入队函数和 m207_send_raw 都在 m207.c 的
 * "#ifndef TCS_UNIT_TEST" 段里——主机单测编译不到，入队用 strncpy 截到 23 字符这件事
 * 也就一直没有判别性用例：`m207 send <超过 22 字符的指令>` 回 rv=0（"已发送"），实际
 * 被截成另一条指令排进队列。现在队列是独立的一格一格的环形缓冲：
 *   - 过长的指令**不入队、不截断**，返回 -2；
 *   - 队列满返回 -1；空指令 / NULL 返回 -1。
 * m207.c 的驱动部分只在 m207_poll() 里按 50ms 节拍把队头送到串口。
 */
#ifndef TCS_M207_TXQ_H
#define TCS_M207_TXQ_H

#include <stdint.h>

/* 队列深度 8。环形留一格判满（next == tail 即满），所以实际最多排 7 条——与修复前相同
 * （上电两条 TCSW=0 + 若干设定 / 映射参数）。*/
#define M207_TXQ_DEPTH   8u
#define M207_CMD_MAX     23u    /* 一条指令最长 23 字符（含结尾的 \r，不含 NUL）= 每格 24 字节 */

typedef struct {
    char    slot[M207_TXQ_DEPTH][M207_CMD_MAX + 1u];
    uint8_t head;                /* 下一个写入格（生产者前移）*/
    uint8_t tail;                /* 下一个读出格（消费者前移）*/
} m207_txq_t;

/* 清空队列 */
void m207_txq_reset(m207_txq_t *q);

/* 入队。返回 0 = 已入队；-1 = 队列满、指令为空或 NULL；-2 = 指令超过 M207_CMD_MAX 字符。
 * 失败时队列不变（绝不截断）。*/
int m207_txq_push(m207_txq_t *q, const char *cmd);

/* 队头指令（队列为空时 NULL）。只看不取。*/
const char *m207_txq_peek(const m207_txq_t *q);

/* 取走队头（队列为空时什么都不做）*/
void m207_txq_pop(m207_txq_t *q);

/* 队列里的条数 */
uint8_t m207_txq_count(const m207_txq_t *q);

#endif /* TCS_M207_TXQ_H */
