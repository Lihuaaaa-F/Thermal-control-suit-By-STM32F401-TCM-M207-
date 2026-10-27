# 研学卡：HAL_LTDC（LCD-TFT 显示控制器 · 1024×600 RGB565 直驱）

> 任务书 v13.4 §7b W8 · 制卡 2026-10-27 · 基线=git HEAD `43e5f4b` + **G 线未提交工作区**（ltdc.c/ltdc.h/main.c/CMakeLists/flash.ld 均为 M 态；App/lvgl_port/、third_party/ 未跟踪——本卡 ltdc.c 行号即 G 线工作区快照，G 线合入后需复核）
> 门禁：③ 节表行数 == grep 命中数（本卡 **6 行 == 6 命中** ✓）

## ① 官方文档与版本章节

- **随库官方源（API 语义权威）**：`project_tcs/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_ltdc.c`
  - 头注 `@file`（MCD Application Team）；**L26 起 "How to use this driver" 用法章节**（Init→ConfigLayer 流程、层参数、同步重载语义）；全文件 144 处 `@brief/@param` 函数头注（本卡 grep 实测计数）；
  - 关键函数头注：`HAL_LTDC_Init`（MspInit 回调+返回值域）、`HAL_LTDC_ConfigLayer`（层窗/像素格式/起始地址/行步进 CFBAR/CFBLR/CFBLNR）、`HAL_LTDC_SetAddress`/`HAL_LTDC_ReLoad`（shadow 寄存器+IMR/VBR 立即/垂直消隐重载——本工程 flip 的寄存器直写即此语义，见 ④）。
- **扩展 API**：`Src/stm32h7xx_hal_ltdc_ex.c`（9 处函数头注：重载参数/双 LTDC 域，本工程未用）。
- **API 声明全集**：`Inc/stm32h7xx_hal_ltdc.h`（LTDC_HandleTypeDef/LTDC_LayerCfgTypeDef/LTDC_LAYER_1 等符号）。
- **版本**：随库 CMSIS Device H7 = **V1.10.7**（`Drivers/CMSIS/Device/ST/STM32H7xx/Release_Notes.html` 首行 "V1.10.7 / 04-February-2026"）——W9 依赖清单在案；HAL=vendor 裁剪快照无版本宏，锁=git tree hash `7dca0af2…`（W9）。

## ② 官方示例文件路径

- 本机探查：`find /d -maxdepth 2 -iname "STM32CubeH7" 2>/dev/null` → **空输出（无命中）**。
- **本机无 CubeH7 例程仓**；官方例程位置=GitHub `STMicroelectronics/STM32CubeH7` 仓
  `Projects/STM32H743I-EVAL/Examples/LTDC/`（URL：https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/LTDC ）。
  其下子例程名本机无仓未枚举（防编造），以仓库目录为准。
- 本工程在案对照件（如实区分）：ltdc.c 头注声明——**SDRAM 时序/引脚、LTDC 时序/引脚、PLL3、MPU 配置全部抄"实验12 LTDC LCD RGB 屏"例程原文**（板卡课程例程，非 ST CubeH7 仓；屏=HX8282，ID=0x7016 分支，1024×600 纯 RGB 直驱无初始化命令）。

## ③ 我们全部调用面清单

