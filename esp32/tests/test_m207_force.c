/* test_m207_force.c — P1-09 的另一半：安全关断在模块离线时也记得住，复联第一帧补发（S 线 i3，2026-11-26）
 *
 * 基座（fix/s-u-int2 @ bfd8725）上的事实：控制核这边 SAFE 的关断 / 500ms 重申已经走 ops.tec_force，
 * 但 tcs_m207 没有 m207_tec_force()，main.c 也没接——退化成 m207_tec()。而 m207_tec() 第一行就是
 * "离线 → 回 -2"：拔掉 M207 之后的安全关断连旁路槽都进不了。复联时的补发又只看回显
 * g_tcs.tecN_on：关断之后晚到的 TCSW? 应答会把它写成"开"，于是漏发。
 *
 * 测试台见 m207_tb.h。F1 / F3 / F5 是"控制 + 服务内核"联调：装配与 esp32/main/main.c 相同
 * （tec_sw → m207_tec，tec_force → m207_tec_force），控制节拍每 2 个台架拍（100ms）一次。
 * 未修的内核上：F2 / F3 / F4 / F6 / F7 红（F1 / F5 在旧内核上也是绿的，原因见各条注释）；配"退化为 m207_tec"的力通道桩（= 修前 main.c 的实际
 * 行为：tec_force 为空、退回 tec_sw）也是红。红输出见 docs/results/S_G_i3_结果.md。
 */
#include "m207_tb.h"
#include "post.h"                     /* POST_ARM_*：布防状态直写（与 test_control_esp32.c 的 set_armed 同款）*/

typedef void (*tick_fn)(void);

/* ---- 与 main.c 相同的装配 ---- */
static int n_force;                  /* 经安全关断通道进来的请求数 */
static uint32_t f_t;
static void f_fan(uint8_t on) { (void)on; }
static void f_tec_sw(uint8_t ch, uint8_t on) { (void)m207_tec(ch, on); }
static int f_tec_temp(uint8_t ch, float v) { return m207_set_temp(ch, v); }
static void f_tec_force(uint8_t ch, uint8_t on) { n_force++; (void)m207_tec_force(ch, on); }

static void f_init(void)
{
    n_force = 0;
    f_t = 0;
    tb_init();
    param_registry_init(&(param_hooks_t){0});
    control_init(&(control_ops_t){
        .fan_set = f_fan, .tec_sw = f_tec_sw, .tec_temp = f_tec_temp, .on_state = 0,
        .tec_force = f_tec_force,
    });
    post_arm_commit(POST_ARM_UNARMED_ABSENT);   /* 未布防：模块失联本身不进 SAFE，急停是唯一的触发 */
    g_tcs.body_sensor_ok = 1; g_tcs.body_temp = 37.0f; g_tcs.m207_sensor_open = 0;
}
static void f_tick(void)
{
    tb_tick();
    if (++f_t % 2u == 0u) control_poll(TB.now_ms);   /* 控制节拍 100ms */
}
static void f_run(int n) { while (n-- > 0) f_tick(); }
static int f_wait_online(int max_ticks)
{
    while (!m207_online() && max_ticks-- > 0) f_tick();
    return m207_online();
}

/* 拔线（模块不应答；仍供电，TCSW 保持）到被判离线。返回 1 = 已离线 */
static int go_offline(tick_fn tk)
{
    int guard = 200;
    uint16_t before = m207_stats_offline();
    TB.m.silent = 1;
    while (m207_stats_offline() == before && guard-- > 0) tk();
    return (m207_stats_offline() != before) && !m207_online();
}
/* 插回。*idx = 复联那一拍发出帧的下标。返回 1 = 已复联 */
static int reconnect(tick_fn tk, int *idx)
{
    int guard = 200;
    TB.m.silent = 0;
    while (!m207_online() && guard-- > 0) { *idx = TB.ntx; tk(); }
    return m207_online();
}

/* 自下标 idx 起，只允许两路的 TCSW=0（可重复），直到两路都出现过；别的帧先出现 = 0。
 * "复联后先补发关断、再放行普通队列"的顺序断言。调用前先跑够拍数（两路各一帧）。*/
