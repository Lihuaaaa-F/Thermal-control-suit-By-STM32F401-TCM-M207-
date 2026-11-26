/* test_ota_pure.c — OTA 自拷贝的纯逻辑（main/ota_pure.c；S 线 S-G e3b）
 *
 * X03-F-12：单实例闸门。同一时刻第二次触发必须被拒；放开之后下一次能再进；几个线程同时抢，
 * 只有一个拿到。
 * M08-F-12：运行镜像的长度。段头一个接一个排在各自段数据的后面，必须顺着读下去，每一步先校验
 * 偏移再读；镜像头或段表不合法时不给长度，读函数不会收到越界的请求。
 * 审查时的代码这两处都没有（每个触发都建一个任务；只读开头 256 字节、按偏移取段头），本文件的用例
 * 对它是红的——见 docs/results/S_G_e3b_结果.md。
 *
 * 用法：test_ota_pure.exe [固件.bin]   —— 给了一个真实的 esp_app 镜像时，再核对
 *        "算出来的长度 == 文件长度"（.bin 以 SHA-256 结尾，文件长度就是镜像全长）。
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include "ota_pure.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* ====== 单实例闸门（X03-F-12）====== */
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

/* ====== 镜像长度（M08-F-12）====== */
typedef struct {
    uint8_t  *img;
    uint32_t  size;      /* 分区大小，也是合成镜像缓冲的大小 */
    int       oob;       /* 读函数被要求读分区之外的次数：必须一直是 0 */
    int       fail_at;   /* 第几次读失败（0 = 不失败）*/
    int       reads;
} img_t;

static int img_rd(uint32_t off, uint8_t *dst, uint32_t len, void *ctx)
{
    img_t *im = ctx;
    im->reads++;
    if (off > im->size || len > im->size - off) { im->oob++; return -1; }
    if (im->fail_at && im->reads >= im->fail_at) return -1;
    memcpy(dst, im->img + off, len);
    return 0;
}

static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

/* 造一个 esp_app 镜像：段数据全 0（长度计算不看内容）*/
static img_t *mk_img(uint32_t part_size, const uint32_t *lens, int n, int hash)
{
    img_t *im = calloc(1, sizeof *im);
    if (!im) { printf("FAIL: out of memory\n"); exit(2); }
    im->img = calloc(1, part_size ? part_size : 1u);
    if (!im->img) { printf("FAIL: out of memory\n"); exit(2); }
    im->size = part_size;
    if (part_size >= 24u) {
        im->img[0] = 0xE9;
        im->img[1] = (uint8_t)n;
        im->img[23] = (uint8_t)(hash ? 1 : 0);
    }
    uint32_t off = 24;
    for (int i = 0; i < n; i++) {
        if (off + 8u <= part_size) put32(im->img + off + 4, lens[i]);
        off += 8 + lens[i];
    }
    return im;
}

static void free_img(img_t *im)
{
    if (im) { free(im->img); free(im); }
}

/* 期望值按格式定义独立写一遍（不引用被测函数）*/
static uint32_t expect_len(const uint32_t *lens, int n, int hash)
{
    uint32_t end = 24;
    for (int i = 0; i < n; i++) end += 8 + lens[i];
    end = ((end + 1) + 15u) & ~15u;      /* 校验字节所在的 16 字节块 */
    return end + (hash ? 32u : 0u);
}

static int len_of(img_t *im, uint32_t *len)
{
    im->reads = 0;
    return ota_image_len(im->size, img_rd, im, len);
}

