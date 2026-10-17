/**
 * iwdg.h — IWDG 独立看门狗（§0D，#4 优化）
 *
 * 工程未拉 HAL IWDG 驱动源，但 LL IWDG 头在。这里用寄存器级实现（不依赖 HAL_IWDG），
 * 抄官方例程 sys.c 的 LSI 使能 + IWDG 寄存器配置。
 *
 * LSI≈32kHz, /256=125Hz(8ms/tick), RLR=4095 → 超时 4095×8ms ≈ 32.7s。
 * 主循环每轮喂狗（< 10ms），HardFault 死循环不喂狗 → 32s 自动复位重启。
 */
#ifndef IWDG_H
#define IWDG_H

#include <stdint.h>

/* 初始化 IWDG（LSI 启动 + 预分频/重装，超时 ≈2.0s（TCS §3.9））。*/
void iwdg_init(void);

/* 喂狗（写 0xAAAA 到 IWDG_KR）。主循环每轮调。*/
void iwdg_refresh(void);

#endif /* IWDG_H */
