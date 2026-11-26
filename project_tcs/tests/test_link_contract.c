/**
 * test_link_contract.c — X01-F-05（S 线 e1，2026-11-26）：待发队列深度是协议契约，两端同一个值。
 *
 * 修复前：H743 的 comm.h 写 16、ESP32 的 comm.h 写 8，H743 的注释还留着"注册表 8 条目"的
 * 容量模型；同一条 0x14 突发在两端进入不同深度的队列，8 那一侧更早"满丢最老"。
 * 修复后：值只在 protocol/tcs_proto.h 的 TCS_MAX_TXQ 里写一次，两端 comm.h 从它取，
 * 各带两条 _Static_assert（相等 + 装得下整批重传）。
 *
 * 本用例编译两份 comm.h 各一次（屏端 run_all.sh 与 esp32 run_m207_tests.sh 各跑一遍），
 * 断言的是"编进来的是哪一份"，所以两端的值都要对。
 */
#include <stdio.h>
#include "comm.h"
#include "tcs_proto.h"

static int fails;
static int checks;

#define CHECK(cond, fmt, ...)                                              \
    do {                                                                   \
        checks++;                                                          \
        if (cond) {                                                        \
            printf("  ok   " fmt "\n", ##__VA_ARGS__);                     \
        } else {                                                           \
            printf("  FAIL " fmt "\n", ##__VA_ARGS__);                     \
            fails++;                                                       \
        }                                                                  \
    } while (0)

int main(void)
{
    printf("test_link_contract (X01-F-05)\n");

    /* 1. 协议契约本身 */
    CHECK(TCS_MAX_TXQ == 16u, "TCS_MAX_TXQ = %u (expect 16)", (unsigned)TCS_MAX_TXQ);

    /* 2. 本端 comm.h 从契约取值，而不是各写一个字面量 */
    CHECK(MAX_TXQ == TCS_MAX_TXQ, "MAX_TXQ = %d follows TCS_MAX_TXQ = %u",
          (int)MAX_TXQ, (unsigned)TCS_MAX_TXQ);
    CHECK(MAX_TXQ == 16, "MAX_TXQ = %d (expect 16)", (int)MAX_TXQ);

    /* 3. 容量模型：8 条等 ACK 在途的重传要能整批进队，再加 8 条周期帧/应答余量 */
    CHECK(MAX_TXQ >= COMM_MAX_PENDING + 8,
          "MAX_TXQ = %d >= COMM_MAX_PENDING(%d) + 8", (int)MAX_TXQ, (int)COMM_MAX_PENDING);

    /* 4. 队列存储真的按这个深度分配（防止改了宏没改数组） */
    CHECK(sizeof(((comm_link_t *)0)->tx_queue) / MAX_FRAME == (size_t)MAX_TXQ,
          "tx_queue = %u frames (expect %d)",
          (unsigned)(sizeof(((comm_link_t *)0)->tx_queue) / MAX_FRAME), (int)MAX_TXQ);

    /* 5. H743 注释里的旧容量模型（"注册表 8 条目"）不留残余——这条在源码门禁
     *    （tests/test_link_contract.py）里逐字核对，因为注释进不了二进制。 */

    printf("%s: PASS=%d FAIL=%d\n", "test_link_contract", checks - fails, fails);
    return fails ? 1 : 0;
}
