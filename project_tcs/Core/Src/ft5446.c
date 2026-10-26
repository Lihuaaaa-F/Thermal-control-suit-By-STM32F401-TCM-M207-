/**
 * ft5446.c — FT5446/FT5206 触摸驱动实现（M3，软件 I2C bit-bang）
 *
 * 整体重写为软件 bit-bang，抄官方 LVGL 例程 ctiic.c + ft5206.c 原码：
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
 */
#include "ft5446.h"

/* ====== 纯逻辑（PC 单测）：坐标映射唯一出口（T-13）====== */
#ifdef TCS_UNIT_TEST
#define LCD_WIDTH   1024
#define LCD_HEIGHT  600
#else
#include "ltdc.h"
/* 触摸校准：映射后仿射微调。默认恒等（四点校准留 I5）。*/
static float g_cal_a = 1.0f, g_cal_b = 0.0f, g_cal_c = 1.0f, g_cal_d = 0.0f;
#endif

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

#ifndef TCS_UNIT_TEST
#include "segger_rtt.h"
#include <stdio.h>              /* snprintf/printf：F16 预格式化 + 控制台镜像 */

/* 映射常量一致性编译断言（纯函数段 fallback 与 ltdc.h 真值）*/
_Static_assert(LCD_WIDTH == 1024 && LCD_HEIGHT == 600,
               "ft5446 map fallback dims must match ltdc.h");

/* ====== 软件 I2C 引脚（抄 ctiic.h，SCL=PD12/SDA=PD13）====== */
#define CT_IIC_SCL_PORT   GPIOD
#define CT_IIC_SCL_PIN    GPIO_PIN_12
#define CT_IIC_SDA_PORT   GPIOD
#define CT_IIC_SDA_PIN    GPIO_PIN_13

#define CT_IIC_SCL(x)  HAL_GPIO_WritePin(CT_IIC_SCL_PORT, CT_IIC_SCL_PIN, (x)?GPIO_PIN_SET:GPIO_PIN_RESET)
#define CT_IIC_SDA(x)  HAL_GPIO_WritePin(CT_IIC_SDA_PORT, CT_IIC_SDA_PIN, (x)?GPIO_PIN_SET:GPIO_PIN_RESET)
#define CT_READ_SDA    HAL_GPIO_ReadPin(CT_IIC_SDA_PORT, CT_IIC_SDA_PIN)

/* ====== TP_INT/RST（抄 ft5206.h，INT=PH7/RST=PH6）====== */
#define FT_RST_PORT   GPIOH
#define FT_RST_PIN    GPIO_PIN_6
#define FT_INT_PORT   GPIOH
#define FT_INT_PIN    GPIO_PIN_7
#define FT_RST(x)     HAL_GPIO_WritePin(FT_RST_PORT, FT_RST_PIN, (x)?GPIO_PIN_SET:GPIO_PIN_RESET)

static int g_touch_ok = 0;
static uint16_t g_fw_version;          /* T-11：IC 固件版本（init 锁存）*/
uint16_t ft5446_fw_version(void) { return g_fw_version; }
volatile uint32_t g_ft5446_nak_cnt;    /* M1：读事务地址/寄存器/读相位 NAK 计数 */
volatile uint32_t g_ft5446_read_cnt;   /* M1：读事务总数（NAK 率分母）*/

static void ct_iic_init(void);
static void ct_iic_stop(void);
static void ct_iic_start(void);
static uint8_t ct_iic_wait_ack(void);
static void ct_iic_send_byte(uint8_t data);
static uint8_t ct_iic_read_byte(uint8_t ack);
static uint8_t ft5446_probe(uint8_t wr_cmd);

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

/* ====== 软件 I2C（抄 ctiic.c 原码）====== */

/* 微秒级延迟——抄官方 H743 例程 delay.c 的 SysTick->VAL 递减计数法。
 * 不用 DWT（DWT 在某些 H743 配置下需解锁 LAR，未验证前不可靠）。
 * SysTick 由 HAL_Init 配成 1ms 中断、HCLK 时钟、递减计数。读 VAL 差值计时。
 * g_fac_us = SystemCoreClock/1000000（CPU 核频，H743=480）。*/
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

