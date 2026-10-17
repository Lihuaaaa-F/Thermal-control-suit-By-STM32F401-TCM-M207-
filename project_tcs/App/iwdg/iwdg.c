/**
 * iwdg.c — IWDG 独立看门狗寄存器级实现（§0D）
 *
 * 不依赖 HAL_IWDG 驱动（工程未拉 iwdg.c 源），用 CMSIS 寄存器直写。
 * LSI 在 SystemClock_Config 里已 HAL_RCC_OscConfig 启用（LSION）。
 */
#include "iwdg.h"
#include "stm32h7xx_hal.h"

#define IWDG_BASE_ADDR   0x58004800U
#define IWDG_KR          (*(volatile uint32_t *)(IWDG_BASE_ADDR + 0x00))
#define IWDG_PR          (*(volatile uint32_t *)(IWDG_BASE_ADDR + 0x04))
#define IWDG_RLR         (*(volatile uint32_t *)(IWDG_BASE_ADDR + 0x08))
#define IWDG_SR          (*(volatile uint32_t *)(IWDG_BASE_ADDR + 0x0C))

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

    IWDG_KR  = IWDG_KEY_ENABLE;    /* 启动 IWDG（一旦启动不可停）*/
    IWDG_KR  = IWDG_KEY_WRITE;     /* 允许写 PR/RLR */
    IWDG_PR  = 6U;                 /* 预分频 256：LSI≈32kHz /256 = 125Hz, 8ms/tick */
    IWDG_RLR = 4095U;              /* ⚠ 暂定 32.7s（GS 同款），见 docs/evidence/WP6_iwdg_defect.md */               /* 重装值 249 → 250×8ms ≈ 2.0s 超时（§3.9，F5）*/
    /* 等 PVU/RVU 更新完成 */
    while ((IWDG_SR & 0x07) != 0) {}
    IWDG_KR  = IWDG_KEY_REFRESH;   /* 首次喂狗，装载重装值 */
}

void iwdg_refresh(void)
{
    IWDG_KR = IWDG_KEY_REFRESH;
}
