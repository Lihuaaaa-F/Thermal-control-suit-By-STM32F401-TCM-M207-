/* test_m207_sim.c — 台架水温注入（只在 CONFIG_TCS_SIM 档编译；S 线 i3，M09-F-09 的注入档口径）
 *
 * with -DCONFIG_TCS_SIM=1（run_m207_tests.sh 的 m207sim 一行）。注入值每拍在 tick_end 里覆盖对外读数
 * g_tcs.tcNX_act（在 RX 之后，模块的真应答也盖得掉）；生效状态由 m207_sim_water_active() 给出，
 * main.c 把它接进 control_ops.stuck_exempt（注入在生效的那一路，卡死判据豁免——注入值是人给的常数）。
 * 不注入时一行代码都不执行（默认档不编译这套实现，默认档 bin 里也就查不到 water 命令）。
 */
#include "m207_tb.h"

int main(void)
{
    tb_init();
    TB_CHECK(m207_sim_water_active() == 0, "off by default");
    TB_CHECK(tb_wait_online(120), "online");

    m207_sim_water(1, 31.5f);
    TB_CHECK(m207_sim_water_active() == 1, "TC1 injection is active");
    tb_tick();
    TB_CHECK(g_tcs.tc1_act == 31.5f, "the injected value reaches g_tcs.tc1_act");
    tb_run(20);                                   /* 1 s：模块还在回它的 25.x */
    TB_CHECK(g_tcs.tc1_act == 31.5f, "the injected value wins over the module's replies, tick after tick");
    TB_CHECK(g_tcs.tc2_act != 31.5f, "the other channel is untouched");

    m207_sim_water(3, 10.0f);                     /* 非法通道 */
    m207_sim_water(1, 999.0f);                    /* 越界 */
    m207_sim_water(2, -100.0f);                   /* 越界（负值）*/
    TB_CHECK(m207_sim_water_active() == 1 && g_tcs.tc1_act == 31.5f,
             "bad channel / out-of-range values are ignored");

    m207_sim_water(2, 22.25f);
    tb_tick();
    TB_CHECK(m207_sim_water_active() == 3 && g_tcs.tc2_act == 22.25f, "both channels");

    m207_sim_water_off(1);
    TB_CHECK(m207_sim_water_active() == 2, "TC1 injection off, TC2 still on");
    tb_run(20);
    TB_CHECK(g_tcs.tc1_act != 31.5f, "TC1 is back to the module's reading");
    TB_CHECK(g_tcs.tc2_act == 22.25f, "TC2 is still injected");

    m207_sim_water_off(0);
    TB_CHECK(m207_sim_water_active() == 0, "both off");
    tb_run(20);
    TB_CHECK(g_tcs.tc2_act != 22.25f, "TC2 is back to the module's reading");

    /* 重新初始化把注入清空（上电 / 复位语义）*/
    m207_sim_water(1, 40.0f);
    tb_init();
    TB_CHECK(m207_sim_water_active() == 0, "core init clears the injection");

    return tb_done("test_m207_sim");
}
