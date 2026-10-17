/**
 * ft5446.c — FT5446/FT5206 触摸驱动实现（M3，软件 I2C bit-bang）
 *
 * 整体重写为软件 bit-bang，抄官方 LVGL 例程 ctiic.c + ft5206.c 原码：
 *   - SCL=PD12 推挽、SDA=PD13 开漏+上拉
 *   - ct_iic_start/stop/wait_ack/ack/nack/send_byte/read_byte（抄原码）
 *   - ft5206_wr_reg/rd_reg/ft5206_init/ft5206_scan（抄原码，FT5446 同协议）
 *   - X 镜像：tp_dev.x = lcddev.width - (buf[0]<<8|buf[1])（抄原码横屏分支）
 *
 * 不依赖 HAL_I2C（用户决策 D1 软件 bit-bang）。
 */
#include "ft5446.h"
#include "ltdc.h"      /* LCD_WIDTH/HEIGHT */
#include "segger_rtt.h"

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

/* 触摸校准：原始坐标 → 屏幕像素。默认恒等（M3b 待人校准）。
 * X 已在读取时做了镜像（横屏），这里再仿射微调。*/
static float g_cal_a = 1.0f, g_cal_b = 0.0f, g_cal_c = 1.0f, g_cal_d = 0.0f;

static void ct_iic_init(void);
static void ct_iic_stop(void);
static void ct_iic_start(void);
static uint8_t ct_iic_wait_ack(void);
static void ct_iic_send_byte(uint8_t data);
static uint8_t ct_iic_read_byte(uint8_t ack);
static uint8_t ft5446_probe(uint8_t wr_cmd);

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
    ct_iic_start();
    ct_iic_send_byte(FT5446_CMD_WR);
    ct_iic_wait_ack();
    ct_iic_send_byte(reg);
    ct_iic_wait_ack();
    ct_iic_start();
    ct_iic_send_byte(FT5446_CMD_RD);
    ct_iic_wait_ack();
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

/* ====== 初始化（抄 ft5206.c ft5206_init）======
 * ⚠️ 硬件状态（2026-07-26）：本板 FT5446U 触摸 IC 经多轮调试 I2C 无应答
 *   （wr_reg 全 NAK / 总线扫描 0 设备 / SCL·SDA·RST·INT 四线 GPIO 翻转均正常）。
 *   引脚映射（PD12=SCL/PD13=SDA/PH6=RST/PH7=INT）经原理图+用户手册+屏规格书
 *   三方核实正确，软件时序照抄官方 ft5206_init。判定：硬件层问题（触摸 IC 供电/
 *   FFC 排线接触/IC 损坏），需人物理检查，不阻塞固件主线。
 *   g_touch_ok 保持 0 → 主循环 touch_ready()=0 跳过触摸轮询，协议/显示/键盘照跑。
 *   人修好硬件后无需改代码，重新上电即自动 g_touch_ok=1。*/
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

    /* 读版本号验证在线（抄 ft5206_init：版本 0x3003/0x0001/0x0002/CST340）*/
    ft5446_rd_reg(FT5446_ID_G_LIB_VERSION, temp, 2);
    if ((temp[0] == 0x30 && temp[1] == 0x03) || temp[1] == 0x01
        || temp[1] == 0x02 || (temp[0] == 0x00 && temp[1] == 0x00)) {
        ver_ok = 1;
    }

    /* 进一步验证：读触点数寄存器 0x02，在线应 ≤ 5 */
    uint8_t fingers = 0xFF;
    ft5446_rd_reg(FT5446_REG_NUM_FINGER, &fingers, 1);
    if ((fingers & 0x0F) <= 5) ver_ok = 1;

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

void ft5446_set_calibration(float a, float b, float c, float d)
{
    g_cal_a = a; g_cal_b = b; g_cal_c = c; g_cal_d = d;
}

/* ====== 读触摸点（抄 ft5206_scan 单点简化）====== */
int ft5446_read(touch_event_t *ev)
{
    ev->touched = 0;
    if (!g_touch_ok) return -1;

    uint8_t sta = 0;
    ft5446_rd_reg(FT5446_REG_NUM_FINGER, &sta, 1);
    uint8_t n = sta & 0x0F;
    if (n == 0 || n > 5) return 0;   /* 无触摸 */

    /* 读第1触点 4 字节（0x03 起）*/
    uint8_t buf[4];
    ft5446_rd_reg(FT5446_TP1_REG, buf, 4);

    /* FT5446 datasheet：
     *   buf[0]=X_H（高4位事件标志，低4位 X[11:8]）
     *   buf[1]=X_L（X[7:0]）
     *   buf[2]=Y_H（高4位触摸ID，低4位 Y[11:8]）
     *   buf[3]=Y_L（Y[7:0]）
     * 横屏 X 镜像（抄 ft5206_scan else 分支）：
     *   x = lcddev.width - (buf[0]<<8|buf[1])
     *   y = buf[2]<<8|buf[3] */
    uint16_t xr = (uint16_t)(((uint16_t)(buf[0] & 0x0F) << 8) | buf[1]);
    uint16_t yr = (uint16_t)(((uint16_t)(buf[2] & 0x0F) << 8) | buf[3]);

    /* 必须 contact 事件（buf[0] 高4位=0x80），否则视为无效 */
    if ((buf[0] & 0xF0) != 0x80) return 0;

    /* X 镜像（横屏，屏排线方向与显示相反）*/
    int sx = LCD_WIDTH - xr;
    int sy = yr;

    /* 仿射校准（M3b 调参，默认恒等）*/
    sx = (int)(g_cal_a * sx + g_cal_b);
    sy = (int)(g_cal_c * sy + g_cal_d);
    if (sx < 0) sx = 0;
    if (sx >= LCD_WIDTH) sx = LCD_WIDTH - 1;
    if (sy < 0) sy = 0;
    if (sy >= LCD_HEIGHT) sy = LCD_HEIGHT - 1;

    ev->touched = 1;
    ev->x = sx;
    ev->y = sy;
    return 0;
}
