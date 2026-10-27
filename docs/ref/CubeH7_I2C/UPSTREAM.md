# CubeH7 I2C 官方参考物料留档（W1 §2.4；W1a 落地，v2=红队 F4 整改版）

- 上游：https://github.com/STMicroelectronics/STM32CubeH7 **官方仓** master
- 锚定 commit：8ec3113d4039d30ba9dc2bf0cb6eefc756b48d06（2026-10-27 取）
- 许可核对（F12 补）：ST CubeH7 仓根 LICENSE.md = **BSD-3-Clause**（ST 版权行随各源文件头）；vendor 文件头版权段保留完整。

## 采用件（零改动逐字拷贝，SHA256 见 SHA256.txt）

| 文件 | 上游路径 | 作用 |
|---|---|---|
| **i2c_timing_utility.c/.h** | Projects/NUCLEO-H723ZG/Examples/I2C/I2C_TwoBoards_ComPolling/{Src,Inc}/ | **本工程 TIMINGR 唯一来源**：ST 官方时序计算代码，init 期以实际 PCLK4(120MHz)×目标速率（400k/100k）现算。聚合链入 Core/Src/ft5446.c（W4.2 同款；该 .c 严禁再入 CMake 源列表防重复定义） |
| I2C_TwoBoards_ComPolling_main.c | Projects/NUCLEO-H743ZI/Examples/I2C/I2C_TwoBoards_ComPolling/Src/ | 旁证参考（L39-42：100MHz 内核→400kHz=0x00901954 的 ST 静态值样例——**本工程内核 120MHz 与之不同，不采用其值**，仅证官方计算惯例） |
| I2C_TwoBoards_ComIT_main.c | 同上 ComIT | 同旁证（三文件同点同值） |
| I2C_TwoBoards_ComDMA_main.c | 同上 ComDMA | 同旁证 |

## 计算器在 120MHz 内核的独立复算值（红队 W1a 亲跑，W1b 上板对照基准）

| 目标 | TIMINGR | 实际速率 | 合规 |
|---|---|---|---|
| 400 kHz | **0x20E21F33** | 400.3 kHz | ✓ |
| 100 kHz | **0x70D04648** | 103.1 kHz | ✓ |

**使用约束**：I2C_GetTiming 内含静态累加器，每次启动只可调用一次（降档须重启或自行重置）；
同进程二次调用（先 400k 再 100k）会产出非合规值（红队反例 0x20E2BDC1）。

## 已废弃方案留档（红队 F4 勘正：v1 文案与本表相反）

- ❌ PLL3R=100MHz 复现 H743ZI 静态值——PLL3 已归 LTDC 50MHz（main.c:360），改则毁屏时钟。
- ❌ HSI 静态值——官方例程注释自相矛盾（"0x0020098E" vs define 0x00200817，"HSI=16MHz"系 F4 残留），出处不洁。
- ❌ CubeMX GUI/CLI——CLI 可载 .ioc（日志在 project_tcs/cubemx/）但代码生成需 FW_H7 包（本机仅 F1/F4），无头不可达；`tcs_i2c4.ioc` 仅为装载测试件（HSI 64MHz/0x00B03FDB），**非本工程数值来源**。