static int off_frames_first(int idx)
{
    int seen1 = 0, seen2 = 0;
    for (int i = idx; i < TB.ntx; i++) {
        if (!strcmp(TB.tx[i].f, "TC1:TCSW=0")) seen1 = 1;
        else if (!strcmp(TB.tx[i].f, "TC2:TCSW=0")) seen2 = 1;
        else return 0;
        if (seen1 && seen2) return 1;
    }
    return 0;
}

/* ================= F1：关单条件——拔掉 M207 后触发急停，重连后 TEC 为 0 =================
 * 端到端：控制任务在跑，装配与 main.c 相同。复联后关断先于一切只读查询 / 队列帧上线路，
 * 模块两路 TCSW 归 0。说明：这一条在未修的内核上也是绿的——控制任务每 500ms 重申时会把
 * 共用字段 g_tcs.tecN_on 改回 0，旧的复联判据（看这个字段）碰巧成立。它钉的是关单条件本身；
 * 旧实现真正漏掉的两种情形在 F2 / F3 / F6。*/
static void test_estop_offline_then_reconnect(void)
{
    int idx = -1;
    f_init();
    TB_CHECK(f_wait_online(120), "F1 online");
    f_run(20);
    control_manual_tec(1, 1);                        /* 手动开着 TC1（加热中）*/
    f_run(20);
    TB_CHECK(TB.m.sw[1] == 1 && control_tec_cmd(1) == 1 && control_tec_act(1) == 1,
             "F1 precondition: TC1 heating, command on, echo on");
    TB_CHECK(g_tcs.tec_known == 3, "F1 precondition: TEC state known while online");

    TB_CHECK(go_offline(f_tick), "F1 unplugged: offline");
    f_run(2);                                        /* tec_known 是控制节拍刷的缓存副本（最多晚一拍）：先让节拍跟上离线沿 */
    n_force = 0;
    tcs_emergency_stop();                            /* 离线期间触发急停 */
    TB_CHECK(control_state() == CTRL_SAFE && n_force == 2,
             "F1 estop while offline -> SAFE, both shutdowns went through the force channel");
    TB_CHECK(control_tec_cmd(1) == 0 && control_tec_cmd(2) == 0, "F1 command intent: both off");
    TB_CHECK(control_tec_known() == 0 && g_tcs.tec_known == 0,
             "F1 tec_known is 0 while the module is offline (not 'off')");
    n_force = 0;
    f_run(10);                                       /* 500ms */
    TB_CHECK(n_force >= 2 && n_force <= 4,
             "F1 while offline the latch keeps pushing the shutdown through the force channel every 500ms");
    TB_CHECK(control_state() == CTRL_SAFE, "F1 stays SAFE while offline");

    /* 插回：模块没掉电，TC1 的 TCSW 仍是 1 */
    TB_CHECK(reconnect(f_tick, &idx), "F1 back online");
    TB_CHECK(idx >= 0 && tb_tx_at(idx, "TC1:TCSW=0"),
             "F1 the very first frame after reconnect is the TC1 shutdown");
    f_run(5);                                        /* 250ms：早于控制任务的下一次 500ms 重申 */
    TB_CHECK(off_frames_first(idx),
             "F1 both shutdowns go out before any read-back / queued / poll frame");
    f_run(40);
    TB_CHECK(TB.m.sw[1] == 0 && TB.m.sw[2] == 0, "F1 after reconnect TEC is 0 on both channels");
    TB_CHECK(control_tec_act(1) == 0 && control_tec_known() == 3 && g_tcs.tec_known == 3,
             "F1 echo and tec_known agree again after reconnect");
    TB_CHECK(control_state() == CTRL_SAFE, "F1 still SAFE after reconnect (estop needs an ack)");
}

