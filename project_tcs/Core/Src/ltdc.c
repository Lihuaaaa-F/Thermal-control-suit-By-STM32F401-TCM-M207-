/**
 * ltdc.c — LTDC + 外部 SDRAM 显存驱动实现（M2）
 *
 * 所有数值经 4+ agent 从官方例程（实验12 LTDC LCD RGB 屏）原文核实，照抄不推算：
 *   - SDRAM 时序：CAS2/刷新839/列9行13/ReadPipeDelay=1/ModeReg 0x0220（§2.2，抄 sdram.c）
 *   - SDRAM 引脚：FMC 全 AF12（GPIOC PIN0 / GPIOD 7pin / GPIOE 11pin / GPIOF 11pin /
 *     GPIOG 7pin / GPIOH 2pin，抄 HAL_SDRAM_MspInit 原文）
 *   - LTDC 时序：HSync=19/VSync=2/AccHBP=159/AccVBP=22/AccActiveW=1183/AccActiveH=622/
 *     TotalW=1343/TotalHeigh=634（vfp=12，抄 ltdc.c 0x7016 分支；M02-F-09 勘正：旧注释写
 *     621/633，比代码少 1）。像素时钟 50MHz ÷ (1344×635) = 58.59Hz，比 60Hz 低 2.3%，
 *     在面板 ±5% 容差内。
 *   - LTDC 引脚：RGB 全 AF14（PA8=AF13），PCLK=PG7/HSYNC=PI10/VSYNC=PI9/DE=PF10，
 *     背光 PB5 推挽，复位 PD11（抄 HAL_LTDC_MspInit 原文）
 *   - PLL3：M25/N300/R6→PLL3R=50M 给 LTDC PCLK（抄 ltdc_clk_set，在 ltdc_init 开头配）
 *   - MPU：SDRAM 0xC0000000 区 Cacheable+Bufferable + SCB->CACR|=1<<2 write-through（§2.8，抄 mpu.c+sys.c）
 *   - 填充：CPU 写穿透 32 位 store（gfx_fill_rect）。DMA2D 没有启用——文件末尾"历史记录"
 *     有本片的实测结论（M02-F-08：旧注释写"DMA2D R2M 加速"，与实现不符）
 *
 * 屏 = 1024×600 RGB565 纯 RGB 直驱（HX8282，ID=0x7016），不需 SPI/I2C 初始化命令。
 *
 * S-G h1a（2026-11-25）：
 *   - 文件分两段：上段纯逻辑（SDRAM 自检图样，主机单测直接编译），下段硬件。
 *   - SDRAM 自检读回前做 cache 维护，覆盖范围从 1MB 扩到两块帧缓冲 + LVGL 渲染块（M02-F-05）。
 *   - PLL3 / 刷新率配置的 HAL 返回值要判（M02-F-11）。
 *   - 直接写 LTDC 寄存器的地方集中到 ltdc_set_fb()；死函数 ltdc_show_buffer 已删
 *     （M02-F-10 / X05-F-08）。
 */
#ifdef TCS_UNIT_TEST
/* ====== 纯逻辑段（PC 单测直接编译）：SDRAM 自检图样 ====== */
#include <stdint.h>
#define LCD_WIDTH        1024
#define LCD_HEIGHT       600
#define LCD_PIXELBYTES   2
#define LCD_FBSIZE       (LCD_WIDTH * LCD_HEIGHT * LCD_PIXELBYTES)
#else
#include "ltdc.h"
#include "segger_rtt.h"
#include <string.h>
#endif

/* 自检覆盖范围 = 3 块帧缓冲大小的区域：front / back 两块帧缓冲，加上紧随其后的 LVGL 渲染块
 * （App/lvgl_port/lv_port_disp.c 的 CHUNK_BUF，同样大小）。原来只测前 1MB——背缓冲和
 * 渲染块根本没碰，而 LTDC 和 LVGL 每天都在用它们。*/
#define SDRAM_SELFTEST_FRAMES  3u
/* 每 4KB 取一个探测点（正好一条 32B cache line 的首地址）*/
#define SDRAM_SELFTEST_STEP    0x1000u

