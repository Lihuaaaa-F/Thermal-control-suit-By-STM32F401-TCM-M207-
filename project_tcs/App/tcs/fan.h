/**
 * fan.h — 风扇 GPIO 简单封装（任务书 §3.1：并入 gpio 简单封装，头文件内联实现）
 *
 * PB14 推挽输出 → 风扇光耦输入，**低电平=开**（沿用 F401 PA1 极性，F401 代码
 * RESET=开风扇；接线表 v1 #7）。光耦限流电阻沿用原 F401 板。
 *
 * 仅主循环上下文调用（control/console/UI 均在主循环，无并发风险）。
 */
#ifndef TCS_FAN_H
#define TCS_FAN_H

#include "stm32h7xx_hal.h"

#define FAN_PORT        GPIOB
#define FAN_PIN         GPIO_PIN_14

/* 上电初始化：默认风扇关（高电平）。安全态时由 control 显式开。 */
static inline void fan_init(void)
{
    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    gi.Pin   = FAN_PIN;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(FAN_PORT, &gi);
    HAL_GPIO_WritePin(FAN_PORT, FAN_PIN, GPIO_PIN_SET);   /* 关 */
}

/* on=1 开风扇（拉低），on=0 关（拉高）*/
static inline void fan_set(uint8_t on)
{
    HAL_GPIO_WritePin(FAN_PORT, FAN_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static inline uint8_t fan_get(void)
{
    return (HAL_GPIO_ReadPin(FAN_PORT, FAN_PIN) == GPIO_PIN_RESET) ? 1u : 0u;
}

#endif /* TCS_FAN_H */
