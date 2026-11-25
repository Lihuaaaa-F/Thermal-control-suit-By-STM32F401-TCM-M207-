/* test_m207_proto.c — ESP32 侧 m207_proto 纯函数单测（R1b；Q5 复制件回归 +
 * ERRORCODE 全表 0..8 锚定=V-5 的 L1 部分。蓝本：project_tcs/tests/test_m207_parse.c）*/
#include <stdio.h>
#include <string.h>
#include "m207_proto.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

int main(void)
{
    m207_reply_t r;

    /* 正常：TC1:TCACTTEMP=28.5 → (TC1, TCACTTEMP, 28.5) */
    CHECK(m207_parse_line("TC1:TCACTTEMP=28.5\r", &r) == 1, "basic TC1");
    CHECK(r.module == M207_MOD_TC1 && strcmp(r.param, "TCACTTEMP") == 0, "basic fields");
    CHECK(r.value > 28.49f && r.value < 28.51f, "basic value");

    CHECK(m207_parse_line("TC2:TCACTTEMP=30.05", &r) == 1, "basic TC2");
    CHECK(r.module == M207_MOD_TC2 && r.value > 30.04f && r.value < 30.06f, "TC2 value");

    /* 缺冒号 */
    CHECK(m207_parse_line("TCACTTEMP=28.5", &r) == 0, "no colon");
    /* 缺等号 */
    CHECK(m207_parse_line("TC1:TCACTTEMP", &r) == 0, "no equals");
    /* 空串 */
    CHECK(m207_parse_line("", &r) == 0, "empty");
    CHECK(m207_parse_line(NULL, &r) == 0, "NULL");
    /* 64 超长 */
    {
        char long_line[128];
        memset(long_line, 'A', sizeof(long_line));
        long_line[127] = 0;
        CHECK(m207_parse_line(long_line, &r) == 0, "overlong");
    }
    /* 小写自动大写 */
    CHECK(m207_parse_line("tc1:tcacttemp=28.5", &r) == 1, "lowercase");
    CHECK(strcmp(r.param, "TCACTTEMP") == 0, "lowercase param");
    /* TC3 未知模块 */
    CHECK(m207_parse_line("TC3:TCACTTEMP=1.0", &r) == 0, "TC3 unknown");

    /* M12-F-09：已知模块名查表（不再按名字长度判定）；未知模块仍拒绝 */
    CHECK(m207_parse_line("PCB:SYSMODEL=1.0", &r) == 1 && r.module == M207_MOD_PCB,
          "module PCB");
    CHECK(m207_parse_line("SYSTEM:SYSMODEL=207", &r) == 1 && r.module == M207_MOD_SYSTEM,
          "module SYSTEM");
    CHECK(m207_parse_line("PCRS232:BAUD=57600", &r) == 1 && r.module == M207_MOD_PCRS232,
          "module PCRS232");
    CHECK(m207_parse_line("TC3:TCACTTEMP=1.0", &r) == 0, "unknown module rejected");
    CHECK(m207_parse_line("TC:TCACTTEMP=1.0", &r) == 0, "short prefix rejected");
    CHECK(strcmp(m207_mod_name(M207_MOD_TC2), "TC2") == 0
          && strcmp(m207_mod_name(M207_MOD_PCB), "PCB") == 0
          && strcmp(m207_mod_name(99), "?") == 0, "mod_name");
    /* 负值 */
    CHECK(m207_parse_line("TC1:TCACTTEMP=-5.5", &r) == 1, "negative");
    CHECK(r.value < -5.49f && r.value > -5.51f, "negative value");
    /* 非数值 */
    CHECK(m207_parse_line("TC1:TCACTTEMP=abc", &r) == 0, "non-numeric");
    /* TCSW 设置回显 */
    CHECK(m207_parse_line("TC2:TCSW=1", &r) == 1, "TCSW echo");
    CHECK(strcmp(r.param, "TCSW") == 0 && r.value == 1.0f, "TCSW fields");
    /* 查询行（?）*/
    CHECK(m207_parse_line("TC1:TCACTTEMP?", &r) == 1 && r.kind == M207_KIND_POLL, "poll kind");

    /* ERRORCODE 全表 0..8（正典 §3；V-5 L1 锚定）*/
    for (int code = 0; code <= 8; code++) {
        char ln[24];
        snprintf(ln, sizeof(ln), "CMD:REPLY=%d", code);
        CHECK(m207_parse_line(ln, &r) == 1, "errcode parse");
        CHECK(r.kind == M207_KIND_ERRCODE && r.errcode == code, "errcode value");
    }
    /* CMD:REPLY 非数值/越界拒绝 */
    CHECK(m207_parse_line("CMD:REPLY=", &r) == 0, "errcode empty");
    CHECK(m207_parse_line("CMD:REPLY=x", &r) == 0, "errcode non-numeric");
    CHECK(m207_parse_line("CMD:REPLY=256", &r) == 0, "errcode over 255");

    /* 粘连双行：行拼装器一次喂两行 → 出两帧 */
    {
        m207_lineasm_t la;
        char line[M207_LINE_MAX + 1];
        m207_lineasm_reset(&la);
        const char *two = "TC1:TCACTTEMP=28.5\rTC2:TCACTTEMP=30.0\r";
        int got = 0;
        float v[2] = {0, 0};
        for (const char *p = two; *p; p++) {
            if (m207_lineasm_feed(&la, (uint8_t)*p, line, sizeof(line))) {
                m207_reply_t rr;
                if (m207_parse_line(line, &rr)) {
                    v[got++] = rr.value;
                }
            }
        }
        CHECK(got == 2, "glued two lines -> two frames");
        CHECK(v[0] > 28.49f && v[0] < 28.51f && v[1] > 29.99f && v[1] < 30.01f,
              "glued values");
    }
    /* \r\n 连续空行不误触发 */
    {
        m207_lineasm_t la;
        char line[M207_LINE_MAX + 1];
        m207_lineasm_reset(&la);
        int got = 0;
        const char *s = "TC1:TCACTTEMP=1.0\r\n\r\n";
        for (const char *p = s; *p; p++)
            if (m207_lineasm_feed(&la, (uint8_t)*p, line, sizeof(line))) got++;
        CHECK(got == 1, "crlf no spurious frame");
    }

    /* H10：映射表模板格式化（纯函数）*/
    {
        m207_map_entry_t e = {0x100, 1, "PIDKP", 3};
        char buf[24];
        if (!(m207_map_format(&e, 1.5f, buf, sizeof(buf)) == 0 &&
              strcmp(buf, "TC1:PIDKP=1.500\r") == 0)) { printf("FAIL: map format 3dp\n"); g_fail++; }
        m207_map_entry_t e2 = {0x101, 2, "PIDKI", 2};
        if (!(m207_map_format(&e2, 0.1f, buf, sizeof(buf)) == 0 &&
              strcmp(buf, "TC2:PIDKI=0.10\r") == 0)) { printf("FAIL: map format ch2\n"); g_fail++; }
        m207_map_entry_t bad = {0, 0, "", 1};
        if (!(m207_map_format(&bad, 1.0f, buf, sizeof(buf)) == -1)) { printf("FAIL: map reject ch=0\n"); g_fail++; }
        if (!(m207_map_format(&e, 1.0f, buf, 8) == -1)) { printf("FAIL: map cap guard\n"); g_fail++; }
    }

    /* 轴分离（R1b 修复）：轮询等待期 ERRCODE 分类——协议错≠通信失败 */
    CHECK(m207_classify_poll_errcode(2, 0, "TCACTTEMP") == M207_POLLERR_UNEXPECTED,
          "classify: not waiting -> unexpected");
    CHECK(m207_classify_poll_errcode(2, 1, "TCACTTEMP") == M207_POLLERR_SENSOR_OPEN,
          "classify: err2 waiting TCACTTEMP -> sensor open");
    CHECK(m207_classify_poll_errcode(2, 1, "TCSW") == M207_POLLERR_LINK_ALIVE,
          "classify: err2 other param -> link alive");
    CHECK(m207_classify_poll_errcode(2, 1, NULL) == M207_POLLERR_LINK_ALIVE,
          "classify: err2 NULL param -> link alive");
    CHECK(m207_classify_poll_errcode(0, 1, "TCACTTEMP") == M207_POLLERR_LINK_ALIVE,
          "classify: err0 unknown module -> link alive (not comm fail)");
    CHECK(m207_classify_poll_errcode(4, 1, "TCACTTEMP") == M207_POLLERR_LINK_ALIVE,
          "classify: err4 -> link alive");
    CHECK(m207_classify_poll_errcode(6, 1, "TCACTTEMP") == M207_POLLERR_LINK_ALIVE,
          "classify: err6 -> link alive");
    CHECK(m207_classify_poll_errcode(7, 1, "TCACTTEMP") == M207_POLLERR_LINK_ALIVE,
          "classify: err7 bad checksum -> link alive");
    CHECK(m207_classify_poll_errcode(1, 1, "TCACTTEMP") == M207_POLLERR_LINK_ALIVE,
          "classify: err1 stray ACK -> link alive (never sensor/comm)");
    CHECK(m207_classify_poll_errcode(8, 1, "TCACTTEMP") == M207_POLLERR_LINK_ALIVE,
          "classify: err8 stray SAVE ACK -> link alive");


    /* M12-F-05：模块前缀（PCB:/PCRS232:）；四段旧初始化器语义不变（mod=0）*/
    {
        char mb[40];
        m207_map_entry_t ep = {0x120, 0, "TMOTPHT", 1, M207_MOD_PCB};
        CHECK(m207_map_format(&ep, 60.0f, mb, sizeof mb) == 0
              && strncmp(mb, "PCB:TMOTPHT=60.0", 16) == 0, "PCB prefix");
        m207_map_entry_t es = {0x126, 0, "COMADDR", 0, M207_MOD_PCRS232};
        CHECK(m207_map_format(&es, 7.0f, mb, sizeof mb) == 0
              && strncmp(mb, "PCRS232:COMADDR=7", 17) == 0, "PCRS232 prefix");
        m207_map_entry_t et = {0x100, 1, "TCPIDP", 3};
        CHECK(m207_map_format(&et, 1.5f, mb, sizeof mb) == 0
              && strncmp(mb, "TC1:TCPIDP=1.500", 16) == 0, "TC prefix unchanged");
        m207_map_entry_t bad = {0x999, 3, "X", 1, 0};   /* 无 mod 且 ch 非法 */
        CHECK(m207_map_format(&bad, 1.0f, mb, sizeof mb) == -1, "bad ch rejected");
    }


    /* M12-F-10（S-G e2）：超长行丢弃至行尾，不上送截断的前缀；之后状态干净。
     * 原实现超限后只丢字节，遇 \r 仍把 64 字节的前缀当整行上送——与注释"丢弃至行尾"相反。*/
    {
        m207_lineasm_t la;
        char line[M207_LINE_MAX + 1];
        char big[200];
        m207_lineasm_reset(&la);
        /* 一条看起来合法、但超过行长上限 64 的行：截断后的前缀照样"能解析" */
        snprintf(big, sizeof big, "PCRS232:COMPUSHLIST=%.3f", 1.5);
        while (strlen(big) < 80) strcat(big, "0");
        int delivered = 0, got = 0;
        float v = 0;
        for (const char *p = big; *p; p++)
            delivered += m207_lineasm_feed(&la, (uint8_t)*p, line, sizeof(line));
        delivered += m207_lineasm_feed(&la, '\r', line, sizeof(line));
        CHECK(delivered == 0, "M12-F-10: over-long line is not delivered");
        /* 紧跟的正常行照常上送、值正确（不与被丢弃的前缀粘连）*/
        const char *next = "TC1:TCACTTEMP=27.5\r";
        for (const char *p = next; *p; p++) {
            if (m207_lineasm_feed(&la, (uint8_t)*p, line, sizeof(line))) {
                m207_reply_t rr;
                got++;
                if (m207_parse_line(line, &rr)) v = rr.value;
            }
        }
        CHECK(got == 1 && v > 27.49f && v < 27.51f,
              "M12-F-10: the line after an over-long one is intact");
        /* 恰好 64 字节的行仍要上送（上限本身没变）*/
        m207_lineasm_reset(&la);
        memset(big, 'A', M207_LINE_MAX);
        big[M207_LINE_MAX] = '\0';
        delivered = 0;
        for (int i = 0; i < M207_LINE_MAX; i++)
            delivered += m207_lineasm_feed(&la, (uint8_t)big[i], line, sizeof(line));
        delivered += m207_lineasm_feed(&la, '\r', line, sizeof(line));
        CHECK(delivered == 1 && strlen(line) == M207_LINE_MAX,
              "M12-F-10: a line of exactly M207_LINE_MAX bytes is delivered");
    }

    printf("test_m207_proto: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