uint32_t sdram_selftest_span(void) { return (uint32_t)LCD_FBSIZE * SDRAM_SELFTEST_FRAMES; }
uint32_t sdram_selftest_step(void) { return SDRAM_SELFTEST_STEP; }

/* 第 k 个探测点（k = 0, 1, …, span/step-1）：字节偏移与期望值。
 * 偏移 = 32 + k×4KB：开头 32 字节留给 16 字连续探测，第一个探测点紧跟其后，
 * 最后一个落在范围末尾那 4KB 之内。
 * 期望值 = k 的 16 位散列（奇数乘法，k < 2^16 内单射）：任何两个探测点的值都不一样，
 * 地址线卡死把两点叠在一起时一定读得出矛盾。原来的 (偏移 & 0xFFFF) 每 64KB 重复一次，
 * A16 及以上卡死时两点同值，查不出来（见 tests/test_drv_pure.c 的故障注入）。*/
uint32_t sdram_probe_off(uint32_t k) { return 32u + k * SDRAM_SELFTEST_STEP; }
uint16_t sdram_probe_val(uint32_t k) { return (uint16_t)(k * 40503u + 1u); }

#ifndef TCS_UNIT_TEST

/* ====== 全局句柄 ====== */
static SDRAM_HandleTypeDef hsdram;
static LTDC_HandleTypeDef  hltdc;
static FMC_SDRAM_CommandTypeDef sdcmd;

/* 显存是否可用（ltdc_init 成功且 SDRAM 自检通过才置 1）。
 * 为 0 时所有 gfx_* 直接返回，避免访问 0xC0000000 触发 BusFault。*/
static int g_lcd_ok = 0;

/* WP-C.5 真双缓冲：绘制目标指针，flip 时与 LTDC CFBAR 乒乓切换（零拷贝）。
 * flip 后 s_back 指向刚离屏的旧帧=上一帧，脏区增量重绘语义不变（逐帧脏区
 * 在 flip 时全部冲刷，两帧内容除脏区外一致）。*/
static uint16_t *s_back = LCD_BACK_BUF;

/* 当前屏上显示的缓冲地址（probe-rs/fb_dump 取证用；RAM 变量可可靠直读，
 * LTDC 寄存器经调试口读数不可靠——WP-C.5 实测恒 0）*/
volatile uint32_t g_shown_fb = (uint32_t)LCD_BACK_BUF;

/* G4：LVGL 侧查询渲染目标帧（LTDC 正扫描的那块）。
 * 注意（X05-F-08）：这里原还有一个 ltdc_show_buffer()——"CFBAR+VBR 上屏"。它零调用点：
 * LVGL 的 flush_cb（lv_port_disp.c）把像素直接 memcpy 进两块帧缓冲，不翻页。
 * 已删；本工程里切帧缓冲的唯一入口是下面的 ltdc_set_fb()。*/
#if TCS_USE_LVGL
uint16_t * ltdc_display_buffer(void) { return LCD_FRONT_BUF; }
#endif

/* 唯一一处直接写 LTDC 寄存器的地方（M02-F-10）。
 * 为什么不走 HAL：HAL_LTDC_SetAddress 不是"只改 CFBAR"——它经 LTDC_SetConfig 用句柄里的
 * 图层参数重写整组图层寄存器（WHPCR/WVPCR/PFCR/DCCR/CFBAR/CFBLR/CFBLNR/CFCR），并触发
 * 立即重载（IMR）；HAL_LTDC_Reload 还会打开重载中断 RRIE，而本工程没有 LTDC 中断处理。
 * 这里要的只是"消隐期把 CFBAR 换成另一块帧缓冲"，两条写就够了。*/
