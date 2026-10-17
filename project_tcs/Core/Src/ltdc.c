/**
 * ltdc.c — LTDC + 外部 SDRAM 显存驱动实现（M2）
 *
 * 所有数值经 4+ agent 从官方例程（实验12 LTDC LCD RGB 屏）原文核实，照抄不推算：
 *   - SDRAM 时序：CAS2/刷新839/列9行13/ReadPipeDelay=1/ModeReg 0x0220（§2.2，抄 sdram.c）
 *   - SDRAM 引脚：FMC 全 AF12（GPIOC PIN0 / GPIOD 7pin / GPIOE 11pin / GPIOF 11pin /
 *     GPIOG 7pin / GPIOH 2pin，抄 HAL_SDRAM_MspInit 原文）
 *   - LTDC 时序：HSync=19/VSync=2/AccHBP=159/AccVBP=22/AccActiveW=1183/AccActiveH=621/
 *     TotalW=1343/TotalH=633（vfp=12，抄 ltdc.c 0x7016 分支）
 *   - LTDC 引脚：RGB 全 AF14（PA8=AF13），PCLK=PG7/HSYNC=PI10/VSYNC=PI9/DE=PF10，
 *     背光 PB5 推挽，复位 PD11（抄 HAL_LTDC_MspInit 原文）
 *   - PLL3：M25/N300/R6→PLL3R=50M 给 LTDC PCLK（抄 ltdc_clk_set，在 ltdc_init 开头配）
 *   - MPU：SDRAM 0xC0000000 区 Cacheable+Bufferable + SCB->CACR|=1<<2 write-through（§2.8，抄 mpu.c+sys.c）
 *   - DMA2D R2M：gfx_clear/gfx_fill_rect 抄 ltdc_fill 寄存器直写（§0C.2）
 *
 * 屏 = 1024×600 RGB565 纯 RGB 直驱（HX8282，ID=0x7016），不需 SPI/I2C 初始化命令。
 */
#include "ltdc.h"
#include "segger_rtt.h"
#include <string.h>

/* ====== 全局句柄 ====== */
static SDRAM_HandleTypeDef hsdram;
static LTDC_HandleTypeDef  hltdc;
static FMC_SDRAM_CommandTypeDef sdcmd;

/* 显存是否可用（ltdc_init 成功且 SDRAM 自检通过才置 1）。
 * 为 0 时所有 gfx_* 直接返回，避免访问 0xC0000000 触发 BusFault。*/
static int g_lcd_ok = 0;

/* ====== 内置 8×16 ASCII 字库（ASCII 0x20-0x7E）======
 * 16 行 × 8 位，每字节一行（bit7=最左像素）。仅含可见 ASCII。*/
static const uint8_t font8x16[][16] = {
#include "font8x16.inc"   /* 字库数据单独文件生成 */
};

/* ===================================================================== */
/*                    §2.8 MPU + D-Cache write-through                   */
/* ===================================================================== */
/* 抄官方例程 mpu.c mpu_set_protection + sys.c sys_cache_enable。
 * SDRAM 0xC0000000 区设 Cacheable+Bufferable（region6，32MB），
 * 再 SCB_EnableDCache + SCB->CACR|=1<<2 强制 write-through（防显存脏数据花屏）。
 * write-through 下 CPU 写 back 立即穿透 SDRAM，LTDC 立即读到 → 不花屏。*/
