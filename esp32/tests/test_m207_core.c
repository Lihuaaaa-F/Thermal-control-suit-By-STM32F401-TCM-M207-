/* test_m207_core.c — M207 服务内核（m207_core.c）主机用例
 *
 * 背景（S-G e2，X06-F-05 / M12-F-12）：m207_service.c 的状态机原来只能在取证构建
 * （CONFIG_TCS_M207_SELFLOOP）上靠真机自测，主机门禁完全测不到。抽取出 m207_core.c 之后，
 * 这里逐拍驱动它：上电首联、真机 selfloop 九步的主机移植、探测交替、传感器开路去抖、
 * 超温切断、写路径（拒绝/超时/重发/NAK）、离线停写，以及下行帧的黄金样本逐字节对照。
 *
 * 测试台见 m207_tb.h。编译：esp32/tests/run_m207_tests.sh 的 m207core 一行（经 run_all.sh 进门禁）。
 */
#include "m207_tb.h"

/* ---------- 1. 上电：安全默认帧 → 首问 → 首联上线 → 复联查询 ---------- */
static void test_boot_first_online(void)
{
    tb_init();
    TB_CHECK(!m207_online(), "boot: offline");
    TB_CHECK(m207_set_temp(1, 30.0f) == -2, "boot: write rejected while offline");
    TB_CHECK(m207_temp_age_ms() == 0xFFFFu, "boot: temp age unknown");
    tb_tick();
    TB_CHECK(tb_tx_at(0, "TC1:TCSW=0"), "tick1: TC1:TCSW=0 (safe default)");
    tb_tick();
    TB_CHECK(tb_tx_at(1, "TC2:TCSW=0"), "tick2: TC2:TCSW=0 (safe default)");
    tb_tick();
    TB_CHECK(tb_tx_at(2, "TC1:TCACTTEMP?") && TB.ntx == 3, "tick3: first poll TC1");
    TB_CHECK(!m207_online(), "tick3: still offline (reply not yet arrived)");
    tb_tick();                                   /* 应答到 → 上线；TEC 关着 → 双通道经旁路补发 TCSW=0 */
    TB_CHECK(m207_online(), "tick4: online on first temperature");
    TB_CHECK(g_tcs.m207_online == 1, "g_tcs.m207_online mirrors online");
    /* X03-F-07：复联补发的安全关断帧走旁路，先于只读查询上线路；每条自带回读，
     * 所以复联序列里每个通道恰好一次 TCSW?（原来查询在前、关断排在队尾）*/
    tb_tick(); tb_tick();
    TB_CHECK(tb_tx_at(3, "TC1:TCSW=0") && tb_tx_at(4, "TC2:TCSW=0"),
             "resync: safe-off frames first (one per tick)");
    tb_tick(); tb_tick();
    TB_CHECK(tb_tx_at(5, "TC1:TCSW?") && tb_tx_at(6, "TC2:TCSW?"),
             "resync: read back TCSW once per channel");
    tb_tick(); tb_tick();
    TB_CHECK(tb_tx_at(7, "TC1:TCADJTEMP?") && tb_tx_at(8, "TC2:TCADJTEMP?") && TB.ntx == 9,
             "resync: then TCADJTEMP? x2, nothing else");
    /* 50ms 铁律：相邻两帧不同拍 */
    for (int i = 1; i < TB.ntx; i++)
        TB_CHECK(TB.tx[i].tick > TB.tx[i - 1].tick, "one frame per tick (50ms rule)");
    /* 双通道都有温度后年龄已知且 ≤ 轮询周期量级 */
    tb_run(40);
    TB_CHECK(m207_last_temp(1) > 25.0f && m207_last_temp(1) < 26.0f, "tc1 cached");
    TB_CHECK(m207_last_temp(2) > 24.0f && m207_last_temp(2) < 25.0f, "tc2 cached");
    TB_CHECK(m207_temp_age_ms() < 2000u, "temp age known after both channels");
    TB_CHECK(g_tcs.tc1_act > 25.0f && g_tcs.tc2_act > 24.0f, "g_tcs temps mirrored");
}