static void ltdc_set_fb(const uint16_t *fb)
{
    LTDC_Layer1->CFBAR = (uint32_t)fb;
    LTDC->SRCR = LTDC_SRCR_VBR;   /* 垂直消隐期重载，防撕裂 */
    g_shown_fb = (uint32_t)fb;
}

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
static int ltdc_clk_set(void)
{
    RCC_PeriphCLKInitTypeDef p = {0};
    p.PeriphClockSelection = RCC_PERIPHCLK_LTDC;
    p.PLL3.PLL3M = 25;
    p.PLL3.PLL3N = 300;
    p.PLL3.PLL3P = 2;
    p.PLL3.PLL3Q = 2;
    p.PLL3.PLL3R = 6;       /* VCO=25/25*300=300M, /6=50M 给 LTDC PCLK */
    /* 返回值要判（M02-F-11）：PLL3 参数被 HAL 拒绝（VCO 范围/时钟源/等锁超时）时像素时钟
     * 就不是 50MHz——黑屏或帧率不对，而初始化原来照样报成功。0=成功。*/
    return (HAL_RCCEx_PeriphCLKConfig(&p) == HAL_OK) ? 0 : -1;
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

    /* 刷新率计数器=839（64ms/8192行，SDCLK=110M）。返回值要判（M02-F-11）：没写进去 =
     * 刷新周期不对，高温下显存丢数据/花屏，而初始化原来照样报成功。*/
    if (HAL_SDRAM_ProgramRefreshRate(&hsdram, 839) != HAL_OK) return -1;

    return 0;
}

/* 自检：写 → cache 维护 → 读回。返回 1=通 0=不通。
 * 覆盖范围与图样见文件开头的纯逻辑段（M02-F-05）。*/
int sdram_selftest(void)
{
    volatile uint16_t *p = (volatile uint16_t *)SDRAM_BASE_ADDR;
    const uint32_t n = sdram_selftest_span() / sdram_selftest_step();

    /* 1. 开头 16 个连续地址：写 0xAA00+i */
    for (uint32_t i = 0; i < 16; i++) p[i] = (uint16_t)(0xAA00 + i);
    /* 2. 步进图样：整个显示区（两块帧缓冲 + LVGL 渲染块）每 4KB 一个点 */
    for (uint32_t k = 0; k < n; k++) p[sdram_probe_off(k) / 2u] = sdram_probe_val(k);

    /* 读回之前做 cache 维护：SDRAM 区是 Cacheable 的，刚写过的行还留在 D-Cache 里，
     * 紧接着读回读到的是 cache——SDRAM 数据线/地址线坏了也查不出来，LTDC 拿到的却是
     * 物理显存里的错像素。clean+invalidate 之后下面的读才真正走 FMC。
     * （现在靠 CACR 强制写透，clean 是空操作；留着，不依赖那一位。）*/
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)SDRAM_BASE_ADDR, (int32_t)sdram_selftest_span());

    /* 3. 读回 */
    for (uint32_t i = 0; i < 16; i++) {
        if (p[i] != (uint16_t)(0xAA00 + i)) return 0;
    }
    for (uint32_t k = 0; k < n; k++) {
        if (p[sdram_probe_off(k) / 2u] != sdram_probe_val(k)) return 0;
    }
    return 1;
}

