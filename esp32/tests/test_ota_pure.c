/* test_ota_pure.c — OTA 自拷贝的纯逻辑（main/ota_pure.c；S 线 S-G e3b）
 *
 * X03-F-12：单实例闸门。同一时刻第二次触发必须被拒；放开之后下一次能再进；几个线程同时抢，
 * 只有一个拿到。审查时的代码没有这道闸门（每次触发都建一个 ota_copy_task），本文件的用例
 * 对它是红的——见 docs/results/S_G_e3b_结果.md。
 */
#include <stdio.h>
#include <stdint.h>
#include <threads.h>
#include "ota_pure.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static void test_gate_single_thread(void)
{
    ota_gate_leave();                                   /* 从"没人占用"开始 */
    CHECK(ota_gate_busy() == 0, "gate starts free");
    CHECK(ota_gate_try_enter() == 1, "first trigger enters");
    CHECK(ota_gate_try_enter() == 0, "second trigger while the first is running is refused (409)");
    CHECK(ota_gate_try_enter() == 0, "third as well");
    CHECK(ota_gate_busy() == 1, "gate reports busy");
    ota_gate_leave();
    CHECK(ota_gate_busy() == 0, "leave releases the gate");
    CHECK(ota_gate_try_enter() == 1, "after leave the next trigger enters again");
    ota_gate_leave();
}

/* 几个线程同时抢：每一轮都必须恰好一个拿到 */
#define N_RACERS  4
#define N_ROUNDS  300
static volatile int g_go;
static int g_won[N_RACERS];

static int racer(void *arg)
{
    int id = (int)(intptr_t)arg;
    while (!g_go) { /* 等起跑 */ }
    g_won[id] = ota_gate_try_enter();
    return 0;
}

static void test_gate_race(void)
{
    int bad = 0, first_bad = 0;
    for (int r = 0; r < N_ROUNDS; r++) {
        thrd_t t[N_RACERS];
        int sum = 0, created = 0;
        ota_gate_leave();
        for (int i = 0; i < N_RACERS; i++) {
            g_won[i] = -1;
            if (thrd_create(&t[i], racer, (void *)(intptr_t)i) == thrd_success) created++;
        }
        g_go = 1;
        for (int i = 0; i < created; i++) thrd_join(t[i], NULL);
        g_go = 0;
        for (int i = 0; i < N_RACERS; i++) sum += (g_won[i] == 1);
        if (sum != 1 || created != N_RACERS) {
            if (!bad) first_bad = r;
            bad++;
        }
    }
    if (bad) printf("  (race: %d of %d rounds had != 1 winner; first at round %d)\n", bad, N_ROUNDS, first_bad);
    CHECK(bad == 0, "4 threads racing for the gate: exactly one wins, every round");
}

int main(void)
{
    test_gate_single_thread();
    test_gate_race();
    printf("test_ota_pure: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
