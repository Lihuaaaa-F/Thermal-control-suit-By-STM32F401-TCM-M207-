/**
 * test_tcs_proto.c — 0x20/0x21 跨端字节对照 PC 单测（R2；L1 非最终验收依据）
 * 与 project_tcs/tools/proto_ref.py 同布局互为对照（Q10：先写预期字节再实现）。
 */
#include <stdio.h>
#include <string.h>
#include "tcs_proto.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { printf("FAIL: %s\n", name); g_fail++; } \
} while (0)

int main(void)
{
    /* ---- 0x20 快照：手工拼预期字节 ---- */
    tcs_snap_pl_t s = {
        .ver = 1, .seq = 0x11223344, .ms = 0x00AABBCC,
        .tc1_act = 22.5f, .tc2_act = 23.5f, .body = 36.8f,
        .tc1_set = 25.0f, .tc2_set = 30.0f,
        .tec1 = 0, .tec2 = 1, .fan = 1, .mode = 1,
        .alarm = 0x0003, .online = 0x03, .fresh_ms = 1234, .badge = 1, .rfu = 0,
    };
    uint8_t buf[64];
    size_t n = tcs_pl_snapshot(buf, sizeof(buf), &s);
    CHECK(n == TCS_SNAP_PL_LEN && n == 40, "snap len 40");
    CHECK(buf[0] == 1, "snap ver@0");
    CHECK(buf[1] == 0x44 && buf[2] == 0x33 && buf[3] == 0x22 && buf[4] == 0x11,
          "snap seq 小端@1-4");
    /* 22.5f = 0x41B40000 小端 → B4 41 前缀在偏移 9 */
    CHECK(buf[9] == 0x00 && buf[10] == 0x00 && buf[11] == 0xB4 && buf[12] == 0x41,
          "snap tc1_act=22.5 IEEE754 小端");
    CHECK(buf[33] == 0x03 && buf[34] == 0x00, "snap alarm=0x0003 小端");
    CHECK(buf[35] == 0x03, "snap online=3");
    CHECK(buf[36] == 0xD2 && buf[37] == 0x04, "snap fresh_ms=1234 小端");
    CHECK(buf[38] == 1 && buf[39] == 0, "snap badge/rfu");

    tcs_snap_pl_t d;
    CHECK(tcs_scan_snapshot(buf, n, &d) == 0, "snap decode ok");
    CHECK(d.seq == 0x11223344 && d.ms == 0x00AABBCC, "snap decode seq/ms");
    CHECK(d.tc1_act > 22.49f && d.tc1_act < 22.51f &&
          d.body > 36.79f && d.body < 36.81f &&
          d.tc2_set > 29.99f && d.tc2_set < 30.01f, "snap decode floats");
    CHECK(d.alarm == 3 && d.online == 3 && d.fresh_ms == 1234 && d.badge == 1,
          "snap decode scalars");
    CHECK(tcs_scan_snapshot(buf, 10, &d) == -1, "snap 短帧拒绝");

    /* ---- 0x21 增量：mask 紧排 + 变长 ---- */
    tcs_delta_src_t src = {
        .tc1_set = 26.5f, .tc2_set = 0, .body = 37.2f,
        .tec = 0x02, .fan = 1, .mode = 0, .badge = 2, .alarm = 0x0002,
    };
    uint16_t mask = TCS_D_TC1_SET | TCS_D_TEC | TCS_D_ALARM | TCS_D_BODY;
    uint8_t db[24];
    size_t dn = tcs_pl_delta(db, sizeof(db), mask, &src);
    CHECK(dn == 2 + 4 + 1 + 2 + 4, "delta 变长=13");
    CHECK(db[0] == (uint8_t)mask && db[1] == (uint8_t)(mask >> 8), "delta mask 小端");
    /* 仅 mask 内字段：tc1_set(4)+tec(1)+alarm(2)+body(4)=11 +2 头 */
    tcs_delta_src_t dd; uint16_t dmask = 0;
    memset(&dd, 0, sizeof(dd));
    CHECK(tcs_scan_delta(db, dn, &dd, &dmask) == 0 && dmask == mask, "delta decode mask");
    CHECK(dd.tc1_set > 26.49f && dd.tc1_set < 26.51f, "delta decode tc1_set");
    CHECK(dd.tec == 0x02 && dd.alarm == 2 && dd.body > 37.19f && dd.body < 37.21f,
          "delta decode fields");
    /* 未入选字段不得污染解码输出（tec2_set 等保持调用方初值 0）*/
    CHECK(dd.tc2_set == 0.0f && dd.fan == 0 && dd.mode == 0, "delta 未选字段不填");

    uint8_t db2[8];
    CHECK(tcs_pl_delta(db2, 4, mask, &src) == 0, "delta cap 不足返回 0");
    CHECK(tcs_scan_delta(db, 1, &dd, &dmask) == -1, "delta 短帧拒绝");

    /* 全字段 mask 的长度 */
    mask = 0xFF;
    dn = tcs_pl_delta(db, sizeof(db), mask, &src);
    CHECK(dn == 2 + 4 + 4 + 1 + 1 + 1 + 2 + 4 + 1, "delta 全字段=20");

    /* ====== U 线 W3：0x22 INFO 编解码（设备信息 → 屏的连接页）====== */
    {
        char pl[TCS_INFO_MAX_LEN + 1];
        char out[80];
        const char *keys[] = { "ssid", "pass", "url", "fw" };
        const char *vals[] = { "TCS-9A3C", "12345678", "http://192.168.4.1", "1.2.0" };
        size_t n = tcs_pl_info(pl, sizeof pl, keys, vals, 4);
        CHECK(n > 0 && n == strlen(pl) && n <= TCS_INFO_MAX_LEN, "info 编码长度");
        CHECK(strcmp(pl, "ssid=TCS-9A3C\npass=12345678\nurl=http://192.168.4.1\nfw=1.2.0") == 0,
              "info 文本格式：每行 key=value");
        CHECK(tcs_info_get(pl, n, "ssid", out, sizeof out) == 8 && strcmp(out, "TCS-9A3C") == 0,
              "info 取 ssid");
        CHECK(tcs_info_get(pl, n, "pass", out, sizeof out) == 8 && strcmp(out, "12345678") == 0,
              "info 取 pass");
        CHECK(tcs_info_get(pl, n, "url", out, sizeof out) > 0 &&
              strcmp(out, "http://192.168.4.1") == 0, "info 取 url（值里的 ':' '/' 不受影响）");
        CHECK(tcs_info_get(pl, n, "fw", out, sizeof out) == 5 && strcmp(out, "1.2.0") == 0,
              "info 取最后一行（结尾无换行）");
        CHECK(tcs_info_get(pl, n, "ss", out, sizeof out) == -1, "info 键只匹配整段（ss ≠ ssid）");
        CHECK(tcs_info_get(pl, n, "nope", out, sizeof out) == -1, "info 未知键返回 -1");
        CHECK(tcs_info_get(pl, n, "ssid", out, 5) == -1, "info 值放不下返回 -1（不截断热点名/密码）");

        /* 值里的换行/等号被换成空格：保证一行一键 */
        const char *k2[] = { "k" };
        const char *v2[] = { "a=b\nc" };
        n = tcs_pl_info(pl, sizeof pl, k2, v2, 1);
        CHECK(n == 7 && strcmp(pl, "k=a b c") == 0, "info 值内换行/等号换成空格");
        /* 非法键整对跳过 */
        const char *k3[] = { "bad\nkey", "", "ok" };
        const char *v3[] = { "x", "y", "z" };
        n = tcs_pl_info(pl, sizeof pl, k3, v3, 3);
        CHECK(n == 4 && strcmp(pl, "ok=z") == 0, "info 非法键跳过");
        /* 放不下：整条返回 0，不留半截 */
        const char *k4[] = { "a", "b" };
        const char *v5[] = { "0123456789", "x" };
        CHECK(tcs_pl_info(pl, 14, k4, v5, 2) == 0, "info 第二对放不下 → 整条返回 0");
        CHECK(tcs_pl_info(pl, 17, k4, v5, 2) == 16 && strcmp(pl, "a=0123456789\nb=x") == 0,
              "info 恰好放下（16 字节 + NUL = 17）");
        CHECK(tcs_pl_info(pl, sizeof pl, NULL, v5, 2) == 0, "info 空指针返回 0");
        CHECK(tcs_pl_info(pl, sizeof pl, k4, v5, 0) == 0, "info n=0 返回 0");
        /* 空指针值按空串处理 */
        const char *v6[] = { NULL };
        n = tcs_pl_info(pl, sizeof pl, k4, v6, 1);
        CHECK(n == 2 && strcmp(pl, "a=") == 0, "info 空值写成 key=");
        CHECK(tcs_info_get(pl, n, "a", out, sizeof out) == 0 && out[0] == '\0', "info 取到空值");
        CHECK(tcs_info_get(pl, n, "a", out, 1) == 0, "info cap=1 放得下空串的结尾符");
        CHECK(tcs_info_get(pl, n, "a", out, 0) == -1, "info cap=0 返回 -1");
        CHECK(MT_TCS_INFO == 0x22, "info 消息号 0x22");
    }

    printf("test_tcs_proto: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