static void mpu_dcache_init(void)
{
    MPU_Region_InitTypeDef mpu = {0};

    /* 关 MPU 再配（例程写法）*/
    HAL_MPU_Disable();

    /* SDRAM 区 0xC0000000，32MB，region6，Cacheable+Bufferable（抄 mpu_memory_protection 第6段）*/
    mpu.Enable            = MPU_REGION_ENABLE;
    mpu.Number            = MPU_REGION_NUMBER6;
    mpu.BaseAddress       = 0xC0000000U;
    mpu.Size              = MPU_REGION_SIZE_32MB;
    mpu.SubRegionDisable  = 0x00;
    mpu.TypeExtField      = MPU_TEX_LEVEL0;
    mpu.AccessPermission  = MPU_REGION_FULL_ACCESS;
    mpu.DisableExec       = MPU_INSTRUCTION_ACCESS_ENABLE;
    mpu.IsShareable       = MPU_ACCESS_NOT_SHAREABLE;
    mpu.IsCacheable       = MPU_ACCESS_CACHEABLE;
    mpu.IsBufferable      = MPU_ACCESS_BUFFERABLE;
    HAL_MPU_ConfigRegion(&mpu);

    HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

    /* 使能 D-Cache + 强制 write-through（抄 sys_cache_enable）*/
    SCB_EnableICache();
    SCB_EnableDCache();
    SCB->CACR |= 1U << 2;   /* 强制 D-Cache 写透，防显存脏数据 */
}

/* ===================================================================== */
/*                    §4.7 PLL3 → LTDC PCLK=50MHz                       */
/* ===================================================================== */
/* 抄官方例程 ltdc.c ltdc_clk_set（M25/N300/P2/Q2/R6 → PLL3R=50M）。
 * H743 经 HAL_RCCEx_PeriphCLKConfig(RCC_PERIPHCLK_LTDC) 内部自动接 PLL3R 给 LTDC。*/
static void ltdc_clk_set(void)
{
    RCC_PeriphCLKInitTypeDef p = {0};
    p.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    p.PLL3.PLL3M = 25;
    p.PLL3.PLL3N = 300;
    p.PLL3.PLL3P = 2;
    p.PLL3.PLL3Q = 2;
    p.PLL3.PLL3R = 6;       /* VCO=25/25*300=300M, /6=50M 给 LTDC PCLK */
    HAL_RCCEx_PeriphCLKConfig(&p);
}

/* ===================================================================== */
/*                         外部 SDRAM（FMC）初始化                       */
/* ===================================================================== */
/* 时序+命令序列照抄官方例程 sdram.c（§2.2 原文）：
 *   CAS2/列9行13/ReadPipeDelay=1/ModeReg 0x0220/刷新839/7字段时序 2/8/7/7/2/2/2 */