/* ---------- 2. 真机 selfloop 九步（R1b [M207SL]，docs/evidence/R1b_selfloop.log）的主机移植 ---------- */
static void test_selfloop_nine(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "1 online-first");
    tcs_alarm_ack();

    tb_run(30);
    TB_CHECK(m207_last_temp(1) != 0.0f && m207_last_temp(2) != 0.0f, "2 temp-cache tc1/tc2");

    uint16_t vok0 = m207_stats_verify_ok();
    TB_CHECK(m207_set_temp(1, 30.5f) == 0, "3 set enqueued");
    int w = 40;
    while (m207_stats_verify_ok() == vok0 && w-- > 0) tb_tick();
    TB_CHECK(m207_stats_verify_ok() == vok0 + 1, "3 verify-ok set 30.5");

    uint16_t rs0 = m207_stats_resend();
    TB.m.wrong_echo = 1;
    (void)m207_set_temp(1, 31.0f);
    w = 50;
    while (m207_stats_resend() == rs0 && w-- > 0) tb_tick();
    tb_run(30);
    TB_CHECK(m207_stats_resend() == rs0 + 1 && m207_stats_verify_ok() == vok0 + 2,
             "4 mismatch-resend-then-ok");

    uint16_t nak0 = m207_stats_nak();
    TB.m.wrong_echo = 2;
    (void)m207_set_temp(1, 32.0f);
    w = 60;
    while (m207_stats_nak() == nak0 && w-- > 0) tb_tick();
    TB.m.wrong_echo = 0;
    TB_CHECK(m207_stats_nak() == nak0 + 1, "5 nak-after-retry");

    uint16_t off0 = m207_stats_offline();
    TB.m.silent = 1;
    w = 200;
    while (m207_stats_offline() == off0 && w-- > 0) tb_tick();
    TB_CHECK(m207_stats_offline() == off0 + 1 && !m207_online() &&
             m207_alarm_active(), "6 offline-5fails-ALM_M207");

    TB.m.silent = 0;
    w = 120;
    while (!m207_online() && w-- > 0) tb_tick();
    TB_CHECK(m207_online() && m207_alarm_active(), "7 recover-alarm-sticky");

    tcs_alarm_ack();
    TB_CHECK(!m207_alarm_active(), "8 ack-clears-ALM_M207");

    TB.m.silent = 1;
    w = 200;
    while (m207_stats_offline() == off0 + 1 && w-- > 0) tb_tick();
    int rc = m207_set_temp(2, 25.0f);
    TB_CHECK(rc == -2, "9 write-rejected-while-offline");
}

/* ---------- 3. 离线探测：1s 一次、TC1/TC2 交替 ---------- */
static void test_probe_alternates(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online before unplug");
    TB.m.silent = 1;
    int w = 200;
    while (m207_stats_offline() == 0 && w-- > 0) tb_tick();
    TB_CHECK(m207_stats_offline() == 1, "offline after 5 fails");
    int n = TB.ntx;
    tb_run(65);                                  /* 三次探测 */
    TB_CHECK(TB.ntx >= n + 3, "three probes in 65 ticks");
    const char *p0 = TB.tx[n].f, *p1 = TB.tx[n + 1].f, *p2 = TB.tx[n + 2].f;
    TB_CHECK(!strcmp(p0, "TC1:TCACTTEMP?") || !strcmp(p0, "TC2:TCACTTEMP?"), "probe is a temperature query");
    TB_CHECK(strcmp(p0, p1) != 0 && strcmp(p1, p2) != 0 && strcmp(p0, p2) == 0,
             "probes alternate TC1/TC2");
    TB_CHECK(TB.tx[n + 1].tick - TB.tx[n].tick == 20 &&
             TB.tx[n + 2].tick - TB.tx[n + 1].tick == 20, "probe every 1s (20 ticks)");
}

/* ---------- 4. 传感器开路：errcode=2 连续 3 次才坐实；温度回来即清 ---------- */
static void test_sensor_open_debounce(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    TB_CHECK(g_tcs.m207_sensor_open == 0, "no sensor-open before");
    TB.m.open[1] = 1;
    tb_run(40);                                  /* 2s：ch1 被问 2 次 */
    TB_CHECK(g_tcs.m207_sensor_open == 0, "2 x errcode=2 still debouncing");
    tb_run(40);                                  /* 第 3 次 */
    TB_CHECK(g_tcs.m207_sensor_open == 1, "3 x errcode=2 -> sensor-open latched");
    TB_CHECK(m207_online(), "sensor open does not drop the link");
    TB_CHECK(m207_stats_offline() == 0, "errcode=2 never counts as comm fail");
    TB.m.open[1] = 0;
    tb_run(30);
    TB_CHECK(g_tcs.m207_sensor_open == 0, "temperature back clears sensor-open");
}