static void test_len_valid(void)
{
    uint32_t len = 0;
    {
        const uint32_t l[] = { 100 };
        img_t *im = mk_img(0x300000u, l, 1, 1);
        int rc = len_of(im, &len);
        CHECK(rc == OTA_IMG_OK && len == 176u,
              "1 segment of 100 B + hash: 24+8+100 = 132 -> pad to 144 -> +32 = 176");
        CHECK(rc == OTA_IMG_OK && len == expect_len(l, 1, 1) && im->oob == 0,
              "agrees with the format formula; no read outside the partition");
        free_img(im);
    }
    {
        /* 真实固件的形状：4 段，第一段就 12KB——后面三个段头都在开头的 256 字节之外 */
        const uint32_t l[] = { 0x3000, 0x800, 0x2000, 0x400 };
        img_t *im = mk_img(0x300000u, l, 4, 1);
        int rc = len_of(im, &len);
        CHECK(rc == OTA_IMG_OK && len == expect_len(l, 4, 1),
              "4 segments (12K/2K/8K/1K): length follows all four segment headers");
        CHECK(im->oob == 0, "4 segments: no read outside the partition");
        free_img(im);
    }
    {
        /* 两个镜像开头的 256 字节完全相同，只有第二段的长度不同（200 / 216，差一个 16 字节块）：
         * 长度必须跟着变 */
        const uint32_t a[] = { 224, 200 }, b[] = { 224, 216 };
        uint32_t la = 0, lb = 0;
        img_t *ia = mk_img(0x300000u, a, 2, 1), *ib = mk_img(0x300000u, b, 2, 1);
        int ra = len_of(ia, &la), rb = len_of(ib, &lb);
        CHECK(ra == OTA_IMG_OK && rb == OTA_IMG_OK && la == expect_len(a, 2, 1) && lb == expect_len(b, 2, 1),
              "2 segments, 2nd header just past the first 256 bytes: both lengths exact");
        CHECK(la != lb, "a different 2nd segment length gives a different image length");
        CHECK(ia->oob == 0 && ib->oob == 0, "2 segments: no read outside the partition");
        free_img(ia); free_img(ib);
    }
    {
        /* 审查给的恶意头：首段 128KB。第二个段头要顺着读过去才拿得到 */
        const uint32_t l[] = { 0x20000, 0x100 };
        img_t *im = mk_img(0x300000u, l, 2, 1);
        int rc = len_of(im, &len);
        CHECK(rc == OTA_IMG_OK && len == expect_len(l, 2, 1),
              "review's crafted header: first segment 128 KB, 2nd header found by following the chain");
        CHECK(im->oob == 0, "128 KB first segment: no read outside the partition");
        free_img(im);
    }
    {
        const uint32_t l[] = { 100 };
        img_t *im = mk_img(0x300000u, l, 1, 0);
        int rc = len_of(im, &len);
        CHECK(rc == OTA_IMG_OK && len == 144u,
              "no hash appended (header byte 23 = 0): stops after the checksum block");
        free_img(im);
    }
    {
        /* 恰好放得下 / 差 1 字节放不下 */
        const uint32_t l[] = { 100 };
        img_t *im = mk_img(176u, l, 1, 1);
        CHECK(len_of(im, &len) == OTA_IMG_OK && len == 176u, "image exactly fills the partition");
        free_img(im);
        im = mk_img(175u, l, 1, 1);
        CHECK(len_of(im, &len) == OTA_IMG_ETOOLONG && im->oob == 0, "1 byte short: ETOOLONG");
        free_img(im);
    }
}

