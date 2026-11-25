/* test_audit_ring.c — M07-F-08（2026-11-25，S-G l1）：关键控制台命令的审计留痕环
 * （被测：App/tcs/audit_ring.c，纯逻辑）。
 *
 * 关单条件："执行 5 类命令，审计环有 5 条记录"（用例第 2 节）。另外覆盖：哪些命令算关键、
 * 环满后覆盖最旧、绕回之后顺序不乱、返回值与时间戳的边界、命令原文的折叠与截断、回放的容量上限。
 * console.c 的接入（执行后记录、往 RTT 打一行、audit 回放命令）依赖 HAL，只做了构建验证。
 */
#include <stdio.h>
#include <string.h>
#include "audit_ring.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static int count_lines(const char *s)
{
    int n = 0;
    for (; *s; s++) if (*s == '\n') n++;
    return n;
}

/* 环的槽区正好 512 字节（独立的一块，不走控制台 TX 环）*/
_Static_assert(sizeof(((audit_ring_t *)0)->slot) == 512u, "audit ring slots must be 512 bytes");
/* 测试记录用的命令原文：按 AUDIT_CMD_MAX 程序化生成，不手数字符 */
#define A26 "abcdefghijklmnopqrstuvwxyz"
_Static_assert(sizeof(A26) - 1u == AUDIT_CMD_MAX, "A26 must be AUDIT_CMD_MAX chars");
static char g_b26[AUDIT_CMD_MAX + 1u];
static const char *b26(void)
{
    for (size_t i = 0; i < AUDIT_CMD_MAX; i++) g_b26[i] = (char)('0' + (int)(i % 10u));
    g_b26[AUDIT_CMD_MAX] = '\0';
    return g_b26;
}