/* ---------- 5. 超温：解析到 tc1>48°C 立刻锁存 + 报警 + 下发 TCSW=0 ---------- */
static void test_overtemp_trip(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    TB_CHECK(control_safety_latched() == 0, "no latch before");
    TB.m.act[1] = 33.0f; TB.m.ramp[1] = 8.0f;    /* 33 → 41 → 49：每步 8°C/s，过得了合理性滤波 */
    int n0 = TB.ntx, w = 120;
    while (!control_safety_latched() && w-- > 0) tb_tick();
    TB_CHECK((control_safety_latched() & SAFETY_BIT_OVERTEMP) != 0, "overtemp latches safety");
    TB_CHECK(tcs_alarm_active(ALM_OVERTEMP), "overtemp raises ALM_OVERTEMP");
    TB_CHECK(tb_tx_since(n0, "TC1:TCSW=0") >= 0, "overtemp sends TC1:TCSW=0");
    TB_CHECK(m207_online(), "link stays online after trip");
}

/* ---------- 6. 写路径：协议拒绝不重发；应答丢失重发一次后 NAK ---------- */
static void test_write_paths(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);                                  /* 复联序列（含安全关断的读回）发完再取基线 */
    uint16_t nak0 = m207_stats_nak(), rs0 = m207_stats_resend();
    TB.m.reject_set = 1;
    TB_CHECK(m207_set_temp(1, 30.0f) == 0, "set enqueued");
    tb_run(10);
    TB_CHECK(m207_stats_nak() == nak0 + 1, "CMD:REPLY=4 -> NAK");
    TB_CHECK(m207_stats_resend() == rs0, "protocol reject: no resend");
    TB_CHECK(tb_tx_count("TC1:TCADJTEMP=30.0") == 1, "rejected set sent once");
    TB.m.reject_set = 0;

    uint16_t nak1 = m207_stats_nak(), rs1 = m207_stats_resend();
    TB.m.silent = 1;
    (void)m207_set_temp(2, 31.0f);
    tb_run(24);
    TB_CHECK(m207_stats_resend() == rs1 + 1 && m207_stats_nak() == nak1 + 1,
             "ack timeout -> resend once -> NAK");
    TB_CHECK(tb_tx_count("TC2:TCADJTEMP=31.0") == 2, "set frame sent exactly twice");
    TB_CHECK(m207_online(), "24 ticks of silence is not yet offline");
}

/* ---------- 7. 离线：写一律拒绝并计数，ALM_M207 粘滞 ---------- */
static void test_offline_writes(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    TB.m.silent = 1;
    int w = 200;
    while (m207_stats_offline() == 0 && w-- > 0) tb_tick();
    uint16_t wd0 = m207_stats_wdrop();
    TB_CHECK(m207_set_temp(1, 25.0f) == -2, "set_temp rejected");
    TB_CHECK(m207_map_set(0x100, 1.0f) == -2, "map_set rejected");
    TB_CHECK(m207_send_raw("TC1:TCSW?") == -2, "send_raw rejected");
    TB_CHECK(m207_save_all() == -2, "save_all rejected");
    TB_CHECK(m207_stats_wdrop() == wd0 + 4, "each rejected write counted in wdrop");
    TB_CHECK(m207_alarm_active(), "ALM_M207 raised at offline edge");
    TB_CHECK(g_tcs.m207_online == 0, "g_tcs.m207_online cleared");
}

