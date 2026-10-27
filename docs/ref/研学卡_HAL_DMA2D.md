# 研学卡：HAL_DMA2D（Chrom-Art 加速器 · 现状=仅时钟使能，功能面 0 调用）

> 任务书 v13.4 §7b W8 · 制卡 2026-10-27 · 基线=git HEAD `43e5f4b` + G 线未提交工作区（ltdc.c 为 M 态，本卡行号=G 线工作区快照）
> 门禁：③ 节表行数 == grep 命中数（本卡 **1 行 == 1 命中** ✓）

## ① 官方文档与版本章节

- **随库官方源（API 语义权威）**：`project_tcs/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_dma2d.c`
  - 头注 `@file`（MCD Application Team）；**L26 起 "How to use this driver" 用法章节**（Init→Start 轮询/IT/DMA 三模式、R2M/M2M/M2M+PFC 传输类型、CLUT）；全文件 117 处 `@brief/@param` 函数头注（本卡 grep 实测计数）；
  - 关键 API（**本工程全部未调用**，列此供研学）：HAL_DMA2D_Init/ConfigLayer/Start/Start_IT/PollForTransfer/RefreshLUT 等。
- **API 声明全集**：`Inc/stm32h7xx_hal_dma2d.h`。
- **版本**：随库 CMSIS Device H7 = **V1.10.7**（`Drivers/CMSIS/Device/ST/STM32H7xx/Release_Notes.html` 首行 "V1.10.7 / 04-February-2026"）——W9 依赖清单在案；HAL=vendor 裁剪快照无版本宏，锁=git tree hash `7dca0af2…`（W9）。

## ② 官方示例文件路径

- 本机探查：`find /d -maxdepth 2 -iname "STM32CubeH7" 2>/dev/null` → **空输出（无命中）**。
- **本机无 CubeH7 例程仓**；官方例程位置=GitHub `STMicroelectronics/STM32CubeH7` 仓
  `Projects/STM32H743I-EVAL/Examples/DMA2D/`（URL：https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/DMA2D ）。
  其下子例程名本机无仓未枚举（防编造），以仓库目录为准。
- 本工程在案对照件：无（本组件无本地例程抄录源；历史试验记录见 ltdc.c L438-442/L589-592 注释，见 ④）。

## ③ 我们全部调用面清单

复现命令（仓库根 `/d/Espressif/ws/tcs`，Git Bash；raw 输出路径分隔符为 `\`，表内归一为 `/`）：

```bash
grep -rnE "DMA2D_" project_tcs/App project_tcs/Core/Src --include=*.c
# → 1 命中（2026-10-27 实测），下表 1 行逐一对应
```

vendor 域说明：`App/rtt/`（SEGGER RTT vendor 五文件）同一 pattern 单独跑=**0 命中**（无需列行）。
补充（pattern 外，供审计）：
- `grep -rnE "HAL_DMA2D_" project_tcs/App project_tcs/Core/Src --include=*.c` → **0 命中**（功能 API 零调用实证）；
- `grep -rnE "DMA2D->" project_tcs/App project_tcs/Core/Src --include=*.c` → **0 命中**（寄存器直写路径也已清零）。

| # | 文件:行 | API | 语义一句话 |
|---|---|---|---|
| 1 | project_tcs/Core/Src/ltdc.c:360 | __HAL_RCC_DMA2D_CLK_ENABLE()（宏） | 在用户实现的 HAL_LTDC_MspInit 内开 DMA2D 内核时钟（跟随例程原文保留；当前无消费者） |

## ④ 对照结论

| 条目 | 结论 |
|---|---|
| 时钟使能位置（#1） | **符合所抄例程原文 ✓**——MspInit 内 `__HAL_RCC_LTDC_CLK_ENABLE()+__HAL_RCC_DMA2D_CLK_ENABLE()` 成对（例程原文位置）；时钟常开无功能调用=无害现状 |
| **HAL_DMA2D_ 功能面=0（补充面实证）** | **偏差+理由（有档定论，非欠账）**——ltdc.c L438-442（WP-C.5 终版注释）：**填充改走 CPU 写穿透 32 位 store，不再用 DMA2D**，依据=① 本片 DMA2D 多行 R2M 每行步进 +32px（斜带漏色，WP-A.2d 硬件异常）；② DMA2D 小搬运每次 ~18µs 固定开销（LTDC 抢占总线），整页重绘 ~85ms 超预算。L589-592 复记：M2M 同样 +32px/行+小搬运开销不可接受。gfx_fill_rect/gfx_clear 现为 CPU 逐行 32 位写（本卡 grep `DMA2D->`=0 实证）。**定论=弃用合理，留档保留** |
| 头注 L14 与实现不一致 | **文档债（非代码债）**——ltdc.c 头注 L14 "DMA2D R2M：gfx_clear/gfx_fill_rect 抄 ltdc_fill 寄存器直写（§0C.2）"为历史期描述，与现实现（CPU 写）不符；**待整改：随 G 线合入时同步修订该头注行**（G 线正在改 ltdc.c，顺带清账） |
| G 线 PARTIAL 改造在途 | **未提交区注记**——G 线局部刷新改造（LVGL 9.6.0，App/lvgl_port/+third_party/ 未跟踪，详见 LTDC 卡 ④）；LVGL 软件渲染路径下 DMA2D 仍无调用面；G 线合入后若引入/移除加速路径，本卡 ③ 重跑重录 |
| 复用评估（二期可选） | 若 WP-A.2d 步进异常在日后勘误/换片消除，M2M 搬运可按官方 Start+PollForTransfer 模式重评估——现无 W 号挂账，仅在案备忘 |

——研学卡完（1/1）。
