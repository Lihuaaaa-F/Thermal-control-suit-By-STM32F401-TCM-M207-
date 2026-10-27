# 研学卡：HAL_IWDG（独立看门狗 · CMSIS 符号直写版，F15 豁免留档定论在案）

> 任务书 v13.4 §7b W8 · 制卡 2026-10-27 · 基线=git HEAD `43e5f4b` + G 线未提交工作区（ltdc.c 为 M 态，本卡 ltdc.c 行号=G 线工作区快照）
> 门禁：③ 节表行数 == grep 命中数（本卡 **28 行 == 28 命中** ✓）

## ① 官方文档与版本章节

- **随库官方源（API 语义权威）**：`project_tcs/Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_iwdg.h`
  - **本工程 Src/ 未拉 `stm32h7xx_hal_iwdg.c`**（`ls Src/` 实证无该文件；W10 表缩写注"本工程未拉 iwdc.c 源，Inc 头为语义权威"在案）；
  - 头注关键锚点（本卡 grep 实测行号）：**L111（宏 __HAL_IWDG_START 之注） `@brief Enable the IWDG peripheral`（=HAL_IWDG_Init）**、**L118（宏 __HAL_IWDG_RELOAD_COUNTER 之注） `@brief Reload IWDG counter with value defined in the reload register`（=HAL_IWDG_Refresh）**、**L162-186 IWDG Key Register BitMask 三键**（0xCCCC 启动/0x5555 解锁写 PR·RLR·WINR/0xAAAA 喂狗）——我们 iwdg.c 的寄存器序列即与此逐键对照；
  - 备选官方域：`Inc/stm32h7xx_ll_iwdg.h`（LL 版在库，寄存器级封装——与本项目 CMSIS 直写语义同层）。
- **CMSIS 设备头（实际使用的符号域）**：`Drivers/CMSIS/Device/ST/STM32H7xx/.../stm32h743xx.h` 的 `IWDG_TypeDef`（IWDG1->KR/PR/RLR/SR）与 `DBGMCU_APB4FZ1_DBG_IWDG1`——ST 官方符号，非裸地址。
- **版本**：随库 CMSIS Device H7 = **V1.10.7**（`Drivers/CMSIS/Device/ST/STM32H7xx/Release_Notes.html` 首行 "V1.10.7 / 04-February-2026"）——W9 依赖清单在案；HAL=vendor 裁剪快照无版本宏，锁=git tree hash `7dca0af2…`（W9）。

## ② 官方示例文件路径

- 本机探查：`find /d -maxdepth 2 -iname "STM32CubeH7" 2>/dev/null` → **空输出（无命中）**。
- **本机无 CubeH7 例程仓**；官方例程位置=GitHub `STMicroelectronics/STM32CubeH7` 仓
  `Projects/STM32H743I-EVAL/Examples/IWDG/`（URL：https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/IWDG ）。
  其下子例程名本机无仓未枚举（防编造），以仓库目录为准。
- 本工程在案对照件（库内证据，非例程）：`docs/evidence/WP6_iwdg_defect.md`（WP-C.1 定参与 32.7s 缺陷根因历史证，本卡 ls 实证在库）+ iwdg.c 头注官方查证块（F15 定论）。

## ③ 我们全部调用面清单

