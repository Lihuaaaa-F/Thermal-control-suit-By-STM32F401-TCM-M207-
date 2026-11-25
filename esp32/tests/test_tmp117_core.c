/* test_tmp117_core.c — TMP117 采样内核（tmp117_core.c）主机用例
 *
 * S-G e2：M13-F-04（dt 用真实毫秒）、M13-F-08（DEVID 不符不得当体温源）、M13-F-02（写配置、
 * 周期回读校验、转换周期与采样对齐）。寄存器语义取自 TI TMP117 数据手册 SNOSD2D
 * （Table 7-3 寄存器表、Table 7-6 配置寄存器字段、Table 7-7 连续转换模式下的转换周期）。
 *
 * 假器件（文件内实现）记录每一次寄存器读/写；"不在总线上"= 所有事务失败。
 * 标 [红] 的用例在未修的内核（提交 F0：行为保持的抽取）上失败。
 */
#include <stdio.h>
#include <string.h>
#include "tmp117_service.h"
#include "tmp117_core.h"
#include "param_registry.h"
#include "control.h"
#include "post.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* ================= 假器件 ================= */
#define FAKE_REG_TEMP   0x00
#define FAKE_REG_CONFIG 0x03
#define FAKE_REG_DEVID  0x0F

static struct {
    int      present;        /* 0=不在总线上 */
    uint16_t devid;
    uint16_t config;
    int16_t  temp_raw;
    int      cfg_stuck;      /* 1=配置寄存器写不动（器件不认）*/
    /* 访问日志：按时间顺序记录读与写 */
    struct { uint8_t is_wr, reg; uint16_t val; } log[8192];
    int nlog;
} F;

static uint16_t fake_get(uint8_t reg)
{
    if (reg == FAKE_REG_TEMP)   return (uint16_t)F.temp_raw;
    if (reg == FAKE_REG_CONFIG) return F.config;
    if (reg == FAKE_REG_DEVID)  return F.devid;
    return 0xEEEE;
}
static int f_read(void *ctx, uint8_t reg, uint8_t out[2])
{
    uint16_t v;
    (void)ctx;
    if (!F.present) return -1;
    v = fake_get(reg);
    if (F.nlog < (int)(sizeof F.log / sizeof F.log[0])) {
        F.log[F.nlog].is_wr = 0; F.log[F.nlog].reg = reg; F.log[F.nlog].val = v; F.nlog++;
    }
    out[0] = (uint8_t)(v >> 8);
    out[1] = (uint8_t)v;
    return 0;
}
static int f_write(void *ctx, uint8_t reg, uint16_t val)
{
    (void)ctx;
    if (!F.present) return -1;
    if (F.nlog < (int)(sizeof F.log / sizeof F.log[0])) {
        F.log[F.nlog].is_wr = 1; F.log[F.nlog].reg = reg; F.log[F.nlog].val = val; F.nlog++;
    }
    /* 只实现配置寄存器的写；EEPROM 锁存（出厂即锁），别的寄存器写了也无效 */
    if (reg == FAKE_REG_CONFIG && !F.cfg_stuck) F.config = val;
    return 0;
}
static int fake_count(int is_wr, uint8_t reg)
{
    int c = 0;
    for (int i = 0; i < F.nlog; i++)
        if (F.log[i].is_wr == is_wr && F.log[i].reg == reg) c++;
    return c;
}

/* ================= 测试台 ================= */
static tmp117_core_t C;
static uint32_t now_ms;

static int16_t c_to_raw(float c) { return (int16_t)(c / TMP117_LSB_C + (c >= 0.0f ? 0.5f : -0.5f)); }

static void t_reset(int present, float temp_c)
{
    memset(&F, 0, sizeof F);
    F.present = present;
    F.devid = 0x0117;
    F.config = 0x0220;                        /* 手册：出厂复位值 */
    F.temp_raw = c_to_raw(temp_c);
    now_ms = 0;
    control_init(NULL);                       /* 清报警位 */
    g_tcs.body_temp = 0.0f;                   /* 上电初值：没有样本时体温为 0、不就绪 */
    g_tcs.body_sensor_ok = 0;
    post_arm_commit(POST_ARM_UNARMED_ABSENT); /* 默认不布防 */
    tmp117_core_init(&C, &(tmp117_io_t){ .read_reg = f_read, .write_reg = f_write });
}
static void t_tick(void)
{
    now_ms += tmp117_core_period_ms(&C);      /* 任务按内核给的周期排下一拍 */
    tmp117_core_tick(&C, now_ms);
}
static void t_run(int n) { while (n-- > 0) t_tick(); }

/* ================= 表征用例（抽取前后都应通过）================= */

/* C1. 器件不在总线上：1s（10 拍）后掉线沿；未布防只记沿不报警；真值为"不就绪" */
static void test_absent_unarmed(void)
{
    t_reset(0, 36.5f);
    t_run(9);
    CHECK(tmp117_core_ok(&C) == 1, "absent: 9 errors still ok");
    t_tick();
    CHECK(tmp117_core_ok(&C) == 0, "absent: 10th error trips");
    CHECK(C.lost_edges == 1, "absent: one lost edge");
    CHECK(g_tcs.body_sensor_ok == 0, "absent: body_sensor_ok is 0");
    CHECK(g_tcs.body_temp == 0.0f, "absent: body_temp stays 0");
    CHECK(tcs_alarm_active(ALM_BODY_SENSOR) == 0, "absent + unarmed: no alarm");
    CHECK(tmp117_core_period_ms(&C) == 1000u, "absent: cadence drops to 1s");
}

