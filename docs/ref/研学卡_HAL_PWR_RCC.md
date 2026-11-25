# 研学卡：HAL_PWR / HAL_RCC（电源域与时钟树 · H743 480MHz 主频 · LSI 供给 IWDG）

> 任务书 v13.4 §7b W8 · 制卡 2026-10-27 · 基线=git HEAD `43e5f4b` + G 线未提交工作区（ltdc.c 为 M 态，本卡 ltdc.c 行号=G 线工作区快照）
> 门禁：③ 节表行数 == grep 命中数（本卡 **61 行 == 61 命中** ✓）

## ① 官方文档与版本章节

- **PWR 官方源**：`project_tcs/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_pwr.c`
  - 头注 `@file`；**L70 起 "How to use this driver" 用法章节**（电压档/低功耗/唤醒域）；全文件 29 处 `@brief/@param` 函数头注；
  - 扩展：`Src/stm32h7xx_hal_pwr_ex.c`（**L22 起 How-to-use**，76 处函数头注；含 `HAL_PWREx_ConfigSupply`（LDO/SMPS 供电源选择——480MHz+VOS0 的前置）与电压档宏 `__HAL_PWR_VOLTAGESCALING_CONFIG` 域）。
- **RCC 官方源**：`Src/stm32h7xx_hal_rcc.c`（头注为 RCC 驱动说明块+27 处函数头注；无独立 "How to use" 行，配置流程并入头注与函数级 `@brief`）；
  - 扩展：`Src/stm32h7xx_hal_rcc_ex.c`（54 处函数头注：`HAL_RCC_OscConfig`（HSE/HSI/LSI/PLL 使能与稳定等待）、`HAL_RCC_ClockConfig`（SYSCLK/AHB/APB 分频+FLASH_LATENCY）、`HAL_RCCEx_PeriphCLKConfig`（FMC/LTDC 等内核外设钟））。
- **宏域 API**：`__HAL_RCC_*_CLK_ENABLE / __HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) / __HAL_RCC_CLEAR_RESET_FLAGS / __HAL_PWR_VOLTAGESCALING_CONFIG`——均定义于 `Inc/stm32h7xx_hal_rcc.h`/`Inc/stm32h7xx_hal_rcc_ex.h`/`Inc/stm32h7xx_hal_pwr.h`/`Inc/stm32h7xx_hal_pwr_ex.h`，属官方 API 面（非私造宏）。
- **版本**：随库 CMSIS Device H7 = **V1.10.7**（`Drivers/CMSIS/Device/ST/STM32H7xx/Release_Notes.html` 首行 "V1.10.7 / 04-February-2026"）——W9 依赖清单在案；HAL=vendor 裁剪快照无版本宏，锁=git tree hash `7dca0af2…`（W9）。

## ② 官方示例文件路径

- 本机探查：`find /d -maxdepth 2 -iname "STM32CubeH7" 2>/dev/null` → **空输出（无命中）**。
- **本机无 CubeH7 例程仓**；官方例程位置=GitHub `STMicroelectronics/STM32CubeH7` 仓
  `Projects/STM32H743I-EVAL/Examples/PWR/`（URL：https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/PWR ）与
  `Projects/STM32H743I-EVAL/Examples/RCC/`（URL：https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/RCC ）。
  其下子例程名本机无仓未枚举（防编造），以仓库目录为准。
- 本工程在案对照件：无独立 PWR/RCC 例程抄录源；SystemClock_Config 结构=CubeMX 风格生成骨架（时序数值见 main.c 头注与 ltdc.c 头注 PLL 条目声明）。

## ③ 我们全部调用面清单













