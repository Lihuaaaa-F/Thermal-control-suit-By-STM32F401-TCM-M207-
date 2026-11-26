/* test_web_hardening.c — 网页端点的纯逻辑加固（components/tcs_web/web_status.c；S 线 S-G e3b）
 *
 * M14-F-13：POST /api/ota?m=… 的 m 取值。以前在查询串里找子串 "m=1"：?m=10、?m=1x 也当回滚；
 * 取不到 m（没带 query，或 query 超过 31 字节读不进来）时静默按 m=0 走"拷贝 + 重启"。
 * 现在 m 必须是精确的 "0" 或 "1"，其余一律 400、什么都不做。
 * 审查时的语义对本文件的用例是红的——见 docs/results/S_G_e3b_结果.md。
 */
#include <stdio.h>
#include <string.h>
#include "web_status.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

static void test_ota_mode_value(void)
{
    CHECK(web_ota_mode_value("0") == 0, "m=0 -> copy + switch");
    CHECK(web_ota_mode_value("1") == 1, "m=1 -> rollback");

    /* 审查的例子：?m=10 命中 strstr(q, "m=1")，被当成回滚 */
    CHECK(web_ota_mode_value("10") == -1, "m=10 -> rejected (400), neither rollback nor copy");
    CHECK(web_ota_mode_value("11") == -1, "m=11 -> rejected");
    CHECK(web_ota_mode_value("1x") == -1, "m=1x -> rejected");
    CHECK(web_ota_mode_value("01") == -1, "m=01 -> rejected");
    CHECK(web_ota_mode_value("2") == -1, "m=2 -> rejected");
    CHECK(web_ota_mode_value("-1") == -1, "m=-1 -> rejected");
    CHECK(web_ota_mode_value("0.0") == -1 && web_ota_mode_value("1.0") == -1, "m=0.0 / 1.0 -> rejected");
    CHECK(web_ota_mode_value("") == -1, "empty value -> rejected");
    CHECK(web_ota_mode_value(NULL) == -1, "no value at all (query without m) -> rejected, not silently m=0");
    CHECK(web_ota_mode_value("0 ") == -1 && web_ota_mode_value(" 1") == -1, "surrounding spaces -> rejected");
    CHECK(web_ota_mode_value("true") == -1 && web_ota_mode_value("false") == -1, "true / false -> rejected");
    CHECK(web_ota_mode_value("m=1") == -1, "a whole \"m=1\" pair as the value -> rejected");
}

int main(void)
{
    test_ota_mode_value();
    printf("test_web_hardening: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