/* ---------- 8. 下行帧黄金样本：逐字节对照 m207_golden_tx.txt ---------- */
static void golden_script(void)
{
    static const float sv[] = { 30.5f, -7.0f, 80.0f, -20.0f, 25.04f, 0.0f };
    static const char *const raws[] = { "TC1:TCERRORSTATUS?", "TC1:TCSW=1@0#7D", "ABC", "PCB:TMOTPHT?" };
    static const char *const save_names[16] = {
        "TCPIDP", "TCPIDTI", "TCPIDTD", "TCOTPHT", "TCOTPLT", "TCOTPHHYS", "TCOTPMODE",
        "TCOCPADJTHR", "TCOVPADJTHR", "TCRTOCPHT", "TCRTOCPLT", "TCMINTEMP", "TCMAXTEMP",
        "TCMAXV", "TCCOOLVRATIO", "TCSTEPTEMP",
    };
    char f[40];

    TB_CHECK(tb_wait_online(400), "golden: online");
    tb_run(12);
    for (size_t i = 0; i < sizeof sv / sizeof sv[0]; i++) {
        (void)m207_set_temp((uint8_t)(1 + (i & 1u)), sv[i]);
        tb_run(12);
    }
    (void)m207_tec(1, 1); tb_run(12);
    (void)m207_tec(2, 0); tb_run(12);
    for (uint16_t pid = 0x100; pid <= 0x122; pid++) {
        (void)m207_map_set(pid, (pid & 1u) ? 1.5f : -3.25f);
        tb_run(10);
    }
    (void)m207_map_set(0x126, 7.0f); tb_run(10);
    for (size_t i = 0; i < sizeof raws / sizeof raws[0]; i++) {
        (void)m207_send_raw(raws[i]);
        tb_run(3);
    }
    (void)m207_save_all();
    tb_run(45);

    /* 保存帧格式：只允许 "TC{1|2}:<16 个参数名之一>!" */
    int nsave = 0;
    for (int i = 0; i < TB.ntx; i++) {
        size_t L = strlen(TB.tx[i].f);
        if (L == 0 || TB.tx[i].f[L - 1] != '!') continue;
        nsave++;
        int ok = 0;
        for (int c = 1; c <= 2 && !ok; c++)
            for (size_t k = 0; k < 16 && !ok; k++) {
                snprintf(f, sizeof f, "TC%d:%s!", c, save_names[k]);
                if (strcmp(TB.tx[i].f, f) == 0) ok = 1;
            }
        if (!ok) { printf("  bad save frame: '%s'\n", TB.tx[i].f); g_fail++; }
    }
    /* 保存帧只认格式与条数（M12-F-08 修复后 32 条全发）；黄金样本集合不含保存帧 */
    (void)nsave;
}

static int cmp_str(const void *a, const void *b)
{
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static void test_golden_tx(void)
{
    static const char *fname = "tests/m207_golden_tx.txt";
    tb_init();
    golden_script();

    /* 固件实际发出的帧（去重、不含保存帧）*/
    static char *got[512];
    int ngot = 0;
    for (int i = 0; i < TB.ntx; i++) {
        size_t L = strlen(TB.tx[i].f);
        if (L == 0 || TB.tx[i].f[L - 1] == '!') continue;
        int dup = 0;
        for (int k = 0; k < ngot && !dup; k++) if (!strcmp(got[k], TB.tx[i].f)) dup = 1;
        if (!dup && ngot < 512) { got[ngot] = TB.tx[i].f; ngot++; }
    }
    /* 黄金样本 */
    static char gbuf[128][64];
    static char *want[128];
    int nwant = 0;
    FILE *fp = fopen(fname, "r");
    if (!fp) { printf("FAIL: golden corpus not found: %s\n", fname); g_fail++; return; }
    char line[256];
    while (fgets(line, sizeof line, fp)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] == '\0' || line[0] == '#') continue;
        if (nwant < 128) { snprintf(gbuf[nwant], sizeof gbuf[0], "%s", line); want[nwant] = gbuf[nwant]; nwant++; }
    }
    fclose(fp);
    qsort(got, (size_t)ngot, sizeof got[0], cmp_str);
    qsort(want, (size_t)nwant, sizeof want[0], cmp_str);

    int missing = 0, extra = 0;
    for (int i = 0, k = 0; i < nwant || k < ngot;) {
        if (i < nwant && (k >= ngot || strcmp(want[i], got[k]) < 0)) {
            printf("  MISSING frame (firmware no longer sends): '%s'\n", want[i]); missing++; i++;
        } else if (k < ngot && (i >= nwant || strcmp(want[i], got[k]) > 0)) {
            printf("  EXTRA frame (new wire format): '%s'\n", got[k]); extra++; k++;
        } else { i++; k++; }
    }
    printf("  golden tx: corpus=%d firmware=%d missing=%d extra=%d\n", nwant, ngot, missing, extra);
    TB_CHECK(missing == 0, "golden tx: every golden frame still sent byte-identical");
    TB_CHECK(extra == 0, "golden tx: no new frame format on the wire");
}

/* ---------- 9. 下行队列容量恰为 16（与抽取前的 FreeRTOS 队列一致）---------- */
static void test_queue_capacity(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);                                  /* 复联查询全部发完，队列空 */
    int okc = 0;
    for (int i = 0; i < 16; i++) okc += (m207_map_set((uint16_t)(0x100 + i), 1.0f + (float)i) == 0);
    TB_CHECK(okc == 16, "queue holds 16 verified writes");
    TB_CHECK(m207_map_set(0x110, 2.0f) == -1, "17th write rejected: queue full");
    /* 环形回绕后容量不变 */
    tb_run(40);
    okc = 0;
    for (int i = 0; i < 16; i++) okc += (m207_map_set((uint16_t)(0x100 + i), 1.0f + (float)i) == 0);
    TB_CHECK(okc == 16, "after wrap-around the queue still holds 16");
}