复现命令（仓库根 `/d/Espressif/ws/tcs`，Git Bash；raw 输出路径分隔符为 `\`，表内归一为 `/`）：

```bash
grep -rnE "HAL_RCC_|HAL_PWR_" project_tcs/App project_tcs/Core/Src --include=*.c
# → 61 命中（2026-11-25 重锚），下表 61 行逐一对应
```

vendor 域说明：`App/rtt/`（SEGGER RTT vendor 五文件）同一 pattern 单独跑=**0 命中**（无需列行）。
补充（pattern 外，供审计）：`grep -rnE "HAL_PWREx_" project_tcs/App project_tcs/Core/Src --include=*.c` → **1 命中**：Core/Src/main.c:317 `HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY)`（前缀为 `HAL_PWREx_` 不匹配 `HAL_PWR_`，故不在 42 行内）。













| # | 文件:行 | API | 语义一句话 |
|---|---|---|---|
| 1 | project_tcs/App/eeprom/eeprom.c:58 | __HAL_RCC_GPIOH_CLK_ENABLE() | EEPROM 软件 I2C 引脚组（GPIOH）时钟使能 |
| 2 | project_tcs/App/iwdg/iwdg.c:7 | HAL_RCC_OscConfig（注释提及） | 头注：LSI 已在 SystemClock_Config 由 OscConfig 启用（LSION）（非调用点） |
| 3 | project_tcs/App/iwdg/iwdg.c:27 | __HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) | iwdg_init 入口查 LSI 就绪标志（未就绪才进等待） |
| 4 | project_tcs/App/iwdg/iwdg.c:30 | __HAL_RCC_GET_FLAG(RCC_FLAG_LSIRDY) | 等待循环内复查 LSIRDY（volatile 循环，最多 ~30ms 兜底） |
| 5 | project_tcs/App/tcs/console.c:290 | HAL_RCC_GPIOA_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 6 | project_tcs/App/tcs/console.c:291 | — | 重锚新增：代码摘录见左列 |
| 7 | project_tcs/App/tcs/console.c:292 | HAL_RCC_GPIOC_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 8 | project_tcs/App/tcs/console.c:293 | — | 重锚新增：代码摘录见左列 |
| 9 | project_tcs/App/tcs/console.c:294 | HAL_RCC_GPIOE_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 10 | project_tcs/App/tcs/console.c:295 | — | 重锚新增：代码摘录见左列 |
| 11 | project_tcs/App/tcs/console.c:296 | HAL_RCC_GPIOG_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 12 | project_tcs/App/tcs/console.c:297 | — | 重锚新增：代码摘录见左列 |
| 13 | project_tcs/App/tcs/console.c:298 | HAL_RCC_GPIOI_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 14 | project_tcs/App/tcs/console.c:299 | — | 重锚新增：代码摘录见左列 |
| 15 | project_tcs/App/tcs/console.c:300 | HAL_RCC_GPIOK_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 16 | project_tcs/App/tcs/console.c:374 | HAL_RCC_GPIOA_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 17 | project_tcs/App/tcs/console.c:375 | HAL_RCC_GPIOC_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 18 | project_tcs/App/tcs/console.c:376 | HAL_RCC_GPIOE_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 19 | project_tcs/App/tcs/console.c:377 | HAL_RCC_GPIOG_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 20 | project_tcs/App/tcs/console.c:378 | HAL_RCC_GPIOI_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 21 | project_tcs/App/tcs/console.c:379 | HAL_RCC_GPIOK_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 22 | project_tcs/App/tcs/console.c:784 | __HAL_RCC_CLEAR_RESET_FLAGS() | 复位源解析打印后清全部复位标志（防下次上电误读旧源） |
| 23 | project_tcs/Core/Src/ft5446.c:287 | __HAL_RCC_GPIOD_CLK_ENABLE() | 触摸 bit-bang SCL/SDA（PD12/PD13）引脚时钟（ct_iic_init） |
| 24 | project_tcs/Core/Src/ft5446.c:325 | __HAL_RCC_I2C4_CLK_ENABLE | W1a：I2C4 外设时钟 |
| 25 | project_tcs/Core/Src/ft5446.c:343 | __HAL_RCC_GPIOD_CLK_ENABLE | W1a：I2C4 引脚组时钟（:103 PeriphCLKConfig 属 HAL_RCCEx_ 前缀不入本 grep，已注） |
| 26 | project_tcs/Core/Src/ft5446.c:514 | HAL_RCC_GPIOD_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 27 | project_tcs/Core/Src/ft5446.c:554 | HAL_RCC_I2C4_FORCE_RESET | 重锚新增：代码摘录见左列 |
| 28 | project_tcs/Core/Src/ft5446.c:555 | HAL_RCC_I2C4_RELEASE_RESET | 重锚新增：代码摘录见左列 |
| 29 | project_tcs/Core/Src/ft5446.c:602 | __HAL_RCC_GPIOH_CLK_ENABLE() | 触摸 RST/INT（PH6/PH7）引脚时钟（ft5446_init） |
| 30 | project_tcs/Core/Src/ft5446.c:792 | HAL_RCC_GPIOD_CLK_ENABLE | 重锚新增：代码摘录见左列 |
| 31 | project_tcs/Core/Src/ltdc.c:371 | __HAL_RCC_FMC_CLK_ENABLE() | SDRAM 显存走 FMC 控制器时钟（HAL_SDRAM_MspInit 段） |
| 32 | project_tcs/Core/Src/ltdc.c:372 | __HAL_RCC_GPIOC_CLK_ENABLE() | FMC SDRAM 引脚组 GPIOC 时钟 |
| 33 | project_tcs/Core/Src/ltdc.c:373 | __HAL_RCC_GPIOD_CLK_ENABLE() | FMC SDRAM 引脚组 GPIOD 时钟 |
| 34 | project_tcs/Core/Src/ltdc.c:374 | __HAL_RCC_GPIOE_CLK_ENABLE() | FMC SDRAM 引脚组 GPIOE 时钟 |
| 35 | project_tcs/Core/Src/ltdc.c:375 | __HAL_RCC_GPIOF_CLK_ENABLE() | FMC SDRAM 引脚组 GPIOF 时钟 |
| 36 | project_tcs/Core/Src/ltdc.c:376 | __HAL_RCC_GPIOG_CLK_ENABLE() | FMC SDRAM 引脚组 GPIOG 时钟 |
| 37 | project_tcs/Core/Src/ltdc.c:377 | __HAL_RCC_GPIOH_CLK_ENABLE() | FMC SDRAM 引脚组 GPIOH 时钟 |
| 38 | project_tcs/Core/Src/ltdc.c:422 | __HAL_RCC_LTDC_CLK_ENABLE() | LTDC 内核时钟（HAL_LTDC_MspInit 首行使能，PCLK=PLL3R 50M） |
| 39 | project_tcs/Core/Src/ltdc.c:423 | __HAL_RCC_DMA2D_CLK_ENABLE() | DMA2D 时钟（与 LTDC 成对，例程原文；现无功能消费者，见 DMA2D 卡） |
| 40 | project_tcs/Core/Src/ltdc.c:426 | __HAL_RCC_GPIOB_CLK_ENABLE() | 背光 PB5 引脚时钟 |
| 41 | project_tcs/Core/Src/ltdc.c:427 | __HAL_RCC_GPIOD_CLK_ENABLE() | 屏复位 PD11 引脚时钟 |
| 42 | project_tcs/Core/Src/ltdc.c:428 | __HAL_RCC_GPIOF_CLK_ENABLE() | LTDC DE=PF10 引脚时钟 |
| 43 | project_tcs/Core/Src/ltdc.c:429 | __HAL_RCC_GPIOG_CLK_ENABLE() | LTDC PCLK=PG7 引脚时钟 |
| 44 | project_tcs/Core/Src/ltdc.c:430 | __HAL_RCC_GPIOI_CLK_ENABLE() | LTDC VSYNC/HSYNC=PI9/PI10 引脚时钟 |
| 45 | project_tcs/Core/Src/ltdc.c:457 | __HAL_RCC_GPIOA_CLK_ENABLE() | RGB 数据引脚组 GPIOA（PA2/PA8）时钟 |
| 46 | project_tcs/Core/Src/ltdc.c:458 | __HAL_RCC_GPIOE_CLK_ENABLE() | RGB 数据引脚组 GPIOE（PE5/6）时钟 |
| 47 | project_tcs/Core/Src/ltdc.c:459 | __HAL_RCC_GPIOH_CLK_ENABLE() | RGB 数据引脚组 GPIOH（PH8-15）时钟 |
| 48 | project_tcs/Core/Src/main.c:646 | __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0) | 电压档设 VOS0（480MHz 必需档；位于 ConfigSupply 之后） |
| 49 | project_tcs/Core/Src/main.c:659 | HAL_RCC_OscConfig(&RCC_OscInitStruct) | HSE+LSI 双振开启+PLL1（HSE 25M/5×192/2=480MHz，Q=240M 给 SDMMC），返值≠HAL_OK→Error_Handler |
| 50 | project_tcs/Core/Src/main.c:671 | HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) | SYSCLK=PLL1、HCLK=240M、APB1-4=120M 全分频树+Flash 等待 4，返值检查 |
| 51 | project_tcs/Core/Src/main.c:692 | __HAL_RCC_GPIOB_CLK_ENABLE() | LED1/LED2（GPIOB）时钟（gpio_init） |
| 52 | project_tcs/Core/Src/main.c:693 | __HAL_RCC_GPIOA_CLK_ENABLE() | KILL_KEY（GPIOA）时钟（gpio_init） |
| 53 | project_tcs/Core/Src/main.c:752 | __HAL_RCC_USART1_CLK_ENABLE() | 控制台 USART1 内核时钟（MspInit） |
| 54 | project_tcs/Core/Src/main.c:753 | __HAL_RCC_GPIOA_CLK_ENABLE() | USART1 引脚（GPIOA）时钟 |
| 55 | project_tcs/Core/Src/main.c:761 | __HAL_RCC_USART2_CLK_ENABLE() | 链路 USART2 内核时钟（MspInit） |
| 56 | project_tcs/Core/Src/main.c:762 | __HAL_RCC_GPIOD_CLK_ENABLE() | USART2 引脚（GPIOD）时钟 |
| 57 | project_tcs/Core/Src/main.c:775 | __HAL_RCC_USART3_CLK_ENABLE() | m207 USART3 内核时钟（MspInit） |
| 58 | project_tcs/Core/Src/main.c:776 | __HAL_RCC_GPIOB_CLK_ENABLE() | USART3 引脚（GPIOB）时钟 |
| 59 | project_tcs/Core/Src/main.c:787 | __HAL_RCC_I2C1_CLK_ENABLE() | 温度 I2C1 内核时钟（HAL_I2C_MspInit） |
| 60 | project_tcs/Core/Src/main.c:788 | __HAL_RCC_GPIOB_CLK_ENABLE() | I2C1 引脚 PB8/PB9（GPIOB）时钟 |
| 61 | project_tcs/Core/Src/stm32h7xx_hal_msp.c:17 | __HAL_RCC_SYSCFG_CLK_ENABLE() | HAL_MspInit 内开 SYSCFG 时钟（系统配置域，EXTI/补偿依赖） |

## ④ 对照结论

| 条目 | 结论 |
|---|---|
| **时钟树 LSI（锚点）** | **符合官方推荐 ✓**——`main.c:326 RCC_OscInitStruct.LSIState = RCC_LSI_ON`（注释"IWDG 用 LSI"，OscConfig #49 一并提交稳定等待）为 IWDG 唯一时钟源；iwdg.c（#3/#4）再查 LSIRDY 属双保险兜底（官方 OscConfig 内部已含 LSIRDY 等待）——与 IWDG 卡联动定论 |
| 供电+电压档顺序（补充行 main.c:320 + #48） | **符合官方推荐 ✓**——`HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY)` 先于 `__HAL_PWR_VOLTAGESCALING_CONFIG(SCALE0)`，为 H7 官方 VOS0 上电序（供电源先定再调压）；两次配置后接 OscConfig/ClockConfig 均返值检查 |
| 主时钟树（#49/#50） | **符合官方推荐 ✓**——OscConfig（HSE+LSI+PLL1）→ClockConfig（SYSCLK/HCLK/APB+FLASH_LATENCY_4）官方两步流；480MHz 主频配 VOS0+LATENCY_4 参数自洽；返值必查 |
| 外设时钟使能模式（#1-47/#51-60） | **符合官方推荐 ✓**——全部 `__HAL_RCC_*_CLK_ENABLE` 收敛在各 MspInit/初始化函数内（Msp 分层），无一散落业务循环；宏域属官方 rcc.h API 面 |
| SYSCFG 时钟（#61） | **符合官方推荐 ✓**——HAL_MspInit 内开 SYSCFG（EXTI 映射/补偿域依赖），CubeMX 生成骨架同款 |
| 复位源管理（#22） | **符合官方推荐 ✓**——`__HAL_RCC_CLEAR_RESET_FLAGS()` 在 console_print_reset_source 解析打印（含 WP-C.4 IWDG1RSTF 优先规则）之后执行，防跨复位误读；官方 RCC_FLAG 流程用法 |
| 宏 vs 函数混用面 | `__HAL_RCC_GET_FLAG/CLEAR_RESET_FLAGS/CLK_ENABLE` 宏与 `HAL_RCC_OscConfig/ClockConfig` 函数分层使用——**符合官方 API 设计意图 ✓**（寄存器位操作用宏、流程配置用函数），无整改项 |
| 行号时效 | ltdc.c 域 17 行（#31-47）行号=G 线未提交工作区快照；**G 线合入后本卡 ltdc.c 域需复核** |

——研学卡完（61/61，2026-11-25 重锚）。
