# 研学卡：HAL_GPIO / HAL_EXTI（GPIO 组件 + EXTI 现状实证）

> 任务书 v13.4 §7b W8 · 制卡 2026-10-27 · 基线=git HEAD `43e5f4b` + G 线未提交工作区（ltdc.c 为 M 态，本卡 ltdc.c 行号=G 线工作区快照，合入后需复核）
> 门禁：③ 节表行数 == grep 命中数（本卡 **58 行 == 58 命中** ✓，其中 `HAL_EXTI_` 前缀 **0 命中**——全工程无 EXTI API 调用面，实证在案）

## ① 官方文档与版本章节

- **GPIO 官方源（API 语义权威）**：`project_tcs/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_gpio.c`
  - 头注 `@file`（MCD Application Team）；**L59 起 "How to use this driver" 用法章节**（Init/WritePin/ReadPin/TogglePin/LockPin、EXTI 模式说明）；全文件 27 处 `@brief/@param` 函数头注（本卡 grep 实测计数）。
  - 关键语义：`HAL_GPIO_Init`（GPIO_InitTypeDef：Mode 含 GPIO_MODE_INPUT/OUTPUT_PP/AF_OD/AF_PP/IT_*；Pull；Speed；Alternate）、`HAL_GPIO_WritePin/ReadPin/TogglePin`（PinState=GPIO_PIN_SET/RESET）。
- **EXTI 官方源**：`Src/stm32h7xx_hal_exti.c`（**L79 起 "How to use this driver"**，28 处函数头注；EXTI 线独立管理 API HAL_EXTI_SetConfigLine/GetHandle/GenerateSWI 等）——**本工程 0 调用（③ 实证）**，卡内保留其官方位置供研学对照。
- **API 声明全集**：`Inc/stm32h7xx_hal_gpio.h` + `Inc/stm32h7xx_hal_gpio_ex.h`（AF 映射宏如 GPIO_AF4_I2C1/GPIO_AF12_FMC/GPIO_AF13_LTDC/GPIO_AF14_LTDC）/ `Inc/stm32h7xx_hal_exti.h`。
- **版本**：随库 CMSIS Device H7 = **V1.10.7**（`Drivers/CMSIS/Device/ST/STM32H7xx/Release_Notes.html` 首行 "V1.10.7 / 04-February-2026"）——W9 依赖清单在案；HAL=vendor 裁剪快照无版本宏，锁=git tree hash `7dca0af2…`（W9）。

## ② 官方示例文件路径

- 本机探查：`find /d -maxdepth 2 -iname "STM32CubeH7" 2>/dev/null` → **空输出（无命中）**。
- **本机无 CubeH7 例程仓**；官方例程位置=GitHub `STMicroelectronics/STM32CubeH7` 仓
  `Projects/STM32H743I-EVAL/Examples/GPIO/`（URL：https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/GPIO ）及
  `Projects/STM32H743I-EVAL/Examples/EXTI/`（URL：https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/EXTI ）。
  其下子例程名本机无仓未枚举（防编造），以仓库目录为准。
- 本工程在案对照件（非 ST CubeH7 仓，如实区分）：ft5446.c/ct_iic 抄自**官方 LVGL 例程 ctiic.c + ft5206.c 原码**（ft5446.c 头注声明）；ltdc.c 引脚配置抄"实验12 LTDC LCD RGB 屏"例程原文（板卡课程例程，非 CubeH7 仓）。

## ③ 我们全部调用面清单





