/* ===================================================================== */
/*                            LTDC 初始化                                */
/* ===================================================================== */
/* 时序照抄官方例程 ltdc.c 0x7016 分支（§2.3 原文，vfp=12）：
 *   HSync=19/VSync=2/AccHBP=159/AccVBP=22/AccActiveW=1183/AccActiveH=622/
 *   TotalW=1343/TotalHeigh=634，极性全 AL+IPC
 * （M02-F-09：旧注释写 AccActiveH=621/TotalH=633，比下面的表达式少 1。
 *   LCD_HEIGHT=600 → 3+20+600-1=622、3+20+600+12-1=634；HAL 写寄存器时各 +1。）*/
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
    hltdc.Init.AccumulatedActiveH = 3 + 20 + LCD_HEIGHT - 1;    /* =622 (vfp=12) */
    hltdc.Init.TotalWidth       = 20 + 140 + LCD_WIDTH + 160 - 1;  /* =1343 */
    hltdc.Init.TotalHeigh       = 3 + 20 + LCD_HEIGHT + 12 - 1;    /* =634 (vfp=12) */
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

    if (ltdc_clk_set() != 0) {          /* PLL3 → LTDC PCLK=50M；失败就不往下点屏 */
        SEGGER_RTT_WriteString("RTT: pll3 ltdc FAIL (HAL_RCCEx_PeriphCLKConfig rejected)\n");
        return -4;
    }
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
                      (unsigned)RCC->AHB4ENR, (unsigned)GPIOB->MODER, (unsigned)GPIOB->ODR);
    SEGGER_RTT_printf(0, "RTT: bl_diag PB5: MODER[11:10]=%u ODR_bit5=%u (expect 1,1)\n",
                      (unsigned)((GPIOB->MODER >> 10) & 3U), (unsigned)((GPIOB->ODR >> 5) & 1U));

    g_lcd_ok = 1;
    /* 双缓冲初始状态：两个缓冲都清黑，第一帧起 CFBAR 乒乓（WP-C.5）*/
    gfx_clear(COLOR_BLACK);
    gfx_flip();
    gfx_clear(COLOR_BLACK);
    gfx_flip();
    SEGGER_RTT_printf(0, "RTT: ltdc init rv=0 g_lcd_ok=%d double_buf=1\n", g_lcd_ok);
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
/*                          基础绘图                                      */
/* ===================================================================== */
/* WP-C.5 终版：填充走 CPU 写穿透 32 位 store，不再用 DMA2D。
 * 依据（2026-10-18 实测）：
 *  a) 本片 DMA2D 多行 R2M 每行步进 +32px（斜带漏色，WP-A.2d）；单次多行 R2M
 *     更会挂死系统总线（scrtest raw 实验 → 主循环假死 → IWDG 复位救回）；
 *  b) DMA2D 小搬运每次 ~18µs 固定开销（LTDC 抢占总线），整页重绘 ~85ms 超
 *     "UI ≤25ms"预算 3 倍；CPU 写穿透整页 ~10ms 级且天然无一致性/斜带问题。*/
void gfx_fill_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!g_lcd_ok) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;
    if (x0 > x1 || y0 > y1) return;

    uint32_t c32 = (uint32_t)color << 16 | color;
    int w = x1 - x0 + 1;
    for (int y = y0; y <= y1; y++) {
        uint16_t *p = &s_back[(size_t)y * LCD_WIDTH + x0];
        int n = w;
        while (n >= 1 && ((uintptr_t)p & 3u)) { *p++ = color; n--; }  /* 对齐到 32 位 */
        uint32_t *p32 = (uint32_t *)(void *)p;
        while (n >= 8) { p32[0]=c32; p32[1]=c32; p32[2]=c32; p32[3]=c32; p32 += 4; n -= 8; }
        while (n >= 2) { *p32++ = c32; n -= 2; }
        if (n) *(uint16_t *)p32 = color;
    }
}

void gfx_clear(uint16_t color)
{
    gfx_fill_rect(0, 0, LCD_WIDTH - 1, LCD_HEIGHT - 1, color);
}

void gfx_draw_pixel(int x, int y, uint16_t color)
{
    if (!g_lcd_ok) return;
    if (x < 0 || x >= LCD_WIDTH || y < 0 || y >= LCD_HEIGHT) return;
    s_back[y * LCD_WIDTH + x] = color;
}

void gfx_draw_hline(int x0, int x1, int y, uint16_t color)
{
    gfx_fill_rect(x0, y, x1, y, color);
}

void gfx_draw_vline(int x, int y0, int y1, uint16_t color)
{
    gfx_fill_rect(x, y0, x, y1, color);
}

void gfx_draw_rect(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (!g_lcd_ok) return;
    gfx_draw_hline(x0, x1, y0, color);
    gfx_draw_hline(x0, x1, y1, color);
    gfx_draw_vline(x0, y0, y1, color);
    gfx_draw_vline(x1, y0, y1, color);
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
    /* WP-C.5 真双缓冲：CFBAR 指向刚画完的缓冲 + 垂直消隐期重载（零拷贝、无撕裂）。
     * 原 memcpy 版 1.2MB 写穿透实测 ~100ms/次，是"UI 全屏 ≤25ms"预算的元凶；
     * flip 后 s_back 指向旧帧（=屏上上一帧），脏区增量重绘语义不变。
     * 现在只有 ltdc_init 上电清屏时调两次（两块帧缓冲各清一次再切过去）。*/
    ltdc_set_fb(s_back);
    s_back = (s_back == LCD_FRONT_BUF) ? LCD_BACK_BUF : LCD_FRONT_BUF;
}

