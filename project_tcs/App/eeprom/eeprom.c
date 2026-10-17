/**
 * eeprom.c — AT24C02 参数存储 v2 物理层（GS eeprom.c 复用+扩展布局，§3.3）
 *
 * 软件 bit-bang I2C 抄官方"实验21"myiic.c 原码（GS 工程已验证）：SCL=PH4 推挽、
 * SDA=PH5 开漏+上拉；iic_start/stop/wait_ack/send_byte/read_byte 原样。
 * delay_us 抄 ft5446.c 同款 SysTick->VAL 递减计数法（独立一份防跨文件耦合）。
 *
 * v2：整片镜像缓存 s_img[256]；commit 只写与上次差异的 8B 对齐页（含 CRC 页）。
 */
#include "eeprom.h"
#include "stm32h7xx_hal.h"
#include "proto.h"        /* proto_crc8 */
#include "segger_rtt.h"
#include <string.h>

/* ====== 软件 I2C 引脚（SCL=PH4/SDA=PH5）====== */
#define IIC_SCL_PORT   GPIOH
#define IIC_SCL_PIN    GPIO_PIN_4
#define IIC_SDA_PORT   GPIOH
#define IIC_SDA_PIN    GPIO_PIN_5

#define IIC_SCL(x)  HAL_GPIO_WritePin(IIC_SCL_PORT, IIC_SCL_PIN, (x)?GPIO_PIN_SET:GPIO_PIN_RESET)
#define IIC_SDA(x)  HAL_GPIO_WritePin(IIC_SDA_PORT, IIC_SDA_PIN, (x)?GPIO_PIN_SET:GPIO_PIN_RESET)
#define IIC_READ_SDA  HAL_GPIO_ReadPin(IIC_SDA_PORT, IIC_SDA_PIN)

#define AT24C02_DEV_WR  0xA0
#define AT24C02_DEV_RD  0xA1

static int g_eeprom_ok = 0;
static uint8_t s_img[EE_IMAGE_SIZE];      /* 上次提交内容（差异页计算基准）*/
static uint8_t s_img_valid = 0;

/* ====== 微秒延迟（SysTick->VAL 递减计数法）====== */
static uint32_t g_fac_us = 480;
static void delay_us_init(void) { g_fac_us = SystemCoreClock / 1000000U; }
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
static void iic_delay(void) { delay_us(2); }

/* ====== 软件 I2C（抄 myiic.c 原码）====== */
static void iic_init(void)
{
    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_GPIOH_CLK_ENABLE();

    gi.Pin = IIC_SCL_PIN;
    gi.Mode = GPIO_MODE_OUTPUT_PP;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(IIC_SCL_PORT, &gi);

    gi.Pin = IIC_SDA_PIN;
    gi.Mode = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(IIC_SDA_PORT, &gi);

    IIC_SDA(1);
    IIC_SCL(1);
}

static void iic_start(void)
{
    IIC_SDA(1);
    IIC_SCL(1);
    iic_delay();
    IIC_SDA(0);
    iic_delay();
    IIC_SCL(0);
    iic_delay();
}

static void iic_stop(void)
{
    IIC_SDA(0);
    iic_delay();
    IIC_SCL(1);
    iic_delay();
    IIC_SDA(1);
    iic_delay();
}

static uint8_t iic_wait_ack(void)
{
    uint8_t waittime = 0;
    IIC_SDA(1);
    iic_delay();
    IIC_SCL(1);
    iic_delay();
    while (IIC_READ_SDA) {
        waittime++;
        if (waittime > 250) { iic_stop(); return 1; }
        iic_delay();
    }
    IIC_SCL(0);
    iic_delay();
    return 0;
}

static void iic_send_byte(uint8_t data)
{
    for (uint8_t t = 0; t < 8; t++) {
        IIC_SDA((data & 0x80) >> 7);
        iic_delay();
        IIC_SCL(1);
        iic_delay();
        IIC_SCL(0);
        data <<= 1;
    }
    IIC_SDA(1);
}

static uint8_t iic_read_byte(uint8_t ack)
{
    uint8_t receive = 0;
    for (uint8_t i = 0; i < 8; i++) {
        receive <<= 1;
        IIC_SCL(1);
        iic_delay();
        if (IIC_READ_SDA) receive++;
        IIC_SCL(0);
        iic_delay();
    }
    if (!ack) {
        IIC_SDA(1);
        iic_delay();
        IIC_SCL(1);
        iic_delay();
        IIC_SCL(0);
        iic_delay();
    } else {
        IIC_SDA(0);
        iic_delay();
        IIC_SCL(1);
        iic_delay();
        IIC_SCL(0);
        iic_delay();
        IIC_SDA(1);
        iic_delay();
    }
    return receive;
}

