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
#include "svc_status.h"
#include "param_registry.h"

static int g_pass, g_fail;

/* param_registry.c 的 param_load_eeprom()（零调用死函数）引用的 NVS 桩：nvs_store.c 的读写段
 * 在 ESP_PLATFORM 下才编译，主机上由测试提供同名符号（与 test_control_esp32.c 同款）。*/
int eeprom_ready(void) { return 0; }
int eeprom_read_image(uint8_t img[EE_IMG_SIZE]) { (void)img; return -1; }
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

/* ====== S：服务启动结果汇总（svc_status，P1-12）====== */
static char s_log_text[8][160];
static int  s_log_level[8];
static int  s_log_n;

static void log_rec(void *arg, int level, const char *text)
{
    (void)arg;
    if (s_log_n < 8) {
        s_log_level[s_log_n] = level;
        strncpy(s_log_text[s_log_n], text ? text : "(null)", sizeof s_log_text[0] - 1);
        s_log_text[s_log_n][sizeof s_log_text[0] - 1] = '\0';
    }
    s_log_n++;
}

/* 多少条该级别的日志包含 needle（level < 0 = 任意级别）*/
static int log_count(int level, const char *needle)
{
    int n = 0;
    for (int i = 0; i < s_log_n && i < 8; i++)
        if ((level < 0 || s_log_level[i] == level) && strstr(s_log_text[i], needle)) n++;
    return n;
}

static void test_svc_status(void)
{
    svc_status_t s;
    uint32_t f;

    /* S1 全部成功：一条 I 级，带 (all up)，没有 E / W */
    svc_init(&s);
    (void)svc_note(&s, SVC_BIT_SOFTAP, 0);
    (void)svc_note(&s, SVC_BIT_M207, 0);
    (void)svc_note(&s, SVC_BIT_TMP117, 0);
    (void)svc_note(&s, SVC_BIT_CTRL, 0);
    (void)svc_note(&s, SVC_BIT_CONSOLE, 0);
    (void)svc_note(&s, SVC_BIT_SNAP, 0);
    (void)svc_note(&s, SVC_BIT_BOOTREC, 0);
    s_log_n = 0;
    CHECK(svc_report(&s, log_rec, NULL) == 0, "S1 nothing failed -> returns 0");
    CHECK(s_log_n == 1 && s_log_level[0] == SVC_LOG_I, "S1 exactly one info line");
    CHECK(log_count(SVC_LOG_I, "services up: softap m207 tmp117 ctrl console snap bootrec (all up)") == 1,
          "S1 info line lists every service and says all up");
    CHECK(log_count(SVC_LOG_E, "") == 0 && log_count(SVC_LOG_W, "") == 0, "S1 no error / warning lines");
    CHECK(svc_all_up(&s), "S1 svc_all_up");

    /* S2 人为让 m207 起不来、ctrl 回的是 esp_err_t：报错、点名，不再说 all up */
    svc_init(&s);
    (void)svc_note(&s, SVC_BIT_SOFTAP, 0);
    (void)svc_note(&s, SVC_BIT_M207, -1);
    (void)svc_note(&s, SVC_BIT_TMP117, 0);
    (void)svc_note(&s, SVC_BIT_CTRL, 0x101);
    (void)svc_note(&s, SVC_BIT_SNAP, 0);
    s_log_n = 0;
    f = svc_report(&s, log_rec, NULL);
    CHECK(f == ((uint32_t)(SVC_BIT_M207 | SVC_BIT_CTRL)),
          "S2 failed mask is returned (the caller raises ALM_SERVICE on it)");
    CHECK(log_count(SVC_LOG_E, "") == 1, "S2 exactly one error line");
    CHECK(log_count(SVC_LOG_E, "services FAILED to start: m207 ctrl") == 1,
          "S2 error line names exactly the failed services");
    CHECK(log_count(-1, "all up") == 0, "S2 never says 'all up' when something failed");
    CHECK(log_count(SVC_LOG_I, "services up: softap tmp117 snap") == 1,
          "S2 info line still lists what did start");
    CHECK(!svc_all_up(&s), "S2 svc_all_up is false");

    /* S3 坏参数：位为 0 / 多位 / 超范围 / 空指针——拒绝，状态不动 */
    svc_init(&s);
    CHECK(svc_note(&s, 0, -1) == -1 && svc_note(&s, 1u << 20, -1) == -1
          && svc_note(&s, SVC_BIT_M207 | SVC_BIT_CTRL, -1) == -1
          && svc_note(NULL, SVC_BIT_M207, -1) == -1, "S3 bad bits / null status are rejected");
    svc_unverified(&s, 1u << 20);
    svc_unverified(NULL, SVC_BIT_LINK);
    CHECK(s.noted == 0 && s.failed == 0 && s.unverified == 0 && svc_all_up(&s),
          "S3 rejected calls leave the status untouched");

    /* S4 多个失败按固定顺序列全 */
    svc_init(&s);
    (void)svc_note(&s, SVC_BIT_SNAP, -1);
    (void)svc_note(&s, SVC_BIT_CONSOLE, -1);
    (void)svc_note(&s, SVC_BIT_M207, -1);
    s_log_n = 0;
    (void)svc_report(&s, log_rec, NULL);
    CHECK(log_count(SVC_LOG_E, "services FAILED to start: m207 console snap") == 1,
          "S4 all failed services are listed in a fixed order");

    /* S5 接口是 void 的服务（link_start / web_start）：如实报"未核实"——不算失败，也不冒充成功 */
    svc_init(&s);
    (void)svc_note(&s, SVC_BIT_SOFTAP, 0);
    svc_unverified(&s, SVC_BIT_LINK);
    svc_unverified(&s, SVC_BIT_WEB);
    (void)svc_note(&s, SVC_BIT_M207, 0);
    s_log_n = 0;
    CHECK(svc_report(&s, log_rec, NULL) == 0 && svc_all_up(&s), "S5 unverified is not a failure");
    CHECK(log_count(SVC_LOG_W, "not verifiable") == 2 && log_count(SVC_LOG_W, ": link") == 1
          && log_count(SVC_LOG_W, ": web") == 1, "S5 each unverified service gets a warning naming it");
    CHECK(log_count(-1, "all up") == 0, "S5 no 'all up' while some results are unverifiable");
    CHECK(log_count(SVC_LOG_I, "services up: softap m207") == 1,
          "S5 info line lists only the verified ones (not link / web)");

    /* S6 没有日志回调也安全；S7 名单截断安全 */
    CHECK(svc_report(&s, NULL, NULL) == 0, "S6 null logger is safe");
    {
        char small[8];
        memset(small, 'x', sizeof small);
        (void)svc_names(0xFFFFFFFFu, small, sizeof small);
        CHECK(strnlen(small, sizeof small) < sizeof small && strcmp(small, "softap") == 0,
              "S7 names stop at the last complete name and stay NUL-terminated");
        CHECK(svc_names(SVC_BIT_M207, NULL, 16) == 0 && svc_names(SVC_BIT_M207, small, 0) == 0,
              "S7 null buffer / zero capacity");
        (void)svc_names(SVC_BIT_CTRL | SVC_BIT_M207 | SVC_BIT_BOOTREC, small, sizeof small);
        CHECK(strcmp(small, "m207") == 0, "S7 names stop before overflowing (only the first, 'm207', fits)");
    }
}