复现命令（仓库根 `/d/Espressif/ws/tcs`，Git Bash；raw 输出路径分隔符为 `\`，表内归一为 `/`）：

```bash
grep -rnE "IWDG" project_tcs/App project_tcs/Core/Src --include=*.c
# → 28 命中（2026-10-27 实测），下表 28 行逐一对应（含注释/字符串行，注明；大小写敏感，小写 iwdg_refresh 调用点不在本 pattern 内）
```

vendor 域说明：`App/rtt/`（SEGGER RTT vendor 五文件）同一 pattern 单独跑=**0 命中**（无需列行）。

| # | 文件:行 | API/符号 | 语义一句话 |
|---|---|---|---|
| 1 | project_tcs/App/iwdg/iwdg.c:2 | IWDG（注释提及） | 头注：iwdg.c=IWDG 独立看门狗（CMSIS 符号版，§0D/D3 债清偿）（非调用点） |
| 2 | project_tcs/App/iwdg/iwdg.c:5 | HAL_IWDG（注释提及） | 头注：不依赖 HAL_IWDG 驱动（工程未拉该源），寄存器经 CMSIS IWDG_TypeDef 访问（非调用点） |
| 3 | project_tcs/App/iwdg/iwdg.c:9 | IWDG1->KR/PR/RLR/SR（注释提及） | 头注：官方查证 F15 定论=W10 豁免留档（2026-10-26），红队独立裁决=豁免成立（非调用点） |
| 4 | project_tcs/App/iwdg/iwdg.c:10 | IWDG_TypeDef（注释提及） | 头注：四寄存器均为 ST 官方 CMSIS 设备头符号，非裸地址（非调用点） |
| 5 | project_tcs/App/iwdg/iwdg.c:11 | HAL_IWDG_Init/Refresh（注释提及） | 头注：寄存器序列与 HAL_IWDG_Init/Refresh 等价说明起点（非调用点） |
| 6 | project_tcs/App/iwdg/iwdg.c:20 | IWDG_KEY_ENABLE=0xCCCCU（宏） | 启动键值定义（对齐官方头 L162 域键值） |
| 7 | project_tcs/App/iwdg/iwdg.c:21 | IWDG_KEY_REFRESH=0xAAAAU（宏） | 喂狗键值定义 |
| 8 | project_tcs/App/iwdg/iwdg.c:22 | IWDG_KEY_WRITE=0x5555U（宏） | 解锁写键值定义 |
| 9 | project_tcs/App/iwdg/iwdg.c:34 | IWDG1->KR = 0xCCCC | 启动 IWDG（一旦启动不可停）——W10 表 **#14** |
| 10 | project_tcs/App/iwdg/iwdg.c:35 | IWDG1->KR = 0x5555 | 解锁允许写 PR/RLR——W10 表 **#15** |
| 11 | project_tcs/App/iwdg/iwdg.c:36 | IWDG1->PR = 6 | 预分频 256：LSI≈32kHz/256=125Hz，8ms/tick |
| 12 | project_tcs/App/iwdg/iwdg.c:37 | IWDG1->RLR = 249 | (249+1)×8ms=2.0s 超时（WP-C.1 定参+E3 勘误） |
| 13 | project_tcs/App/iwdg/iwdg.c:39 | while((IWDG1->SR & 0x07)!=0) | 死等 PVU/RVU 更新完成（比 HAL 超时退出更保守）——W10 表 **#16** 配套 |
| 14 | project_tcs/App/iwdg/iwdg.c:40 | IWDG1->KR = 0xAAAA | 首次喂狗装载重装值——W10 表 **#16** |
| 15 | project_tcs/App/iwdg/iwdg.c:45 | IWDG1->KR = 0xAAAA | iwdg_refresh() 周期喂狗（主循环调用）——W10 表 **#17** |
| 16 | project_tcs/App/tcs/console.c:388 | IWDG（注释提及） | wdttest 实验命令注释：死循环不喂狗→IWDG 2s 复位（WP-C.1 实验臂）（非调用点） |
| 17 | project_tcs/App/tcs/console.c:390 | IWDG（日志字符串） | tcs_log 文案 "IWDG test: busy-loop 5s (timeout=2s)"（字符串行，非寄存器操作） |
| 18 | project_tcs/App/tcs/console.c:543 | IWDG1RSTF（注释提及） | WP-C.4 规则注释：IWDG 复位时 NRST 同被拉低，PINRSTF 同置位，解析须 IWDG1RSTF 优先（非调用点） |
| 19 | project_tcs/App/tcs/console.c:549 | RCC_RSR_IWDG1RSTF | 复位源解析：查 IWDG1 复位标志位，命中则报 "IWDG"（CMSIS 官方位符号） |
| 20 | project_tcs/Core/Src/ltdc.c:441 | IWDG（注释提及） | 历史证注释：raw 直写实验→总线挂死主循环假死→IWDG 复位救回（非调用点） |
| 21 | project_tcs/Core/Src/ltdc.c:591 | IWDG（注释提及） | 历史证注释（同上教训复记）（非调用点） |
| 22 | project_tcs/Core/Src/main.c:66 | IWDG（注释提及） | 历史证注释：WP6 期 32.7s IWDG 复位循环根因（docs/evidence/WP6_iwdg_defect.md）（非调用点） |
| 23 | project_tcs/Core/Src/main.c:326 | RCC_LSI_ON | OscConfig 参数行：LSI 开启（注释"IWDG 用 LSI"）——看门狗时钟源供给（与 PWR/RCC 卡 #29 联动） |
| 24 | project_tcs/Core/Src/main.c:520 | IWDG（注释提及） | H3 根因注释：探针 halt CPU 时 IWDG 继续计数→2s 打狗复位（非调用点） |
| 25 | project_tcs/Core/Src/main.c:522 | IWDG（注释提及） | 接上：调试停顿期冻结 IWDG 策略说明（非调用点） |
| 26 | project_tcs/Core/Src/main.c:523 | DBGMCU->APB4FZ1 \|= DBGMCU_APB4FZ1_DBG_IWDG1 | 调试 halt 期冻结 IWDG 计数（仅调试生效，现场运行不受影响）——CMSIS 官方调试冻结符号 |
| 27 | project_tcs/Core/Src/stm32h7xx_it.c:55 | IWDG（注释提及） | Fault_Handler 策略注释：不喂 IWDG（2s，WP-C.1）→自动复位，闪 LED2 指示（非调用点） |
| 28 | project_tcs/Core/Src/stm32h7xx_it.c:65 | IWDG（注释提及） | WP-C 注释：实验臂 B（ISR 喂狗）已随 blackbox 移除——2s IWDG 必须能抓主循环假死（非调用点） |

## ④ 对照结论

| 条目 | 结论 |
|---|---|
| **F15 豁免留档定论（锚点）** | **豁免成立，留档定论 ✓**——依据三件：① 序列逐键等价：KR=0xCCCC 启动→0x5555 解锁→PR/RLR→等 SR(PVU/RVU)→0xAAAA 喂狗，与 `HAL_IWDG_Init`（Inc 头 L111（宏 __HAL_IWDG_START 之注） 语义）+`HAL_IWDG_Refresh`（L118（宏 __HAL_IWDG_RELOAD_COUNTER 之注） 语义）官方序列一致（W10 表 **#14-17** 四行全 ✅）；② 全部经 CMSIS 官方符号（stm32h743xx.h IWDG_TypeDef），非裸地址（D3 债已清偿）；③ WP-C.1/E3/E-20 历史证+红队独立裁决=豁免成立（W10 表 #17 定论行原文引用）。豁免依据=v13.4 §7d 第 10 条；iwdg.c 头注查证注释在案（③ #3-#5 即其落点） |
| 与 HAL 的真实差异 | **偏差（已定论可接受）**——HAL 版另有超时退出/assert/NULL 句柄防护层；本项目 SR 死等（③ #13）比 HAL 更保守（HAL 超时即返错，本项目死等=宁卡不漏），init-once+refresh 场景句柄层零行为增益——此差异即 F15 豁免论证核心，已在案 |
| 时钟源 LSI（#23） | **符合官方推荐 ✓**——LSI 经 OscConfig 开启并稳定等待，iwdg_init 再查 LSIRDY 兜底（详见 PWR/RCC 卡 ④"时钟树 LSI"行）；2.0s 定参基于 LSI≈32kHz 实测口径（WP-C.1+E3 勘误） |
| 调试冻结（#26） | **符合官方推荐 ✓**——`DBGMCU_APB4FZ1_DBG_IWDG1`=ST 官方调试停顿冻结机制（H3 探针会话教训落地），仅 halt 生效不影响现场 |
| 复位源解析（#18/#19） | **符合官方语义 ✓**——IWDG1RSTF 优先于 PINRSTF（WP-C.4 实测：IWDG 复位时 NRST 同拉低），用官方 RCC_RSR 位符号 |
| 喂狗点策略（#27/#28） | **符合设计意图 ✓**——仅主循环喂狗（ISR/故障处理器一律不喂），保证任何假死（总线挂死/死循环）2s 内被复位捕获（WP-C 黑盒实验定论） |
| 实验臂留档（#16/#17/#20-#22/#24/#25） | wdttest 命令与历史证注释=可复现实验面，非调用点，保留不动 |
| LL 备选（stm32h7xx_ll_iwdg.h 在库） | 若二期统一走 LL/HAL 调用面可平移（键值/序列同），现无整改 W 号——F15 定论即终态 |

——研学卡完（28/28）。