/* C2. 布防后同一故障要报警（H743 同语义）*/
static void test_absent_armed(void)
{
    t_reset(0, 36.5f);
    post_arm_commit(POST_ARM_ARMED_ONLINE);
    t_run(10);
    CHECK(tmp117_core_ok(&C) == 0 && tcs_alarm_active(ALM_BODY_SENSOR) == 1,
          "absent + armed: ALM_BODY_SENSOR raised");
}

/* C3. 正常采样：首拍即有体温、真值就绪；均值窗 8 */
static void test_normal_sampling(void)
{
    t_reset(1, 36.5f);
    t_tick();
    CHECK(g_tcs.body_sensor_ok == 1, "present: body_sensor_ok after first sample");
    CHECK(g_tcs.body_temp > 36.49f && g_tcs.body_temp < 36.51f, "present: body_temp = 36.5");
    CHECK(C.stat_samples == 1, "present: one sample in window");
    for (int i = 0; i < 12; i++) { F.temp_raw = c_to_raw(36.0f + 0.1f * (float)i); t_tick(); }
    /* 窗口 8：最后 8 个样本 36.4..37.1，均值 36.75（量化误差 < 0.01）*/
    CHECK(g_tcs.body_temp > 36.73f && g_tcs.body_temp < 36.77f, "present: 8-sample moving mean");
}

/* C4. 合理性：越界 / 跳变样本拒收并计错（H6.7；首样本也查范围 M06-F-09）*/
static void test_plausibility(void)
{
    t_reset(1, 20.0f);
    t_run(10);
    CHECK(g_tcs.body_sensor_ok == 0 && g_tcs.body_temp == 0.0f, "first sample out of range: never used");
    t_reset(1, 36.5f);
    t_tick();
    F.temp_raw = c_to_raw(40.0f);             /* 一拍跳 3.5°C：>2°C/s */
    t_tick();
    CHECK(g_tcs.body_temp > 36.49f && g_tcs.body_temp < 36.51f, "jump sample dropped");
    CHECK(C.stat_err == 1, "jump sample counted as error");
    F.temp_raw = c_to_raw(36.6f);
    t_tick();
    CHECK(g_tcs.body_temp > 36.54f && g_tcs.body_temp < 36.56f, "small step accepted");
}

/* C5. 失联后回来：3 个成功样本恢复数据流；节拍回到 100ms */
static void test_recover(void)
{
    t_reset(1, 36.5f);
    t_run(3);
    F.present = 0;
    t_run(10);
    CHECK(tmp117_core_ok(&C) == 0, "recover: lost");
    F.present = 1;
    t_run(2);
    CHECK(tmp117_core_ok(&C) == 0, "recover: 2 oks still down");
    t_tick();
    CHECK(tmp117_core_ok(&C) == 1 && C.recover_edges == 1, "recover: 3rd ok recovers");
    CHECK(tmp117_core_period_ms(&C) == 100u, "recover: cadence back to 100ms");
}

/* C6. 配置字比对（手册 Table 7-6 / 7-7）：只比可读写字段，状态位不参与 */
static void test_cfg_word(void)
{
    static volatile uint16_t word = TMP117_CFG_WORD;
    CHECK(word == 0x00A0u, "cfg word: CC, CONV=001(125ms), AVG=01(8)");
    CHECK(tmp117_cfg_matches(TMP117_CFG_WORD) == 1, "cfg: our word matches");
    CHECK(tmp117_cfg_matches(0x0220u) == 0, "cfg: factory reset (CONV=1s) does not match");
    CHECK(tmp117_cfg_matches(0x20A0u) == 1, "cfg: Data_Ready status bit ignored");
    CHECK(tmp117_cfg_matches(0xF0A3u) == 1, "cfg: all status bits + Soft_Reset + bit0 ignored");
    CHECK(tmp117_cfg_matches(0x02A0u) == 0, "cfg: CONV=101 (bit 9) is a config field, not a status bit");
    CHECK(tmp117_cfg_matches(0x04A0u) == 0, "cfg: shutdown mode does not match");
    CHECK(tmp117_cfg_matches(0x00A4u) == 0, "cfg: therm mode does not match");
    CHECK(tmp117_cfg_matches(0x0120u) == 0, "cfg: CONV=000 differs (kept explicit)");
}

#if TCS_SIM_ENABLED
/* C7.（仅注入构建编入）假体温直入均值窗、恒 ok，不产生 I2C 事务；关掉假源后走真 I2C 路径 */
static void test_sim_injection(void)
{
    t_reset(0, 0.0f);                         /* 器件不在总线上 */
    tmp117_core_sim_set(&C, 37.0f);
    t_run(12);
    CHECK(tmp117_core_ok(&C) == 1 && g_tcs.body_sensor_ok == 1, "sim: forced value is always ok");
    CHECK(g_tcs.body_temp > 36.99f && g_tcs.body_temp < 37.01f, "sim: body_temp = forced value");
    CHECK(tmp117_core_sim_active(&C) == 1, "sim: active flag");
    CHECK(F.nlog == 0, "sim: no I2C traffic while forced");
    tmp117_core_sim_disable(&C);
    t_run(10);
    CHECK(tmp117_core_ok(&C) == 0, "sim off: absent device -> lost");
}
#endif



int main(void)
{
    test_absent_unarmed();
    test_absent_armed();
    test_normal_sampling();
    test_plausibility();
    test_recover();
    test_cfg_word();
#if TCS_SIM_ENABLED
    test_sim_injection();
#endif
    printf("test_tmp117_core: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

/* param_registry.c 的目标机桩（主机用例不碰 EEPROM 镜像）*/
int eeprom_ready(void) { return 0; }
int eeprom_read_image(uint8_t *img) { (void)img; return -1; }
