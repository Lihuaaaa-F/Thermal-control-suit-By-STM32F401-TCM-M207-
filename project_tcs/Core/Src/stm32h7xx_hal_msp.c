/* stm32h7xx_hal_msp.c — HAL MSP（底层外设时钟/GPIO 配置）
 *
 * 把 MSP 从 main.c 拆出来独立文件，避免与 main.c 重复定义。
 * §0F：HAL_UART_MspInit 死代码清理——实际用 USART1（main.h PROTO_UART_SEL=1），
 *      MX_PROTO_UART_Init 已自包含时钟+GPIO 配置。这里留空注释防误导后续 agent。
 *
 * 各外设的 HAL_xxx_MspInit 在 HAL_xxx_Init 内部回调：
 *   - SDRAM/LTDC 的 MspInit 在 ltdc.c（引脚多，就近写）
 *   - I2C（EEPROM）的 MspInit 在 eeprom.c
 *   - SD 的 MspInit 在 storage.c
 */
#include "stm32h7xx_hal.h"
#include "main.h"

void HAL_MspInit(void)
{
    __HAL_RCC_SYSCFG_CLK_ENABLE();
}

/* §0F：USART1 时钟+GPIO 由 MX_PROTO_UART_Init 自包含，这里不重复配。
 * 历史：曾用 USART3，已切 USART1（main.h PROTO_UART_SEL=1）。*/
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    (void)huart;
}
