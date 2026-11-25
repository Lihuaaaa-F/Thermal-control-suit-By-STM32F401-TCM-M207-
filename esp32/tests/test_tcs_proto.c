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
    /* m1"模式"页（2026-11-26，TCS_PROTO_VER 2）：末尾追加 9 字节，40 → 49 */
    CHECK(n == TCS_SNAP_PL_LEN && n == 49, "snap len 49");
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

    /* ---- m1"模式"页（2026-11-26，TCS_PROTO_VER 2）：末尾追加 9B。与 project_tcs/tools/proto_ref.py
     *     的 selftest、project_tcs/tests/test_proto_m1.c 用的是同一组字节 ---- */
    CHECK(TCS_PROTO_VER == 2, "proto ver 2");
    CHECK(TCS_SNAP_PL_LEN == 49, "snap len 49");
    s.ver = TCS_PROTO_VER;
    s.tec_dir = tcs_tec_dir_pack(TCS_TEC_DIR_COOL, TCS_TEC_DIR_HEAT);
    s.tec1_v = -823; s.tec1_i = 310;                     /* -8.23 V / 3.10 A */
    s.tec2_v = TCS_ELEC_V_UNKNOWN; s.tec2_i = TCS_ELEC_I_UNKNOWN;
    n = tcs_pl_snapshot(buf, sizeof(buf), &s);
    CHECK(n == 49, "snap (m1) len 49");
    CHECK(buf[0] == 2, "snap (m1) ver@0 = 2");
    CHECK(buf[40] == 0x21, "snap (m1) tec_dir@40 = cool | heat<<4");
    CHECK(buf[41] == 0xC9 && buf[42] == 0xFC, "snap (m1) tec1_v@41 = -823 (i16 LE)");
    CHECK(buf[43] == 0x36 && buf[44] == 0x01, "snap (m1) tec1_i@43 = 310 (u16 LE)");
    CHECK(buf[45] == 0x00 && buf[46] == 0x80, "snap (m1) tec2_v@45 = unknown 0x8000");
    CHECK(buf[47] == 0xFF && buf[48] == 0xFF, "snap (m1) tec2_i@47 = unknown 0xFFFF");
    CHECK(tcs_scan_snapshot(buf, n, &d) == 0 && d.ver == 2 && d.tec_dir == 0x21 && d.tec1_v == -823
          && d.tec1_i == 310 && d.tec2_v == TCS_ELEC_V_UNKNOWN && d.tec2_i == TCS_ELEC_I_UNKNOWN,
          "snap (m1) decode tail");
    CHECK(d.seq == 0x11223344 && d.badge == 1 && d.rfu == 0, "snap (m1) head 40B untouched");
    CHECK(tcs_scan_snapshot(buf, 40, &d) == -1, "snap (m1) 40B old length rejected under ver 2");
    CHECK(tcs_tec_dir_get(0x21, 1) == TCS_TEC_DIR_COOL && tcs_tec_dir_get(0x21, 2) == TCS_TEC_DIR_HEAT
          && tcs_tec_dir_get(0x21, 3) == TCS_TEC_DIR_HEAT, "tec_dir nibbles (ch other than 1 = high)");
    CHECK(tcs_tec_dir_pack(0x11, 0x22) == 0x21, "tec_dir pack keeps 4 bits each");
    CHECK(tcs_elec_pack_v(-8.23f) == -823 && tcs_elec_pack_v(0.0f) == 0
          && tcs_elec_pack_v(-0.0f) == 0 && tcs_elec_pack_v(0.004f) == 0
          && tcs_elec_pack_v(1e9f) == TCS_ELEC_V_UNKNOWN, "pack voltage (0.01 V, rounding toward zero)");
    CHECK(tcs_elec_pack_i(3.10f) == 310 && tcs_elec_pack_i(-0.5f) == TCS_ELEC_I_UNKNOWN
          && tcs_elec_pack_i(700.0f) == TCS_ELEC_I_UNKNOWN, "pack current (0.01 A, negatives unknown)");
    {
        tcs_delta_src_t e;
        uint16_t emask = 0;
        uint8_t dbm[40];   /* 本块用自己的缓冲：原文件里那两个变量在这里还没声明 */
        size_t dnm;
        memset(&e, 0, sizeof e);
        e.tec_dir = 0x21; e.tec1_v = -823; e.tec1_i = 310;
        e.tec2_v = TCS_ELEC_V_UNKNOWN; e.tec2_i = TCS_ELEC_I_UNKNOWN;
        dnm = tcs_pl_delta(dbm, sizeof(dbm), (uint16_t)(TCS_D_TEC_DIR | TCS_D_ELEC), &e);
        CHECK(dnm == 11, "delta (m1) tec_dir + elec = 11B");
        CHECK(dbm[0] == 0x00 && dbm[1] == 0x03, "delta (m1) mask 0x0300 LE (0x0100 dir | 0x0200 elec)");
        CHECK(dbm[2] == 0x21 && dbm[3] == 0xC9 && dbm[4] == 0xFC && dbm[5] == 0x36 && dbm[6] == 0x01
              && dbm[11 - 1] == 0xFF && dbm[10] == 0xFF, "delta (m1) body bytes");
        memset(&e, 0x55, sizeof e);
        CHECK(tcs_scan_delta(dbm, dnm, &e, &emask) == 0 && emask == 0x0300 && e.tec_dir == 0x21
              && e.tec1_v == -823 && e.tec1_i == 310 && e.tec2_v == TCS_ELEC_V_UNKNOWN
              && e.tec2_i == TCS_ELEC_I_UNKNOWN, "delta (m1) decode");
        CHECK(tcs_scan_delta(dbm, 10, &e, &emask) == -1, "delta (m1) truncated elec rejected");
        /* 新 mask 位排在低 8 位的字段之后 */
        memset(&e, 0, sizeof e);
        e.fan = 1; e.tec_dir = 0x21;
        dnm = tcs_pl_delta(dbm, sizeof(dbm), (uint16_t)(TCS_D_FAN | TCS_D_TEC_DIR | TCS_D_ELEC), &e);
        CHECK(dnm == 2 + 1 + 1 + 8 && dbm[2] == 1 && dbm[3] == 0x21, "delta (m1) field order: low bits first");
        dnm = tcs_pl_delta(dbm, 4, (uint16_t)(TCS_D_TEC_DIR | TCS_D_ELEC), &e);
        CHECK(dnm == 0, "delta (m1) cap too small returns 0");
    }

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
