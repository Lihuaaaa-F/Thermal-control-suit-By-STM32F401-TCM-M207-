/* test_drv_pure.c — 驱动层纯逻辑的 PC 单测（S-G h1a，2026-11-25）
 *
 *   1. 堆上界（M01-F-08）：_sbrk 越过 _Heap_Stack_Limit 必须返回 -1 / ENOMEM。
 *      syscalls_stub.c 原文包含进来（符号改名），_end / _Heap_Stack_Limit 由这里摆布。
 *   2. SDRAM 自检图样（M02-F-05）：覆盖范围 ≥ 两块帧缓冲；单根地址线/数据线卡死都查得出。
 *      Core/Src/ltdc.c 在 TCS_UNIT_TEST 下只编译文件开头的纯逻辑段。
 *
 * 编译：cl/gcc -DTCS_UNIT_TEST tests/test_drv_pure.c Core/Src/ltdc.c
 */
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* ====== 1. 堆上界 ===== */

/* 目标板上堆的上下界是链接脚本给的两个地址。PC 上用一个结构体把"堆区"和"上界"摆成
 * 紧挨着的两段：上界正好在堆区最后一个字节之后，可用空间 = sizeof(heap)，不依赖编译器
 * 对两个独立全局变量的排布。*/
static struct { uint8_t heap[8192]; uint8_t limit[1]; } g_pool;
uint8_t *sbrk_ut_base(void)  { return g_pool.heap; }
uint8_t *sbrk_ut_limit(void) { return g_pool.limit; }

/* 先把这两个头文件包进来：下面的改名宏不能作用到 CRT 头文件里的同名声明上。*/
#include <sys/stat.h>
#include <errno.h>

/* syscalls_stub.c 原文包含进来，符号改名：
 *  _read/_close/_exit 等是 CRT 自己也有的名字，原名定义会和 CRT 打架
 *  （CRT 的 exit() 内部会调 _exit，换成桩里的 while(1) 会把测试挂死）。
 *  CRT 头文件把其中一些定义成了宏（_fstat → _fstat64i32），先 undef 再改。*/
#undef _read
#undef _close
#undef _lseek
#undef _fstat
#undef _isatty
#undef _kill
#undef _getpid
#undef _exit
#undef _sbrk
#define _read              tcs_stub_read
#define _close             tcs_stub_close
#define _lseek             tcs_stub_lseek
#define _fstat             tcs_stub_fstat
#define _isatty            tcs_stub_isatty
#define _kill              tcs_stub_kill
#define _getpid            tcs_stub_getpid
#define _exit              tcs_stub_exit
#define _sbrk              tcs_stub_sbrk
#define g_heap_used_max    tcs_stub_heap_max
/* 桩文件按 newlib 的样子把 errno 当"全局 int"用（#undef 掉宏再声明 extern int errno）。
 * PC 平台上 errno 是宏（MSVC: (*_errno())），没有这个实体——这里给它一个，测试和桩共用。*/
#undef errno
int errno;
#include "../Core/Src/syscalls_stub.c"

#define ROOM ((int)sizeof g_pool.heap)

