# W3-A comm txq 官方化·阶段 A 评估（v2=红队整改版；v13.4 §4）

日期：2026-10-27。v1 枚举两链虚构/一真源漏列/push_front 幻影行/md5 断言失实，经红队 F1-F3 证伪后全量重核（红队报告 docs/results/W3_红队.md）；本 v2 每行断言附【复跑命令→期望】。

## 1 生产者/消费者枚举表 v2（调用点级对账）

复跑：`grep -rn "comm_send\(\|comm_txq_push\|comm_poll_tx\(\|comm_tick\|link_send_frame_mt\|link_set_param\|link_send_cmd\|link_request_registry" project_tcs esp32 --include=*.c | grep -v build` → 全命中逐一归属如下；**对账规则（红队 DoD 改写采纳）：枚举表调用点级行数 == grep 调用点级命中数**。

**H743 端（裸机主循环单线程；无 FreeRTOS——红队核实）**

| # | 调用点 | 文件:行 | 上下文 |
|---|---|---|---|
| H1a | comm_txq_push（0x90 ACK） | main.c:156 | 主循环 |
| H1b | comm_txq_push（0x11 PARAM） | main.c:187 | 主循环 |
| H1c | comm_txq_push（0x14 REGISTRY_ENTRY） | main.c:282 | 主循环 |
| H1d | comm_txq_push（0x10 STATUS） | main.c:311 | 主循环 |
| H2 | comm_poll_tx | main.c:599 | 主循环（TX 唯一消费） |
| H3 | comm_tick | main.c:608 | 主循环 |

**ESP32 端（多任务经 link API 直调 comm_*；钩子 portMUX 保护）**

| # | 调用点 | 文件:行 | 任务上下文 |
|---|---|---|---|
| E1a | comm_send（SET_PARAM）←link_set_param←ws_on_recv | link_task.c:324；websocket.c（httpd 任务） | **httpd 任务** |
| E1b | comm_send（link_send_frame_mt 通用）←tcs_snap | link_task.c:334；tcs_snap.c:68/95（**真最大流量源：5Hz 0x20 SNAPSHOT+0x21 DELTA**，v1 漏列） | **tcs_snap 任务** |
| E1c | comm_send（CMD 急停）←link_send_cmd←ws_on_recv | link_task.c:345 | httpd 任务 |
| E1d | comm_send（GET_REGISTRY）←link_request_registry | link_task.c:364（仅 link 任务内调用，v1 误归跨任务） | tcs_link 任务 |
| E2 | comm_poll_tx | link_task.c:427 | tcs_link 任务（TX 唯一消费） |
| E3 | comm_tick | link_task.c:426 | tcs_link 任务 |

**双端 LPBK 自测路径**（诊断，非常态）：link_task.c:490-558（站点 543/547/548；回环 install/delete 配对，W10 #33/35 已审）。
**【新发现 v2-1（重大，死代码）】comm_txq_push_front 双端 0 调用**——定义在 comm.c:98，全库仅注释提及（comm.c:8/comm.h:17）；急停真实路径=ws cmd8→link_send_cmd→**常规尾插**（E1c）+H743 本地 tcs_emergency_stop（main.c:242/493 双通道安全侧）。v1 的 E4 行=幻影（承袭册前提未实测）。处置见 §5。

## 2 六操作语义映射表 v2（实测语义；push_front 行改死代码定性）

| txq 操作 | 现实现（实测） | FreeRTOS 官方 | 差异 |
|---|---|---|---|
| push | 尾插+钩子包络（comm.c:90） | xMessageBufferSend（**SPSC**；message_buffer.h:42-55 官方多写者处方=临界区包 Send+阻塞时间 0——红队 F6 勘正 v1"外互斥"高估） | 多生产者（E1a-c）下官方件须以项目既有钩子包 Send；处方可行但逐操作组合 |
| push_front | **死代码（0 调用，§1 v2-1）** | 无对应 | 无迁移负担；v1"双通道"论据作废 |
| poll_tx | 一发一帧（comm.c:113） | Receive | 一次一发节拍语义保持需包装 |
| count | tx_count 直读容忍一拍 | SpacesAvailable 反算 | 无收益 |
| 满丢老 | 满→丢最老再入（comm.c:76-80） | Send 满即失败 | 官方组合=Receive 丢一帧再 Send（临界区内） |
| 临界区钩子 | init 注入（PC=NULL/H743=禁中断/ESP32=portMUX，link_task.c:448/227） | SPSC 内建 | 多生产者仍需钩子，官方件不省现有保护 |