/* ====== P：参数写入 param_set（X02-F-05 / M12-F-08 的 param_set 一半）======
 * 模块服务没受理（离线 / 队列满 / 忙）时，"成功"只代表 RAM 注册表被改：原先钩子的返回值被丢，
 * param_set 回 0，网页 / 屏幕都显示成功。现在经返回值上抛（2 = 已记录、模块没受理）。
 * PF_M207 参数的同值重写原先被相等短路——模块离线时写过的值，用户重写同一个值正是为了补发。*/
static int s_hook_rc, s_hook_calls, s_bcast;
static uint16_t s_hook_id;
static int hook_map(uint16_t pid, float v) { (void)v; s_hook_calls++; s_hook_id = pid; return s_hook_rc; }
static int hook_set(uint8_t ch, float v)   { (void)v; s_hook_calls++; s_hook_id = ch; return s_hook_rc; }
static void bcast_cnt(param_desc_t *p, uint8_t src) { (void)p; (void)src; s_bcast++; }
static int s_commit_rc, s_commit_calls;
static int commit_stub(const uint8_t *img) { (void)img; s_commit_calls++; return s_commit_rc; }

static void test_param_set(void)
{
    int rv;
    param_registry_init(&(param_hooks_t){ .on_m207_map_set = hook_map, .on_m207_set = hook_set,
                                          .on_broadcast = bcast_cnt });
    /* P1 模块离线（服务回 -2）：值进注册表、照常广播，但返回 2，apply_err 对回调可见 */
    s_hook_rc = -2; s_hook_calls = 0; s_bcast = 0;
    rv = param_set(0x103, 150.0f, PARAM_SRC_WEB);
    CHECK(rv == 2 && param_get(0x103) == 150.0f, "P1 module did not take it: value stored, rv=2 (not 0)");
    CHECK(s_hook_calls == 1 && s_bcast == 1, "P1 side effect attempted once, change broadcast once");
    CHECK(param_apply_err(0x103) == 2, "P1 apply_err is visible to the broadcast callback (the receipt)");
    /* P2 模块受理：返回 0 */
    s_hook_rc = 0;
    rv = param_set(0x103, 30.0f, PARAM_SRC_WEB);
    CHECK(rv == 0 && param_apply_err(0x103) == 0, "P2 module accepted: rv=0");
    /* P3 同值重写（M12-F-08）：补发给模块，不当作一次变更广播 */
    s_hook_rc = -2;
    (void)param_set(0x103, 20.0f, PARAM_SRC_CONSOLE);      /* 离线时写：注册表 20，模块还是旧值 */
    s_hook_calls = 0; s_bcast = 0;
    rv = param_set(0x103, 20.0f, PARAM_SRC_WEB);           /* 用户重写同一个值 */
    CHECK(s_hook_calls == 1, "P3 same-value rewrite of an M207 param is re-sent to the module");
    CHECK(s_bcast == 0, "P3 same-value rewrite is not broadcast as a change");
    CHECK(rv == 2, "P3 rewrite while the module is away reports rv=2 (no fake success)");
    s_hook_rc = 0;
    rv = param_set(0x103, 20.0f, PARAM_SRC_WEB);
    CHECK(rv == 0 && s_hook_calls == 2, "P3 rewrite once the module takes it: rv=0");
    /* P4 非 M207 参数的同值写仍然短路（M05-F-19 的口径不变）*/
    (void)param_set(PARAM_ID_HYST, 0.3f, PARAM_SRC_WEB);
    s_bcast = 0;
    rv = param_set(PARAM_ID_HYST, 0.3f, PARAM_SRC_WEB);
    CHECK(rv == 0 && s_bcast == 0, "P4 non-M207 same-value write still short-circuits");
    /* P5 老签名那一路（tc1_set / tc2_set）：同样把结果带回来 */
    s_hook_rc = -1; s_hook_calls = 0; s_hook_id = 0;
    rv = param_set(PARAM_ID_TC1_SET, 26.0f, PARAM_SRC_WEB);
    CHECK(rv == 2 && s_hook_calls == 1 && s_hook_id == 1, "P5 setpoint path reports 'not taken' with the channel");

    /* E1 / E2（X04-F-07）：落盘失败要计数（control_poll 据此置 ALM_EEPROM）；首次上电不报警 */
    s_commit_rc = -1; s_commit_calls = 0;
    param_registry_init(&(param_hooks_t){ .eeprom_commit = commit_stub });
    (void)param_set(PARAM_ID_HYST, 0.4f, PARAM_SRC_CONSOLE);         /* 置脏 */
    param_persist_flush(1000u);                                      /* 窗口起点 */
    CHECK(s_commit_calls == 0, "E1 nothing is written before the debounce window");
    param_persist_flush(1000u + PARAM_PERSIST_DEBOUNCE_MS);          /* 到点：commit 失败 */
    CHECK(s_commit_calls == 1 && param_persist_fail_count() == 1, "E1 a failed commit is counted");
    param_persist_flush(1000u + PARAM_PERSIST_DEBOUNCE_MS + 100u);
    CHECK(s_commit_calls == 1, "E1 no retry on every call after a failure (backs off one window)");
    s_commit_rc = 0;
    param_persist_flush(1000u + 2u * PARAM_PERSIST_DEBOUNCE_MS + 100u);
    CHECK(s_commit_calls == 2 && param_persist_fail_count() == 0, "E1 a later successful commit clears the count");
    CHECK(param_restore_alarm_needed(1, 0) == 0, "E2 first boot (no image) is not an EEPROM alarm");
    CHECK(param_restore_alarm_needed(0, 0) == 1, "E2 unreadable / corrupt image is an EEPROM alarm");
    CHECK(param_restore_alarm_needed(0, 1) == 0, "E2 a good image is not an alarm");
}

int main(void)
{
    test_boot_record();
    test_safety_record();
    test_svc_status();
    test_param_set();
    printf("test_tcs_core: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
