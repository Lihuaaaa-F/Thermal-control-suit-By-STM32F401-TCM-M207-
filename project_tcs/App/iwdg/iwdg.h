/**
 * iwdg.h — IWDG 独立看门狗（§0D，#4 优化）
 *
 * 工程未拉 HAL IWDG 驱动源，这里用寄存器级实现（不依赖 HAL_IWDG），
 * 抄官方例程 sys.c 的 LSI 使能 + IWDG 寄存器配置。实际数值以 iwdg.c 为准：
 *   LSI≈32kHz，预分频 256 → 125Hz（8ms 一拍）；RLR=249 → (249+1)×8ms = **2.0s** 超时。
 * 主循环每轮喂狗（< 10ms）；HardFault / Error_Handler 的死循环不喂狗 → 约 2s 后自动复位。
 *
 * 历史（M01-F-06 勘正，2026-11-25）：本注释原来写"RLR=4095 → 4095×8ms ≈ 32.7s /
 * 32s 自动复位"。那是 WP6 期间的临时发布配置（见 docs/evidence/WP6_iwdg_defect.md），
 * 早就不是现值，和 iwdg.c 里的 2.0s 正好相反。32.7s 只是"LSI 32kHz、/256、RLR 取满
 * 4095"时的理论上限，本工程没有采用。按 32s 去算喂狗余量或安排阻塞任务会出错：实机是 2s。
 */
#ifndef IWDG_H
#define IWDG_H

#include <stdint.h>

/* 初始化 IWDG（LSI 启动 + 预分频/重装，超时 ≈2.0s（TCS §3.9））。一旦启动停不掉。*/
void iwdg_init(void);

/* 喂狗（写 0xAAAA 到 IWDG_KR）。主循环每轮调；两次喂狗之间不得超过 2.0s。*/
void iwdg_refresh(void);

#endif /* IWDG_H */
