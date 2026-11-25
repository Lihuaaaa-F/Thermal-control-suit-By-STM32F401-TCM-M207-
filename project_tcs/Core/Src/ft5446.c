/**
 * ft5446.c — FT5446/FT5206 触摸驱动实现（M3；**W1a 起传输层=ST HAL I2C4 缺省**，
 * 软件 bit-bang 旧实现按册 §2.2 条4 以 FT5446_BITBANG 宏保留一个版本期）。
 * 寄存器序列抄官方 LVGL 例程 ft5206.c 原码；位bang 域抄 ctiic.c：
 *   - SCL=PD12 推挽、SDA=PD13 开漏+上拉
 *   - ct_iic_start/stop/wait_ack/ack/nack/send_byte/read_byte（抄原码）
 *   - ft5206_wr_reg/rd_reg/ft5206_init/ft5206_scan（抄原码，FT5446 同协议）
 *
 * WP-M2（v10.11）：
 *   - 坐标映射恢复 GS 验证版（交换 X/Y 不镜像，2026-07-28 真手指实测），收敛到
 *     唯一纯函数 ft5446_map_raw()；旧"X 镜像"映射为 D1 头号嫌疑已废弃（T-13）。
 *   - 读路径不再做帧级幽灵过滤（H1 跨会话误杀根因），过滤决策移交会话状态机。
 *   - 新增 raw 日志环形缓冲（M0：ghost/真击分布取证 + T-11 IC 版本入日志头）。
 *   - 新增 raw 级注入（TCS_SIM，T-13：封死"注入绕过变换层"盲区）。
 *
 * S-G h1a（2026-11-25，审查 M02-F-01 / M02-F-02 / M02-F-04）：
 *   - 文件分两段：上段纯逻辑（主机单测直接编译），下段硬件（总线动作 + HAL）。
 *   - 读失败就是读失败：ft5446_read() 返回 -1、样本无效。原来读失败被当成"合法无触摸"，
 *     6 字节读失败时还会拿没初始化的栈缓冲解析坐标（幽灵触点）。
 *   - 在线 = 4 个配置寄存器写成功且回读一致（ft5446_probe_online）。版本号只记录，
 *     不再当在线凭据。
 *   - 触摸链路掉线后自动恢复：判定在 touch_session.c，这里由 ft5446_service()
 *     每拍执行一小步，不阻塞主循环。
 *   - 两线被拉死时读/写都不进 HAL（HAL 要等 25ms 超时才返回），直接报失败。
 */
#include "ft5446.h"
#include "tcs_profile.h"   /* TCS_PROFILE：台架钩子只在台架/仿真档编译 */

/* ====== 纯逻辑段（PC 单测可编译）：映射 / 日志环 / 读一拍 / 在线判定 ====== */
#ifdef TCS_UNIT_TEST
#define LCD_WIDTH   1024
#define LCD_HEIGHT  600
#else
#include "ltdc.h"
/* 触摸校准：映射后仿射微调。默认恒等（四点校准留 I5）。*/
static float g_cal_a = 1.0f, g_cal_b = 0.0f, g_cal_c = 1.0f, g_cal_d = 0.0f;
#endif
#include <string.h>

void ft5446_map_raw(uint16_t xr, uint16_t yr, int *sx, int *sy)
{
    /* GS 验证版（2026-07-28 本屏真手指实测）：交换 X/Y，不镜像 */
    int mx = (int)yr;
    int my = (int)xr;
#ifndef TCS_UNIT_TEST
    /* 仿射校准（默认恒等；四点校准留 I5）*/
    mx = (int)(g_cal_a * mx + g_cal_b);
    my = (int)(g_cal_c * my + g_cal_d);
#endif
    if (mx < 0) mx = 0;
    if (mx >= LCD_WIDTH) mx = LCD_WIDTH - 1;
    if (my < 0) my = 0;
    if (my >= LCD_HEIGHT) my = LCD_HEIGHT - 1;
    *sx = mx;
    *sy = my;
}

/* ====== raw 日志环（M0）====== */
static ftlog_ent_t s_ftlog[FTLOG_RING_N];
static uint32_t s_ftlog_head;      /* 下一写入位 */
static uint32_t s_ftlog_total;     /* 累计入环条数（覆盖不减少）*/
static uint8_t  s_ftlog_on;
static uint8_t  s_ftlog_last_n;    /* 上一记录的 n（抬起沿标记用）*/

void ftlog_clear(void)  { s_ftlog_head = 0; s_ftlog_total = 0; s_ftlog_last_n = 0; }
void ftlog_enable(uint8_t on) { s_ftlog_on = on; }
uint32_t ftlog_count(void) { return s_ftlog_total; }
const ftlog_ent_t *ftlog_at(uint32_t idx)
{
    if (idx >= s_ftlog_total) return 0;
    uint32_t base = (s_ftlog_total >= FTLOG_RING_N)
                  ? (s_ftlog_total - FTLOG_RING_N) : 0u;
    return &s_ftlog[(base + idx) % FTLOG_RING_N];
}

static void ftlog_push(uint32_t t_ms, int16_t xr, int16_t yr,
                       uint8_t n, uint8_t ev, uint8_t id, uint8_t src)
{
    if (!s_ftlog_on) return;
    ftlog_ent_t *e = &s_ftlog[s_ftlog_head];
    e->t_ms = t_ms; e->xr = xr; e->yr = yr;
    e->n = n; e->ev = ev; e->id = id; e->src = src;
    s_ftlog_head = (s_ftlog_head + 1u) % FTLOG_RING_N;
    s_ftlog_total++;
}

/* ====== 驱动层计数（只读遥测，三档都有）====== */
volatile uint32_t g_ft5446_nak_cnt;    /* M1：读事务地址/寄存器/读相位 NAK 计数 */
volatile uint32_t g_ft5446_read_cnt;   /* M1：读事务总数（NAK 率分母）*/
volatile uint32_t g_ft5446_buserr_cnt; /* 读事务里 NAK 以外的失败：超时/总线错/两线被拉死 */
volatile uint32_t g_ft5446_badsample_drops;   /* 坏包/越界（会话机外驱动层防御）*/