static int sdram_init(void)
{
    FMC_SDRAM_TimingTypeDef Timing = {0};

    hsdram.Instance = FMC_SDRAM_DEVICE;
    hsdram.Init.SDBank             = FMC_SDRAM_BANK1;
    hsdram.Init.ColumnBitsNumber   = FMC_SDRAM_COLUMN_BITS_NUM_9;    /* 列地址 9bit */
    hsdram.Init.RowBitsNumber      = FMC_SDRAM_ROW_BITS_NUM_13;      /* 行地址 13bit */
    hsdram.Init.MemoryDataWidth    = FMC_SDRAM_MEM_BUS_WIDTH_16;     /* 16bit 数据线 */
    hsdram.Init.InternalBankNumber = FMC_SDRAM_INTERN_BANKS_NUM_4;   /* 4 内部 Bank */
    hsdram.Init.CASLatency         = FMC_SDRAM_CAS_LATENCY_2;        /* CAS=2 */
    hsdram.Init.WriteProtection    = FMC_SDRAM_WRITE_PROTECTION_DISABLE;
    hsdram.Init.SDClockPeriod      = FMC_SDRAM_CLOCK_PERIOD_2;       /* SDCLK=fmc_ker_ck/2=110M */
    hsdram.Init.ReadBurst          = FMC_SDRAM_RBURST_ENABLE;
    hsdram.Init.ReadPipeDelay      = FMC_SDRAM_RPIPE_DELAY_1;        /* ReadPipeDelay=1 */

    /* 7 字段时序（抄例程 sdram.c）*/
    Timing.LoadToActiveDelay    = 2;
    Timing.ExitSelfRefreshDelay = 8;
    Timing.SelfRefreshTime      = 7;
    Timing.RowCycleDelay        = 7;
    Timing.WriteRecoveryTime    = 2;
    Timing.RPDelay              = 2;
    Timing.RCDDelay             = 2;

    if (HAL_SDRAM_Init(&hsdram, &Timing) != HAL_OK) return -1;

    /* SDRAM 上电序列（抄 sdram_initialization_sequence）：
     *   CLK_ENABLE(等500us) → PALL → AutoRefresh×8 → LOAD_MODE(ModeReg=0x0220) */
    HAL_Delay(1);   /* ≥200us，给 1ms 裕量 */

    sdcmd.CommandMode            = FMC_SDRAM_CMD_CLK_ENABLE;
    sdcmd.CommandTarget          = FMC_SDRAM_CMD_TARGET_BANK1;
    sdcmd.AutoRefreshNumber      = 1;
    sdcmd.ModeRegisterDefinition = 0;
    if (HAL_SDRAM_SendCommand(&hsdram, &sdcmd, 100) != HAL_OK) return -1;
    HAL_Delay(1);   /* 等 ≥500us（例程 delay_us(500)）*/

    sdcmd.CommandMode = FMC_SDRAM_CMD_PALL;
    if (HAL_SDRAM_SendCommand(&hsdram, &sdcmd, 100) != HAL_OK) return -1;

    sdcmd.CommandMode       = FMC_SDRAM_CMD_AUTOREFRESH_MODE;
    sdcmd.AutoRefreshNumber = 8;
    if (HAL_SDRAM_SendCommand(&hsdram, &sdcmd, 100) != HAL_OK) return -1;

    /* ModeRegister=0x0220 = BURST_LENGTH_1 | SEQUENTIAL | CAS_LATENCY_2 | STANDARD | WRITEBURST_SINGLE */
    sdcmd.CommandMode            = FMC_SDRAM_CMD_LOAD_MODE;
    sdcmd.AutoRefreshNumber      = 1;
    sdcmd.ModeRegisterDefinition = 0x0220;
    if (HAL_SDRAM_SendCommand(&hsdram, &sdcmd, 100) != HAL_OK) return -1;

    /* 刷新率计数器=839（64ms/8192行，SDCLK=110M）*/
    HAL_SDRAM_ProgramRefreshRate(&hsdram, 839);

    return 0;
}

int sdram_selftest(void)
{
    volatile uint16_t *p = (volatile uint16_t *)SDRAM_BASE_ADDR;
    /* 写 0xAA00+i 到前 16 地址再读回 */
    for (int i = 0; i < 16; i++) p[i] = (uint16_t)(0xAA00 + i);
    for (int i = 0; i < 16; i++) {
        if (p[i] != (uint16_t)(0xAA00 + i)) return 0;
    }
    /* 写递增模式到大范围验证地址线 */
    for (uint32_t i = 0; i < 0x100000; i += 0x1000) p[i] = (uint16_t)(i & 0xFFFF);
    for (uint32_t i = 0; i < 0x100000; i += 0x1000) {
        if (p[i] != (uint16_t)(i & 0xFFFF)) return 0;
    }
    return 1;
}

/* ===================================================================== */
/*                            LTDC 初始化                                */
/* ===================================================================== */
/* 时序照抄官方例程 ltdc.c 0x7016 分支（§2.3 原文，vfp=12）：
 *   HSync=19/VSync=2/AccHBP=159/AccVBP=22/AccActiveW=1183/AccActiveH=621/
 *   TotalW=1343/TotalH=633，极性全 AL+IPC */
