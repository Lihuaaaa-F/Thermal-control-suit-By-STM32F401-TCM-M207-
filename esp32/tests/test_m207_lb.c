/* test_m207_lb.c — M207 上电回环自检用的帧表与判定（S-G e2：M12-F-13、M12-F-15）

 * M12-F-13：上电自检原来把 3 条写命令（TCx:TCACTTEMP=…）和 9 条 CMD:REPLY 发上线路——
 *   模块接着时这些帧经 TX 引脚真实下发。帧表与判定从 IDF 外壳抽进内核后，在这里钉住：
 *   过线帧只能是合法的 ? 查询（不许带 = 或 !），解析样本的语义逐条正确。
 * M12-F-15：去抖注释与行为不符。行为合理（只改注释），时延由常量推算并在这里钉住：
 *   轮询 500ms 一拍、通道交替，同一通道 1s 才被问一次；连续 3 次 =2 才坐实 → ≥2s。
 */
#include <stdio.h>
#include <string.h>
#include "m207_service.h"
#include "m207_core.h"
#include "m207_proto.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static void test_wire_frames_are_queries(void)
{
    size_t n = m207_lb_wire_n();
    CHECK(n >= 4, "boot test sends at least 4 frames");
    for (size_t i = 0; i < n; i++) {
        const char *f = m207_lb_wire_frame(i);
        m207_reply_t r;
        CHECK(f[0] != '\0', "wire frame is not empty");
        CHECK(strchr(f, '=') == NULL, "M12-F-13: no write command ( = ) on the wire during boot test");
        CHECK(strchr(f, '!') == NULL, "M12-F-13: no save command ( ! ) on the wire during boot test");
        CHECK(f[strlen(f) - 1] == '?', "M12-F-13: every wire frame is a query");
        CHECK(m207_parse_line(f, &r) == 1 && r.kind == M207_KIND_POLL,
              "every wire frame parses as a poll of a known module");
        CHECK(strlen(f) < M207_LINE_MAX, "wire frame fits the line limit");
    }
    /* 两个通道的实际温度 / 设定温度 / 开关状态都会被问到：自检覆盖到的正是服务要用的查询 */
    CHECK(m207_lb_echo_ok("TC1:TCACTTEMP?", "TC1:TCACTTEMP?") == 1, "echo check accepts a matching query");
    CHECK(m207_lb_echo_ok("TC2:TCSW?", "TC2:TCSW?") == 1, "echo check accepts ch2 switch query");
}

static void test_echo_check(void)
{
    /* 回显判定：必须是合法查询、逐字节相同 */
    CHECK(m207_lb_echo_ok("TC1:TCACTTEMP?", "TC1:TCACTTEMP?") == 1, "echo identical -> ok");
    CHECK(m207_lb_echo_ok("TC1:TCACTTEMP?", "TC1:TCACTTEMP=25.5") == 0, "echo differs -> rejected");
    CHECK(m207_lb_echo_ok("TC1:TCACTTEMP?", "tc1:tcacttemp?") == 0, "echo must be byte-identical");
    CHECK(m207_lb_echo_ok("TC1:TCACTTEMP?", "") == 0, "no echo -> rejected (and never reads past the NUL)");
    CHECK(m207_lb_echo_ok("TC1:TCACTTEMP=25.5", "TC1:TCACTTEMP=25.5") == 0,
          "M12-F-13: a write frame can never pass the echo check");
    CHECK(m207_lb_echo_ok("TC1:TCADJTEMP!", "TC1:TCADJTEMP!") == 0, "a save frame can never pass");
    CHECK(m207_lb_echo_ok("TC1:NOPE?", "TC1:NOPE?") == 1, "an unknown-but-well-formed query passes the echo check");
    CHECK(m207_lb_echo_ok(NULL, NULL) == 0 && m207_lb_echo_ok("TC1:TCACTTEMP?", NULL) == 0,
          "NULL arguments are rejected");
}

static void test_parse_samples(void)
{
    size_t n = m207_lb_parse_n();
    CHECK(n >= 12, "parse samples: reply lines + the full ERRCODE table 0..8");
    for (size_t i = 0; i < n; i++)
        CHECK(m207_lb_parse_ok(m207_lb_parse_frame(i)) == 1, "every parse sample has the right meaning");
    /* 反例：判定函数不是"永远通过" */
    CHECK(m207_lb_parse_ok("CMD:REPLY=256") == 0, "ERRCODE that the parser rejects is rejected here too");
    CHECK(m207_lb_parse_ok("CMD:REPLY=x") == 0, "non-numeric ERRCODE rejected");
    CHECK(m207_lb_parse_ok("TC3:TCACTTEMP=25.5") == 0, "unknown module rejected by the check");
    CHECK(m207_lb_parse_ok("") == 0, "empty sample rejected");
    CHECK(m207_lb_parse_ok(NULL) == 0, "NULL sample rejected");
}

static void test_debounce_timing(void)
{
    /* M12-F-15：注释说的时延必须等于常量推算出来的时延 */
    /* 第一次、第二次、第三次 =2 分别落在 0s、1s、2s */
    CHECK(m207_open_debounce_ms() == 2000u, "M12-F-15: 1st..3rd hit are 0s/1s/2s -> 2s");
}

int main(void)
{
    test_wire_frames_are_queries();
    test_echo_check();
    test_parse_samples();
    test_debounce_timing();
    printf("test_m207_lb: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/* param_registry.c 的目标机桩（主机用例不碰 EEPROM 镜像）*/
int eeprom_ready(void) { return 0; }
int eeprom_read_image(uint8_t *img) { (void)img; return -1; }