/* ---------- 10. M12-F-10：超长行 / 半行不进解析器，且丢弃计数可见 ---------- */
static void test_rx_line_hygiene(void)
{
    m207_qstats_t st;
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    TB.m.silent = 1;                            /* 不让轮询应答来掺和 */
    tb_run(2);

    /* 超长行（COMPUSHLIST 之类）：截断的前缀不得被解析；紧随其后的正常行完好 */
    char big[160];
    snprintf(big, sizeof big, "PCRS232:COMPUSHLIST=%.3f", 1.5);
    while (strlen(big) < 100) strcat(big, "0");
    tb_rx_inject(big);
    tb_rx_inject("\rTC2:TCACTTEMP=24.3\r");    /* 台阶要小：合理性滤波按距上个样本的 dt 限速 */
    tb_tick();
    memset(&st, 0, sizeof st);
    m207_stats_queue(&st);
    TB_CHECK(st.linedrop == 1, "M12-F-10: over-long line dropped and counted");
    TB_CHECK(m207_last_temp(2) > 24.29f && m207_last_temp(2) < 24.31f,
             "the line right after it is parsed intact");

    /* 半行在空闲窗内接上：仍是一条完整行 */
    tb_rx_inject("TC1:TCACTTEMP=2");           /* 没有 \r */
    tb_run(4);                                  /* 200ms：还在等后续字节 */
    tb_rx_inject("6.0\r");
    tb_tick();
    TB_CHECK(m207_last_temp(1) > 25.99f && m207_last_temp(1) < 26.01f,
             "a line completed within the idle window is accepted (26.0)");

    /* 半行 500ms 没有后续字节（模块复位 / 噪声）：整段丢弃，不与迟到的尾巴拼成一条 */
    tb_rx_inject("TC1:TCACTTEMP=2");
    tb_run(12);                                 /* 600ms */
    memset(&st, 0, sizeof st);
    m207_stats_queue(&st);
    TB_CHECK(st.linedrop == 2, "M12-F-10: partial line idle > 500ms is discarded and counted");
    tb_rx_inject("7.0\r");                      /* 若没丢弃，会拼成 27.0 被采纳 */
    tb_tick();
    TB_CHECK(m207_last_temp(1) > 25.99f && m207_last_temp(1) < 26.01f,
             "M12-F-10: the late tail does not resurrect the discarded line");
}

/* ---------- 11. 温度龄与合理性 dt 用真实毫秒：被饿掉的拍不能让数据显得"新鲜" ----------
 * 服务拍被饿后 vTaskDelayUntil 的追赶拍会被吞掉（M12-F-07），拍号于是落后于真实时间；
 * 按拍号算年龄会低估，按拍号算 dt 会把隔了很久才来的合理样本误判成跳变。 */
static void test_time_base_is_real_ms(void)
{
    tb_init();
    TB_CHECK(tb_wait_online(120), "online");
    tb_run(30);
    TB.m.silent = 1;
    tb_run(2);
    TB.now_ms += 1000u;                         /* 服务任务 1s 一拍都没跑 */
    tb_tick();
    TB_CHECK(m207_temp_age_ms() >= 1000u && m207_temp_age_ms() != 0xFFFFu,
             "temp age follows real time (starved ticks do not make data look fresh)");
    /* 隔了 10s 真实时间才来的下一个样本：25→45 摊在 10s 里是 2°C/s，合理；
     * 按拍数算 dt 只有 0.2s，会被误判成跳变而拒收 */
    TB.now_ms += 10000u;
    tb_rx_inject("TC1:TCACTTEMP=45.0\r");
    tb_tick();
    TB_CHECK(m207_last_temp(1) > 44.99f && m207_last_temp(1) < 45.01f,
             "plausibility dt is real time: a sample after a 10s gap is judged over 10s");
}

int main(void)
{
    test_boot_first_online();
    test_selfloop_nine();
    test_probe_alternates();
    test_sensor_open_debounce();
    test_overtemp_trip();
    test_write_paths();
    test_offline_writes();
    test_golden_tx();
    test_queue_capacity();
    test_rx_line_hygiene();
    test_time_base_is_real_ms();
    return tb_done("test_m207_core");
}
