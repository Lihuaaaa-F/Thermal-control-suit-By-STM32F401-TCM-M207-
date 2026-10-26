/**
 * iwdg.c — IWDG 独立看门狗（CMSIS 符号版，§0D / D3 债清偿）
 *
 * WP-C.1：RLR=249 → LSI≈32kHz/256=125Hz，(249+1)/125 = 2.0s 超时。
 * 不依赖 HAL_IWDG 驱动（工程未拉该源），寄存器经 CMSIS IWDG_TypeDef 访问
 * （原裸地址宏已按守护规则 D3 换符号）。
 * LSI 在 SystemClock_Config 里已 HAL_RCC_OscConfig 启用（LSION）。
 *
 * 官方查证（F15 定论=W10 豁免留档，2026-10-26；红队独立裁决=豁免成立）：IWDG1->KR/PR/
 * RLR/SR 均为 ST 官方 CMSIS 设备头（stm32h743xx.h IWDG_TypeDef）符号，非裸地址；
 * 寄存器序列与 ST HAL_IWDG_Init/Refresh 等价（KR=0xCCCC 启动→0x5555 解锁→PR/RLR→
 * 等 PVU/RVU→0xAAAA 喂狗；差异仅 HAL 另带超时退出/assert/NULL 防护层，此处对 SR
 * 死等更多位=更保守，见 stm32h7xx_hal_iwdg.h 官方语义）；init-once+refresh 场景
 * 句柄层零行为增益。历史证：WP-C.1 定参+E3 勘误（docs/evidence/WP6_iwdg_defect.md）
 * +E-20 停喂复位自恢复实测。豁免依据=v13.4 §7d 第 10 条。
 */
#include "iwdg.h"
#include "stm32h7xx_hal.h"

#define IWDG_KEY_ENABLE  0xCCCCU
#define IWDG_KEY_REFRESH 0xAAAAU
#define IWDG_KEY_WRITE   0x5555U

void iwdg_init(void)
{
    /* LSI 已在 SystemClock_Config 里 LSION。这里确认 LSI 就绪。*/
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) == RESET) {
        /* LSI 未就绪，等一会儿（最多 ~30ms）。LSI 启动典型 < 1ms。*/
        for (volatile int i = 0; i < 200000; i++) {
            if (__HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) != RESET) break;
        }
    }

    IWDG1->KR  = IWDG_KEY_ENABLE;    /* 启动 IWDG（一旦启动不可停）*/
    IWDG1->KR  = IWDG_KEY_WRITE;     /* 允许写 PR/RLR */
    IWDG1->PR  = 6U;                 /* 预分频 256：LSI≈32kHz/256=125Hz, 8ms/tick */
    IWDG1->RLR = 249U;               /* (249+1)×8ms = 2.0s 超时（WP-C.1，E3 勘误）*/
    /* 等 PVU/RVU 更新完成 */
    while ((IWDG1->SR & 0x07) != 0) {}
    IWDG1->KR  = IWDG_KEY_REFRESH;   /* 首次喂狗，装载重装值 */
}

void iwdg_refresh(void)
{
    IWDG1->KR = IWDG_KEY_REFRESH;
}