static int ltdc_hw_init(void)
{
    LTDC_LayerCfgTypeDef layer = {0};

    hltdc.Instance = LTDC;
    /* 极性：HSYNC/VSYNC/DE=AL（低有效），PCPolarity=IPC（例程 0x7016 分支）*/
    hltdc.Init.HSPolarity       = LTDC_HSPOLARITY_AL;
    hltdc.Init.VSPolarity       = LTDC_VSPOLARITY_AL;
    hltdc.Init.DEPolarity       = LTDC_DEPOLARITY_AL;
    hltdc.Init.PCPolarity       = LTDC_PCPOLARITY_IPC;
    /* 8 个时序字段（抄例程，已含 -1）*/
    hltdc.Init.HorizontalSync   = 20 - 1;        /* =19 */
    hltdc.Init.VerticalSync     = 3 - 1;         /* =2 */
    hltdc.Init.AccumulatedHBP   = 20 + 140 - 1;  /* =159 */
    hltdc.Init.AccumulatedVBP   = 3 + 20 - 1;    /* =22 */
    hltdc.Init.AccumulatedActiveW = 20 + 140 + LCD_WIDTH - 1;   /* =1183 */
    hltdc.Init.AccumulatedActiveH = 3 + 20 + LCD_HEIGHT - 1;    /* =621 (vfp=12) */
    hltdc.Init.TotalWidth       = 20 + 140 + LCD_WIDTH + 160 - 1;  /* =1343 */
    hltdc.Init.TotalHeigh       = 3 + 20 + LCD_HEIGHT + 12 - 1;    /* =633 (vfp=12) */
    hltdc.Init.Backcolor.Red    = 0;
    hltdc.Init.Backcolor.Green  = 0;
    hltdc.Init.Backcolor.Blue   = 0;
    if (HAL_LTDC_Init(&hltdc) != HAL_OK) return -1;

    /* Layer1：RGB565，显存 front buffer，全屏窗（抄例程 ltdc_layer_parameter_config）*/
    layer.WindowX0        = 0;
    layer.WindowY0        = 0;
    layer.WindowX1        = LCD_WIDTH;
    layer.WindowY1        = LCD_HEIGHT;
    layer.PixelFormat     = LTDC_PIXEL_FORMAT_RGB565;
    layer.Alpha           = 255;
    layer.Alpha0          = 0;
    layer.BlendingFactor1 = LTDC_BLENDING_FACTOR1_PAxCA;
    layer.BlendingFactor2 = LTDC_BLENDING_FACTOR2_PAxCA;
    layer.FBStartAdress   = (uint32_t)LCD_FRONT_BUF;
    layer.ImageWidth      = LCD_WIDTH;
    layer.ImageHeight     = LCD_HEIGHT;
    layer.Backcolor.Red   = 0;
    layer.Backcolor.Green = 0;
    layer.Backcolor.Blue  = 0;
    if (HAL_LTDC_ConfigLayer(&hltdc, &layer, LTDC_LAYER_1) != HAL_OK) return -1;

    return 0;
}

int ltdc_init(void)
{
    /* 顺序：MPU+D-Cache → PLL3 → SDRAM → LTDC → 复位序列 → 背光 → 清屏
     * （与官方例程 main.c 一致，SDRAM 必须在 LTDC 前，帧缓冲在 SDRAM）*/
    mpu_dcache_init();
    SEGGER_RTT_WriteString("RTT: mpu+d-cache write-through ok\n");

    ltdc_clk_set();   /* PLL3 → LTDC PCLK=50M */
    SEGGER_RTT_WriteString("RTT: pll3 ltdc pclk=50M ok\n");

    if (sdram_init() != 0) {
        SEGGER_RTT_WriteString("RTT: sdram_init FAIL\n");
        return -1;
    }
    int st = sdram_selftest();
    SEGGER_RTT_printf(0, "RTT: sdram selftest=%d\n", st);
    if (!st) return -2;   /* SDRAM 不通，不点屏，协议层照跑 */

    if (ltdc_hw_init() != 0) {
        SEGGER_RTT_WriteString("RTT: ltdc_hw_init FAIL\n");
        return -3;
    }

    /* 屏复位序列（抄例程：RST 高10ms→低50ms→高200ms）。
     * PD11 在 HAL_LTDC_MspInit 配成推挽输出。*/
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);
    HAL_Delay(10);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_RESET);
    HAL_Delay(50);
    HAL_GPIO_WritePin(GPIOD, GPIO_PIN_11, GPIO_PIN_SET);
    HAL_Delay(200);

    /* 开背光：PB5 拉高（抄例程 LTDC_BL(1)，纯 GPIO 推挽非 PWM）*/
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_SET);

    /* M2 背光诊断（2026-07-26）：运行时读回 GPIOB 时钟/MODER/ODR + AHB4ENR，
     * 确认 PB5 真被配成推挽输出且拉高。若寄存器值对但背光仍不亮 → 100% 硬件层
     * （LCD1 pin1-2 VCC5V 缺失 / 排线接触 / 屏故障）。*/
    SEGGER_RTT_printf(0, "RTT: bl_diag AHB4ENR=%08x GPIOB_MODER=%08x ODR=%08x\n",
                      RCC->AHB4ENR, GPIOB->MODER, GPIOB->ODR);
    SEGGER_RTT_printf(0, "RTT: bl_diag PB5: MODER[11:10]=%lu ODR_bit5=%lu (expect 1,1)\n",
                      (GPIOB->MODER >> 10) & 3U, (GPIOB->ODR >> 5) & 1U);

    g_lcd_ok = 1;
    gfx_clear(COLOR_BLACK);
    SEGGER_RTT_printf(0, "RTT: ltdc init rv=0 g_lcd_ok=%d\n", g_lcd_ok);
    return 0;
}