/* WP-C.5 补丁：乒乓下不同脏区的局部重绘会落不同缓冲 → 内容分叉
 * （实测 TEC 徽章停在旧态）。局部重绘一律直写显示缓冲；整页/弹窗先选
 * 隐藏缓冲再 flip。*/
void gfx_use_shown_buffer(void)
{
    s_back = (g_shown_fb == (uint32_t)LCD_FRONT_BUF) ? LCD_FRONT_BUF : LCD_BACK_BUF;
}

void gfx_use_hidden_buffer(void)
{
    s_back = (g_shown_fb == (uint32_t)LCD_FRONT_BUF) ? LCD_BACK_BUF : LCD_FRONT_BUF;
}

void gfx_sync_buffers(void)
{
    if (!g_lcd_ok) return;
    /* 切页等"整页重绘"后把新帧同步到另一缓冲：乒乓下 flip 后 s_back=旧帧、
     * g_shown_fb=刚画好的新帧。同步方向必须是 新帧→旧帧缓冲（H1 修正：
     * 原实现 memcpy(other, s_back) 反向拷贝，把旧帧盖到新帧上——按下残影
     * 与页面切换残留的根因；WP-C.5 的"顶栏带回旧页"症状实为同源问题）。
     * 仅整页重绘后调用一次（+~15ms），日常局部重绘仍零拷贝。*/
    void *other = (s_back == LCD_FRONT_BUF) ? (void *)LCD_BACK_BUF
                                            : (void *)LCD_FRONT_BUF;
    memcpy(s_back, other, LCD_FBSIZE);   /* 新帧(显示中) → 旧帧缓冲 */
}

/* K3 v9.5 小更新直绘配套：gfx_use_shown_buffer() 直绘完成后调用，把 rect 从
 * 显示缓冲拷入隐藏缓冲（两缓冲恒一致，任何时刻 flip 都不残影）。
 * 仅用于 <整卡级 小更新；整页/弹窗内容仍走 flip+gfx_sync_buffers。*/
void gfx_sync_rect(int x0, int y0, int x1, int y1)
{
    if (!g_lcd_ok) return;
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= LCD_WIDTH) x1 = LCD_WIDTH - 1;
    if (y1 >= LCD_HEIGHT) y1 = LCD_HEIGHT - 1;
    if (x0 > x1 || y0 > y1) return;
    uint16_t *src = (uint16_t *)g_shown_fb;
    uint16_t *dst = (s_back == src) ? ((s_back == LCD_FRONT_BUF) ? LCD_BACK_BUF
                                                                 : LCD_FRONT_BUF)
                                    : s_back;
    size_t rowbytes = (size_t)(x1 - x0 + 1) * LCD_PIXELBYTES;
    for (int y = y0; y <= y1; y++)
        memcpy((uint8_t *)dst + (size_t)y * LCD_WIDTH * LCD_PIXELBYTES
                               + (size_t)x0 * LCD_PIXELBYTES,
               (const uint8_t *)src + (size_t)y * LCD_WIDTH * LCD_PIXELBYTES
                                    + (size_t)x0 * LCD_PIXELBYTES,
               rowbytes);
}

/* 历史记录（WP-A.2d/WP-C.5 实验结论，代码已移除防误用）：
 *  - DMA2D R2M 多行填充在本片存在 +32px/行步进异常（斜带漏色）；
 *  - 单次多行 R2M（scrtest raw 实验）会挂死系统总线（LTDC 同时在跑时），
 *    主循环假死由 IWDG 复位救回；
 *  - DMA2D M2M 同样 +32px/行，且小搬运 ~18µs/次开销不可接受。
 *  → 显示写路径全部改 CPU 写穿透（gfx_fill_rect）+ CFBAR 乒乓（ltdc_set_fb）。
 *  现在全工程对 DMA2D 只有 MspInit 里一处时钟使能，没有任何一次传输调用。*/

#endif /* !TCS_UNIT_TEST */