/* ====== AT24C02 单字节/页读写 ====== */
static uint8_t at24_read_one(uint8_t addr)
{
    uint8_t temp = 0;
    iic_start();
    iic_send_byte(AT24C02_DEV_WR);
    if (iic_wait_ack()) { iic_stop(); return 0xFF; }
    iic_send_byte(addr);
    if (iic_wait_ack()) { iic_stop(); return 0xFF; }
    iic_start();
    iic_send_byte(AT24C02_DEV_RD);
    if (iic_wait_ack()) { iic_stop(); return 0xFF; }
    temp = iic_read_byte(0);
    iic_stop();
    return temp;
}

/* 页写：addr 必须 8B 对齐（AT24C02 页=8B），一次 8B + tWR 5ms */
static int at24_write_page_aligned(uint8_t addr, const uint8_t *b8)
{
    if (addr & 0x07) return -1;
    iic_start();
    iic_send_byte(AT24C02_DEV_WR);
    if (iic_wait_ack()) { iic_stop(); return -1; }
    iic_send_byte(addr);
    if (iic_wait_ack()) { iic_stop(); return -1; }
    for (uint8_t i = 0; i < 8; i++) {
        iic_send_byte(b8[i]);
        if (iic_wait_ack()) { iic_stop(); return -1; }
    }
    iic_stop();
    HAL_Delay(5);                    /* tWR 页写周期 */
    return 0;
}

int eeprom_init(void)
{
    delay_us_init();
    iic_init();
    HAL_Delay(2);

    /* 在线探测：发写地址看 ACK（不写数据）*/
    iic_start();
    iic_send_byte(AT24C02_DEV_WR);
    uint8_t nak = iic_wait_ack();
    iic_stop();
    if (nak) {
        SEGGER_RTT_WriteString("RTT: eeprom OFFLINE (NAK on probe)\n");
        g_eeprom_ok = 0;
        return -1;
    }
    g_eeprom_ok = 1;
    /* 开机把整片读进基准镜像（≈120ms，一次性，换 commit 精确差异页写）*/
    s_img_valid = (eeprom_read_image(s_img) == 0);
    SEGGER_RTT_printf(0, "RTT: eeprom online magic=0x%02x\n", s_img[0x00]);
    return 0;
}

int eeprom_ready(void) { return g_eeprom_ok; }

int eeprom_read_image(uint8_t img[EE_IMAGE_SIZE])
{
    if (!g_eeprom_ok) return -1;
    for (int i = 0; i < EE_IMAGE_SIZE; i++) img[i] = at24_read_one((uint8_t)i);
    return 0;
}

int eeprom_commit_image(const uint8_t img[EE_IMAGE_SIZE])
{
    if (!g_eeprom_ok) return -1;
    /* 与基准镜像逐页比对，只写差异页（init 时已缓存整片；未缓存则全写兜底）*/
    const uint8_t *base = s_img;
    for (int page = 0; page < EE_IMAGE_SIZE / 8; page++) {
        int addr = page * 8;
        int diff = (!s_img_valid) || (memcmp(&img[addr], &base[addr], 8) != 0);
        if (diff) {
            if (at24_write_page_aligned((uint8_t)addr, &img[addr]) != 0) return -1;
        }
    }
    memcpy(s_img, img, EE_IMAGE_SIZE);
    s_img_valid = 1;
    return 0;
}

/* ====== 写读自测（0xF6，写后恢复原值保 CRC）====== */
int eeprom_selftest(void)
{
    if (!g_eeprom_ok) return -1;
    uint8_t orig = at24_read_one(0xF6);
    uint8_t pat = 0x55;
    /* 单字节写经整页读改写（页写助手要求 8B 对齐整页）*/
    uint8_t page[8];
    for (int i = 0; i < 8; i++) page[i] = at24_read_one((uint8_t)(0xF0 + i));
    page[6] = pat;                          /* 0xF6 */
    if (at24_write_page_aligned(0xF0, page) != 0) return -1;
    if (at24_read_one(0xF6) != pat) return -1;
    pat = 0xAA;
    page[6] = pat;
    if (at24_write_page_aligned(0xF0, page) != 0) return -1;
    if (at24_read_one(0xF6) != pat) return -1;
    /* 恢复原值（0xF6 在 CRC 覆盖区 0x00..0xF7 内，必须还原）*/
    page[6] = orig;
    if (at24_write_page_aligned(0xF0, page) != 0) return -1;
    SEGGER_RTT_WriteString("RTT: eeprom selftest OK (0xF6 wr/rd/restore)\n");
    return 0;
}