## 3 Q5 保护核对 v2（红队 F3 勘正）

- **comm.c 双端逐字节一致（除 W7 注释行）**【diff → 仅 1 注释行】✓
- **comm.h 双端存在功能性差异**：MAX_TXQ=16（H743，WP-A.5 注册表全量）vs **8**（ESP32）【grep MAX_TXQ 两文件 → 16/8】——v1"逐字节一致"断言仅对 .c 成立，**md5 同源门禁实为"语义同源+允许定差异"口径**（该门禁具体实现与豁免规则=Q5 维护问题，二期成文）。
- H743/PC 无 FreeRTOS（红队核实裸机）：官方后端只能覆盖 ESP32 半边；函数表注入下文件级 md5 可保持（红队 F7 勘正 v1"门禁作废"），**失效的是行为同源**——双端行为分叉后，PC 单测+H743 真机与 ESP32 真机三套行为须分别认证。

## 4 NO-GO 判据 v2（主判据换腿=红队独立裁定采纳）

**主判据（独立成立）：H743/PC 端无 FreeRTOS→等价性金测与行为同源不可达。** 官方后端最多覆盖 ESP32 半边；"同输入帧序列→双后端消费序一致"的金测在双端口径分叉后无法闭合（H743 永远环形），且金测恰是册 §4.2 的 GO 硬前置——结构上 GO 路径自锁。
**次判据：安全关键路径零收益+全量重认证风险。** 现实现 16×253B（H743）/8×253B（ESP32）静态零堆，E-04/E-09/E-10/E-19 历轮全过；换后端收益=无（SPSC 限制下连锁保护都不省），代价=ESP32 侧 E-04/E-09/E-10/E-19 四项重跑+72h 浸泡重证——安全链路零收益承担全量重认证=负期望。
**附：官方多写者处方（临界区包 Send，F6）确实可行**——本评估如实记录 GO 最小形态≈30-40 行注入后端；其不成立的原因不是不可行，而是主判据的结构性自锁。

## 5 结论与裁定（v2）

**W3-A 结论：NO-GO（维持）。** 主判据=金测不可达（§4 主腿）；次判据=零收益全量重认证（§4）。
- **F7 定论**：FreeRTOS 队列族官方等价候选=**评估拒绝**（清单行已更新指本文件 v2）。
- **W3-B 不执行**（前置=GO 未满足）；§4.2 各项由现实现历轮覆盖，不欠账。
- **v2-1 新发现移交**：①push_front 死代码（+comm.h:17"<20ms 优先发出"未实现承诺注释）→**二期清单：删除或实现二选一**（删=遵循 §1d-3 零死代码；留=若 R 线急停需要队头优先，先实现调用再留）——本评估不改生产代码（纪律 4）；②comm.h MAX_TXQ 双端定差异口径成文（二期）；③册 §0"ESP32 生产者收敛于 link 任务"前提勘误（真源=httpd+tcs_snap+link 三任务，靠钩子保护非收敛）。
- **W10 #11 联动**：ESP32 TX 唯一消费在 link 任务（E2），最大帧 254B@230400≈11ms vs Transmit 20ms 裕度 1.8×维持注记。

## 6 门禁复跑（§1c W3 行）

```
$ 调用点级对账（权威口径=红队机器复跑，勿手写计数）：四函数调用点级实测 12 常态+LPBK 3==15 命中，§1 全覆盖零幻影零漏报（§1 表 10 push/consume 行+H3/E3 tick 两行）
$ diff 双端 comm.c → 仅 W7 注释 1 行
$ grep MAX_TXQ 双端 comm.h → 16 / 8（§3 定差异在案）
$ grep -rn comm_txq_push_front 全库 → 0 真调用（死代码坐实，复跑命令即证据）
```