static void test_sbrk_bound(void)
{
    printf("[sbrk_bound]\n");
    uint8_t *base = g_pool.heap;
    uint8_t *lim = g_pool.limit;
    CHECK(lim - base == ROOM, "limit sits right after the heap area");

    /* 越界的大申请：必须被拒，且不能因为指针相加回绕而"看起来合法" */
    errno = 0;
    CHECK(tcs_stub_sbrk(0x7FFFFFFF) == (void *)-1, "huge request rejected");
    CHECK(errno == ENOMEM, "huge request sets ENOMEM");
    CHECK(tcs_stub_sbrk(ROOM + 1) == (void *)-1, "room+1 rejected");
    CHECK(sbrk_fits(base, 0x7FFFFFFF, lim) != 0, "sbrk_fits: INT_MAX never fits");

    /* 一字节一字节把可用空间用完 */
    int granted = 0;
    int in_range = 1;
    while (granted < ROOM) {
        void *p = tcs_stub_sbrk(1);
        if (p == (void *)-1) break;
        if ((uint8_t *)p != base + granted) in_range = 0;
        granted++;
    }
    CHECK(granted == ROOM, "every byte up to the limit is granted");
    CHECK(in_range, "pointers are consecutive from the base");
    errno = 0;
    CHECK(tcs_stub_sbrk(1) == (void *)-1, "one byte past the limit -> -1");
    CHECK(errno == ENOMEM, "past the limit sets ENOMEM");
    CHECK(tcs_stub_sbrk(1) == (void *)-1, "stays refused");
    CHECK(tcs_stub_heap_max == (uint32_t)ROOM, "heap high-water mark = room");
    /* 归还之后又能分配（_sbrk 归还时返回的是归还前的堆顶）*/
    CHECK(tcs_stub_sbrk(-ROOM) == base + ROOM, "free back to the base");
    CHECK(tcs_stub_sbrk(ROOM - 1) == base, "room-1 bytes granted again from base");
    CHECK(tcs_stub_sbrk(2) == (void *)-1, "2 more bytes don't fit");
    CHECK(tcs_stub_sbrk(1) == base + ROOM - 1, "1 more byte fits exactly");
    CHECK(tcs_stub_sbrk(1) == (void *)-1, "then full");
    printf("  sbrk_bound done (room=%d bytes)\n", ROOM);
}

/* ====== 2. SDRAM 自检图样 ===== */
extern uint32_t sdram_selftest_span(void);
extern uint32_t sdram_selftest_step(void);
extern uint32_t sdram_probe_off(uint32_t k);
extern uint16_t sdram_probe_val(uint32_t k);

#define FRAME_BYTES   (1024u * 600u * 2u)     /* 一块帧缓冲（RGB565 1024×600）*/

static void test_sdram_span(void)
{
    printf("[sdram_span]\n");
    uint32_t span = sdram_selftest_span();
    uint32_t step = sdram_selftest_step();
    CHECK(span >= 2u * FRAME_BYTES, "covers both frame buffers (was 1MB)");
    CHECK(span >= 3u * FRAME_BYTES, "covers the LVGL render chunk after them");
    CHECK(span % step == 0, "span is a whole number of steps");
    CHECK(step == 4096u, "probe spacing = 4KB");
    CHECK((step & 31u) == 0u, "probe addresses are cache-line aligned");
    uint32_t n = span / step;
    uint32_t prev_off = 0;
    int ordered = 1, inside = 1, no_overlap = 1, unique_vals = 1;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t off = sdram_probe_off(k);
        if (off <= prev_off) ordered = 0;
        if (off + 2u > span) inside = 0;
        if (off < 32u) no_overlap = 0;                 /* 开头 32B 留给 16 字连续探测 */
        prev_off = off;
    }
    CHECK(ordered && inside, "probe offsets strictly increasing and inside the span");
    CHECK(no_overlap, "probes do not overlap the 16-word area at the base");
    /* 值两两不同（散列单射）：地址线卡死把两点叠在一起时一定读得出矛盾 */
    for (uint32_t k = 1; k < n && unique_vals; k++)
        for (uint32_t j = 0; j < k; j++)
            if (sdram_probe_val(k) == sdram_probe_val(j)) { unique_vals = 0; break; }
    CHECK(unique_vals, "probe values are pairwise distinct");
    printf("  sdram_span done (span=%u bytes, %u probes)\n", (unsigned)span, (unsigned)n);
}

/* 故障注入：按探测点写图样 → 读回比较。fault_kind/idx 见下。返回 1=查得出。*/
#define FLT_NONE        0
#define FLT_ADDR_STUCK  1     /* 地址线 idx 恒 0：高位地址塌到低位 */
#define FLT_DATA_STUCK0 2     /* 数据线 idx 恒 0 */
#define FLT_DATA_STUCK1 3     /* 数据线 idx 恒 1 */

static uint8_t g_mem[3u * FRAME_BYTES];

static uint32_t phys_addr(uint32_t off, int kind, int idx)
{
    if (kind == FLT_ADDR_STUCK) off &= ~(1u << idx);
    return off;
}