/* ====== raw 级注入（TCS_SIM，T-13：携带 raw 坐标走完整变换链）====== */
#ifdef TCS_SIM
static uint8_t s_rinj_active;      /* 0=无 1=按下 2=抬起沿（消费一次）*/
static uint16_t s_rinj_xr, s_rinj_yr;

void ft5446_raw_inject(int down, uint16_t xr, uint16_t yr)
{
    if (down) { s_rinj_xr = xr; s_rinj_yr = yr; s_rinj_active = 1; }
    else      { s_rinj_active = 2; }
}
#endif

void ft5446_ev_clear(touch_event_t *ev)
{
    ev->touched = 0;
    ev->valid = 0;
    ev->injected = 0;
    ev->contacts = 0;
    ev->touch_id = 0;
    ev->weight = 0;
    ev->area = 0;
}

/* ====== 读一拍（raw→映射→日志；过滤决策在会话机）======
 * 经回调读寄存器：目标机 = HAL，PC 单测 = 桩。返回值口径见 ft5446.h。*/
int ft5446_read_ops(touch_event_t *ev, uint32_t now_ms, ft5446_rd_fn rd, void *ctx)
{
    ft5446_ev_clear(ev);
    uint8_t sta = 0;
    int16_t xr = 0, yr = 0;
    uint8_t evflag = 0, tid = 0, wt = 0, ar = 0;
    uint8_t inject_sample = 0;

#ifdef TCS_SIM
    if (s_rinj_active == 1u) {
        /* raw 注入：合成首触点 6 字节，走与真 IC 完全相同的解析/映射/日志链 */
        sta = 1;
        xr = (int16_t)s_rinj_xr;
        yr = (int16_t)s_rinj_yr;
        evflag = 0x80;   /* contact 事件 */
        tid = 0;
        inject_sample = 1;
    } else if (s_rinj_active == 2u) {
        s_rinj_active = 0;   /* 抬起沿消费一次：sta=0 走正常无触点路径 */
    } else
#endif
    {
        /* 触点状态读失败：不拿 sta 的旧值去当"无触摸"（M02-F-01 的静默失效点）*/
        if (rd(ctx, FT5446_REG_NUM_FINGER, &sta, 1) != 0) return -1;
    }

    uint8_t n = sta & 0x0F;
    if (n == 0) {
        if (s_ftlog_last_n != 0) {
            ftlog_push(now_ms, 0, 0, 0, 0, 0, 0);   /* 抬起沿标记 */
            s_ftlog_last_n = 0;
        }
        ev->valid = 1;   /* 合法"无触摸"样本——会话机据此结算抬起（M2）*/
        return 0;
    }
    if (n > 5) {
        /* 本屏 FT5446 空闲态实测（2026-10-20 touch diag 回读佐证数据通路健康）：
         * TD_STATUS 恒 0x0F（非法触点数=无有效触点）。按空闲处理——样本有效但
         * 无触摸，会话机静默忽略；不计坏包、不进日志环（否则 100Hz 灌水）。
         * 旧固件同路径静默丢弃=行为一致。*/
        if (s_ftlog_last_n != 0) {
            ftlog_push(now_ms, 0, 0, 0, 0, 0, 0);   /* 视为抬起沿 */
            s_ftlog_last_n = 0;
        }
        ev->valid = 1;   /* 空闲态=合法样本，非坏包 */
        return 0;
    }

    /* 读第1触点 6 字节（0x03 起：X_H X_L Y_H Y_L weight area）*/
    uint8_t buf[6] = {0};
#ifdef TCS_SIM
    if (inject_sample) {
        buf[0] = (uint8_t)((evflag & 0xF0) | ((xr >> 8) & 0x0F));
        buf[1] = (uint8_t)(xr & 0xFF);
        buf[2] = (uint8_t)((tid << 4) | ((yr >> 8) & 0x0F));
        buf[3] = (uint8_t)(yr & 0xFF);
        buf[4] = 0; buf[5] = 0;
    } else
#endif
    {
        /* 6 字节没读回来：不解析（M02-F-02。原来 buf 未初始化，残值高半字节是 0x80
         * 就成一个坐标任意的"按下"）*/
        if (rd(ctx, FT5446_TP1_REG, buf, 6) != 0) return -1;
    }

    /* FT5446 datasheet：
     *   buf[0]=X_H（高4位事件标志，低4位 X[11:8]）
     *   buf[1]=X_L（X[7:0]）
     *   buf[2]=Y_H（高4位触摸ID，低4位 Y[11:8]）
     *   buf[3]=Y_L（Y[7:0]）  buf[4]=weight  buf[5]=area */
    xr = (int16_t)((((uint16_t)(buf[0] & 0x0F)) << 8) | buf[1]);
    yr = (int16_t)((((uint16_t)(buf[2] & 0x0F)) << 8) | buf[3]);
    evflag = (uint8_t)(buf[0] & 0xF0);
    tid = (uint8_t)(buf[2] >> 4);
    wt = buf[4];
    ar = buf[5];

    /* 必须 contact 事件（buf[0] 高4位=0x80），否则视为无效（防御性丢弃）*/
    if (evflag != 0x80) {
        g_ft5446_badsample_drops++;
        ftlog_push(now_ms, xr, yr, n, evflag, tid, inject_sample);
        return 0;
    }

    int sx, sy;
    ft5446_map_raw((uint16_t)xr, (uint16_t)yr, &sx, &sy);

    /* M0 raw 日志：触点期间全记录（按下/移动/抬起全序列）*/
    ftlog_push(now_ms, xr, yr, n, evflag, tid, inject_sample);
    s_ftlog_last_n = n;

    ev->touched = 1;
    ev->valid = 1;
    ev->injected = inject_sample;
    ev->contacts = n;
    ev->touch_id = tid;
    ev->weight = wt;
    ev->area = ar;
    ev->x = sx;
    ev->y = sy;
    return 0;
}