复现命令（仓库根 `/d/Espressif/ws/tcs`，Git Bash；raw 输出路径分隔符为 `\`，表内归一为 `/`）：

```bash
grep -rnE "HAL_LTDC_" project_tcs/App project_tcs/Core/Src --include=*.c
# → 6 命中（2026-10-27 实测），下表 6 行逐一对应
```

vendor 域说明：`App/rtt/`（SEGGER RTT vendor 五文件）同一 pattern 单独跑=**0 命中**（无需列行）。
补充（pattern 外，供审计）：帧切换用 LTDC 影子寄存器直写，`grep -rnE "LTDC->|LTDC_Layer1" project_tcs/Core/Src/ltdc.c` → 5 命中（L49/L50/L52/L530/L531：`LTDC_Layer1->CFBAR=…` + `LTDC->SRCR=LTDC_SRCR_VBR` 及守护循环）。

| # | 文件:行 | API | 语义一句话 |
|---|---|---|---|
| 1 | project_tcs/Core/Src/ltdc.c:11 | HAL_LTDC_MspInit（注释提及） | 头注：复位 PD11 配置抄 HAL_LTDC_MspInit 原文（非调用点） |
| 2 | project_tcs/Core/Src/ltdc.c:222 | HAL_LTDC_Init(&hltdc) | LTDC 主初始化（时序参数抄例程 0x7016 分支原文），返值 ≠HAL_OK 返 -1 |
| 3 | project_tcs/Core/Src/ltdc.c:240 | HAL_LTDC_ConfigLayer(&hltdc, &layer, LTDC_LAYER_1) | 层 1 配置（窗/RGB565/显存地址/行步进），返值 ≠HAL_OK 返 -1 |
| 4 | project_tcs/Core/Src/ltdc.c:269 | HAL_LTDC_MspInit（注释提及） | 注释：PD11 在 MspInit 内配成推挽输出（非调用点） |
| 5 | project_tcs/Core/Src/ltdc.c:355 | HAL_LTDC_MspInit（注释提及） | 注释：本函数抄官方例程 ltdc.c 的 HAL_LTDC_MspInit 原文（非调用点） |
| 6 | project_tcs/Core/Src/ltdc.c:356 | HAL_LTDC_MspInit（用户实现） | LTDC+DMA2D 时钟使能与 RGB/控制/背光/复位引脚 AF 配置（详见 GPIO 卡 #27-44） |

## ④ 对照结论

| 条目 | 结论 |
|---|---|
| Init→ConfigLayer→MspInit 三件套（#2/#3/#6） | **符合官方推荐 ✓**——官方 How-to-use 流程（Msp 回调由 Init 触发、层配置后自重载）；两次调用均返值检查 |
| 时序/引脚数值面 | **符合所抄例程原文 ✓**——HSync/VSync/累计消隐/Total、PLL3 50M PCLK、SDRAM 时序全部照抄不推算（ltdc.c 头注 4+agent 核实声明） |
| 帧切换 CFBAR/SRCR 寄存器直写（补充面 L49-52/L530-531） | **偏差+理由**——未走 `HAL_LTDC_SetAddress`+`HAL_LTDC_ReLoad`：直写 `LTDC_Layer1->CFBAR`+`LTDC->SRCR=VBR` 与官方两函数语义逐位等价（写 shadow CFBAR+垂直消隐重载防撕裂），WP-C.5 零拷贝真双缓冲路径；带 guard 循环防 SRCR 等待卡死。等价性成立=留档保留，不整改 |
| 双缓冲/防撕裂（WP-C.5、G4 DIRECT） | **符合官方 VBR 重载语义 ✓**——flip 全部落在垂直消隐期，无撕裂 |
| **G 线 PARTIAL 改造在途** | **未提交区注记（务必随卡）**——G 线正改 LTDC/LVGL 局部刷新（PARTIAL）：工作区 M=ltdc.c/ltdc.h/main.c/CMakeLists/stm32h743xx_flash.ld，未跟踪=App/lvgl_port/、third_party/lvgl（LVGL 9.6.0 暂态锁，W9 清单行 15）；**本卡行号与调用面=G 线工作区快照，G 线合入 commit 后本卡 ③ 必须重跑重录**（LVGL flush 面可能新增 HAL_LTDC_/寄存器调用） |
| SDRAM 显存（FMC 域） | SDRAM 走 HAL_SDRAM 系（本卡 pattern 外），引脚/时钟面已在 GPIO/PWR-RCC 卡覆盖；gfx_* 经显存指针直写=设计现状 ✓ |
| 行号时效 | ltdc.c 全域行号随 G 线未提交状态浮动，复核门禁以 ③ 节命令复跑为准 |

——研学卡完（6/6）。