static void test_len_rejects(void)
{
    uint32_t len = 0;
    {
        img_t *im = mk_img(0x300000u, NULL, 0, 1);
        im->img[0] = 0;
        CHECK(len_of(im, &len) == OTA_IMG_EMAGIC, "bad magic -> EMAGIC (not an app image)");
        free_img(im);
    }
    {
        img_t *im = mk_img(0x300000u, NULL, 0, 1);
        CHECK(len_of(im, &len) == OTA_IMG_ESEGS, "0 segments -> ESEGS");
        free_img(im);
    }
    {
        const uint32_t l[] = { 0x300000u };        /* 首段数据就"越过分区"：审查说的"段长很大" */
        img_t *im = mk_img(0x300000u, l, 1, 1);
        CHECK(len_of(im, &len) == OTA_IMG_EBOUND && im->oob == 0,
              "segment data runs past the partition -> EBOUND, no read past the end");
        free_img(im);
    }
    {
        const uint32_t l[] = { 0xFFFFFFF0u, 16 };  /* 24 + 8 + len 在 32 位里回绕 */
        img_t *im = mk_img(0x300000u, l, 2, 1);
        CHECK(len_of(im, &len) == OTA_IMG_EBOUND && im->oob == 0,
              "segment length wraps around 32 bits -> EBOUND");
        free_img(im);
    }
    {
        const uint32_t l[10] = { 0 };              /* 10 个空段，分区只放得下 3 个段头再多 4 字节 */
        img_t *im = mk_img(24u + 3u * 8u + 4u, l, 10, 1);
        CHECK(len_of(im, &len) == OTA_IMG_EBOUND && im->oob == 0, "4th segment header does not fit -> EBOUND");
        free_img(im);
    }
    {
        img_t *im = mk_img(16u, NULL, 0, 1);       /* 连镜像头都放不下 */
        CHECK(len_of(im, &len) == OTA_IMG_EBOUND && im->oob == 0 && im->reads == 0,
              "partition shorter than the 24-byte header -> EBOUND without any read");
        free_img(im);
    }
    {
        img_t *im = mk_img(0x300000u, (const uint32_t[]){ 100, 200 }, 2, 1);
        im->fail_at = 1;
        CHECK(len_of(im, &len) == OTA_IMG_EREAD, "header read fails -> EREAD");
        im->fail_at = 2;
        CHECK(len_of(im, &len) == OTA_IMG_EREAD, "segment header read fails -> EREAD");
        free_img(im);
    }
    {
        img_t *im = mk_img(0x300000u, NULL, 0, 1);
        CHECK(ota_image_len(im->size, NULL, im, &len) == OTA_IMG_EREAD, "no reader -> EREAD");
        free_img(im);
    }
}

/* 真实固件：算出来的长度必须等于 .bin 的文件长度 */
static void test_real_image(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  (real image %s: cannot open)\n", path); return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (sz > 0) ? malloc((size_t)sz) : NULL;
    if (!data || fread(data, 1, (size_t)sz, f) != (size_t)sz) {
        printf("FAIL: real image %s: cannot read (%ld bytes)\n", path, sz);
        g_fail++;
        free(data); fclose(f);
        return;
    }
    fclose(f);
    img_t im = { data, (uint32_t)sz, 0, 0, 0 };
    uint32_t len = 0;
    int rc = len_of(&im, &len);
    printf("  (real image %s: rc=%d computed=%lu file=%ld segments=%u reads=%d)\n",
           path, rc, (unsigned long)len, sz, data[1], im.reads);
    CHECK(rc == OTA_IMG_OK && len == (uint32_t)sz, "real firmware image: computed length == file size");
    CHECK(im.oob == 0, "real firmware image: no read outside the partition");
    free(data);
}

/* 旧做法（只读开头 256 字节、按偏移取段头）越界读的是"自己上方的栈"。先把上方 256KB 摸一遍并清零：
 * 实现若退回旧做法，读到的是 0，红灯是下面这些明确的 FAIL 行，而不是一次访问冲突把输出吞掉。
 * 现在的实现不读缓冲之外，这段对它是多余的保险。 */
static volatile uint8_t g_sink;

static void primed_call(void (*fn)(void))
{
    uint8_t pad[256u * 1024u];
    memset(pad, 0, sizeof pad);
    fn();
    for (size_t i = 0; i < sizeof pad; i += 4096u) g_sink = ((volatile uint8_t *)pad)[i];
}

static void test_len_all(void)
{
    test_len_valid();
    test_len_rejects();
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);   /* 实现若越界读把进程弄崩，之前的 FAIL 行也要留在输出里 */
    test_gate_single_thread();
    test_gate_race();
    primed_call(test_len_all);
    if (argc > 1) test_real_image(argv[1]);
    printf("test_ota_pure: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