/* ====== 在线判定（纯函数；口径见 ft5446.h）====== */
uint8_t ft5446_probe_online(const ft5446_probe_t *p)
{
    if ((p->wr_ok & 0x0Fu) != 0x0Fu) return 0;      /* 任一配置写失败：不算在线 */
    if ((p->rd_ok & 0x03u) != 0x03u) return 0;      /* 回读本身失败 */
    /* 两个非 0 寄存器都要原样读回：数据线常态低 / 假 ACK 时读回全 0，这里过不去 */
    if (p->thgroup != FT5446_THGROUP_VAL) return 0;
    if (p->period != FT5446_PERIOD_VAL) return 0;
    return 1;      /* 版本号 p->ver 只记录，不设门（旧判定把版本 0x0000 也算在线）*/
}

#ifndef TCS_UNIT_TEST
#include "segger_rtt.h"
#include <stdio.h>              /* snprintf/printf：F16 预格式化 + 控制台镜像 */

/* 映射常量一致性编译断言（纯函数段 fallback 与 ltdc.h 真值）*/
_Static_assert(LCD_WIDTH == 1024 && LCD_HEIGHT == 600,
               "ft5446 map fallback dims must match ltdc.h");

#ifdef FT5446_BITBANG
/* ====== 软件 I2C 引脚（抄 ctiic.h，SCL=PD12/SDA=PD13；仅位bang域）====== */
#define CT_IIC_SCL_PORT   GPIOD
#define CT_IIC_SCL_PIN    GPIO_PIN_12
#define CT_IIC_SDA_PORT   GPIOD
#define CT_IIC_SDA_PIN    GPIO_PIN_13

#define CT_IIC_SCL(x)  HAL_GPIO_WritePin(CT_IIC_SCL_PORT, CT_IIC_SCL_PIN, (x)?GPIO_PIN_SET:GPIO_PIN_RESET)
#define CT_IIC_SDA(x)  HAL_GPIO_WritePin(CT_IIC_SDA_PORT, CT_IIC_SDA_PIN, (x)?GPIO_PIN_SET:GPIO_PIN_RESET)
#define CT_READ_SDA    HAL_GPIO_ReadPin(CT_IIC_SDA_PORT, CT_IIC_SDA_PIN)
#endif /* FT5446_BITBANG：引脚宏 */

/* ====== TP_INT/RST（抄 ft5206.h，INT=PH7/RST=PH6）====== */
#define FT_RST_PORT   GPIOH
#define FT_RST_PIN    GPIO_PIN_6
#define FT_INT_PORT   GPIOH
#define FT_INT_PIN    GPIO_PIN_7
#define FT_RST(x)     HAL_GPIO_WritePin(FT_RST_PORT, FT_RST_PIN, (x)?GPIO_PIN_SET:GPIO_PIN_RESET)

/* ====== W1a（v13.4 §2）：传输层 HAL I2C4 化 ==========
 * 默认走 ST HAL I2C4（PD12=SCL/PD13=SDA，AF4_OD，内核=PCLK4）；
 * 定义 FT5446_BITBANG 则保留旧位bang实现一个版本期（册 §2.2 条 4）。
 * 时序值出处（册 §2.2"官方数值唯一合法来源"）：ST 官方计算代码
 * i2c_timing_utility.c（CubeH7 官方仓 @8ec3113d，NUCLEO-H723ZG I2C 例程自带，
 * 零改动 vendor 于 Core/Src/，出处/SHA256=docs/ref/CubeH7_I2C/UPSTREAM.md），
 * init 期以实际 PCLK4 频率×目标速率（FT5446_I2C_FAST=400k，否则 100k）现算
 * ——比静态常数更强的官方出处：ST 自己的计算器在本工程执行。 */
/* #define FT5446_BITBANG */   /* 一个版本期后删（册 §2.2 条 4）*/
#define FT5446_I2C_TIMEOUT_MS  20u    /* 同 tmp117 Mem_Read 超时口径（W10 #12-13）*/

#ifndef FT5446_BITBANG
/* 档位=真编译开关：缺省 400k；-DFT5446_I2C_100K 切 100k 档（W1d 决策用，
 * 红队 W1a-F6 勘正原注释宏非开关）*/

static I2C_HandleTypeDef hi2c4;

/* ST 官方时序计算器原文链入（聚合包含，W4.2 同款零 CMake 改动路径）。
 * 约束（红队 W1a-F7/F11）：①I2C_GetTiming 有静态累加器，全工程每次启动只能调用
 * 一次（本工程仅 ft5446_i2c4_init 单点调用，且只在首次；运行期恢复沿用算好的值）；
 * ②本 .c 严禁再入 CMake 源列表（否则 I2C_GetTiming 重复定义）。*/
#include "i2c_timing_utility.c"

/* PD12=SCL / PD13=SDA → I2C4 复用（AF4 开漏 + 内部上拉，沿用位bang期配置；
 * 速度 HIGH=400k 档边沿）。清总线时会临时切成普通 GPIO，这里负责切回来。*/
static void ft5446_pins_af(void)
{
    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_GPIOD_CLK_ENABLE();
    gi.Pin = GPIO_PIN_12 | GPIO_PIN_13;
    gi.Mode = GPIO_MODE_AF_OD;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    gi.Alternate = GPIO_AF4_I2C4;
    HAL_GPIO_Init(GPIOD, &gi);
}
#endif /* !FT5446_BITBANG */

/* 两线是否都空闲为高。读事务之前先看一眼：被拉死就别进 HAL——HAL 的 Mem_Read 在
 * 总线 BUSY 时要等 25ms 超时才返回，一拍两次读就把主循环从 10ms 拖到 50ms 以上。*/