int lcd_ready(void) { return g_lcd_ok; }

/* ===================================================================== */
/*                          HAL MSP（引脚+时钟）                          */
/* ===================================================================== */

/* §2.2 SDRAM FMC 引脚全 AF12（抄官方例程 sdram.c HAL_SDRAM_MspInit 原文）*/
void HAL_SDRAM_MspInit(SDRAM_HandleTypeDef *h)
{
    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_FMC_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    gi.Mode = GPIO_MODE_AF_PP;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_HIGH;
    gi.Alternate = GPIO_AF12_FMC;

    /* PC0 = SDNWE */
    gi.Pin = GPIO_PIN_0;
    HAL_GPIO_Init(GPIOC, &gi);

    /* PD0,1,8,9,10,14,15 = D2/D3/D13/D14/D15/D0/D1 */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_8 | GPIO_PIN_9
           | GPIO_PIN_10 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOD, &gi);

    /* PE0,1,7,8,9,10,11,12,13,14,15 = NBL0/NBL1/D4-D15 */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_7 | GPIO_PIN_8 | GPIO_PIN_9
           | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13
           | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOE, &gi);

    /* PF0,1,2,3,4,5,11,12,13,14,15 = A0-5/SDNRAS/A6-9 */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4
           | GPIO_PIN_5 | GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13
           | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOF, &gi);

    /* PG0,1,2,4,5,8,15 = A10-12/BA0/BA1/SDCLK/SDNCAS */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4
           | GPIO_PIN_5 | GPIO_PIN_8 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOG, &gi);

    /* PH2,3 = SDCKE0/SDNE0 */
    gi.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    HAL_GPIO_Init(GPIOH, &gi);

    (void)h;
}

/* §2.3 LTDC RGB 引脚全 AF14（PA8=AF13）+ PCLK/HSYNC/VSYNC/DE + 背光 PB5 + 复位 PD11
 * 抄官方例程 ltdc.c HAL_LTDC_MspInit 原文 */