/* ====== FT5446 寄存器读写（抄 ft5206.c）====== */

static uint8_t ft5446_wr_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
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
}

static void ft5446_rd_reg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    g_ft5446_read_cnt++;
    ct_iic_start();
    ct_iic_send_byte(FT5446_CMD_WR);
    if (ct_iic_wait_ack()) { ct_iic_stop(); g_ft5446_nak_cnt++; return; }
    ct_iic_send_byte(reg);
    if (ct_iic_wait_ack()) { ct_iic_stop(); g_ft5446_nak_cnt++; return; }
    ct_iic_start();
    ct_iic_send_byte(FT5446_CMD_RD);
    if (ct_iic_wait_ack()) { ct_iic_stop(); g_ft5446_nak_cnt++; return; }
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = ct_iic_read_byte(i == (len - 1) ? 0 : 1);
    }
    ct_iic_stop();
}

/* 探测指定写地址是否 ACK（不发数据，只 START+addr+STOP）。0=ACK在线，1=NAK。*/
static uint8_t ft5446_probe(uint8_t wr_cmd)
{
    ct_iic_start();
    ct_iic_send_byte(wr_cmd);
    uint8_t nak = ct_iic_wait_ack();
    ct_iic_stop();
    return nak;
}

/* ====== M3 触摸硬件诊断（2026-07-26 重验）======
 * 区分"软件时序错"vs"硬件层(供电/FFC/IC 死)"。RTT 输出三类证据：
 *   1. SDA 空闲电平：释放后读 SDA。=1 上拉在/总线空闲高（正常）；=0 总线被拉低（短路或 IC 死拉低）
 *   2. SDA 驱动回读：MCU 拉低应读 0、释放应读 1。验证 MCU 引脚→排线→上拉通路
 *   3. 全地址扫描 0x08..0x7E：找任何 ACK 设备（防 IC 实际地址非 0x38）*/