static uint16_t phys_read(uint32_t off, int kind, int idx)
{
    uint16_t v;
    memcpy(&v, &g_mem[phys_addr(off, kind, idx)], 2);
    if (kind == FLT_DATA_STUCK0) v = (uint16_t)(v & ~(1u << idx));
    if (kind == FLT_DATA_STUCK1) v = (uint16_t)(v | (1u << idx));
    return v;
}

static void phys_write(uint32_t off, uint16_t v, int kind, int idx)
{
    if (kind == FLT_DATA_STUCK0) v = (uint16_t)(v & ~(1u << idx));
    if (kind == FLT_DATA_STUCK1) v = (uint16_t)(v | (1u << idx));
    memcpy(&g_mem[phys_addr(off, kind, idx)], &v, 2);
}

static int pattern_detects(int kind, int idx)
{
    uint32_t n = sdram_selftest_span() / sdram_selftest_step();
    memset(g_mem, 0, sizeof(g_mem));
    for (uint32_t k = 0; k < n; k++) phys_write(sdram_probe_off(k), sdram_probe_val(k), kind, idx);
    for (uint32_t k = 0; k < n; k++)
        if (phys_read(sdram_probe_off(k), kind, idx) != sdram_probe_val(k)) return 1;
    return 0;
}

static void test_sdram_fault_detection(void)
{
    printf("[sdram_fault_detection]\n");
    CHECK(pattern_detects(FLT_NONE, 0) == 0, "no fault: pattern reads back clean");
    /* 单根地址线卡死（本图样会走到的 A12..A21）*/
    for (int b = 12; b < 22; b++)
        CHECK(pattern_detects(FLT_ADDR_STUCK, b), "stuck address line detected");
    /* 单根数据线卡 0 / 卡 1 */
    for (int b = 0; b < 16; b++) {
        CHECK(pattern_detects(FLT_DATA_STUCK0, b), "data line stuck at 0 detected");
        CHECK(pattern_detects(FLT_DATA_STUCK1, b), "data line stuck at 1 detected");
    }
    printf("  sdram_fault_detection done\n");
}

/* 对照：旧图样（前 1MB、值 = 偏移 & 0xFFFF）的盲区。写在这里是为了钉住"为什么换图样"：
 * 值每 64KB 重复一次 → A16 起卡死时两个探测点同值，读回一致；低 4 位数据线从来没被驱动过。 */
static void test_sdram_old_pattern_blind_spots(void)
{
    printf("[sdram_old_pattern_blind_spots]\n");
    uint32_t n_old = 0x100000u / 0x1000u;
    /* A16 卡死：0x0000 与 0x10000 塌到同一点，两点的值都是 0x0000 */
    int detected = 0;
    memset(g_mem, 0, sizeof(g_mem));
    for (uint32_t i = 0; i < n_old; i++) {
        uint32_t off = i * 0x1000u;
        g_mem[off & ~(1u << 16)] = (uint8_t)(off & 0xFFFF);
    }
    for (uint32_t i = 0; i < n_old; i++) {
        uint32_t off = i * 0x1000u;
        if (g_mem[off & ~(1u << 16)] != (uint8_t)(off & 0xFFFF)) detected = 1;
    }
    CHECK(!detected, "old pattern misses a stuck A16 (aliased probes share a value)");
    /* 低 4 位数据线：旧图样的值低 4 位恒 0，卡 1 才看得见，卡 0 永远看不见 */
    int low4_ever_set = 0;
    for (uint32_t i = 0; i < n_old; i++)
        if (((i * 0x1000u) & 0xFFFFu) & 0x0Fu) low4_ever_set = 1;
    CHECK(!low4_ever_set, "old pattern never drives D0..D3");
    printf("  sdram_old_pattern_blind_spots done\n");
}

int main(void)
{
    test_sbrk_bound();
    test_sdram_span();
    test_sdram_fault_detection();
    test_sdram_old_pattern_blind_spots();
    printf("test_drv_pure: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