static int ft5446_lines_idle(void)
{
#ifdef FT5446_BITBANG
    return (CT_READ_SDA != GPIO_PIN_RESET) ? 1 : 0;
#else
    return (HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_12) == GPIO_PIN_SET &&
            HAL_GPIO_ReadPin(GPIOD, GPIO_PIN_13) == GPIO_PIN_SET) ? 1 : 0;
#endif
}

#ifndef FT5446_BITBANG
/* 清总线用的粗略微秒级延时：480MHz 下每次循环约 4~8 个周期，480 次约 4~8µs。
 * I2C 快速模式从机要求 SCL 高/低各 ≥ 1.3µs，这里留了 3 倍以上余量；偏长只是让
 * 清总线多花几十微秒。只在恢复流程里跑，不占 SysTick/DWT。*/
static void ft5446_dly(void)
{
    for (volatile uint32_t i = 0; i < 480u; i++) { __NOP(); }
}

static uint32_t s_i2c4_target;    /* 首次算时序时的目标速率（日志用）*/
static uint8_t  s_i2c4_logged;    /* "i2c4 up" 日志只打一次 */

/* 外设带到可用态：时钟 + 引脚 + HAL 句柄。可重复执行（运行期恢复也走这里），
 * 时序值取 hi2c4.Init 里首次算好的那个。0=成功。*/
static int ft5446_i2c4_bring_up(void)
{
    __HAL_RCC_I2C4_CLK_ENABLE();
    ft5446_pins_af();
    if (HAL_I2C_Init(&hi2c4) != HAL_OK) return -1;
    if (HAL_I2CEx_ConfigAnalogFilter(&hi2c4, I2C_ANALOGFILTER_ENABLE) != HAL_OK) return -1;
    return 0;
}

/* I2C4 上电：内核时钟 + 引脚 + 官方计算器算时序 + bring_up。可以重复调用
 * （上电失败后运行期恢复会再来），时序只算首次。0=成功。*/
static int ft5446_i2c4_init(void)
{
    /* 内核时钟：I2C4←PCLK4（D3PCLK1=120MHz@本树：480/2(AHB)=240→APB4/2=120；
     * 无 PLL3 冲突——PLL3 已归 LTDC 50MHz）*/
    RCC_PeriphCLKInitTypeDef rp = {0};
    rp.PeriphClockSelection = RCC_PERIPHCLK_I2C4;
    rp.I2c4ClockSelection = RCC_I2C4CLKSOURCE_D3PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&rp) != HAL_OK) return -1;

    __HAL_RCC_GPIOD_CLK_ENABLE();

    if (hi2c4.Instance != I2C4) {      /* 时序只算一次：I2C_GetTiming 内部有静态累加器 */
        uint32_t target =
#ifdef FT5446_I2C_100K
        100000u;
#else
        400000u;
#endif
        s_i2c4_target = target;
        hi2c4.Instance = I2C4;
        hi2c4.Init.Timing = I2C_GetTiming(HAL_RCCEx_GetD3PCLK1Freq(), target);
        hi2c4.Init.OwnAddress1 = 0;
        hi2c4.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
        hi2c4.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
        hi2c4.Init.OwnAddress2 = 0;
        hi2c4.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
        hi2c4.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
        hi2c4.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    }
    if (ft5446_i2c4_bring_up() != 0) return -1;

    if (!s_i2c4_logged) {              /* 日志只在首次起来时打一次 */
        s_i2c4_logged = 1;
        SEGGER_RTT_printf(0, "RTT: i2c4 up pclk4=%uHz target=%uHz timing=0x%08x\n",
                          (unsigned)HAL_RCCEx_GetD3PCLK1Freq(),
                          (unsigned)s_i2c4_target, (unsigned)hi2c4.Init.Timing);
    }
    return 0;
}
#endif /* !FT5446_BITBANG */

/* ====== FT5446 寄存器读写（抄 ft5206.c；W1a 起双路径）====== */

static uint8_t ft5446_wr_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
#ifdef FT5446_BITBANG
    ct_iic_start();
    ct_iic_send_byte(FT5446_CMD_WR);
    if (ct_iic_wait_ack()) { ct_iic_stop(); return 1; }
    ct_iic_send_byte(reg);
    if (ct_iic_wait_ack()) { ct_iic_stop(); return 1; }
    for (uint8_t i = 0; i < len; i++) {
        ct_iic_send_byte(buf[i]);
        if (ct_iic_wait_ack()) { ct_iic_stop(); return 1; }
    }
    ct_iic_stop();
    return 0;
#else   /* W1a：HAL I2C4 寄存器写（官方 Mem_Write 模式，册 §2.1）*/
    if (!ft5446_lines_idle()) return 1;
    return (HAL_I2C_Mem_Write(&hi2c4, FT5446_CMD_WR, reg,
                              I2C_MEMADD_SIZE_8BIT, buf, len,
                              FT5446_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : 1;
#endif
}

/* 0=成功；非 0=失败（且不写 buf）。失败后调用方不得使用 buf 的内容。*/
static int ft5446_rd_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    g_ft5446_read_cnt++;
#ifdef FT5446_BITBANG
    ct_iic_start();
    ct_iic_send_byte(FT5446_CMD_WR);
    if (ct_iic_wait_ack()) { ct_iic_stop(); g_ft5446_nak_cnt++; return -1; }
    ct_iic_send_byte(reg);
    if (ct_iic_wait_ack()) { ct_iic_stop(); g_ft5446_nak_cnt++; return -1; }
    ct_iic_start();
    ct_iic_send_byte(FT5446_CMD_RD);
    if (ct_iic_wait_ack()) { ct_iic_stop(); g_ft5446_nak_cnt++; return -1; }
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = ct_iic_read_byte(i == (len - 1) ? 0 : 1);
    }
    ct_iic_stop();
    return 0;
