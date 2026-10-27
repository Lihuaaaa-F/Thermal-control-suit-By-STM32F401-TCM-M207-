# 研学卡：HAL_I2C（I2C 组件 · tmp117 温度传感器通路）

> 任务书 v13.4 §7b W8 · 制卡 2026-10-27 · 基线=git HEAD `43e5f4b` + 工作区（G 线改动不触 I2C 域）
> 门禁：③ 节表行数 == grep 命中数（本卡 **10 行 == 10 命中** ✓（W1a 后含 ft5446 HAL I2C4 五调用点））

## ① 官方文档与版本章节

- **随库官方源（API 语义权威）**：`project_tcs/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_i2c.c`
  - 头注 `@file`（MCD Application Team）；**L25 起 "How to use this driver" 用法章节**（初始化流程、三种传输模式 Polling/IT/DMA 说明）；
  - 全文件 324 处 `@brief/@param` 函数头注（本卡 grep 实测计数）；
  - `HAL_I2C_Mem_Read` 函数级语义在同文件该函数头注：`DevAddress`=待移位到 bit7 的 7bit 地址（即调用方传 8bit 写地址）、`MemAddSize`、`Timeout` 单位 ms、返值 HAL_OK/HAL_BUSY/HAL_ERROR/HAL_TIMEOUT。
- **扩展 API**：`Src/stm32h7xx_hal_i2c_ex.c`（L36 起头注，19 处函数头注：FastMode Plus、模拟/数字滤波配置等，本工程未用）。
- **API 声明全集**：`Inc/stm32h7xx_hal_i2c.h`。
- **版本**：随库 CMSIS Device H7 = **V1.10.7**（`Drivers/CMSIS/Device/ST/STM32H7xx/Release_Notes.html` 首行 "V1.10.7 / 04-February-2026"，本卡 grep 实证）——W9 依赖清单在案（`docs/ref/依赖版本清单.md` ST HAL/CMSIS 行）。HAL 侧=vendor 裁剪快照，**无版本宏**（`Inc/stm32h7xx_hal.h` 无 `__STM32H7xx_HAL_VERSION`，本卡 grep 实测），锁=git tree hash `7dca0af2…`（W9）。

## ② 官方示例文件路径

- 本机探查：`find /d -maxdepth 2 -iname "STM32CubeH7" 2>/dev/null` → **空输出（无命中）**。
- **本机无 CubeH7 例程仓**；官方例程位置=GitHub `STMicroelectronics/STM32CubeH7` 仓
  `Projects/STM32H743I-EVAL/Examples/I2C/`（URL：https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/I2C ）。
  其下子例程名本机无仓未枚举（防编造），以仓库目录为准。
- 本工程在案对照件（非 ST 官方源，如实区分）：`esp_lcd_touch_ft5x06` 1.1.1（W9 清单行 19：不引入，仅 W1 寄存器时序对照参考件）。

## ③ 我们全部调用面清单

