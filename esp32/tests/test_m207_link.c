/* test_m207_link.c — M207"链路是否活着"的判据（S-G e2：M12-F-03，台账原记已关闭、对账认定只修一半）
 *
 * 已修的一半：探测帧 TC1/TC2 交替，"TC1 开路而 TC2 正常"能由 TC2 的有效温度触发上线。
 * 没修的一半：探测期（以及开机首联的轮询等待窗）模块回 CMD:REPLY 只被计进 unexpected，
 * 不当作链路活证据。后果：两路传感器同时开路时，链路永远回不到 online，
 * tcs_alarm_ack() 要求 m207_online 才清 ALM_M207，报警确认不掉、SAFE 自锁。
 *
 * 测试台见 m207_tb.h。每条断言在未修的内核（提交 18d1320）上都先跑红过。
 */
#include "m207_tb.h"

static void drain_resync(void) { tb_run(30); }

/* 1. 离线探测期：两路传感器都开路，模块只回 CMD:REPLY=2 → 链路回到 online，报警可确认 */
static void test_probe_reply_is_link_alive(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online before unplug");
    drain_resync();
    TB.m.silent = 1;
    int w = 200;
    while (m207_stats_offline() == 0 && w-- > 0) tb_tick();
    TB_CHECK(!m207_online() && m207_alarm_active(), "unplugged: offline + ALM_M207");

    /* 插回去的时候两路传感器都开着：模块活着，但温度参数族被固件隐藏（只回 =2）*/
    TB.m.open[1] = 1; TB.m.open[2] = 1;
    TB.m.silent = 0;
    tb_run(65);                                  /* 三次探测 */
    TB_CHECK(m207_online(),
             "M12-F-03: CMD:REPLY=2 answering a probe proves the link is alive -> online");
    TB_CHECK(g_tcs.m207_online == 1, "g_tcs.m207_online mirrors online");
    TB_CHECK(g_tcs.m207_sensor_open == 1,
             "M12-F-03: both sensors open is reported on the sensor-open flag, not as link loss");
    TB_CHECK(m207_stats_offline() == 1, "no further offline edge while the module keeps answering");
    tb_run(80);                                  /* 轮询继续拿 =2：保持在线，传感器开路保持 */
    TB_CHECK(m207_online() && g_tcs.m207_sensor_open == 1, "stays online, sensor-open stays");

    tcs_alarm_ack();
    TB_CHECK(!m207_alarm_active(), "M12-F-03: ALM_M207 can be acknowledged once the link is back");
}

/* 2. 开机首联：两路传感器都开着，同样要能建立链路（原来既不上线也不离线，卡死在中间）*/
static void test_boot_both_open_goes_online(void)
{
    tb_init();
    TB.m.open[1] = 1; TB.m.open[2] = 1;
    tb_run(200);
    TB_CHECK(m207_online(), "M12-F-03: boot with both sensors open still establishes the link");
    TB_CHECK(g_tcs.m207_sensor_open == 1, "and reports both channels as sensor-open");
    TB_CHECK(m207_stats_offline() == 0, "a module that answers never trips the offline edge");
}

/* 3. 探测期任意合法回码都算链路活（语法错 6、未知模块 0 …）：留痕，但不当作失联 */
static void test_probe_other_codes_are_link_alive(void)
{
    m207_qstats_t st;
    tb_init();
    TB_CHECK(tb_wait_online(120), "online before unplug");
    drain_resync();
    TB.m.silent = 1;
    int w = 200;
    while (m207_stats_offline() == 0 && w-- > 0) tb_tick();
    int n0 = TB.ntx;
    m207_stats_queue(&st);
    uint16_t un0 = st.unexpected;
    /* 等一条探测上线路，模块回一个与温度无关的回码 */
    while (TB.ntx == n0 && w-- > 0) tb_tick();
    TB_CHECK(TB.ntx == n0 + 1, "a probe went out");
    tb_rx_inject("CMD:REPLY=6\r");
    tb_tick();
    TB_CHECK(m207_online(), "M12-F-03: any valid CMD:REPLY to a probe is link-alive evidence");
    m207_stats_queue(&st);
    TB_CHECK(st.unexpected == un0 + 1, "the odd code is still recorded as unexpected");
    TB_CHECK(g_tcs.m207_sensor_open == 0, "a non-2 code is not mistaken for sensor-open");
    TB_CHECK(m207_stats_offline() == 1, "never counted as a comm failure");
}

/* 4. 边界：没有查询在途时收到的回码不算链路活证据（开机时序与抽取前保持一致）*/
static void test_reply_without_query_is_not_link_alive(void)
{
    tb_init();
    tb_tick();                                   /* TC1:TCSW=0（上电安全默认帧）*/
    tb_rx_inject("CMD:REPLY=1\r");               /* 模块对这条安全默认帧的应答 */
    tb_tick();
    TB_CHECK(!m207_online(), "a reply with no query outstanding does not establish the link");
    TB_CHECK(tb_wait_online(120), "the link is established by the poll reply as before");
}

int main(void)
{
    test_probe_reply_is_link_alive();
    test_boot_both_open_goes_online();
    test_probe_other_codes_are_link_alive();
    test_reply_without_query_is_not_link_alive();
    return tb_done("test_m207_link");
}