int main(void)
{
    audit_ring_t r;
    char out[2048];

    /* ---- 1) 初始为空 ---- */
    audit_ring_init(&r);
    CHECK(r.count == 0 && r.total == 0, "init：空");
    CHECK(audit_ring_format(&r, out, sizeof(out)) == 0 && out[0] == '\0', "空环回放 0 条");

    /* ---- 2) 关单条件：执行 5 类命令，审计环有 5 条记录 ---- */
    CHECK(audit_ring_add(&r, 1000u, "p set tc1_set 40.1", 0) == 1, "记：改参数");
    CHECK(audit_ring_add(&r, 1200u, "mode 1", 0) == 1, "记：改模式");
    CHECK(audit_ring_add(&r, 1500u, "pindrv A 1 0", 0) == 1, "记：驱动引脚");
    CHECK(audit_ring_add(&r, 2100u, "estop", 0) == 1, "记：急停");
    CHECK(audit_ring_add(&r, 4000u, "ack", 0) == 1, "记：确认");
    CHECK(r.count == 5 && r.total == 5, "5 类命令 → 5 条记录");
    {
        int n = audit_ring_format(&r, out, sizeof(out));
        CHECK(n == 5 && count_lines(out) == 5, "回放 5 条");
        CHECK(strcmp(out,
                     "1000 p set tc1_set 40.1 rv=0\n"
                     "1200 mode 1 rv=0\n"
                     "1500 pindrv A 1 0 rv=0\n"
                     "2100 estop rv=0\n"
                     "4000 ack rv=0\n") == 0, "回放文本：时间戳 + 命令原文 + 返回值，从旧到新");
    }

    /* ---- 3) 哪些命令要留痕（同一份用例按台架档和交付档各编译一次）----
     * B = 台架及以上才有的命令：在台架档算关键；在交付档不进表（命令名不能出现在交付档产物里）。*/
    {
#define B (TCS_PROFILE >= 1 ? 1 : 0)
        struct { const char *c, *s; int want; } t[] = {
            { "estop", NULL, 1 }, { "ack", NULL, 1 }, { "reset", NULL, 1 },
            { "p", "set", 1 }, { "p", "defaults", 1 }, { "p", "save", 1 },
            { "p", "list", 0 }, { "p", NULL, 0 }, { "p", "", 0 },
            { "mode", NULL, B }, { "tec", "1", B }, { "fan", "50", B },
            { "pindrv", "A", B }, { "pinrel", "B", B },
            { "m207", "send", B }, { "sim", "arm", B }, { "sim", "body", B },
            { "wdttest", NULL, B },
            { "touch", "cal", 1 }, { "touch", "log", 0 }, { "touch", NULL, 0 },
            { "ver", NULL, 0 }, { "stat", NULL, 0 }, { "perf", NULL, 0 }, { "tbl", NULL, 0 },
            { "ee", NULL, 0 }, { "audit", NULL, 0 }, { "post", NULL, 0 }, { "linkstat", NULL, 0 },
            { "", NULL, 0 }, { NULL, NULL, 0 },
        };
        int bad = 0;
        for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++)
            if (audit_is_critical(t[i].c, t[i].s) != t[i].want) {
                bad++;
                printf("  分类不符：%s %s\n", t[i].c ? t[i].c : "(null)", t[i].s ? t[i].s : "-");
            }
        CHECK(bad == 0, "改参数 / 改模式 / 驱动引脚 / 急停 / 注入 / 复位留痕，只读命令不留");
#undef B
    }

    /* ---- 4) 环满：第 17 条覆盖最旧的一条；total 照常累加 ---- */
    {
        char c[32];
        int i;
        audit_ring_init(&r);
        for (i = 1; i <= (int)AUDIT_SLOT_COUNT; i++) {
            snprintf(c, sizeof(c), "cmd%d", i);
            (void)audit_ring_add(&r, (uint32_t)i * 10u, c, 0);
        }
        CHECK(r.count == AUDIT_SLOT_COUNT && r.total == AUDIT_SLOT_COUNT, "满：16 条");
        (void)audit_ring_add(&r, 999u, "cmd17", -1);
        CHECK(r.count == AUDIT_SLOT_COUNT && r.total == AUDIT_SLOT_COUNT + 1u, "第 17 条：仍 16 条，total = 17");
        (void)audit_ring_format(&r, out, sizeof(out));
        CHECK(strncmp(out, "20 cmd2 rv=0\n", 13) == 0, "最旧的（cmd1）被覆盖，回放从 cmd2 开始");
        CHECK(strcmp(out + strlen(out) - 16u, "999 cmd17 rv=-1\n") == 0, "最新的一条（cmd17）在最后");
        CHECK(strstr(out, "cmd1 ") == NULL, "cmd1 已不在环里");
        CHECK(count_lines(out) == (int)AUDIT_SLOT_COUNT, "回放 16 行");
    }

    /* ---- 5) 绕回很多圈之后顺序仍然正确 ---- */
    {
        char c[32], want[32];
        int i, bad = 0;
        audit_ring_init(&r);
        for (i = 1; i <= 40; i++) {
            snprintf(c, sizeof(c), "c%d", i);
            (void)audit_ring_add(&r, (uint32_t)i, c, 0);
        }
        (void)audit_ring_format(&r, out, sizeof(out));
        for (i = 25; i <= 40; i++) {
            snprintf(want, sizeof(want), "%d c%d rv=0\n", i, i);
            if (i == 25) { if (strncmp(out, want, strlen(want)) != 0) bad++; }
            else if (strstr(out, want) == NULL) bad++;
        }
        CHECK(r.count == AUDIT_SLOT_COUNT && r.total == 40u, "40 条：16 条在环里，total = 40");
        CHECK(bad == 0 && count_lines(out) == (int)AUDIT_SLOT_COUNT, "绕回之后回放的是第 25..40 条，顺序正确");
    }

    /* ---- 6) 返回值与时间戳的边界 ---- */
    audit_ring_init(&r);
    (void)audit_ring_add(&r, 0xFFFFFFFFu, "big", 300);
    (void)audit_ring_add(&r, 4294967295u - 5u, "neg", -300);
    (void)audit_ring_add(&r, 5u, "zero", 0);
    (void)audit_ring_format(&r, out, sizeof(out));
    CHECK(strcmp(out,
                 "4294967295 big rv=127\n"
                 "4294967290 neg rv=-128\n"
                 "5 zero rv=0\n") == 0, "32 位无符号时间戳不打成负数；返回值按 int8 截到 ±127/−128");

    /* ---- 7) 命令原文的折叠与截断 ---- */
    {
        char t[64];
        audit_cmd_text(t, sizeof(t), "  p\tset   tc1_set \t 40.1  ");
        CHECK(strcmp(t, "p set tc1_set 40.1") == 0, "空白折叠成一个空格，去首尾");
        audit_cmd_text(t, sizeof(t), "");
        CHECK(t[0] == '\0', "空行 → 空串");
        audit_cmd_text(t, sizeof(t), NULL);
        CHECK(t[0] == '\0', "NULL → 空串");
        {
            char s[AUDIT_CMD_MAX + 1u];
            size_t i;
            char longcmd[80];
            for (i = 0; i < sizeof(longcmd) - 1; i++) longcmd[i] = 'x';
            longcmd[sizeof(longcmd) - 1] = '\0';
            audit_cmd_text(s, sizeof(s), longcmd);
            CHECK(strlen(s) == AUDIT_CMD_MAX, "超长命令截到 AUDIT_CMD_MAX 个字符");
            CHECK(s[strlen(s) - 1u] == '~' && s[0] == 'x', "截断处标 '~'");
            /* 恰好装得下：不加 '~' */
            memset(longcmd, 'y', AUDIT_CMD_MAX);
            longcmd[AUDIT_CMD_MAX] = '\0';
            audit_cmd_text(s, sizeof(s), longcmd);
            CHECK(strlen(s) == AUDIT_CMD_MAX && s[AUDIT_CMD_MAX - 1u] == 'y', "恰好 AUDIT_CMD_MAX 个字符：不截断");
            /* 截断记号不能把词中间的空格吃成新词：逐字节检查没有越界写 */
            audit_cmd_text(s, sizeof(s), "a b c d e f g h i j k l m n o p q r s t u v w x y z");
            CHECK(strlen(s) <= AUDIT_CMD_MAX, "多个短词也不会越界");
        }
    }

    /* ---- 8) 记录与回放的参数边界 ---- */
    audit_ring_init(&r);
    CHECK(audit_ring_add(&r, 1u, NULL, 0) == 0 && audit_ring_add(&r, 1u, "", 0) == 0 &&
          audit_ring_add(NULL, 1u, "x", 0) == 0, "空命令 / 空环指针：不记");
    CHECK(r.count == 0, "没有记进去");
    (void)audit_ring_add(&r, 7u, "estop", 0);
    CHECK(audit_ring_format(&r, out, 0) == 0 && audit_ring_format(&r, out, 1) == 0 && out[0] == '\0',
          "回放缓冲太小：0 条，out 是空串");
    CHECK(audit_ring_format(NULL, out, sizeof(out)) == 0 && audit_ring_format(&r, NULL, 9u) == 0,
          "空指针：0 条，不崩");
    {
        char small[8];
        memset(small, 'Z', sizeof(small));
        CHECK(audit_ring_format(&r, small, sizeof(small)) == 0 && small[0] == '\0',
              "放不下一整条：一条都不写（不写半条）");
        for (size_t i = 1; i < sizeof(small); i++) CHECK(small[i] == 'Z', "没有越过 NUL 往后写");
        char exact[16];                         /* "7 estop rv=0\n" = 13 字符 + NUL */
        memset(exact, 'Z', sizeof(exact));
        CHECK(audit_ring_format(&r, exact, 14u) == 1 && strcmp(exact, "7 estop rv=0\n") == 0,
              "恰好放得下一条：写 1 条");
        CHECK(exact[14] == 'Z', "没有越过 cap");
    }
    {
        /* 两条记录（命令原文各 26 字符）：一条 = "1 " + 26 + " rv=0" + "\n" = 34 字符。
         * 缓冲给 38：放得下第一条（34 ≤ 37），放不下第二条（34 > 37 − 34）。*/
        audit_ring_init(&r);
        (void)audit_ring_add(&r, 1u, A26, 0);
        (void)audit_ring_add(&r, 2u, b26(), 0);
        char two[40];
        memset(two, 'Z', sizeof(two));
        int n = audit_ring_format(&r, two, 38u);
        CHECK(n == 1 && count_lines(two) == 1, "放得下第一条、放不下第二条：只写 1 条");
        CHECK(two[34] == '\0' && two[35] == 'Z' && two[39] == 'Z', "以 NUL 结尾，后面一个字节都没动");
        CHECK(strncmp(two, "1 " A26 " rv=0\n", 34u) == 0, "写进去的是完整的第一条（不是半条）");
    }
    {
        char big[80];
        memset(big, 'Z', sizeof(big));
        int n = audit_ring_format(&r, big, 69u);
        CHECK(n == 2 && big[68] == '\0' && big[69] == 'Z', "缓冲 69：两条都写得下（68 字符 + NUL）");
        {
            char exp[40];
            snprintf(exp, sizeof(exp), "2 %s rv=0\n", b26());
            CHECK(strcmp(big + 34, exp) == 0, "第二条紧跟在第一条后面");
        }
    }

    printf("test_audit_ring: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
