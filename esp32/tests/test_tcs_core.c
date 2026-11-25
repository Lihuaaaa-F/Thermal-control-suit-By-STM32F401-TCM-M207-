/* test_tcs_core.c — tcs_core 里 control.c / param_registry.c 之外的纯逻辑单测（S 线 e3a）
 *
 *   B 系列：上次运行记录 boot_record（X04-F-03 / R5-13）
 *   N 系列：安全记录 v2 编解码（X04-F-03：报警位随锁存落盘）
 *
 * 源：components/tcs_core/boot_record.c + components/tcs_core/nvs_store.c（NVS 读写段在
 * ESP_PLATFORM 下才编译，主机上只剩纯函数）。判别性验证见 tests/mutation_core.py。
 */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "boot_record.h"
#include "nvs_store.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); fflush(stdout); } \
} while (0)

/* ====== B：上次运行记录 ====== */
static void test_boot_record(void)
{
    static boot_record_t rec;           /* 静态：模拟 NOINIT 存储（初始为全 0）*/
    bootrec_prev_t prev;
    uint32_t seq;

    /* B1 首次运行：没有上一轮的记录，序号从 1 起 */
    memset(&rec, 0, sizeof rec);
    seq = bootrec_start(&rec, BOOTREC_RST_SW, &prev);
    CHECK(seq == 1 && prev.valid == 0, "B1 first run: seq=1, no previous record");
    CHECK((rec.flags & 1u) != 0, "B1 first run: 'no previous record' flag set");
    CHECK(rec.reset_reason == BOOTREC_RST_SW && bootrec_valid(&rec),
          "B1 new record is valid and carries this boot's reset reason");

    /* B2 冷上电：RTC 慢速存储里是随机内容——512 张随机"存储映像"没有一张被当成记录 */
    {
        uint32_t x = 0x12345678u;
        int bad = 0;
        for (int t = 0; t < 512; t++) {
            bootrec_prev_t pv;
            uint8_t *p = (uint8_t *)&rec;
            for (size_t i = 0; i < sizeof rec; i++) {
                x = x * 1664525u + 1013904223u;
                p[i] = (uint8_t)(x >> 24);
            }
            (void)bootrec_start(&rec, BOOTREC_RST_POWERON, &pv);
            if (pv.valid || !bootrec_valid(&rec)) bad++;
        }
        CHECK(bad == 0, "B2 cold power-on garbage is never taken as a record (512 random images)");
    }

    /* B3 现场：上一轮运行 1264 s 时自行复位——时长 / 报警位 / 锁存 / 状态 / 它自己的启动原因都要留到下一轮 */
    memset(&rec, 0, sizeof rec);
    (void)bootrec_start(&rec, BOOTREC_RST_POWERON, &prev);
    for (uint32_t s = 1; s <= 1264u; s++)
        bootrec_refresh(&rec, s, (uint16_t)(s == 1264u ? 0x0008u : 0x0000u), 0x01, 3);
    seq = bootrec_start(&rec, BOOTREC_RST_PANIC, &prev);
    CHECK(seq == 2 && prev.valid == 1, "B3 second boot: seq=2, previous record found");
    CHECK(prev.uptime_s == 1264u, "B3 previous uptime survived the reset (1264 s)");
    CHECK(prev.alarm == 0x0008 && prev.latch == 0x01 && prev.ctrl_state == 3,
          "B3 previous alarm / latch / control state survived");
    CHECK(prev.reset_reason == BOOTREC_RST_POWERON, "B3 previous run's own reset reason survived");
    CHECK(rec.reset_reason == BOOTREC_RST_PANIC, "B3 new record carries this boot's reset reason (PANIC)");
    CHECK(bootrec_is_crash(BOOTREC_RST_PANIC), "B3 the current boot sees that the previous run crashed");

    /* B4 记录里任何一位被改坏都判无效 */
    {
        boot_record_t good = rec;
        int missed = 0;
        for (size_t i = 0; i < offsetof(boot_record_t, crc); i++)
            for (int b = 0; b < 8; b++) {
                boot_record_t c = good;
                ((uint8_t *)&c)[i] ^= (uint8_t)(1u << b);
                if (bootrec_valid(&c)) missed++;
            }
        CHECK(missed == 0, "B4 every single-bit flip in the first 24 bytes invalidates the record");
        {
            boot_record_t c = good;
            ((uint8_t *)&c)[offsetof(boot_record_t, crc)] ^= 1u;
            CHECK(!bootrec_valid(&c), "B4 a flipped CRC bit invalidates the record too");
        }
    }

    /* B5 魔数 / 版本 / 长度各自独立把关：就算 CRC 重算对了，别的检查也要拦住 */
    {
        boot_record_t c;
        c = rec; c.ver = BOOTREC_VER + 1u; c.crc = bootrec_crc32((const uint8_t *)&c, 24);
        CHECK(!bootrec_valid(&c), "B5 unknown version is rejected even with a matching CRC");
        c = rec; c.size = 27u; c.crc = bootrec_crc32((const uint8_t *)&c, 24);
        CHECK(!bootrec_valid(&c), "B5 wrong size is rejected even with a matching CRC");
        c = rec; c.magic ^= 0xFFu; c.crc = bootrec_crc32((const uint8_t *)&c, 24);
        CHECK(!bootrec_valid(&c), "B5 wrong magic is rejected even with a matching CRC");
    }

    /* B6 启动序号跨 1000 次复位连续递增；每轮刷新之后记录仍然有效 */
    {
        uint32_t s = 0;
        for (int i = 0; i < 1000; i++) {
            bootrec_prev_t pv;
            s = bootrec_start(&rec, BOOTREC_RST_SW, &pv);
            bootrec_refresh(&rec, (uint32_t)i, (uint16_t)i, (uint8_t)i, (uint8_t)(i & 3));
            if (!bootrec_valid(&rec)) { s = 0; break; }
        }
        CHECK(s == 1002u, "B6 boot sequence number keeps counting across 1000 resets (2 -> 1002)");
    }

    /* B7 复位原因归类：哪些算"异常结束" */
    CHECK(bootrec_is_crash(BOOTREC_RST_PANIC) && bootrec_is_crash(BOOTREC_RST_TASK_WDT)
          && bootrec_is_crash(BOOTREC_RST_INT_WDT) && bootrec_is_crash(BOOTREC_RST_WDT)
          && bootrec_is_crash(BOOTREC_RST_BROWNOUT) && bootrec_is_crash(BOOTREC_RST_CPU_LOCKUP)
          && bootrec_is_crash(BOOTREC_RST_PWR_GLITCH) && bootrec_is_crash(BOOTREC_RST_UNKNOWN),
          "B7 crash-type reset reasons");
    CHECK(!bootrec_is_crash(BOOTREC_RST_POWERON) && !bootrec_is_crash(BOOTREC_RST_SW)
          && !bootrec_is_crash(BOOTREC_RST_EXT) && !bootrec_is_crash(BOOTREC_RST_USB),
          "B7 intended reset reasons are not crashes");
    CHECK(strcmp(bootrec_reset_name(BOOTREC_RST_TASK_WDT), "TASK_WDT") == 0
          && strcmp(bootrec_reset_name(BOOTREC_RST_CPU_LOCKUP), "CPU_LOCKUP") == 0
          && strcmp(bootrec_reset_name(99), "?") == 0, "B7 reset reason names");

    /* B8 布局：28 字节、无填充（跨复位持久、跨编译选项一致的格式）*/
    CHECK(sizeof(boot_record_t) == 28 && offsetof(boot_record_t, crc) == 24,
          "B8 record layout is 28 bytes with the CRC at offset 24");

    /* B9 格式锁定：期望值由独立实现算出（Python zlib.crc32），不是被测代码自己产生的 */
    {
        boot_record_t c;
        const uint8_t v9[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
        memset(&c, 0, sizeof c);
        c.magic = BOOTREC_MAGIC; c.ver = BOOTREC_VER; c.size = 28u; c.seq = 1u;
        c.uptime_s = 1264u; c.alarm = 0x0008u; c.latch = 0x01; c.ctrl_state = 3u;
        c.reset_reason = BOOTREC_RST_BROWNOUT;
        CHECK(bootrec_crc32(v9, 9) == 0xCBF43926u, "B9 CRC-32 check value for \"123456789\"");
        CHECK(bootrec_crc32((const uint8_t *)&c, 24) == 0x5FA160E3u, "B9 golden CRC for a known record (format lock)");
    }

    /* B10 status 的"上次"字段（R5-13：status 同时打印本次和上次；"本次"的 rst= / up= 由 console 照旧打印）*/
    {
        char buf[176];
        bootrec_prev_t p;
        size_t n;
        memset(&p, 0, sizeof p);
        p.valid = 1; p.reset_reason = BOOTREC_RST_POWERON; p.uptime_s = 1264u;
        p.alarm = 0x0008u; p.latch = 0x01; p.ctrl_state = 3u; p.seq = 7u;
        n = bootrec_format_prev(&p, 0x0010, 1, buf, sizeof buf);
        CHECK(n > 0 && n < sizeof buf, "B10 formats into the buffer");
        CHECK(strstr(buf, " prst=1 ") && strstr(buf, " pup=1264s ") && strstr(buf, " palm=0x0008 ")
              && strstr(buf, " plat=0x01 ") && strstr(buf, " pseq=7 ") && strstr(buf, " nvalm=0x0010 ")
              && strstr(buf, " cd=1"),
              "B10 previous-run fields: prst / pup / palm / plat / pseq / nvalm / cd");
        (void)bootrec_format_prev(&p, 0, 0, buf, sizeof buf);
        CHECK(strstr(buf, " cd=0") != 0, "B10 cd=0 when no core dump is stored");
        (void)bootrec_format_prev(NULL, 0x0004, 0, buf, sizeof buf);
        CHECK(strstr(buf, " prst=-1 ") && strstr(buf, " nvalm=0x0004 "),
              "B10 no previous record -> prst=-1 (still prints the saved alarm snapshot)");
        p.valid = 0;
        (void)bootrec_format_prev(&p, 0, 0, buf, sizeof buf);
        CHECK(strstr(buf, " prst=-1 ") != 0, "B10 invalid previous record -> prst=-1");
        CHECK(bootrec_format_prev(&p, 0, 0, buf, 0) == 0 && bootrec_format_prev(&p, 0, 0, NULL, 16) == 0,
              "B10 zero capacity / null buffer are safe");
        {
            char small[8];                 /* 截断安全：缓冲区再小也以 NUL 结尾、不越界 */
            memset(small, 'x', sizeof small);
            (void)bootrec_format_prev(&p, 0xFFFF, 1, small, sizeof small);
            CHECK(small[sizeof small - 1] == '\0', "B10 truncated output stays NUL-terminated");
        }
    }
}

/* ====== N：安全记录 v2（报警位随锁存落盘）====== */
static void test_safety_record(void)
{
    uint8_t b[SAFETY_REC_SIZE];
    uint8_t latch;
    uint16_t alm;

    /* N1 编解码往返 + 逐字节布局 */
    CHECK(safety_rec_encode(b, 0x09, 0x8008) == 0, "N1 encode ok");
    CHECK(b[0] == 0x53 && b[1] == 0x02 && b[2] == 0x09 && b[3] == 0xF6,
          "N1 v2 layout: magic, ver, latch, ~latch");
    CHECK(b[4] == 0x08 && b[5] == 0x80 && b[6] == 0xF7 && b[7] == 0x7F,
          "N1 v2 layout: alarm lo/hi and their complements");
    CHECK(safety_rec_decode(b, sizeof b, &latch, &alm) == 0 && latch == 0x09 && alm == 0x8008,
          "N1 decode round-trip keeps latch AND alarm");

    /* N2 老 v1 记录（R1 时写下的 4 字节）仍然认：锁存恢复，报警位记 0（那时没存）*/
    {
        const uint8_t v1[4] = { 0x53, 0x01, 0x04, 0xFB };
        CHECK(safety_rec_decode(v1, 4, &latch, &alm) == 0 && latch == 0x04 && alm == 0,
              "N2 v1 record still decodes (alarm unknown = 0)");
    }

    /* N3 坏记录：任何一位被改坏都拒；拒的时候 latch 给"未知"，不会被当成"无锁存" */
    {
        int missed = 0;
        for (size_t i = 0; i < sizeof b; i++)
            for (int k = 0; k < 8; k++) {
                uint8_t c[SAFETY_REC_SIZE];
                uint8_t l = 0;
                uint16_t a = 0;
                memcpy(c, b, sizeof c);
                c[i] ^= (uint8_t)(1u << k);
                if (safety_rec_decode(c, sizeof c, &l, &a) == 0) missed++;
            }
        CHECK(missed == 0, "N3 every single-bit flip in a v2 record is rejected");
        b[2] ^= 0x01;
        CHECK(safety_rec_decode(b, sizeof b, &latch, &alm) == -1 && latch == 0xFF,
              "N3 bad record reports 'unknown' latch, not 'no latch'");
    }

    /* N4 长度不对 / 空指针 */
    CHECK(safety_rec_encode(b, 0, 0) == 0, "N4 precondition: fresh record");
    CHECK(safety_rec_decode(b, 7, &latch, &alm) == -1, "N4 truncated v2 record rejected");
    CHECK(safety_rec_decode(b, 3, &latch, &alm) == -1, "N4 too-short record rejected");
    CHECK(safety_rec_decode(0, 8, &latch, &alm) == -1 && latch == 0xFF, "N4 null record rejected safely");
    CHECK(safety_rec_decode(b, 8, 0, &alm) == -1, "N4 null latch output rejected");
    CHECK(safety_rec_encode(0, 1, 2) == -1, "N4 null encode target rejected");
}

int main(void)
{
    test_boot_record();
    test_safety_record();
    printf("test_tcs_core: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