static void ft5446_diagnose(void)
{
    /* 1. SDA/SCL 释放，读空闲电平 */
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

/* ====== 初始化（抄 ft5206.c ft5206_init）====== */
int ft5446_init(void)
{
    GPIO_InitTypeDef gi = {0};
    uint8_t temp[2];
    uint8_t ver_ok = 0;

    __HAL_RCC_GPIOH_CLK_ENABLE();
    delay_us_init();

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

    ct_iic_init();

    /* 复位序列（抄官方 ft5206_init）：RST(0) 20ms → RST(1) → 等 300ms ROM 加载 */
    FT_RST(0);
    HAL_Delay(20);
    FT_RST(1);
    HAL_Delay(300);

    /* 配寄存器（抄 ft5206_init 序列）*/
    temp[0] = 0;
    ft5446_wr_reg(FT5446_DEVIDE_MODE, temp, 1);       /* 工作模式 */
    ft5446_wr_reg(FT5446_ID_G_MODE, temp, 1);         /* 轮询模式（INT 不触发）*/
    temp[0] = 22;
    ft5446_wr_reg(FT5446_ID_G_THGROUP, temp, 1);      /* 触摸有效值 22 */
    temp[0] = 12;
    ft5446_wr_reg(FT5446_ID_G_PERIODACTIVE, temp, 1); /* 激活周期 12 */

    /* 读版本号验证在线（T-11：值锁存供日志头/自检）。
     * H3 加固：SWD 会话期位bang总线偶发 NAK → 版本/触点读带 3 次重试 */
    for (int t = 0; t < 3 && !ver_ok; t++) {
        ft5446_rd_reg(FT5446_ID_G_LIB_VERSION, temp, 2);
        if ((temp[0] == 0x30 && temp[1] == 0x03) || temp[1] == 0x01
            || temp[1] == 0x02 || (temp[0] == 0x00 && temp[1] == 0x00)) {
            ver_ok = 1;
        } else {
            HAL_Delay(20);
        }
    }
    g_fw_version = (uint16_t)((temp[0] << 8) | temp[1]);

    /* 进一步验证：读触点数寄存器 0x02，在线应 ≤ 5 */
    uint8_t fingers = 0xFF;
    for (int t = 0; t < 3; t++) {
        ft5446_rd_reg(FT5446_REG_NUM_FINGER, &fingers, 1);
        if ((fingers & 0x0F) <= 5) { ver_ok = 1; break; }
        HAL_Delay(20);
    }

    SEGGER_RTT_printf(0, "RTT: ft5446 ver=%02x%02x fingers=%02x ver_ok=%d (0=HW offline, skip touch)\n",
                      temp[0], temp[1], fingers, ver_ok);

    /* M3 诊断：硬件离线时跑 SDA 电平 + 驱动回读 + 全地址扫描，
     * 区分软件时序错 vs 硬件层（供电/FFC/IC）。*/
    if (!ver_ok) {
        ft5446_diagnose();
        return -2;   /* 不在线，g_touch_ok 保持 0，主循环跳过触摸 */
    }

    g_touch_ok = 1;
    return 0;
}

int touch_ready(void) { return g_touch_ok; }

/* ====== 配置寄存器回读诊断（M0/M1）：数据通路健康判据 ======
 * 写入→回读一致 = I2C 双向数据通路健康；此时 TD_STATUS 恒 0x0F 应解释为
 * IC 空闲态合法输出（非读数失效）。不一致 = 数据相位坏（ACK 假象）。*/
void ft5446_diag_readback(void)
{
    static const struct { uint8_t reg; uint8_t wr; const char *name; } tab[] = {
        { FT5446_DEVIDE_MODE,       0,  "DEVIDE_MODE" },
        { FT5446_ID_G_MODE,         0,  "ID_G_MODE" },
        { FT5446_ID_G_THGROUP,      22, "THGROUP" },
        { FT5446_ID_G_PERIODACTIVE, 12, "PERIODACTIVE" },
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

/* ====== 读触摸样本（raw→映射→日志；过滤决策在会话机）====== */
volatile uint32_t g_ft5446_badsample_drops;   /* 坏包/越界（会话机外驱动层防御）*/

int ft5446_read(touch_event_t *ev)
{
    ev->touched = 0;
    ev->valid = 0;
    ev->injected = 0;
    ev->contacts = 0;
    ev->touch_id = 0;
    ev->weight = 0;
    ev->area = 0;
    if (!g_touch_ok) return -1;
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
        ft5446_rd_reg(FT5446_REG_NUM_FINGER, &sta, 1);
    }

    uint8_t n = sta & 0x0F;
    if (n == 0) {
        if (s_ftlog_last_n != 0) {
            ftlog_push(HAL_GetTick(), 0, 0, 0, 0, 0, 0);   /* 抬起沿标记 */
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
            ftlog_push(HAL_GetTick(), 0, 0, 0, 0, 0, 0);   /* 视为抬起沿 */
            s_ftlog_last_n = 0;
        }
        ev->valid = 1;   /* 空闲态=合法样本，非坏包 */
        return 0;
    }

    /* 读第1触点 6 字节（0x03 起：X_H X_L Y_H Y_L weight area）*/
    uint8_t buf[6];
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
        ft5446_rd_reg(FT5446_TP1_REG, buf, 6);
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
        ftlog_push(HAL_GetTick(), xr, yr, n, evflag, tid, inject_sample);
        return 0;
    }

    int sx, sy;
    ft5446_map_raw((uint16_t)xr, (uint16_t)yr, &sx, &sy);

    /* M0 raw 日志：触点期间全记录（按下/移动/抬起全序列）*/
    ftlog_push(HAL_GetTick(), xr, yr, n, evflag, tid, inject_sample);
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

#endif /* !TCS_UNIT_TEST */