void HAL_LTDC_MspInit(LTDC_HandleTypeDef *h)
{
    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_LTDC_CLK_ENABLE();
    __HAL_RCC_DMA2D_CLK_ENABLE();

    /* —— 控制信号 + 背光 + 复位（先配成 GPIO）—— */
    __HAL_RCC_GPIOB_CLK_ENABLE();   /* 背光 PB5 */
    __HAL_RCC_GPIOD_CLK_ENABLE();   /* 复位 PD11 */
    __HAL_RCC_GPIOF_CLK_ENABLE();   /* DE PF10 */
    __HAL_RCC_GPIOG_CLK_ENABLE();   /* PCLK PG7 */
    __HAL_RCC_GPIOI_CLK_ENABLE();   /* VSYNC PI9 / HSYNC PI10 */

    /* 背光 PB5：推挽输出（纯 GPIO，非 PWM）*/
    gi.Pin = GPIO_PIN_5;
    gi.Mode = GPIO_MODE_OUTPUT_PP;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_HIGH;
    HAL_GPIO_Init(GPIOB, &gi);

    /* 复位 PD11：推挽输出 */
    gi.Pin = GPIO_PIN_11;
    HAL_GPIO_Init(GPIOD, &gi);

    /* DE/VSYNC/HSYNC/CLK：AF14 推挽复用 */
    gi.Mode = GPIO_MODE_AF_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_HIGH;
    gi.Alternate = GPIO_AF14_LTDC;

    gi.Pin = GPIO_PIN_10;   /* DE = PF10 */
    HAL_GPIO_Init(GPIOF, &gi);
    gi.Pin = GPIO_PIN_7;    /* PCLK = PG7 */
    HAL_GPIO_Init(GPIOG, &gi);
    gi.Pin = GPIO_PIN_9 | GPIO_PIN_10;   /* VSYNC=PI9 HSYNC=PI10 */
    HAL_GPIO_Init(GPIOI, &gi);

    /* —— RGB 数据线 —— */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOH_CLK_ENABLE();

    /* PA8 = B3，AF13（唯一例外）*/
    gi.Alternate = GPIO_AF13_LTDC;
    gi.Pin = GPIO_PIN_8;
    HAL_GPIO_Init(GPIOA, &gi);

    /* 其余 RGB 全 AF14 */
    gi.Alternate = GPIO_AF14_LTDC;

    /* PA2 = R1 */
    gi.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOA, &gi);

    /* PD6 = B2 */
    gi.Pin = GPIO_PIN_6;
    HAL_GPIO_Init(GPIOD, &gi);

    /* PE5,6 = G0/G1 */
    gi.Pin = GPIO_PIN_5 | GPIO_PIN_6;
    HAL_GPIO_Init(GPIOE, &gi);

    /* PG6,12,13,14 = R7/B1/R0/B0 */
    gi.Pin = GPIO_PIN_6 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14;
    HAL_GPIO_Init(GPIOG, &gi);

    /* PH8,9,10,11,12,13,14,15 = R2-R6/G3-G5 */
    gi.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11
           | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15;
    HAL_GPIO_Init(GPIOH, &gi);

    /* PI0,1,2,4,5,6,7 = G6/G7/B4-B7 */
    gi.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_4
           | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOI, &gi);

    (void)h;
}

/* ===================================================================== */
/*                §0C.2 DMA2D R2M（抄官方例程 ltdc_fill）                */
/* ===================================================================== */
/* 寄存器直写，每次用前先开 DMA2D 时钟（例程写法）。RGB565。*/
static void dma2d_fill_rect(uint16_t *dst, int w, int h, int pitch, uint16_t color)
{
    /* TCS 加固（2026-10-18 运行时实证）：TC 忙等曾观测到不死等 → 主循环 >2s 卡死
     * → IWDG 复位循环。改为：空矩形防御 + TC 超时（约 5ms@HCLK240M）→ 软复位
     * DMA2D 恢复总线，缺一块填充优于整机复位。其余逻辑原样。*/
    __HAL_RCC_DMA2D_CLK_ENABLE();
    if (w <= 0 || h <= 0) return;
    DMA2D->CR = 0;
    DMA2D->CR = DMA2D_R2M;                    /* Register-to-Memory */
    DMA2D->OPFCCR = LTDC_PIXEL_FORMAT_RGB565; /* 0x02 */
    DMA2D->OOR = (uint32_t)(pitch - w);       /* 行间偏移（像素）*/
    DMA2D->OMAR = (uint32_t)dst;
    DMA2D->NLR = (uint32_t)w | ((uint32_t)h << 16);   /* 宽 | 高<<16 */
    DMA2D->OCOLR = color;
    DMA2D->IFCR |= DMA2D_FLAG_TC;             /* 先清旧 TC 再启动，防上一帧残留 */
    DMA2D->CR |= DMA2D_CR_START;
    uint32_t guard = 2000000u;                /* ~5ms @480M 循环上限 */
    while ((DMA2D->ISR & DMA2D_FLAG_TC) == 0) {
        if (--guard == 0) {
            __HAL_RCC_DMA2D_FORCE_RESET();
            __HAL_RCC_DMA2D_RELEASE_RESET();
            __HAL_RCC_DMA2D_CLK_ENABLE();
            SEGGER_RTT_WriteString("[WARN] dma2d TC timeout -> soft reset");
            return;
        }
    }
    DMA2D->IFCR |= DMA2D_FLAG_TC;             /* 清标志（例程写法）*/
}