/* ================= F2：复联判据读命令值，不读回显（只用原有接口；未修的内核直接红）================= */
static void test_reconnect_reads_command_not_echo(void)
{
    int idx = -1;
    tb_init();
    TB_CHECK(tb_wait_online(120), "F2 online");
    tb_run(30);
    TB.m.sw[1] = 1;                                  /* 模块此刻在输出 */
    tb_rx_inject("TC1:TCSW=1\r");                    /* 回显：开 */
    tb_tick();
    TB_CHECK(g_tcs.tec1_on == 1, "F2 precondition: echo says ON");
    TB_CHECK(control_tec_cmd(1) == 0,
             "F2 precondition: command intent is OFF (control never asked for on)");
    TB_CHECK(go_offline(tb_tick), "F2 offline");
    TB_CHECK(reconnect(tb_tick, &idx), "F2 back online");
    TB_CHECK(idx >= 0 && tb_tx_at(idx, "TC1:TCSW=0"),
             "F2 command OFF + late ON echo: the shutdown is still resent first");
    tb_run(30);
    TB_CHECK(TB.m.sw[1] == 0, "F2 TEC is off on the module after reconnect");
}

/* ================= F3：保持位独立于命令意图 =================
 * 服务层自己发起的关断（例：解析到超温的那一拍走 m207_tec_force），控制的命令意图还没来得及
 * 归零，链路就断了。复联时命令是开、回显也是开——只凭保持位也要先补发关断。*/
static void test_force_holds_while_command_says_on(void)
{
    int idx = -1, n_tx0 = 0;
    f_init();
    TB_CHECK(f_wait_online(120), "F3 online");
    f_run(20);
    control_manual_tec(2, 1);                        /* TC2 开着，命令意图 = 开 */
    f_run(20);
    TB_CHECK(TB.m.sw[2] == 1 && control_tec_cmd(2) == 1, "F3 precondition: TC2 on, command on");
    TB_CHECK(go_offline(f_tick), "F3 offline");
    m207_qstats_t st0, st1;
    m207_stats_queue(&st0);
    uint16_t wd0 = m207_stats_wdrop();
    TB_CHECK(m207_tec_force(2, 0) == 0, "F3 shutdown issued by the service layer while offline is accepted");
    n_tx0 = TB.ntx;
    m207_stats_queue(&st1);
    TB_CHECK(st1.qdrop == st0.qdrop && m207_stats_wdrop() == wd0,
             "F3 accepted while offline without counting a rejected write");
    f_run(20);                                       /* 离线 1 s：保持位不能被探测拍冲掉 */
    TB_CHECK(!m207_online() && control_tec_cmd(2) == 1, "F3 still offline; command intent still on");
    TB_CHECK(tb_tx_since(n_tx0, "TC2:TCSW=0") < 0,
             "F3 nothing is sent while the module is away: the request is only held");
    TB_CHECK(reconnect(f_tick, &idx), "F3 back online");
    f_run(5);
    TB_CHECK(idx >= 0 && tb_tx_since(idx, "TC2:TCSW=0") >= 0 && off_frames_first(idx),
             "F3 the held shutdown goes out first, before the queue is released, even though the command says on");
    f_run(40);
    TB_CHECK(TB.m.sw[2] == 0, "F3 TEC2 off on the module after reconnect");
}