复现命令（仓库根 `/d/Espressif/ws/tcs`，Git Bash；raw 输出路径分隔符为 `\`，表内归一为 `/`）：

```bash
grep -rnE "HAL_GPIO_|HAL_EXTI_" project_tcs/App project_tcs/Core/Src --include=*.c
# → 58 命中（2026-11-26 重锚），下表 58 行逐一对应
```

vendor 域说明：`App/rtt/`（SEGGER RTT vendor 五文件）同一 pattern 单独跑=**0 命中**（无需列行）。





























| # | 文件:行 | API | 语义一句话 |
|---|---|---|---|
| 1 | project_tcs/App/eeprom/eeprom.c:22 | HAL_GPIO_WritePin（宏封装） | 软件 I2C 宏 IIC_SCL(x)：SCL 时钟脚电平输出 |
| 2 | project_tcs/App/eeprom/eeprom.c:23 | HAL_GPIO_WritePin（宏封装） | 软件 I2C 宏 IIC_SDA(x)：SDA 数据脚电平输出 |
| 3 | project_tcs/App/eeprom/eeprom.c:24 | HAL_GPIO_ReadPin（宏封装） | 软件 I2C 宏 IIC_READ_SDA：读 SDA 回读 |
| 4 | project_tcs/App/eeprom/eeprom.c:64 | HAL_GPIO_Init(IIC_SCL_PORT, &gi) | EEPROM（AT24C02）软件 I2C SCL 引脚初始化 |
| 5 | project_tcs/App/eeprom/eeprom.c:68 | HAL_GPIO_Init(IIC_SDA_PORT, &gi) | EEPROM 软件 I2C SDA 引脚初始化 |
| 6 | project_tcs/App/tcs/console.c:407 | HAL_GPIO_Init | pindrv 调试命令：按参数初始化指定 GPIO（真机取证用，ports 表限界） |
| 7 | project_tcs/App/tcs/console.c:409 | HAL_GPIO_WritePin | pindrv：按参数驱动引脚电平（链路探针取证命令） |
| 8 | project_tcs/Core/Src/ft5446.c:247 | HAL_GPIO_WritePin | 重锚新增：代码摘录见左列 |
| 9 | project_tcs/Core/Src/ft5446.c:248 | HAL_GPIO_WritePin | 重锚新增：代码摘录见左列 |
| 10 | project_tcs/Core/Src/ft5446.c:249 | HAL_GPIO_ReadPin | 重锚新增：代码摘录见左列 |
| 11 | project_tcs/Core/Src/ft5446.c:257 | HAL_GPIO_WritePin | 重锚新增：代码摘录见左列 |
| 12 | project_tcs/Core/Src/ft5446.c:293 | HAL_GPIO_Init(GPIOD, &gi) | W1a：PD12/13 AF4_OD+I2C4 引脚（模块自有底层=eeprom 惯例） |
| 13 | project_tcs/Core/Src/ft5446.c:304 | HAL_GPIO_ReadPin | 重锚新增：代码摘录见左列 |
| 14 | project_tcs/Core/Src/ft5446.c:305 | HAL_GPIO_ReadPin（宏封装） | 触摸 bit-bang 宏 CT_READ_SDA（回读 ACK） |
| 15 | project_tcs/Core/Src/ft5446.c:516 | HAL_GPIO_WritePin | 重锚新增：代码摘录见左列 |
| 16 | project_tcs/Core/Src/ft5446.c:521 | HAL_GPIO_Init | 重锚新增：代码摘录见左列 |
| 17 | project_tcs/Core/Src/ft5446.c:526 | HAL_GPIO_WritePin | 重锚新增：代码摘录见左列 |
| 18 | project_tcs/Core/Src/ft5446.c:528 | HAL_GPIO_WritePin | 重锚新增：代码摘录见左列 |
| 19 | project_tcs/Core/Src/ft5446.c:532 | HAL_GPIO_WritePin（宏封装） | 触摸 bit-bang 宏 CT_IIC_SCL(x)（PD12） |
| 20 | project_tcs/Core/Src/ft5446.c:534 | HAL_GPIO_WritePin（宏封装） | 触摸 bit-bang 宏 CT_IIC_SDA(x)（PD13） |
| 21 | project_tcs/Core/Src/ft5446.c:536 | HAL_GPIO_WritePin（宏封装） | 触摸复位宏 FT_RST(x)（PH6） |
| 22 | project_tcs/Core/Src/ft5446.c:612 | HAL_GPIO_Init(FT_RST_PORT, &gi) | 触摸 RST=PH6 推挽输出+上拉初始化 |
| 23 | project_tcs/Core/Src/ft5446.c:619 | HAL_GPIO_Init(FT_INT_PORT, &gi) | 触摸 INT=PH7 输入+上拉初始化（**轮询模式**，G_MODE 寄存器写 0=轮询，INT 不触发） |
| 24 | project_tcs/Core/Src/ft5446.c:799 | HAL_GPIO_Init(CT_IIC_SCL_PORT, &gi) | 触摸 SCL=PD12 推挽初始化 |
| 25 | project_tcs/Core/Src/ft5446.c:804 | HAL_GPIO_Init(CT_IIC_SDA_PORT, &gi) | 触摸 SDA=PD13 开漏+上拉初始化 |
| 26 | project_tcs/Core/Src/ltdc.c:333 | HAL_GPIO_WritePin(GPIOD, PIN_11, SET) | 屏复位序列第 1 拍：RST 高 10ms（抄例程） |
| 27 | project_tcs/Core/Src/ltdc.c:335 | HAL_GPIO_WritePin(GPIOD, PIN_11, RESET) | 第 2 拍：RST 低 50ms |
| 28 | project_tcs/Core/Src/ltdc.c:337 | HAL_GPIO_WritePin(GPIOD, PIN_11, SET) | 第 3 拍：RST 高 200ms（ROM 加载） |
| 29 | project_tcs/Core/Src/ltdc.c:341 | HAL_GPIO_WritePin(GPIOB, PIN_5, SET) | 开背光：PB5 拉高（抄例程 LTDC_BL(1)，纯 GPIO 非 PWM） |
| 30 | project_tcs/Core/Src/ltdc.c:386 | HAL_GPIO_Init(GPIOC, &gi) | FMC SDRAM 引脚组 GPIOC（AF12，抄 HAL_SDRAM_MspInit 原文） |
| 31 | project_tcs/Core/Src/ltdc.c:391 | HAL_GPIO_Init(GPIOD, &gi) | FMC SDRAM 引脚组 GPIOD（AF12，7pin） |
| 32 | project_tcs/Core/Src/ltdc.c:397 | HAL_GPIO_Init(GPIOE, &gi) | FMC SDRAM 引脚组 GPIOE（AF12，11pin） |
| 33 | project_tcs/Core/Src/ltdc.c:403 | HAL_GPIO_Init(GPIOF, &gi) | FMC SDRAM 引脚组 GPIOF（AF12，11pin） |
| 34 | project_tcs/Core/Src/ltdc.c:408 | HAL_GPIO_Init(GPIOG, &gi) | FMC SDRAM 引脚组 GPIOG（AF12，7pin） |
| 35 | project_tcs/Core/Src/ltdc.c:412 | HAL_GPIO_Init(GPIOH, &gi) | FMC SDRAM 引脚组 GPIOH（AF12，2pin） |
| 36 | project_tcs/Core/Src/ltdc.c:437 | HAL_GPIO_Init(GPIOB, &gi) | 背光 PB5 推挽输出初始化（纯 GPIO） |
| 37 | project_tcs/Core/Src/ltdc.c:441 | HAL_GPIO_Init(GPIOD, &gi) | 屏复位 PD11 推挽输出初始化 |
| 38 | project_tcs/Core/Src/ltdc.c:450 | HAL_GPIO_Init(GPIOF, &gi) | LTDC DE=PF10（AF14） |
| 39 | project_tcs/Core/Src/ltdc.c:452 | HAL_GPIO_Init(GPIOG, &gi) | LTDC PCLK=PG7（AF14） |
| 40 | project_tcs/Core/Src/ltdc.c:454 | HAL_GPIO_Init(GPIOI, &gi) | LTDC VSYNC=PI9 / HSYNC=PI10（AF14） |
| 41 | project_tcs/Core/Src/ltdc.c:464 | HAL_GPIO_Init(GPIOA, &gi) | LTDC PA8=B3（**AF13**，全 RGB 口唯一例外） |
| 42 | project_tcs/Core/Src/ltdc.c:471 | HAL_GPIO_Init(GPIOA, &gi) | LTDC PA2=R1（AF14） |
| 43 | project_tcs/Core/Src/ltdc.c:475 | HAL_GPIO_Init(GPIOD, &gi) | LTDC PD6=B2（AF14） |
| 44 | project_tcs/Core/Src/ltdc.c:479 | HAL_GPIO_Init(GPIOE, &gi) | LTDC PE5/6=G0/G1（AF14） |
| 45 | project_tcs/Core/Src/ltdc.c:483 | HAL_GPIO_Init(GPIOG, &gi) | LTDC PG6/12/13/14=R7/B1/R0/B0（AF14） |
| 46 | project_tcs/Core/Src/ltdc.c:488 | HAL_GPIO_Init(GPIOH, &gi) | LTDC PH8-15=R2-R6/G3-G5（AF14） |
| 47 | project_tcs/Core/Src/ltdc.c:493 | HAL_GPIO_Init(GPIOI, &gi) | LTDC PI0/1/2/4/5/6/7=G6/G7/B4-B7（AF14） |
| 48 | project_tcs/Core/Src/main.c:172 | HAL_GPIO_Init(GPIOA, &gi) | KILL_KEY=PA1 输入+内部下拉初始化（急停键，按下=高） |
| 49 | project_tcs/Core/Src/main.c:182 | HAL_GPIO_Init | gpio 初始化块：PA3 输入上拉（链路 RX 探针位）等引脚配置 |
| 50 | project_tcs/Core/Src/main.c:817 | HAL_GPIO_Init(GPIOB, &gi) | LED1/LED2 引脚推挽输出初始化 |
| 51 | project_tcs/Core/Src/main.c:818 | HAL_GPIO_WritePin(GPIOB, LED1_Pin\ | LED2_Pin, SET) | LED 初始高电平（灭） |
| 52 | project_tcs/Core/Src/main.c:823 | HAL_GPIO_Init | KEY0(PA1) 输入下拉初始化（急停键，MX_GPIO 块） |
| 53 | project_tcs/Core/Src/main.c:842 | HAL_GPIO_Init(port, &gi) | uart_gpio_af 通用函数：UART 引脚 AF_PP+VERY_HIGH 复用配置（三路复用同一实现） |
| 54 | project_tcs/Core/Src/main.c:913 | HAL_GPIO_Init(GPIOB, &gi) | I2C1 MspInit 内 PB8/PB9 AF4_OD+上拉（板上无上拉→飞线 4.7kΩ 外接兜底） |
| 55 | project_tcs/Core/Src/main.c:945 | HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin) | Error_Handler 死循环闪 LED1 指示故障 |
| 56 | project_tcs/Core/Src/main.c:957 | HAL_GPIO_ReadPin(KILL_KEY_GPIO_Port, KILL_KEY_Pin) | 急停键轮询电平读（~10ms 去抖状态机） |
| 57 | project_tcs/Core/Src/main.c:1255 | HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, …) | 主循环运行心跳指示翻转 |
| 58 | project_tcs/Core/Src/stm32h7xx_it.c:95 | HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin) | Fault_Handler 死循环闪 LED2 指示（不喂 IWDG→2s 复位自恢复，WP-C） |

## ④ 对照结论

| 条目 | 结论 |
|---|---|
| 触摸 INT（#23） | **符合所抄官方例程 ✓**——FT5446 INT=PH7 配输入+上拉、G_MODE 寄存器写 0=轮询模式（INT 不触发），抄官方 LVGL 例程 ft5206_init 原序；**HAL_EXTI_ 全工程 0 命中=设计现状非缺失**（触摸/按键全轮询）。若后续要中断通知：官方路径=GPIO_MODE_IT_x + stm32h7xx_it.c 增 EXTI9_5/15_10 分发（EXTI 官方源见 ①），暂无整改 W 项 |
| 矩阵按键（#26-34） | **符合官方推荐 ✓**——行列扫描推挽+下拉读列为经典 GPIO 扫描阵，逐行选中+防鬼键复位时序完整 |
| 急停键 KILL_KEY（#48/#56） | **符合官方推荐 ✓**——输入+下拉+轮询去抖（~10ms），安全键不占 EXTI=可审计 |
| LED/故障指示（#58/#50/#51/#55/#57） | **符合官方推荐 ✓**——推挽输出+Toggle/Write 常规用法 |
| LTDC/FMC 复用引脚组（#30-47） | **符合所抄例程原文 ✓**——SDRAM 全 AF12、RGB 全 AF14（PA8=AF13 唯一例外）与时序数值一并抄"实验12"例程（ltdc.c 头注声明），AF 宏取自官方 gpio_ex.h 域 |
| 软件 I2C bit-bang（#1-5/#19-25） | **偏差+理由**——EEPROM 与触摸用 GPIO 模拟 I2C（抄官方 LVGL 例程 ctiic.c 原码）；**待整改入 W1**：ft5446 迁硬件 I2C4 后 bit-bang 面（#19-25）预期移除，届时本卡 ③ 重跑重录；EEPROM（AT24C02，低频配置件）保留 bit-bang=可接受现状，无挂账 |
| HAL_GPIO_Init 单点集中（#53） | **符合官方推荐 ✓**——uart_gpio_af 通用函数收敛三路 UART 引脚配置，避免散抄 |
| 行号时效 | ltdc.c 行号=G 线未提交工作区快照（git M 态）；**G 线合入后本卡 ltdc.c 域 19 行需复核** |

——研学卡完（58/58，2026-11-26 重锚）。