/* ===================================================================== */
/*                          基础绘图                                      */
/* ===================================================================== */
void gfx_clear(uint16_t color)
{
    if (!g_lcd_ok) return;
    dma2d_fill_rect(LCD_BACK_BUF, LCD_WIDTH, LCD_HEIGHT, LCD_WIDTH, color);
}

void gfx_draw_pixel(int x, int y, uint16_t color)
{
    if (!g_lcd_ok) return;
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) return;
    LCD_BACK_BUF[y * LCD_WIDTH + x] = color;
}

void gfx_draw_hline(int x0, int x1, int y, uint16_t color)
{
    if (!g_lcd_ok) return;
    if (y < 0 || y >= LCD_HEIGHT) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (x0 < 0) x0 = 0;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    /* DMA2D R2M 填水平线（1 行）*/
    dma2d_fill_rect(&LCD_BACK_BUF[y * LCD_WIDTH + x0], x1 - x0 + 1, 1, LCD_WIDTH, color);
}

void gfx_draw_vline(int x, int y0, int y1, uint16_t color)
{
    if (!g_lcd_ok) return;
    if (x < 0 || x >= LCD_WIDTH) return;
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (y0 < 0) y0 = 0;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;
    /* 垂直线：宽1，高(y1-y0+1)，pitch=LCD_WIDTH（DMA2D 行间跳）*/
    dma2d_fill_rect(&LCD_BACK_BUF[y0 * LCD_WIDTH + x], 1, y1 - y0 + 1, LCD_WIDTH, color);
}

void gfx_draw_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!g_lcd_ok) return;
    gfx_draw_hline(x0, x1, y0, color);
    gfx_draw_hline(x0, x1, y1, color);
    gfx_draw_vline(x0, y0, y1, color);
    gfx_draw_vline(x1, y0, y1, color);
}

void gfx_fill_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!g_lcd_ok) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;
    dma2d_fill_rect(&LCD_BACK_BUF[y0 * LCD_WIDTH + x0],
                    x1 - x0 + 1, y1 - y0 + 1, LCD_WIDTH, color);
}

void gfx_draw_char(int x, int y, char c, uint16_t fg, uint16_t bg)
{
    if (!g_lcd_ok) return;
    if (c < 0x20 || c > 0x7E) c = '?';
    const uint8_t *g = font8x16[c - 0x20];
    for (int row = 0; row < 16; row++) {
        uint8_t bits = g[row];
        for (int col = 0; col < 8; col++) {
            uint16_t color = (bits & (0x80 >> col)) ? fg : bg;
            gfx_draw_pixel(x + col, y + row, color);
        }
    }
}

void gfx_draw_string(int x, int y, const char *s, uint16_t fg, uint16_t bg)
{
    if (!g_lcd_ok) return;
    int cx = x;
    while (*s) {
        gfx_draw_char(cx, y, *s, fg, bg);
        cx += 8;
        s++;
    }
}

void gfx_flip(void)
{
    if (!g_lcd_ok) return;
    /* M2：单 buffer + memcpy + write-through（最稳不花屏，例程同款）。
     * write-through 下 memcpy 写 back 立即穿透 SDRAM，LTDC 立即读到 → 不花屏。
     * 真双 buffer（写 L1CFBAR）作为 §0C.3 可选优化，M2 不做。*/
    memcpy((void *)LCD_FRONT_BUF, (void *)LCD_BACK_BUF, LCD_FBSIZE);
}