复现命令（仓库根 `/d/Espressif/ws/tcs`，Git Bash；raw 输出路径分隔符为 `\`，表内归一为 `/`）：

```bash
grep -rnE "HAL_I2C_" project_tcs/App project_tcs/Core/Src --include=*.c
# → 10 命中（HEAD ec93db0 锚定；含 W1a 五调用点），下表 10 行逐一对应
```

vendor 域说明：`App/rtt/`（SEGGER RTT vendor 五文件）同一 pattern 单独跑=**0 命中**（RTT 不触 HAL，无需列行）。

| # | 文件:行 | API | 语义一句话 |
|---|---|---|---|
| 1 | project_tcs/App/tcs/tmp117.c:5 | HAL_I2C_Mem_Read（注释提及） | 文件头注：声明本模块用 Mem_Read 读 0x00 寄存器 2B 大端（非调用点） |
| 2 | project_tcs/App/tcs/tmp117.c:111 | HAL_I2C_Mem_Read(s_i2c, TMP117_ADDR7<<1, TMP117_REG_DEVID, …, 20u) | 上电自检读 DEVID 寄存器，ADDR7<<1=8bit 写地址、超时 20ms、返值判 HAL_OK（W10 表 #12） |
| 3 | project_tcs/App/tcs/tmp117.c:147 | HAL_I2C_Mem_Read(…, TMP117_REG_TEMP, …, 20u) | 周期采样读温度寄存器，同款参数面（W10 表 #13） |
| 4 | project_tcs/Core/Src/main.c:437 | HAL_I2C_MspInit（用户实现回调） | I2C1 实例的 Msp 配置：开 I2C1+GPIOB 时钟，PB8/PB9 配 AF4_OD+上拉（板上无上拉→飞线 4.7kΩ，内部上拉兜底） |
| 5 | project_tcs/Core/Src/main.c:463 | HAL_I2C_Init(&hi2c_tmp117) | I2C1 句柄初始化（MX_I2C1_Init 内），返值 ≠HAL_OK 即 Error_Handler |

| 6 | project_tcs/Core/Src/ft5446.c:132 | HAL_I2C_Init(&hi2c4) | W1a：I2C4 初始化（Timing=官方计算器现算，PCLK4 内核） |
| 7 | project_tcs/Core/Src/ft5446.c:133 | HAL_I2CEx_ConfigAnalogFilter | W1a：模拟滤波开（官方 How-to-use 第 3 步） |
| 8 | project_tcs/Core/Src/ft5446.c:345 | HAL_I2C_Mem_Write | W1a：ft5446_wr_reg（官方 Mem_Write 模式，20ms 帽） |
| 9 | project_tcs/Core/Src/ft5446.c:369 | HAL_I2C_Mem_Read | W1a：ft5446_rd_reg（Mem_Read 序=写reg→Sr→读N；NAK 仅计 AF） |
| 10 | project_tcs/Core/Src/ft5446.c:389 | HAL_I2C_IsDeviceReady | W1a：ft5446_probe（DevAddress=8bit 形式，F1 勘正后） |

## ④ 对照结论

| 条目 | 结论 |
|---|---|
| Mem_Read 调用模式（#2/#3） | **符合官方推荐 ✓**——8bit 移位地址（ADDR7<<1）、MemAddSize=8BIT、Timeout=ms、返值必查；W10 表 #12-13 在案（"W1 ft5446 复制本模式"注记原文） |
| Init/MspInit 配对（#4/#5） | **符合官方推荐 ✓**——用户实现 Msp 回调+Init 返查，与官方 How-to-use 第 1-3 步（MSP→Init→可选 ConfigAnalogFilter）一致；未用项（模拟滤波）保持默认=官方默认即推荐 |
| 触摸 ft5446 传输层 | **W1a 已换 HAL I2C4（2026-10-27）**：rd_reg=Mem_Read/wr_reg=Mem_Write/probe=IsDeviceReady，复制 #2/#3 模式；Timing=ST 官方计算器 i2c_timing_utility.c（CubeH7@8ec3113d 零改动 vendor）以 PCLK4 实频现算——CubeMX GUI 路径本机缺 FW_H7 包不可达（两轮 CLI 日志在 project_tcs/cubemx/ 留档）；位bang 域 FT5446_BITBANG 宏保留一个版本期 |
| W1a I2C4 调用面补录 | **已落地**：底层在 ft5446_i2c4_init（模块自有，eeprom.c 同款惯例——HAL_I2C_MspInit 在 main.c 系 G 线未提交区不可触碰，G 线合入后迁 MspInit 分支=二期正常化）；③ 表补 W1a 行即本行（HAL_I2C_Mem_×2+IsDeviceReady+I2C_GetTiming 在 ft5446.c，nm 门禁 ct_iic=0 在案） |
| 线挂死/时钟拉伸等 H7 I2C 已知勘误 | 官方 ST 工勘（errata）与 HAL 修复随 HAL 版本走——本工程 HAL=裁剪快照无 tag（W9 GAP 注 1），升级=整树替换全量回归，挂 W9 协议处置（二期录 CubeH7 tag） |

——研学卡完（10/10，W1a 实更）。
