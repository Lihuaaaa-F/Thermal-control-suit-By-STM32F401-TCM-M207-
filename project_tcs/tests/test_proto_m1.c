/* test_proto_m1.c — m1"模式"页（2026-11-26 用户定的新功能）在链路协议上的追加，屏端这一份的用例。
 *
 * TCS_PROTO_VER 1 → 2：0x20 快照末尾追加 9 字节（tec_dir + 两路电压电流）、0x21 增量多了两个
 * mask 位（0x0100 输出方向、0x0200 电压电流）。唯一源 protocol/tcs_proto.h，两端包装只是 include。
 * 这里的字节向量与 esp32/tests/test_tcs_proto.c、project_tcs/tools/proto_ref.py 的 selftest 是同一组。
 * 另测屏端镜像的换算（App/comm/link_snap.h 的纯逻辑；Core/Src/main.c 的 snap_apply 与 0x21 分支调用）。
 * 用法：由 tests/run_all.sh 末尾的 proto_m1 一行编译运行。
 */
#include <stdio.h>
#include <string.h>
#include "tcs_proto.h"
#include "link_snap.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { printf("FAIL: %s (line %d)\n", name, __LINE__); fflush(stdout); g_fail++; } \
} while (0)

int main(void)
{
    tcs_snap_pl_t s, d;
    tcs_delta_src_t e;
    uint8_t buf[64], db[40];
    uint16_t emask = 0;
    size_t n, dn;

    CHECK(TCS_PROTO_VER == 3, "proto ver 3 (i3: 2 -> 3)");
    CHECK(TCS_SNAP_PL_LEN == 52, "snap len 52 (i3: 49 -> 52)");
    CHECK(TCS_D_TEC_DIR == 0x0100 && TCS_D_ELEC == 0x0200, "delta mask bits in the high byte");
    CHECK(TCS_ELEC_V_UNKNOWN == (int16_t)0x8000 && TCS_ELEC_I_UNKNOWN == 0xFFFF, "unknown sentinels");
    CHECK(TCS_TEC_DIR_PAUSE == 0 && TCS_TEC_DIR_COOL == 1 && TCS_TEC_DIR_HEAT == 2 && TCS_TEC_DIR_EXT == 3,
          "output direction codes");

    memset(&s, 0, sizeof s);
    s.ver = 3; s.seq = 0x11223344; s.ms = 0x00AABBCC;
    s.tc1_act = 22.5f; s.tc2_act = 23.5f; s.body = 36.8f; s.tc1_set = 25.0f; s.tc2_set = 30.0f;
    s.tec2 = 1; s.fan = 1; s.alarm = 0x0003; s.online = 0x03; s.fresh_ms = 1234; s.badge = 1;
    s.tec_dir = tcs_tec_dir_pack(TCS_TEC_DIR_COOL, TCS_TEC_DIR_HEAT);
    s.tec1_v = -823; s.tec1_i = 310;
    s.tec2_v = TCS_ELEC_V_UNKNOWN; s.tec2_i = TCS_ELEC_I_UNKNOWN;
    s.tec_cmd = 0x02; s.tec_act = 0x01; s.tec_known = 0x02;   /* i3：命令 TC2 开；回显 TC1 开；只有 TC2 可信 */
    n = tcs_pl_snapshot(buf, sizeof buf, &s);
    CHECK(n == 52, "snap len 52");
    CHECK(buf[0] == 3 && buf[1] == 0x44 && buf[2] == 0x33 && buf[3] == 0x22 && buf[4] == 0x11, "snap ver / seq");
    CHECK(buf[38] == 1 && buf[39] == 0, "snap badge / rfu (offsets unchanged)");
    CHECK(buf[40] == 0x21, "snap tec_dir@40");
    CHECK(buf[41] == 0xC9 && buf[42] == 0xFC, "snap tec1_v@41 = -823");
    CHECK(buf[43] == 0x36 && buf[44] == 0x01, "snap tec1_i@43 = 310");
    CHECK(buf[45] == 0x00 && buf[46] == 0x80, "snap tec2_v@45 = unknown");
    CHECK(buf[47] == 0xFF && buf[48] == 0xFF, "snap tec2_i@47 = unknown");
    CHECK(buf[49] == 0x02 && buf[50] == 0x01 && buf[51] == 0x02, "snap (i3) tec_cmd@49 tec_act@50 tec_known@51");

    memset(&d, 0xAA, sizeof d);
    CHECK(tcs_scan_snapshot(buf, n, &d) == 0, "snap decode ok");
    CHECK(d.ver == 3 && d.seq == 0x11223344 && d.ms == 0x00AABBCC, "snap decode ver/seq/ms");
    CHECK(d.tec_cmd == 0x02 && d.tec_act == 0x01 && d.tec_known == 0x02, "snap (i3) decode tail");
    CHECK(d.tec_dir == 0x21 && d.tec1_v == -823 && d.tec1_i == 310
          && d.tec2_v == TCS_ELEC_V_UNKNOWN && d.tec2_i == TCS_ELEC_I_UNKNOWN, "snap decode tail");
    CHECK(d.tc1_act > 22.49f && d.tc1_act < 22.51f && d.badge == 1 && d.fresh_ms == 1234, "snap decode head");
    CHECK(tcs_tec_dir_get(d.tec_dir, 1) == TCS_TEC_DIR_COOL && tcs_tec_dir_get(d.tec_dir, 2) == TCS_TEC_DIR_HEAT,
          "output direction per channel");
    CHECK(tcs_scan_snapshot(buf, 40, &d) == -1, "40B (old-length) snapshot rejected under ver 2");
    CHECK(tcs_scan_snapshot(buf, 48, &d) == -1, "48B snapshot rejected");
    CHECK(tcs_scan_snapshot(buf, 49, &d) == -1, "49B (ver-2 length) snapshot rejected under ver 3");
    CHECK(tcs_scan_snapshot(buf, 51, &d) == -1, "51B snapshot rejected");
    CHECK(tcs_tec_known_ch(0x02, 1) == 0 && tcs_tec_known_ch(0x02, 2) == 1
          && tcs_tec_known_ch(0x03, 1) == 1 && tcs_tec_known_ch(0x00, 2) == 0,
          "tec_known per channel (bit0 = TEC1, bit1 = TEC2)");

    /* 旧固件（ver 1）的快照：长度就不对，屏端 on_frame 在解帧这一步丢弃（main.c 还会再比 ver）*/
    buf[0] = 1;
    CHECK(tcs_scan_snapshot(buf, 40, &d) == -1, "old ver-1 frame does not decode as a ver-2 snapshot");

    /* 0x21 增量的两个新 mask 位 */
    memset(&e, 0, sizeof e);
    e.tec_dir = 0x21; e.tec1_v = -823; e.tec1_i = 310;
    e.tec2_v = TCS_ELEC_V_UNKNOWN; e.tec2_i = TCS_ELEC_I_UNKNOWN;
    dn = tcs_pl_delta(db, sizeof db, (uint16_t)(TCS_D_TEC_DIR | TCS_D_ELEC), &e);
    CHECK(dn == 11, "delta tec_dir + elec = 11B");
    CHECK(db[0] == 0x00 && db[1] == 0x03, "delta mask 0x0300 LE");
    CHECK(db[2] == 0x21 && db[3] == 0xC9 && db[4] == 0xFC && db[5] == 0x36 && db[6] == 0x01
          && db[7] == 0x00 && db[8] == 0x80 && db[9] == 0xFF && db[10] == 0xFF, "delta body bytes");
    memset(&e, 0x55, sizeof e);
    CHECK(tcs_scan_delta(db, dn, &e, &emask) == 0 && emask == 0x0300, "delta decode mask");
    CHECK(e.tec_dir == 0x21 && e.tec1_v == -823 && e.tec1_i == 310
          && e.tec2_v == TCS_ELEC_V_UNKNOWN && e.tec2_i == TCS_ELEC_I_UNKNOWN, "delta decode fields");
    CHECK(tcs_scan_delta(db, 10, &e, &emask) == -1, "delta truncated elec rejected");
    /* 只带方向 */
    dn = tcs_pl_delta(db, sizeof db, TCS_D_TEC_DIR, &e);
    CHECK(dn == 3 && db[0] == 0x00 && db[1] == 0x01 && db[2] == 0x21, "delta tec_dir only = 3B");
    /* 低 8 位字段在前，新字段在后 */
    memset(&e, 0, sizeof e);
    e.fan = 1; e.badge = 2; e.tec_dir = 0x30;
    dn = tcs_pl_delta(db, sizeof db, (uint16_t)(TCS_D_FAN | TCS_D_BADGE | TCS_D_TEC_DIR), &e);
    CHECK(dn == 5 && db[2] == 1 && db[3] == 2 && db[4] == 0x30, "delta field order: low bits first");

    /* 定点打包（给"未知"和越界留了口子，屏端显示"未知"而不是一个假 0）*/
    CHECK(tcs_elec_pack_v(-8.23f) == -823 && tcs_elec_pack_v(12.346f) == 1235
          && tcs_elec_pack_v(-12.346f) == -1235
          && tcs_elec_pack_v(-0.0f) == 0 && tcs_elec_pack_v(1e9f) == TCS_ELEC_V_UNKNOWN, "pack V");
    CHECK(tcs_elec_pack_i(3.10f) == 310 && tcs_elec_pack_i(-1.0f) == TCS_ELEC_I_UNKNOWN
          && tcs_elec_pack_i(1000.0f) == TCS_ELEC_I_UNKNOWN, "pack I");

    /* 屏端镜像的换算（link_snap.h 的纯逻辑）：半字节 → 方向；定点 → V / A；哪一路可用 */
    {
        tcs_link_tec_t t;
        memset(&t, 0, sizeof t);
        tcs_link_tec_set_dirs(&t, 0x21);
        CHECK(t.dir1 == TCS_TEC_DIR_COOL && t.dir2 == TCS_TEC_DIR_HEAT, "mirror: dirs from the nibbles");
        tcs_link_tec_set_elec(&t, -823, 310, TCS_ELEC_V_UNKNOWN, TCS_ELEC_I_UNKNOWN);
        CHECK(t.v1 > -8.231f && t.v1 < -8.229f && t.i1 > 3.099f && t.i1 < 3.101f,
              "mirror: fixed point -> V / A (negative voltage kept)");
        CHECK(t.valid == 1, "mirror: only TEC1 is usable when TEC2 reads unknown");
        tcs_link_tec_set_elec(&t, 0, 0, 1523, 450);
        CHECK(t.valid == 3 && t.v1 == 0.0f && t.i1 == 0.0f && t.v2 > 15.229f && t.v2 < 15.231f,
              "mirror: both usable; a real 0 is a value, not 'unknown'");
        tcs_link_tec_set_elec(&t, TCS_ELEC_V_UNKNOWN, 5, 0, TCS_ELEC_I_UNKNOWN);
        CHECK(t.valid == 0, "mirror: unknown voltage or unknown current clears that channel");
        tcs_link_tec_set_dirs(&t, 0x03);
        CHECK(t.dir1 == TCS_TEC_DIR_EXT && t.dir2 == TCS_TEC_DIR_PAUSE, "mirror: EXT / pause");
        tcs_link_tec_set_dirs(&t, 0xF0);
        CHECK(t.dir1 == TCS_TEC_DIR_PAUSE && t.dir2 == 0x0Fu,
              "mirror: each nibble is 4 bits; codes above 3 pass through (UI treats them as 'not set by this page')");
        /* i3：命令意图 / 模块回显 / 开关状态是否可信（0x20 末尾的 3 字节原样落镜像，界面据 known 位画"未知"）*/
        memset(&t, 0x55, sizeof t);
        tcs_link_tec_set_state(&t, 0x02, 0x01, 0x02);
        CHECK(t.cmd == 0x02 && t.act == 0x01 && t.known == 0x02, "mirror: cmd / act / known from the snapshot tail");
        CHECK(tcs_tec_known_ch(t.known, 1) == 0 && tcs_tec_known_ch(t.known, 2) == 1,
              "mirror: TEC1 unknown, TEC2 known");
    }

    printf("test_proto_m1: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
