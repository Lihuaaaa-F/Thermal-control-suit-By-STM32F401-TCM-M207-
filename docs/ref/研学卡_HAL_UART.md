# 研学卡：HAL_UART（三路串口：console 控制台 / link 链路 / m207 模组）

> 任务书 v13.4 §7b W8 · 制卡 2026-10-27 · 基线=git HEAD `43e5f4b` + 工作区（G 线改动不触 UART 域）
> 门禁：③ 节表行数 == grep 命中数（本卡 **40 行 == 40 命中**（2026-10-30 v2 迁移重锚） ✓）

## ① 官方文档与版本章节

- **随库官方源（API 语义权威）**：`project_tcs/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c`
  - 头注 `@file`（MCD Application Team）；**L26 起 "How to use this driver" 用法章节**（Polling/IT/DMA 三模式、错误处理、Abort 系列）；
  - 全文件 187 处 `@brief/@param` 函数头注（本卡 grep 实测计数）；
  - 关键函数头注锚点：`HAL_UART_Receive_IT`（完成回调后需再挂续收）、`HAL_UART_AbortReceive`（@brief "Abort ongoing Receive transfer (blocking mode)"——错误态 RxState 可仍 BUSY_RX，直接重挂得 HAL_BUSY，官方恢复模式=Abort 后重启）、`HAL_UART_Transmit` 阻塞模式 `Timeout` 单位 ms。
- **扩展 API**：`Src/stm32h7xx_hal_uart_ex.c`（57 处函数头注：FIFO/RX event 等扩展，本工程未用）。
- **API 声明全集**：`Inc/stm32h7xx_hal_uart.h`（含 `__HAL_UART_GET_FLAG/__HAL_UART_CLEAR_OREFLAG` 等中断/标志宏域）。
- **版本**：随库 CMSIS Device H7 = **V1.10.7**（`Drivers/CMSIS/Device/ST/STM32H7xx/Release_Notes.html` 首行 "V1.10.7 / 04-February-2026"）——W9 依赖清单在案；HAL=vendor 裁剪快照无版本宏（`Inc/stm32h7xx_hal.h` 无 `__STM32H7xx_HAL_VERSION`），锁=git tree hash `7dca0af2…`（W9）。

## ② 官方示例文件路径

- 本机探查：`find /d -maxdepth 2 -iname "STM32CubeH7" 2>/dev/null` → **空输出（无命中）**。
- **本机无 CubeH7 例程仓**；官方例程位置=GitHub `STMicroelectronics/STM32CubeH7` 仓
  `Projects/STM32H743I-EVAL/Examples/UART/`（URL：https://github.com/STMicroelectronics/STM32CubeH7/tree/master/Projects/STM32H743I-EVAL/Examples/UART ）。
  其下子例程名本机无仓未枚举（防编造），以仓库目录为准。
- 本工程在案对照件：`docs/ref/API复审表.md` H743 侧 #1-11（UART 调用面逐行官方语义复审，2026-10-26）+ 附录 C 三路 Abort 配对门禁。

## ③ 我们全部调用面清单