/* ================= F4：只有"关"走这条通道；不占队列 ================= */
static void test_force_off_only(void)
{
    m207_qstats_t st0, st1;
    tb_init();
    TB_CHECK(tb_wait_online(120), "F4 online");
    tb_run(30);
    TB_CHECK(m207_tec_force(3, 0) == -1, "F4 bad channel rejected");
    m207_stats_queue(&st0);
    int n0 = TB.ntx;
    TB_CHECK(m207_tec_force(1, 0) == 0, "F4 off accepted while online");
    tb_tick();
    TB_CHECK(tb_tx_at(n0, "TC1:TCSW=0"), "F4 off is the very next frame on the wire");
    m207_stats_queue(&st1);
    TB_CHECK(st1.qdrop == st0.qdrop,
             "F4 the force channel never counts as a queue drop");
    tb_run(40);

    /* 队列满：旁路照样下一拍上线路 */
    int okc = 0;
    for (int i = 0; i < 16; i++) okc += (m207_map_set((uint16_t)(0x100 + i), 1.0f + (float)i) == 0);
    TB_CHECK(okc == 16, "F4 16 verified writes fill the queue");
    m207_stats_queue(&st0);
    n0 = TB.ntx;
    TB_CHECK(m207_tec_force(2, 0) == 0, "F4 off accepted while the queue is full");
    tb_tick();
    TB_CHECK(tb_tx_at(n0, "TC2:TCSW=0"), "F4 off bypasses the full queue");
    m207_stats_queue(&st1);
    TB_CHECK(st1.qdrop == st0.qdrop, "F4 no queue drop counted");
    tb_run(80);

    /* "开"不走这条通道：排普通队列（保住"先设目标、后开"的顺序）；离线照旧拒收 */
    n0 = TB.ntx;
    TB_CHECK(m207_map_set(0x100, 2.5f) == 0 && m207_tec_force(2, 1) == 0, "F4 a queued write, then 'on' via the force entry");
    tb_tick();
    TB_CHECK(!tb_tx_at(n0, "TC2:TCSW=1"), "F4 'on' does not jump the queue");
    tb_run(20);
    TB_CHECK(tb_tx_since(n0, "TC2:TCSW=1") >= 0 && TB.m.sw[2] == 1, "F4 'on' still goes out through the queue");
    TB_CHECK(go_offline(tb_tick), "F4 offline");
    TB_CHECK(m207_tec_force(2, 1) == -2, "F4 'on' while offline is rejected like any other write (not held)");
}

/* ================= F5：保持位被更新的"开"解除 =================
 * 否则一次没等到读回的关断会在下次复联时把用户刚开的通道关掉。*/
static void test_newer_on_releases_hold(void)
{
    int idx = -1;
    f_init();
    TB_CHECK(f_wait_online(120), "F5 online");
    f_run(20);
    TB.m.silent = 1;                                 /* 在线，但模块此刻不应答 */
    TB_CHECK(m207_tec_force(1, 0) == 0, "F5 force off issued");
    f_run(14);                                       /* 700ms：关断发出去了、重发一次、NAK；失败数不到 5 */
    TB_CHECK(m207_online(), "F5 still online (fail count below the limit)");
    TB_CHECK(m207_stats_nak() >= 1, "F5 the shutdown got no read-back: NAK, hold stays");
    TB.m.silent = 0;
    control_manual_tec(1, 1);                        /* 用户重新开：更新的请求 */
    f_run(30);
    TB_CHECK(TB.m.sw[1] == 1 && control_tec_cmd(1) == 1 && control_tec_act(1) == 1,
             "F5 precondition: TC1 on again (command on, echo on)");
    TB_CHECK(go_offline(f_tick), "F5 offline");
    TB_CHECK(reconnect(f_tick, &idx), "F5 back online");
    f_run(5);
    TB_CHECK(idx >= 0 && tb_tx_since(idx, "TC1:TCSW=0") < 0 && tb_tx_since(idx, "TC1:TCSW?") >= 0,
             "F5 a stale held shutdown does not turn off a channel the user just turned on (read-back only)");
    f_run(30);
    TB_CHECK(TB.m.sw[1] == 1, "F5 TC1 stays on after reconnect");
}

/* ================= F6：控制任务在跑、不在安全态：普通关断之后晚到的回显 =================
 * 不是急停、没有锁存：没有人每 500ms 把共用字段改回去。关断帧发出前链路断掉（模块没听到，
 * TEC 还开着），晚到的 TCSW=1 应答把回显写成"开"。未修的内核复联时只读回不补发——TEC 一直开着，
 * 直到用户再点一次。*/
