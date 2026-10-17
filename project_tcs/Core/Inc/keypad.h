/**
 * keypad.h — 4×4 矩阵键盘驱动（16 键 / 8 针）
 *
 * 接线（慧勤智远 STM32H743IIT6 V1.1 小系统板）：
 *   行 R1..R4 → PC1, PC2, PC3, PC4   （推挽输出，扫描时逐行拉低）
 *   列 C1..C4 → PD3, PD4, PD5, PD7   （输入+内部下拉，读回判断哪列按下）
 *   GND 共地
 *
 * ⚠️ 列故意跳过 PD6（PD6=LCD_B2，屏幕同学用，不能给键盘）。
 *
 * 扫描方式：定时轮询（主循环每 KEYPAD_SCAN_MS ms 扫一次）。
 *   一次扫描 = 4 行依次拉低，读 4 列电平。列读到高 = 该行列上有按下。
 *   去抖：连续 2 次扫描读到同一键且上一次为"松开"状态 → 触发一次按键事件。
 *
 * 键值 0..15 = row*4 + col。main/ui 里按键值 → 命令映射。
 *
 * 不依赖 LTDC/触摸，协议层照跑不受影响。
 */
#ifndef KEYPAD_H
#define KEYPAD_H

#include <stdint.h>

/* —— 引脚映射（与 main.h 一致，单独列在这里方便核对）—— */
#define KP_ROW_PORT     GPIOC
#define KP_ROW_PINS     (GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4)
#define KP_ROW0         GPIO_PIN_1
#define KP_ROW1         GPIO_PIN_2
#define KP_ROW2         GPIO_PIN_3
#define KP_ROW3         GPIO_PIN_4

#define KP_COL_PORT     GPIOD
#define KP_COL0         GPIO_PIN_3
#define KP_COL1         GPIO_PIN_4
#define KP_COL2         GPIO_PIN_5
#define KP_COL3         GPIO_PIN_7
#define KP_COL_PINS     (GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_7)

#define KEYPAD_SCAN_MS  5      /* 扫描周期，主循环调用节拍 */
#define KEYPAD_DEBOUNCE 2      /* 连续命中次数才触发（×SCAN_MS = 10ms 去抖） */

/* —— API —— */

/* 初始化 GPIO。失败不阻塞协议层。返回 0=ok。*/
int  keypad_init(void);

/* 主循环周期调用（传 g_tick_ms）。内部完成扫描+去抖。
 * 有键按下/释放变化时返回事件，否则返回 KEYPAD_NONE。*/
#define KEYPAD_NONE     0xFF
uint8_t keypad_scan(uint32_t now_ms);

/* 是否已初始化 */
int  keypad_ready(void);

#endif /* KEYPAD_H */