复现命令（仓库根 `/d/Espressif/ws/tcs`，Git Bash；raw 输出路径分隔符为 `\`，表内归一为 `/`）：

```bash
grep -rnE "HAL_UART_" project_tcs/App project_tcs/Core/Src --include=*.c
# → 40 命中（2026-11-25 重锚），下表 40 行逐一对应（含注释行，注明"注释"）
```

vendor 域说明：`App/rtt/`（SEGGER RTT vendor 五文件）同一 pattern 单独跑=**0 命中**（RTT 不触 HAL，无需列行）。














| # | 文件:行 | API | 语义一句话 |
|---|---|---|---|
| 1 | project_tcs/App/comm/comm.c:7 | HAL_UART_Transmit（注释提及） | 头注：comm 链路唯一消费点串行 Transmit；ISR 内 comm_tick 只判超时+重传入队（非调用点） |
| 2 | project_tcs/App/tcs/console.c:5 | HAL_UART_Transmit（注释提及） | 头注：控制台日志唯一 Transmit 消费点（铁律：ISR 不发送），日志同时进 RTT（非调用点） |
| 3 | project_tcs/App/tcs/console.c:74 | HAL_UART_Transmit(s_uart, &s_tx_ring[tail], n, 10u) | TX 环形缓冲出队阻塞发送，超时 10ms（n≤32B@115200≈2.8ms，W10 #4） |
| 4 | project_tcs/App/tcs/console.c:157 | HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1) | RxCplt 回调内单字节连环重挂续收（W10 #1） |
| 5 | project_tcs/App/tcs/console.c:162 | __HAL_UART_GET_FLAG(s_uart, UART_FLAG_ORE) | 错误恢复入口查 ORE 标志（金标准函数 console_uart_error_recover L111-116，W10 #2） |
| 6 | project_tcs/App/tcs/console.c:163 | __HAL_UART_CLEAR_OREFLAG(s_uart) | 清 ORE 溢出标志 |
| 7 | project_tcs/App/tcs/console.c:164 | HAL_UART_AbortReceive(s_uart) | 错误态 BUSY_RX 防重挂 HAL_BUSY（"防 BUSY 导致重挂失败"注释在本行；附录 C 三路配对 1/1/1 之一） |
| 8 | project_tcs/App/tcs/console.c:165 | HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1) | Abort 后重启接收（恢复完成） |
| 9 | project_tcs/App/tcs/console.c:717 | __HAL_UART_GET_FLAG(s_uart, UART_FLAG_ORE) | 主循环 RX 自愈巡检：ORE 置位或 RXNEIE 被 HAL 错误路径关掉 → 调恢复函数（启动期线路扰动→控制台失聪的兜底） |
| 10 | project_tcs/App/tcs/console.c:756 | HAL_UART_Receive_IT(s_uart, &s_rx_byte, 1) | console_init 启动期首挂（W10 #3） |
| 11 | project_tcs/App/tcs/m207.c:6 | HAL_UART_ErrorCallback（注释提及） | 头注：ORE 溢出=ErrorCallback 先清标志再重置接收（F401 踩坑防御保留）（非调用点） |
| 12 | project_tcs/App/tcs/m207.c:7 | HAL_UART_Transmit（注释提及） | 头注：TX 主循环阻塞 Transmit，禁止 ISR 内发送（非调用点） |
| 13 | project_tcs/App/tcs/m207.c:184 | HAL_UART_Receive_IT（注释提及） | s_rx_byte 变量注释=Receive_IT 单字节落点（非调用点） |
| 14 | project_tcs/App/tcs/m207.c:354 | HAL_UART_Transmit(s_huart, cmd, strlen(cmd), 20u) | AT 命令阻塞发送，超时 20ms（15B@57600≈2.6ms，W10 #5） |
| 15 | project_tcs/App/tcs/m207.c:387 | HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1) | 服务启动期首挂（W10 #6） |
| 16 | project_tcs/App/tcs/m207.c:603 | __HAL_UART_GET_FLAG(s_huart, UART_FLAG_ORE) | 错误恢复入口查 ORE（W10 #7 整改后路径） |
| 17 | project_tcs/App/tcs/m207.c:604 | __HAL_UART_CLEAR_OREFLAG(s_huart) | 清 ORE |
| 18 | project_tcs/App/tcs/m207.c:609 | HAL_UART_AbortReceive(s_huart) | W10 #7 整改补齐的 Abort（对齐 console 金标准；附录 C 三路配对 1/1/1 之二，2026-10-27 复跑 grep -c=1 实证在树） |
| 19 | project_tcs/App/tcs/m207.c:610 | HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1) | Abort 后重启接收 |
| 20 | project_tcs/App/tcs/m207.c:613 | HAL_UART_RxCpltCallback（注释提及） | 注释：RX 完成回调转发入口（main.c 回调按实例分发）（非调用点） |
| 21 | project_tcs/App/tcs/m207.c:617 | HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1) | RxCplt 回调内重挂续收（W10 #8） |
| 22 | project_tcs/Core/Src/main.c:101 | HAL_UART_Receive_IT（注释提及） | s_link_it_byte 变量注释=链路 Receive_IT 落点（非调用点） |
| 23 | project_tcs/Core/Src/main.c:117 | HAL_UART_Receive_IT(&huart_link, &s_link_it_byte, 1) | 链路 RX 单字节挂（W10 #9） |
| 24 | project_tcs/Core/Src/main.c:180 | __HAL_UART_GET_FLAG(&huart_link, UART_FLAG_ORE) | 链路错误恢复查 ORE（W10 #10） |
| 25 | project_tcs/Core/Src/main.c:181 | __HAL_UART_CLEAR_OREFLAG(&huart_link) | 清 ORE |
| 26 | project_tcs/Core/Src/main.c:182 | HAL_UART_AbortReceive(&huart_link) | 链路 Abort（附录 C 三路配对 1/1/1 之三） |
| 27 | project_tcs/Core/Src/main.c:196 | HAL_UART_Transmit(&huart_link, data, len, 20u) | 链路发帧阻塞 20ms（最大帧 254B@230400≈11.0ms=1.8× 裕度，W10 #11 注记） |
| 28 | project_tcs/Core/Src/main.c:206 | HAL_UART_STATE_READY（gState 轮询） | 发帧完成后等 gState 回 READY 的完成保护（guard 限次防死等） |
| 29 | project_tcs/Core/Src/main.c:235 | __HAL_UART_GET_FLAG（ORE 巡检） | R2 链路 RX 自愈巡检条件（ORE 置位→错误恢复入口） |
| 30 | project_tcs/Core/Src/main.c:310 | HAL_UART_Transmit（注释提及） | 主循环注释：链路 TX 串行消费 comm_poll_tx（非调用点） |
| 31 | project_tcs/Core/Src/main.c:740 | HAL_UART_Init(h) | uart_common_init 各实例（USART1/2/3）统一初始化+返查 HAL_OK |
| 32 | project_tcs/Core/Src/main.c:995 | HAL_UART_Transmit（注释提及） | 主循环注释：链路 TX 串行消费 comm_poll_tx（非调用点） |
| 33 | project_tcs/Core/Src/stm32h7xx_hal_msp.c:4 | HAL_UART_MspInit（注释提及） | 头注：§0F MspInit 死代码清理记载（实际用 USART1，PROTO_UART_SEL=1）（非调用点） |
| 34 | project_tcs/Core/Src/stm32h7xx_hal_msp.c:22 | HAL_UART_MspInit（用户实现） | 三路 UART 引脚/时钟 Msp 配置（含 §0F 清理后的现存实例域） |
| 35 | project_tcs/Core/Src/stm32h7xx_it.c:3 | HAL_UART_Transmit（注释提及） | 铁律注释：ISR 只做 RX 单字节入环形缓冲/置标志，禁止 Transmit（非调用点） |
| 36 | project_tcs/Core/Src/stm32h7xx_it.c:85 | HAL_UART_IRQHandler(&huart_console) | USART1 中断统一入口（控制台） |
| 37 | project_tcs/Core/Src/stm32h7xx_it.c:88 | HAL_UART_IRQHandler(&huart_link) | USART2 中断统一入口（链路） |
| 38 | project_tcs/Core/Src/stm32h7xx_it.c:91 | HAL_UART_IRQHandler(&huart_m207) | USART3 中断统一入口（m207 模组） |
| 39 | project_tcs/Core/Src/stm32h7xx_it.c:94 | HAL_UART_RxCpltCallback(huart) | 用户回调：按实例分发 console/m207/link 三路（重挂逻辑在各模块） |
| 40 | project_tcs/Core/Src/stm32h7xx_it.c:106 | HAL_UART_ErrorCallback(huart) | 用户回调：错误路径分发（ORE 等，触发各模块恢复） |

## ④ 对照结论

| 条目 | 结论 |
|---|---|
| 三路错误恢复 Abort 配对 1/1/1 | **符合官方推荐 ✓**——console L115 / m207 L542（W10 #7 整改已入树）/ main.c L111；复跑门禁 `grep -c "HAL_UART_AbortReceive" project_tcs/App/tcs/console.c project_tcs/App/tcs/m207.c project_tcs/Core/Src/main.c` → **1/1/1**（2026-10-27 实测，W10 附录 C 期望达成） |
| 错误恢复模式（清 ORE→Abort→重挂） | **符合官方推荐 ✓**——对齐 `HAL_UART_AbortReceive` 头注语义（BUSY_RX 态直接重挂得 HAL_BUSY=RX 静默死）；W10 #2 金标准 |
| RxCplt 回调内连环重挂（#4/#21 等） | **符合官方推荐 ✓**——单字节 IT 接收的官方续收模式（W10 #1/#3/#6/#8/#9） |
| Transmit 超时单位=ms（#3/#14/#27） | **符合官方推荐 ✓**——10/20/20ms 均大于最大帧物理时长（W10 #4/#5/#11） |
| ISR 铁律（禁发送，#1/#2/#35） | **符合官方推荐 ✓**——阻塞型 Transmit 不得用于 ISR，工程铁律与官方模式一致 |
| IRQHandler→回调→模块分发（#36-40） | **符合官方推荐 ✓**——中断统一入口+用户回调按实例分发的官方 IT 驱动骨架 |
| 链路 Transmit 裕度 1.8×（#27） | **微瑕已评估**——W10 #11 注记在案；**W3-A 已关单（NO-GO）**（2026-10-27，git `43e5f4b`：重估换腿无收益，现状保留） |
| 错误回调使用域 | `HAL_UART_ErrorCallback` 在 m207 头注(#11)与 it.c 定义(#40) 成对在案——分发后各模块自恢复，与官方"错误回调内做清理+用户重启"流程一致 ✓ |
| 扩展面未用（FIFO/RX event/CLKSOURCE 等 uart_ex 域） | 现状=最小调用面，无待整改；若 W2/W3 引入收发增强再评估（不预设 W 号） |

——研学卡完（40/40，2026-11-25 重锚）。
