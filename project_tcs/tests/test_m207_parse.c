/* test_m207_parse.c — m207 解析器/行拼装 PC 单测（任务书 §5.1 向量）*/
#include <stdio.h>
#include <string.h>
#include "m207.h"

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

    printf("test_m207_parse: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
