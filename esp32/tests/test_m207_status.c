/* test_m207_status.c — M207 模块自带保护状态的轮询与取值（S-G e2：X04-F-05 ①）

 * 现象：TCERRORSTATUS / TCOTPSTATUS 这些模块自带的保护状态，原来只在取证构建
 * （CONFIG_TCS_M207_REALPROBE）里被轮询；生产路径从不读，模块自己报了过温 / 过流 / 传感器开路，
 * 固件一无所知。
 * 修法：服务在线时每 60s 读一次 TCx:TCERRORSTATUS? 与 TCx:TCOTPSTATUS?（只读查询，排普通队列），
 * 原值入缓存，经 m207_module_status() 取值。
 * 这里只做"如实取值"：不报警、不进 SAFE —— 见用例 3 与结果文档里的风险说明。
 */
#include "m207_tb.h"

#define CHECK(cond, name) TB_CHECK(cond, name)   /* 测试台的自增 / 计数宏叫 TB_CHECK */

/* 1. 解码（纯函数）：TCERRORSTATUS 是 8 位、正常=1 故障=0；bit7 不用（真机实录全正常时回 255） */
static void test_fault_decode(void)
{
    CHECK(m207_errstat_fault_mask(255u) == 0x00u, "255 (all ones): no fault");
    CHECK(m207_errstat_fault_mask(127u) == 0x00u, "127 (bit7 unused): no fault");
    CHECK(m207_errstat_fault_mask(239u) == 0x10u, "239 = 0xEF: bit4 (sensor open) latched — real-machine value");
    CHECK(m207_errstat_fault_mask(253u) == 0x02u, "253 = 0xFD: bit1 — real-machine value");
    CHECK(m207_errstat_fault_mask(0u) == 0x7Fu, "0: every protection bit faulted");
    CHECK(m207_errstat_fault_mask(0xFEu) == 0x01u, "0xFE: bit0 (external) faulted");
    CHECK(m207_errstat_fault_mask(0xFFFFu) == 0x00u, "0xFFFF: 16 ones, bits above 7 ignored");
    CHECK(m207_errstat_fault_mask(0x80FFu) == 0x00u && m207_errstat_fault_mask(0xFF7Fu) == 0x00u,
          "only bits 0..6 are decoded (bit 7 and above ignored)");
    CHECK(strcmp(m207_fault_bit_name(4), "sensor-open") == 0 &&
          strcmp(m207_fault_bit_name(3), "over-temp") == 0 &&
          strcmp(m207_fault_bit_name(7), "?") == 0, "fault bit names (from the vendor help file)");
}

/* 2. 上线后很快读到一次；之后每 60s 一次 */
static void test_status_polled_online(void)
{
    m207_mod_status_t st;
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    TB_CHECK(m207_module_status(1, &st) == -1 || st.valid == 0, "not read before the first reply");
    tb_run(12);                                  /* 复联查询 6 拍 + 状态查询 4 拍 */
    TB_CHECK(tb_tx_find("TC1:TCERRORSTATUS?") >= 0 && tb_tx_find("TC2:TCERRORSTATUS?") >= 0,
             "TCERRORSTATUS? polled on both channels right after going online");
    TB_CHECK(tb_tx_find("TC1:TCOTPSTATUS?") >= 0 && tb_tx_find("TC2:TCOTPSTATUS?") >= 0,
             "TCOTPSTATUS? polled on both channels");
    TB_CHECK(m207_module_status(1, &st) == 0, "status readable for TC1");
    CHECK(st.valid == 1, "status valid after the first poll");
    CHECK(st.errstat == 255u && st.fault_mask == 0u, "all-normal module: errstat 255, no fault bit");
    CHECK(st.otpstat == 1u, "TCOTPSTATUS raw value is passed through");
    CHECK(st.age_ms < 1000u, "age is measured in real milliseconds");
    TB_CHECK(m207_module_status(2, &st) == 0 && st.valid == 1 && st.errstat == 255u, "status readable for TC2");
    TB_CHECK(m207_module_status(0, &st) == -1 && m207_module_status(3, &st) == -1, "bad channel rejected");
    TB_CHECK(m207_module_status(1, NULL) == -1, "NULL out rejected");

    int n0 = TB.ntx;
    tb_run(1100);                                /* 55s：还不到下一次 */
    TB_CHECK(tb_tx_since(n0, "TC1:TCERRORSTATUS?") < 0, "no status poll before 60s");
    tb_run(120);                                 /* 过 60s */
    TB_CHECK(tb_tx_since(n0, "TC1:TCERRORSTATUS?") >= 0, "status polled again after 60s");
    int c = 0;
    for (int i = n0; i < TB.ntx; i++) if (!strcmp(TB.tx[i].f, "TC1:TCERRORSTATUS?")) c++;
    TB_CHECK(c == 1, "exactly one status poll per 60s window");
    CHECK(m207_module_status(1, &st) == 0 && st.age_ms < 2000u, "age restarts at each poll");
}