#else   /* W1a：HAL Mem_Read（写 reg→Sr→读 N 字节与位bang序列逐位等价，册 §2.1）；
         * NAK 计数改由 HAL 返回值累加（g_ft5446_nak_cnt=400k 决策数据源，保留）*/
    if (!ft5446_lines_idle()) { g_ft5446_buserr_cnt++; return -1; }
    if (HAL_I2C_Mem_Read(&hi2c4, FT5446_CMD_WR, reg,
                         I2C_MEMADD_SIZE_8BIT, buf, len,
                         FT5446_I2C_TIMEOUT_MS) != HAL_OK) {
        /* W1a-F5：仅真 NAK(AF) 计 nak_cnt（W1d 0.5% 阈值口径）；超时/总线错另计 */
        if (hi2c4.ErrorCode & HAL_I2C_ERROR_AF) g_ft5446_nak_cnt++;
        else                                   g_ft5446_buserr_cnt++;
        return -1;
    }
    return 0;
#endif
}

/* 探测指定写地址是否 ACK（不发数据，只 START+addr+STOP）。0=ACK在线，1=NAK。*/
static uint8_t ft5446_probe(uint8_t wr_cmd)
{
#ifdef FT5446_BITBANG
    ct_iic_start();
    ct_iic_send_byte(wr_cmd);
    uint8_t nak = ct_iic_wait_ack();
    ct_iic_stop();
    return nak;
#else   /* HAL：官方 IsDeviceReady——DevAddress 语义与其他 HAL I2C API 相同，
         * 要求 8-bit 左移形式（红队 W1a-F1 坐实：曾误传 wr_cmd>>1 致总线寻址减半）*/
    if (!ft5446_lines_idle()) return 1;
    return (HAL_I2C_IsDeviceReady(&hi2c4, (uint16_t)wr_cmd, 1,
                                  FT5446_I2C_TIMEOUT_MS) == HAL_OK) ? 0 : 1;
#endif
}

/* ====== 链路状态（判定在 touch_session.c；这里是驱动里唯一的实例）====== */
static tp_link_t s_tp_link = { TP_LINK_ONLINE, 0, 0, 0, 0, 0, 0, 0, 0 };   /* ft5446_init 重新初始化 */
static uint8_t  s_act_ok;          /* 上一步动作的执行结果，下一拍交给 ts_tp_link_step */
static uint16_t g_fw_version;      /* T-11：IC 固件版本（最近一次回读核对时锁存）*/
uint16_t ft5446_fw_version(void) { return g_fw_version; }

#if TCS_PROFILE >= 1
volatile uint8_t g_tp_force_recover;   /* 台架钩子：写 1 → 下一拍强制走一遍恢复流程 */
#endif

static int tp_online(void) { return (s_tp_link.state == (uint8_t)TP_LINK_ONLINE) ? 1 : 0; }

/* ====== M3 触摸硬件诊断（2026-07-26 重验）======
 * 区分"软件时序错"vs"硬件层(供电/FFC/IC 死)"。RTT 输出三类证据：
 *   1. SDA 空闲电平：释放后读 SDA。=1 上拉在/总线空闲高（正常）；=0 总线被拉低（短路或 IC 死拉低）
 *   2. SDA 驱动回读：MCU 拉低应读 0、释放应读 1。验证 MCU 引脚→排线→上拉通路
 *   3. 全地址扫描 0x08..0x7E：找任何 ACK 设备（防 IC 实际地址非 0x38）*/
static void ft5446_diagnose(void)
{
#ifdef FT5446_BITBANG
    /* 1. SDA/SCL 释放，读空闲电平（位bang GPIO 直驱专属诊断；HAL 态引脚归 AF）*/
    CT_IIC_SDA(1);
    CT_IIC_SCL(1);
    delay_us(10);
    uint8_t sda_idle = CT_READ_SDA;
    SEGGER_RTT_printf(0, "RTT: tp_diag sda_idle=%d (1=pullup ok, 0=held low)\n", sda_idle);

    /* 2. SDA 驱动回读 */
    CT_IIC_SDA(0); delay_us(10);
    uint8_t sda_low = CT_READ_SDA;
    CT_IIC_SDA(1); delay_us(10);
    uint8_t sda_high = CT_READ_SDA;
    SEGGER_RTT_printf(0, "RTT: tp_diag sda_drive low=%d high=%d (expect 0,1)\n", sda_low, sda_high);
#else
    /* HAL 态：SDA 直驱诊断不适用（PD12/13 归 AF4），以 IsDeviceReady 扫描代偿 */
    SEGGER_RTT_printf(0, "RTT: tp_diag (HAL I2C4 mode: SDA direct-drive N/A, AF-owned pins)\n");
#endif

    /* 3. 全地址扫描 */
    uint8_t found = 0;
    for (uint16_t addr = 0x08; addr <= 0x7E; addr++) {
        uint8_t wr = (uint8_t)(addr << 1);
        if (ft5446_probe(wr) == 0) {
            SEGGER_RTT_printf(0, "RTT: tp_diag ACK at 0x%02x (wr 0x%02x)\n", addr, wr);
            found++;
        }
    }
    SEGGER_RTT_printf(0, "RTT: tp_diag scan done, %d device(s)\n", found);
}

/* ====== 恢复流程里的总线动作（每拍最多执行其中一个，见 ft5446_service）====== */

/* 清总线（I2C 规范的 bus-clear）：SCL 打最多 9 个脉冲把卡在数据态的从机送完一个字节，
 * 再发 STOP。两脚暂时脱离 I2C4 复用，完事切回。
 * 返回 1=清完两线都回高（总线空闲，可以往下走）；0=还有线被拉死（短路/器件死拉），
 * 这种情况不去碰 I2C 外设，退避后再试。耗时约 0.1ms。*/
