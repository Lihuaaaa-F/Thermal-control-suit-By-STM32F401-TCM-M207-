/* stm32h7xx_hal_conf.h — STM32H7 HAL 配置（按地面站实际用到的外设开启）
 *
 * 对应 stm32h7xx_hal_conf_template.h（HAL Driver Inc/），裁剪掉用不上的模块省 Flash。
 * 地面站用到：RCC/GPIO/UART/TIM/LTDC/DMA2D/FMC(SDRAM)/I2C/EXTI/PWR/DMA/MDMA/CORTEX/FLASH/HSEM/RAMECC/SRAM
 *
 * 机制：用到某模块就 #define HAL_xxx_MODULE_ENABLED（无值）；不用则不定义。
 *       各 .c/.h 用 #ifdef HAL_xxx_MODULE_ENABLED 守卫，未定义→该模块代码不编译。
 *
 * HSE_VALUE 按板子晶振填（惠勤智远 H743 待原理图确认；先按 25MHz）
 */
#ifndef STM32H7xx_HAL_CONF_H
#define STM32H7xx_HAL_CONF_H

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 模块开关：用到就 define，不用就不定义 ====== */
#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_DMA2D_MODULE_ENABLED          /* M2: LTDC 加速 */
#define HAL_DSI_MODULE_ENABLED            /* LTDC 关联（RGB 屏可能不用，留着防 link 错）*/
#define HAL_EXTI_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_FMAC_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_HSEM_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED            /* M3: FT5446 */
#define HAL_LTDC_MODULE_ENABLED           /* M2: 显示 */
#define HAL_MDMA_MODULE_ENABLED           /* DMA2D/LTDC 用 */
#define HAL_PWR_MODULE_ENABLED
#define HAL_RAMECC_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
/* HAL_SD_MODULE_ENABLED —— TF 卡(M4b)在本工作区剔除，关闭 SD 模块省 Flash */
#define HAL_SDRAM_MODULE_ENABLED          /* M2: 外部 SDRAM 显存 */
#define HAL_SRAM_MODULE_ENABLED           /* LTDC/FMC 用 SRAM 接口 */
#define HAL_TIM_MODULE_ENABLED            /* M1: TIM6 tick */
#define HAL_UART_MODULE_ENABLED           /* M1: 协议串口 */
/* 未启用（不定义即可）：ADC/CEC/COMP/CORDIC/CRC/CRYP/DAC/DCMI/DFSDM/DTS/ETH/FDCAN/
   GFXMMU/HASH/HCD/HRTIM/I2S/IRDA/JPEG/LPTIM/MDIOS/MMC/NAND/NOR/OPAMP/OSPI/
   OTFDEC/PCD/PSSI/QSPI/RNG/RTC/SAI/SD/SMARTCARD/SMBUS/SPDIFRX/SPI/SWPMI/USART/WWDG */

/* ====== 外部晶振（待原理图确认，惠勤智远 H743 常见 25MHz）====== */
#if !defined(HSE_VALUE)
#define HSE_VALUE    25000000U
#endif
#define HSE_STARTUP_TIMEOUT 100U

#if !defined(HSI_VALUE)
#define HSI_VALUE    64000000U
#endif
#define CSI_VALUE    4000000U

/* 外部 I2S/SAI 时钟（HAL_RCCEx_GetPeriphCLKFreq 引用，给 0 表示外部未接）*/
#define EXTERNAL_CLOCK_VALUE    48000U

#if !defined(LSE_VALUE)
#define LSE_VALUE    32768U
#endif
#define LSE_STARTUP_TIMEOUT 5000U

/* ====== VOS / 缓存 ====== */
#define VDD_VALUE                    3300U
#define TICK_INT_PRIORITY            (15U)
#define USE_RTOS                     0U
#define PREFETCH_ENABLE              1U
#define INSTRUCTION_CACHE_ENABLE     1U
#define DATA_CACHE_ENABLE            1U

/* #define USE_FULL_ASSERT    1 */   /* 开发期可开 */

/* ====== 各模块头文件 include（HAL 标准机制，由 conf 统一引入）====== */
#ifdef HAL_RCC_MODULE_ENABLED
 #include "stm32h7xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
 #include "stm32h7xx_hal_gpio.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
 #include "stm32h7xx_hal_dma.h"
#endif
#ifdef HAL_MDMA_MODULE_ENABLED
 #include "stm32h7xx_hal_mdma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
 #include "stm32h7xx_hal_cortex.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
 #include "stm32h7xx_hal_pwr.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
 #include "stm32h7xx_hal_flash.h"
#endif
#ifdef HAL_EXTI_MODULE_ENABLED
 #include "stm32h7xx_hal_exti.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
 #include "stm32h7xx_hal_uart.h"
#endif
#ifdef HAL_TIM_MODULE_ENABLED
 #include "stm32h7xx_hal_tim.h"
#endif
#ifdef HAL_LTDC_MODULE_ENABLED
 #include "stm32h7xx_hal_ltdc.h"
#endif
#ifdef HAL_DMA2D_MODULE_ENABLED
 #include "stm32h7xx_hal_dma2d.h"
#endif
#ifdef HAL_SRAM_MODULE_ENABLED
 #include "stm32h7xx_hal_sram.h"
#endif
#ifdef HAL_SDRAM_MODULE_ENABLED
 #include "stm32h7xx_hal_sdram.h"
#endif
#ifdef HAL_SD_MODULE_ENABLED
 #include "stm32h7xx_hal_sd.h"
#endif
#ifdef HAL_I2C_MODULE_ENABLED
 #include "stm32h7xx_hal_i2c.h"
#endif
#ifdef HAL_HSEM_MODULE_ENABLED
 #include "stm32h7xx_hal_hsem.h"
#endif
#ifdef HAL_RAMECC_MODULE_ENABLED
 #include "stm32h7xx_hal_ramecc.h"
#endif

#define assert_param(expr) ((void)0U)

#ifdef __cplusplus
}
#endif

#endif /* STM32H7xx_HAL_CONF_H */
