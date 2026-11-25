/* main.h — TCS 板级引脚映射与全局句柄（接线表 v1 终稿，§2.3）
 *
 * 惠勤智远 STM32H743IIT6 V1.1 成品板。TCS 新增外设引脚（审计核验空脚，§2.2）：
 *   USART3 PB10(TX)/PB11(RX) AF7  @57600  → M207（接线表 #1/#2）          〔台架/仿真档〕
 *   USART2 PD5(TX)/PA3(RX)   AF7  @230400 → ESP32-S3 GPIO18(RX)/GPIO17(TX)（#3/#4，
 *                                           PD5+PA3 混搭合法：外设引脚独立映射）
 *   I2C1    PB8(SCL)/PB9(SDA) AF4 @100kHz → TMP117 @0x48（#5/#6，飞线 4.7kΩ 上拉×2）〔台架/仿真档〕
 *   GPIO    PB14 推挽 → 风扇光耦，低电平=开（#7）                          〔台架/仿真档〕
 *   GPIO    PH4(SCL)/PH5(SDA) 软件 I2C → AT24C02 参数 EEPROM              〔台架/仿真档〕
 * 板载：LED1=PB0/LED2=PB1（低亮）、KEY0=PA1（急停键）、CH340↔USART1 PA9/PA10 控制台。
 *
 * v2 档位（P1-06 残余，2026-11-25）：标〔台架/仿真档〕的四路是 v1 时代的本地外设，
 * 交付档（TCS_H743_LINK_ONLY=1）上电不初始化、不碰这些引脚——M207、体温传感器、风扇、
 * 参数存储都归 ESP32。特别注意 PB14：v2 里风扇由 ESP32 GPIO42 驱动，H743 这边在交付档
 * 必须对它保持高阻（不配成输出），否则就是和 ESP32 并联的第二个写者。
 *
 * ⚠️ keypad.c 矩阵键盘含 KP_COL2=PD5，与 USART2_TX 冲突 → TCS 固件只编译不初始化
 *    keypad（链接器 --gc-sections 剔除），见 main.c 注释。
 */
#ifndef TCS_MAIN_H
#define TCS_MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h7xx_hal.h"

/* ====== 串口分配 ====== */
#define CONSOLE_UART       USART1      /* 控制台 @115200（CH340 COM 口）*/
#define CONSOLE_BAUD       115200
#define LINK_UART          USART2      /* ESP32-S3 链路 @230400 */
#define LINK_BAUD          230400
#define M207_UART          USART3      /* M207 @57600 */
#define M207_BAUD          57600

/* ====== LED（低电平点亮）====== */
#define LED1_Pin       GPIO_PIN_0      /* 红：心跳 */
#define LED1_GPIO_Port GPIOB
#define LED2_Pin       GPIO_PIN_1      /* 绿：报警/链路 */
#define LED2_GPIO_Port GPIOB

/* ====== 板载按键 ====== */
#define KILL_KEY_Pin       GPIO_PIN_1   /* KEY0=PA1 下拉 active-high，按下=高 */
#define KILL_KEY_GPIO_Port GPIOA

/* 全局句柄（stm32h7xx_it.c 与模块共用）*/
extern UART_HandleTypeDef huart_console;   /* USART1 */
extern UART_HandleTypeDef huart_link;      /* USART2 */
extern UART_HandleTypeDef huart_m207;      /* USART3（交付档不初始化，保持零值）*/
extern I2C_HandleTypeDef  hi2c_tmp117;     /* I2C1（交付档不初始化，保持零值）*/

void Error_Handler(void);

#ifdef __cplusplus
}
#endif
#endif /* TCS_MAIN_H */