static int ft5446_bus_clear(void)
{
#ifdef FT5446_BITBANG
    /* 位bang 域：SCL 本来就是 GPIO 推挽。打 9 个脉冲再 STOP，然后看 SDA 是否回高。*/
    for (int i = 0; i < 9; i++) { CT_IIC_SCL(0); ct_iic_delay(); CT_IIC_SCL(1); ct_iic_delay(); }
    ct_iic_stop();
    return ft5446_lines_idle();
#else
    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_GPIOD_CLK_ENABLE();
    /* 先把输出锁存置高再切模式：切过去的瞬间两线就是"释放"，不会先拉低一下 */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12 | GPIO_PIN_13, GPIO_PIN_SET);
    gi.Pin = GPIO_PIN_12 | GPIO_PIN_13;
    gi.Mode = GPIO_MODE_OUTPUT_OD;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOD, &gi);
    ft5446_dly();

    for (int i = 0; i < 9; i++) {
        if (ft5446_lines_idle()) break;            /* SDA 已回高：够了 */
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_RESET);
        ft5446_dly();
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);
        ft5446_dly();
    }
    /* STOP：SCL 高时 SDA 由低到高，把还停在数据态的从机带回空闲 */
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_RESET);
    ft5446_dly();
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_12, GPIO_PIN_SET);
    ft5446_dly();
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_13, GPIO_PIN_SET);
    ft5446_dly();

    int idle = ft5446_lines_idle();
    ft5446_pins_af();
    return idle;
#endif
}

/* I2C4 外设整体复位重配。HAL 的 DeInit 只关外设、清句柄状态；I2C 在 BUSY 态被关掉后
 * 光靠再调一次 Init 不一定回得到空闲，所以再过一遍 RCC 复位。1=成功。*/
static int ft5446_periph_reinit(void)
{
#ifdef FT5446_BITBANG
    ct_iic_init();
    return 1;
#else
    if (hi2c4.Instance == I2C4) (void)HAL_I2C_DeInit(&hi2c4);
    __HAL_RCC_I2C4_FORCE_RESET();
    __HAL_RCC_I2C4_RELEASE_RESET();
    return (ft5446_i2c4_init() == 0) ? 1 : 0;
#endif
}

/* 重写 4 个配置寄存器（抄 ft5206_init 序列）。任一写失败就停，1=全部成功。
 * 从机不应答时每次写很快返回；第一个失败就停，最坏耗时只有一次超时。*/
static int ft5446_cfg_write(void)
{
    uint8_t v = 0;
    if (ft5446_wr_reg(FT5446_DEVIDE_MODE, &v, 1) != 0) return 0;        /* 工作模式 */
    if (ft5446_wr_reg(FT5446_ID_G_MODE, &v, 1) != 0) return 0;          /* 轮询模式（INT 不触发）*/
    v = FT5446_THGROUP_VAL;
    if (ft5446_wr_reg(FT5446_ID_G_THGROUP, &v, 1) != 0) return 0;       /* 触摸有效值 22 */
    v = FT5446_PERIOD_VAL;
    if (ft5446_wr_reg(FT5446_ID_G_PERIODACTIVE, &v, 1) != 0) return 0;  /* 激活周期 12 */
    return 1;
}

/* 回读核对：配置寄存器读得回写入值才算在线（ft5446_probe_online）；顺带读版本号。
 * wr_ok = 写配置那一步的结果。tries 次内过即返回 1；每次之间停 gap_ms。*/
static int ft5446_cfg_verify(int tries, uint32_t gap_ms, uint8_t wr_ok, uint16_t *ver_out)
{
    uint8_t v[2] = {0, 0};
    for (int t = 0; t < tries; t++) {
        ft5446_probe_t pr;
        memset(&pr, 0, sizeof pr);
        pr.wr_ok = wr_ok;
        if (ft5446_rd_reg(FT5446_ID_G_THGROUP, &pr.thgroup, 1) == 0)      pr.rd_ok |= 1u << 0;
        if (ft5446_rd_reg(FT5446_ID_G_PERIODACTIVE, &pr.period, 1) == 0) pr.rd_ok |= 1u << 1;
        if (ft5446_rd_reg(FT5446_ID_G_LIB_VERSION, v, 2) == 0) {
            pr.ver_ok = 1;
            pr.ver[0] = v[0]; pr.ver[1] = v[1];
            if (ver_out) *ver_out = (uint16_t)((v[0] << 8) | v[1]);
        }
        if (ft5446_probe_online(&pr)) return 1;
        if (t + 1 < tries) HAL_Delay(gap_ms);
    }
    return 0;
}

/* ====== 初始化（抄 ft5206.c ft5206_init）====== */
int ft5446_init(void)
{
    GPIO_InitTypeDef gi = {0};
    uint8_t v[2] = {0, 0};

    __HAL_RCC_GPIOH_CLK_ENABLE();
#ifdef FT5446_BITBANG
    delay_us_init();          /* 位bang 域专属（红队 W1a-F8）*/
#endif

    /* RST=PH6 推挽输出 + 上拉 */
    gi.Pin = FT_RST_PIN;
    gi.Mode = GPIO_MODE_OUTPUT_PP;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FT_RST_PORT, &gi);

    /* INT=PH7 输入 + 上拉（轮询模式）*/
    gi.Pin = FT_INT_PIN;
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(FT_INT_PORT, &gi);

#ifdef FT5446_BITBANG
    ct_iic_init();
#else
    if (ft5446_i2c4_init() != 0) {
        SEGGER_RTT_printf(0, "RTT: i2c4 init FAIL (clock/pin/handle)\n");
        g_fw_version = 0;
        ts_tp_link_init(&s_tp_link, 0, HAL_GetTick());
        return -3;
    }