/* 3. 模块报了保护位：如实可取，但不报警、不进 SAFE、不掉线（接线归 control）*/
static void test_fault_visible_but_not_wired(void)
{
    m207_mod_status_t st;
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(20);
    TB_CHECK(m207_module_status(1, &st) == 0 && st.errstat == 255u, "clean at first");
    TB_CHECK(control_state() == CTRL_IDLE, "control idle");
    TB.m.errstat[1] = 239.0f;                    /* 真机实录：TC1 恒回 239（传感器开路锁存位）*/
    TB.m.errstat[2] = 253.0f;                    /* 真机实录：TC2 曾回 253 */
    tb_run(1220);                                /* 等下一次轮询 */
    TB_CHECK(m207_module_status(1, &st) == 0 && st.errstat == 239u && st.fault_mask == 0x10u,
             "X04-F-05: module protection bits are visible to the control layer (TC1)");
    TB_CHECK(m207_module_status(2, &st) == 0 && st.errstat == 253u && st.fault_mask == 0x02u,
             "X04-F-05: module protection bits are visible to the control layer (TC2)");
    TB_CHECK(m207_online(), "link stays online when the module reports protection bits");
    TB_CHECK(g_tcs.alarm_bits == 0, "NOT wired here: no alarm bit is raised by the M207 service");
    TB_CHECK(control_state() == CTRL_IDLE, "NOT wired here: control is not pushed into SAFE");
    TB_CHECK(m207_last_temp(1) > 25.0f && m207_last_temp(1) < 26.5f, "temperature still sampled normally");
}

/* 4. 离线期间不轮询；复联后马上读一次；掉线后旧值带龄可辨 */
static void test_status_offline(void)
{
    m207_mod_status_t st;
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(20);
    TB.m.silent = 1;
    int w = 200;
    while (m207_stats_offline() == 0 && w-- > 0) tb_tick();
    int n0 = TB.ntx;
    tb_run(300);                                 /* 15s 离线 */
    TB_CHECK(tb_tx_since(n0, "TC1:TCERRORSTATUS?") < 0, "no status poll while offline");
    TB_CHECK(m207_module_status(1, &st) == 0 && st.valid == 1, "last value still readable while offline");
    uint32_t a0 = st.age_ms;
    tb_run(100);
    TB_CHECK(m207_module_status(1, &st) == 0 && st.age_ms > a0 + 4000u,
             "X04-F-05: a stale status is honest — its age keeps growing while offline");

    TB.m.silent = 0;
    w = 150;
    while (!m207_online() && w-- > 0) tb_tick();
    n0 = TB.ntx;
    tb_run(20);
    TB_CHECK(tb_tx_since(n0, "TC1:TCERRORSTATUS?") >= 0, "status polled again soon after reconnect");
    TB_CHECK(m207_module_status(1, &st) == 0 && st.age_ms < 2000u, "fresh value after reconnect");
}

int main(void)
{
    test_fault_decode();
    test_status_polled_online();
    test_fault_visible_but_not_wired();
    test_status_offline();
    return tb_done("test_m207_status");
}