static void test_late_echo_with_control_alive(void)
{
    int idx = -1;
    f_init();
    TB_CHECK(f_wait_online(120), "F6 online");
    f_run(20);
    control_manual_tec(1, 1);                        /* 加热中 */
    f_run(20);
    TB_CHECK(TB.m.sw[1] == 1 && control_tec_cmd(1) == 1, "F6 precondition: TC1 on");
    TB_CHECK(control_state() == CTRL_IDLE, "F6 precondition: not in SAFE");

    control_manual_tec(1, 0);                        /* 普通关断（暂停），不是安全态 */
    tb_rx_inject("TC1:TCSW=1\r");                   /* 晚到的回显 */
    TB.m.silent = 1;                                 /* 关断帧还没发出，链路断掉 */
    f_tick();                                        /* 回显到；关断帧上线路（丢失）*/
    TB_CHECK(control_tec_cmd(1) == 0 && control_tec_act(1) == 1,
             "F6 command off, late echo says on");
    TB_CHECK(go_offline(f_tick), "F6 offline");
    TB_CHECK(control_state() == CTRL_IDLE, "F6 control alive and not in SAFE (unarmed, manual)");

    TB_CHECK(reconnect(f_tick, &idx), "F6 back online");
    TB_CHECK(idx >= 0 && tb_tx_at(idx, "TC1:TCSW=0"),
             "F6 the shutdown is resent first, before anything is read back");
    f_run(60);                                       /* 3 s */
    TB_CHECK(TB.m.sw[1] == 0, "F6 TEC is off after reconnect (used to stay on until the user toggled)");
    TB_CHECK(control_tec_act(1) == 0, "F6 echo is off again");
}

/* ================= F7：N-01 超温——服务层直接关断，链路在读回之前断开 =================
 * 控制任务还没跟上（这里不跑 control_poll：命令意图还是开）。复联时命令是开、回显也是开，
 * 只凭服务层自己记下的保持位补发关断。*/
static void test_overtemp_shutdown_holds(void)
{
    int idx = -1, seen = 0;
    tb_init();
    control_init(&(control_ops_t){
        .fan_set = f_fan, .tec_sw = f_tec_sw, .tec_temp = f_tec_temp, .on_state = 0,
    });
    param_registry_init(&(param_hooks_t){0});
    g_tcs.body_sensor_ok = 1; g_tcs.body_temp = 37.0f;
    TB_CHECK(tb_wait_online(120), "F7 online");
    tb_run(30);
    control_manual_tec(1, 1);                        /* TC1 开着，命令意图 = 开 */
    tb_run(30);
    TB_CHECK(TB.m.sw[1] == 1 && control_tec_cmd(1) == 1 && control_state() == CTRL_IDLE,
             "F7 precondition: TC1 on, command on, not in SAFE");
    TB.m.ramp[1] = 6.0f;                             /* 水温每秒 +6°C：25 → 49（合理性滤波上限 10°C/s）*/
    {
        int guard = 300;
        while (guard-- > 0 && !seen) {
            tb_tick();
            if (tb_tx_at(TB.ntx - 1, "TC1:TCSW=0") && g_tcs.tc1_act > ALM_OVERTEMP_HARD_C) seen = 1;
        }
    }
    TB.m.ramp[1] = 0.0f;
    TB.m.silent = 1;                                 /* 读回之前链路断掉 */
    TB_CHECK(seen, "F7 over-temp parsed by the service: the shutdown goes out at once");
    TB_CHECK((control_safety_latched() & SAFETY_BIT_OVERTEMP) != 0,
             "F7 over-temp latch set by the service layer (N-01)");
    TB_CHECK(go_offline(tb_tick), "F7 offline");
    TB.m.sw[1] = 1;                                  /* 模块没掉电、关断没生效：TC1 仍在输出 */
    TB.m.act[1] = 30.0f;                             /* 断线期间水温降下来了：复联的第一个应答不再走超温路径 */
    TB_CHECK(control_tec_cmd(1) == 1, "F7 command intent still on (control has not run)");
    TB_CHECK(reconnect(tb_tick, &idx), "F7 back online");
    tb_run(5);
    TB_CHECK(idx >= 0 && tb_tx_at(idx, "TC1:TCSW=0"),
             "F7 the service-layer shutdown is resent first on reconnect");
    tb_run(30);
    TB_CHECK(TB.m.sw[1] == 0, "F7 TEC1 off on the module after reconnect");
}

int main(void)
{
    test_estop_offline_then_reconnect();
    test_reconnect_reads_command_not_echo();
    test_force_holds_while_command_says_on();
    test_force_off_only();
    test_newer_on_releases_hold();
    test_late_echo_with_control_alive();
    test_overtemp_shutdown_holds();
    return tb_done("test_m207_force");
}