#endif

    /* 复位序列（抄官方 ft5206_init）：RST(0) 20ms → RST(1) → 等 300ms ROM 加载
     * （W1g 议题：300ms 富余性实测留待板上，序列不动）*/
    FT_RST(0);
    HAL_Delay(20);
    FT_RST(1);
    HAL_Delay(300);

    /* 配寄存器 + 回读核对。和原来一样带 3 次重试（间隔 20ms）：上电时 IC 刚出复位，
     * 偶发一次不应答不该直接判离线。 */
    int wr_ok = 0, online = 0;
    for (int t = 0; t < 3 && !online; t++) {
        wr_ok = ft5446_cfg_write();                 /* 写返回值逐个判（M02-F-04）*/
        if (wr_ok) online = ft5446_cfg_verify(1, 0u, 0x0Fu, &g_fw_version);
        if (!online) HAL_Delay(20);
    }
    v[0] = (uint8_t)(g_fw_version >> 8);
    v[1] = (uint8_t)g_fw_version;

    SEGGER_RTT_printf(0, "RTT: ft5446 ver=%02x%02x cfgwr=%d ver_ok=%d (0=HW offline, auto-recover)\n",
                      v[0], v[1], wr_ok, online);

    if (!online) {
        /* M3 诊断：硬件离线时跑 SDA 电平 + 驱动回读 + 全地址扫描，
         * 区分软件时序错 vs 硬件层（供电/FFC/IC）。*/
        ft5446_diagnose();
        /* 不在线：先按离线起步，主循环里 ft5446_service() 接着试 */
        ts_tp_link_init(&s_tp_link, 0, HAL_GetTick());
        return wr_ok ? -2 : -4;
    }

    ts_tp_link_init(&s_tp_link, 1, HAL_GetTick());
    return 0;
}

int touch_ready(void)
{
    return tp_online();
}

void ft5446_link_status(tp_link_t *out)
{
    if (out) *out = s_tp_link;
}

/* ====== 掉线恢复：主循环每拍调一次，每拍最多做一小步 ======
 * 步与步之间的延时（IC 复位 20ms、ROM 加载 300ms）由 ts_tp_link_step 靠"等时间到"
 * 实现，延时期间这里第一行就返回——主循环节拍（也就是两次急停判定的间隔）不变。
 * 真正碰总线的步：清总线约 0.1ms、外设重配 <0.1ms、写配置/回读各 3~4 次事务
 * （每次约 0.1ms，从机不应答时更快）。两线被拉死时所有事务都不进 HAL（见 rd/wr_reg），
 * 所以卡死的总线上这一拍和在线时没有区别。*/
void ft5446_service(void)
{
    uint32_t now = HAL_GetTick();
    int was_online = tp_online();

#if TCS_PROFILE >= 1
    if (g_tp_force_recover) {
        g_tp_force_recover = 0;
        if (was_online) {
            ts_tp_link_force_offline(&s_tp_link, now);
            SEGGER_RTT_WriteString("[BENCH] touch link forced offline -> exercising recovery\n");
        }
    }
#endif

    tp_link_act_t act = ts_tp_link_step(&s_tp_link, s_act_ok, now);
    s_act_ok = 0;
    switch (act) {
    case TP_ACT_BUS_CLEAR:   s_act_ok = (uint8_t)ft5446_bus_clear(); break;
    case TP_ACT_PERIPH_INIT: s_act_ok = (uint8_t)ft5446_periph_reinit(); break;
    case TP_ACT_RST_LOW:     FT_RST(0); s_act_ok = 1; break;
    case TP_ACT_RST_HIGH:    FT_RST(1); s_act_ok = 1; break;
    case TP_ACT_CFG:         s_act_ok = (uint8_t)ft5446_cfg_write(); break;
    case TP_ACT_VERIFY:      s_act_ok = (uint8_t)ft5446_cfg_verify(1, 0u, 0x0Fu, &g_fw_version); break;
    default: break;
    }

    if (!was_online && tp_online()) {
        char b[96];
        snprintf(b, sizeof b, "[SYSTEM] touch link back online: tries=%u offline=%ums recoveries=%u\n",
                 (unsigned)s_tp_link.recover_tries, (unsigned)s_tp_link.offline_last_ms,
                 (unsigned)s_tp_link.recoveries);
        SEGGER_RTT_WriteString(b);
    }
}

/* ====== 配置寄存器回读诊断（M0/M1）：数据通路健康判据 ======
 * 写入→回读一致 = I2C 双向数据通路健康；此时 TD_STATUS 恒 0x0F 应解释为
 * IC 空闲态合法输出（非读数失效）。不一致 = 数据相位坏（ACK 假象）。*/
void ft5446_diag_readback(void)
{
    static const struct { uint8_t reg; uint8_t wr; const char *name; } tab[] = {
        { FT5446_DEVIDE_MODE,       0,  "DEVIDE_MODE" },
        { FT5446_ID_G_MODE,         0,  "ID_G_MODE" },
        { FT5446_ID_G_THGROUP,      FT5446_THGROUP_VAL, "THGROUP" },
        { FT5446_ID_G_PERIODACTIVE, FT5446_PERIOD_VAL,  "PERIODACTIVE" },
    };
    uint8_t v;
    for (unsigned i = 0; i < sizeof(tab) / sizeof(tab[0]); i++) {
        uint32_t r0 = g_ft5446_read_cnt, n0 = g_ft5446_nak_cnt;
        v = 0xFF;
        ft5446_rd_reg(tab[i].reg, &v, 1);
        /* F16：同 post.c，%lu 预格式化（自研 printf 无 'l' 修饰符 case）*/
        char db[96];
        snprintf(db, sizeof db,
                 "RTT: tp_diag %s reg=0x%02x wrote=%u read=%u %s (nak +%lu/%lu)\n",
                 tab[i].name, tab[i].reg, tab[i].wr, v,
                 (v == tab[i].wr) ? "OK" : "MISMATCH",
                 (unsigned long)(g_ft5446_nak_cnt - n0),
                 (unsigned long)(g_ft5446_read_cnt - r0));
        SEGGER_RTT_WriteString(db);
        /* 同步一份到控制台（mb_console 只读 RTT 环）*/
        printf("tp_diag %s wrote=%u read=%u %s\n", tab[i].name, tab[i].wr, v,
               (v == tab[i].wr) ? "OK" : "MISMATCH");
    }
}

void ft5446_set_calibration(float a, float b, float c, float d)
{
    g_cal_a = a; g_cal_b = b; g_cal_c = c; g_cal_d = d;
}

/* ====== 位bang 域（抄 ctiic.c 原码）====== */

/* 微秒级延迟——抄官方 H743 例程 delay.c 的 SysTick->VAL 递减计数法。
 * 不用 DWT（DWT 在某些 H743 配置下需解锁 LAR，未验证前不可靠）。
 * SysTick 由 HAL_Init 配成 1ms 中断、HCLK 时钟、递减计数。读 VAL 差值计时。
 * g_fac_us = SystemCoreClock/1000000（CPU 核频，H743=480）。*/
#ifdef FT5446_BITBANG   /* 位bang 域：HAL 态 delay_us 系列全未用（红队 W1a-F8）*/
static uint32_t g_fac_us = 480;   /* delay_init 前 HAL 还没改 SystemCoreClock，默认 480 */
static void delay_us_init(void)
{
    g_fac_us = SystemCoreClock / 1000000;   /* 正式值，SystemClock_Config 后调 */
}
static void delay_us(uint32_t nus)
{
    uint32_t ticks = nus * g_fac_us;
    uint32_t told = SysTick->VAL;
    uint32_t tcnt = 0;
    uint32_t reload = SysTick->LOAD;
    while (1) {
        uint32_t tnow = SysTick->VAL;
        if (tnow != told) {
            if (tnow < told) tcnt += told - tnow;
            else             tcnt += reload - tnow + told;
            told = tnow;
            if (tcnt >= ticks) break;
        }
    }
}

static void ct_iic_delay(void)
{
    /* I2C 半周期。官方例程 delay_us(2)≈250kHz。*/
    delay_us(2);
}

static void ct_iic_init(void)
{
    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* SCL：推挽输出 + 上拉 */
    gi.Pin = CT_IIC_SCL_PIN;
    gi.Mode = GPIO_MODE_OUTPUT_PP;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(CT_IIC_SCL_PORT, &gi);

    /* SDA：开漏输出 + 上拉（开漏可读外部信号）*/
    gi.Pin = CT_IIC_SDA_PIN;
    gi.Mode = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(CT_IIC_SDA_PORT, &gi);

    ct_iic_stop();   /* 停止，防外部设备意外 */
}

static void ct_iic_start(void)
{
    CT_IIC_SDA(1);
    CT_IIC_SCL(1);
    ct_iic_delay();
    CT_IIC_SDA(0);   /* SCL 高时 SDA 高→低 = 起始 */
    ct_iic_delay();
    CT_IIC_SCL(0);
    ct_iic_delay();
}

static void ct_iic_stop(void)
{
    CT_IIC_SDA(0);
    ct_iic_delay();
    CT_IIC_SCL(1);
    ct_iic_delay();
    CT_IIC_SDA(1);   /* SCL 高时 SDA 低→高 = 停止 */
    ct_iic_delay();
}

static uint8_t ct_iic_wait_ack(void)
{
    uint8_t waittime = 0;
    CT_IIC_SDA(1);   /* 主机释放 SDA */
    ct_iic_delay();
    CT_IIC_SCL(1);   /* 从机可发 ACK */
    ct_iic_delay();
    while (CT_READ_SDA) {
        waittime++;
        if (waittime > 250) { ct_iic_stop(); return 1; }
        ct_iic_delay();
    }
    CT_IIC_SCL(0);
    ct_iic_delay();
    return 0;
}

static void ct_iic_send_byte(uint8_t data)
{
    for (uint8_t t = 0; t < 8; t++) {
        CT_IIC_SDA((data & 0x80) >> 7);   /* 高位先发 */
        ct_iic_delay();
        CT_IIC_SCL(1);
        ct_iic_delay();
        CT_IIC_SCL(0);
        data <<= 1;
    }
    CT_IIC_SDA(1);   /* 发送完释放 SDA */
}

static uint8_t ct_iic_read_byte(uint8_t ack)
{
    uint8_t receive = 0;
    for (uint8_t i = 0; i < 8; i++) {
        receive <<= 1;
        CT_IIC_SCL(1);
        ct_iic_delay();
        if (CT_READ_SDA) receive++;
        CT_IIC_SCL(0);
        ct_iic_delay();
    }
    if (!ack) {
        /* NACK */
        CT_IIC_SDA(1);
        ct_iic_delay();
        CT_IIC_SCL(1);
        ct_iic_delay();
        CT_IIC_SCL(0);
        ct_iic_delay();
    } else {
        /* ACK */
        CT_IIC_SDA(0);
        ct_iic_delay();
        CT_IIC_SCL(1);
        ct_iic_delay();
        CT_IIC_SCL(0);
        ct_iic_delay();
        CT_IIC_SDA(1);
        ct_iic_delay();
    }
    return receive;
}
#endif /* FT5446_BITBANG：位bang 实现域 */

/* ====== 读触摸样本（每拍由屏端急停兜底和 LVGL 读回调调用）====== */
static int ft5446_bus_rd(void *ctx, uint8_t reg, uint8_t *buf, uint8_t len)
{
    (void)ctx;
    return ft5446_rd_reg(reg, buf, len);
}

int ft5446_read(touch_event_t *ev)
{
    /* 离线 / 恢复中：不碰总线（恢复由 ft5446_service() 分步做）。没有样本。*/
    if (!tp_online()) {
        ft5446_ev_clear(ev);
        return -1;
    }
    int rc = ft5446_read_ops(ev, HAL_GetTick(), ft5446_bus_rd, 0);

    /* 链路健康记账：连续失败到阈值 → 判离线。此后本函数第一行就返回，
     * 主循环节拍不再被总线拖慢；急停屏入口失效这件事也从"无人知晓"变成可查询。*/
    if (ts_tp_link_poll(&s_tp_link, rc == 0, HAL_GetTick())) {
        char b[96];
        snprintf(b, sizeof b, "[WARN] touch link offline: %u polls failed in a row -> recovering\n",
                 (unsigned)s_tp_link.streak);
        SEGGER_RTT_WriteString(b);
    }
    return rc;
}

#endif /* !TCS_UNIT_TEST */
