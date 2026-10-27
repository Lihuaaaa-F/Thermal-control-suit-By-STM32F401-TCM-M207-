# 研学卡 · IDF FreeRTOS（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **115** == 表行数 115 ✓

## ① 官方文档与版本章节

- IDF 增补 API 头（IDF 口径权威，**栈单位出处**）：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/freertos/esp_additions/include/freertos/idf_additions.h`
- 内核 API 头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/freertos/FreeRTOS-Kernel/include/freertos/task.h`（同目录 `queue.h`/`semphr.h`）
- **头注原文摘录（逐字，idf_additions.h L51-52，`xTaskCreatePinnedToCore` 参数注）**：
  > `@param ulStackDepth The size of the task stack specified as the NUMBER OF`
  > `BYTES. Note that this differs from vanilla FreeRTOS.`
- 同一出处被 W10 复审表引用（`docs/ref/API复审表.md` 行 #25-28：〔tasks〕"IDF usStackDepth=字节"）。

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/freertos/basic_freertos_smp_usage/main/basic_freertos_smp_usage.c`
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/freertos/basic_freertos_smp_usage/main/create_task_example.c`
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/freertos/basic_freertos_smp_usage/main/queue_example.c`
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/freertos/basic_freertos_smp_usage/main/lock_example.c`
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/freertos/real_time_stats/main/real_time_stats_example_main.c`（我们 perf 命令的 vTaskGetRunTimeStats 同款）

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rnE "xTaskCreate|vTaskDelete|vTaskDelay|vTaskList|vTaskGetRunTimeStats|xQueue|taskENTER_CRITICAL|taskEXIT_CRITICAL|uxTaskGetStackHighWaterMark|xTaskGetTickCount|pdMS_TO_TICKS" esp32/components esp32/main --include=*.c
```
**命中 115 行 == 下表 115 行**（域：全部为自研代码 tcs_* / main，无 managed_components）。

| 文件:行 | API | 语义 |
|---|---|---|
| esp32/components/tcs_fan/fan_service.c:70 | vTaskDelay+pdMS_TO_TICKS | WS2812 清灯后拉低 2ms 复位锁存 |
| esp32/components/tcs_fan/fan_service.c:93 | vTaskDelay+pdMS_TO_TICKS | ledtest 红帧保持 700ms 再灭 |
| esp32/components/tcs_fan/fan_service.c:166 | vTaskDelay+pdMS_TO_TICKS | boottest duty=0 后电平稳定 10ms |
| esp32/components/tcs_fan/fan_service.c:170 | vTaskDelay+pdMS_TO_TICKS | boottest duty=100 后电平稳定 10ms |
| esp32/components/tcs_fan/fan_service.c:174 | vTaskDelay+pdMS_TO_TICKS | boottest 回 duty=0 后 10ms |
| esp32/components/tcs_core/control_service.c:55 | xTaskGetTickCount | 控制任务周期节拍基准 |
| esp32/components/tcs_core/control_service.c:58 | vTaskDelayUntil+pdMS_TO_TICKS | 500ms 控制轮询绝对节拍 |
| esp32/components/tcs_core/control_service.c:77 | xTaskCreatePinnedToCore | 建 ctrl_svc 任务（栈 4096 字节，核 1） |
| esp32/components/tcs_link/link_task.c:181 | taskENTER_CRITICAL | 演示状态更新入临界区（s_mux） |
| esp32/components/tcs_link/link_task.c:194 | taskEXIT_CRITICAL | 演示状态更新出临界区 |
| esp32/components/tcs_link/link_task.c:227 | taskENTER_CRITICAL | comm 钩子 link_crit_enter 定义体 |
| esp32/components/tcs_link/link_task.c:228 | taskEXIT_CRITICAL | comm 钩子 link_crit_exit 定义体 |
| esp32/components/tcs_link/link_task.c:259 | taskENTER_CRITICAL | 0x10 STATUS 帧入 s_status 临界区 |
| esp32/components/tcs_link/link_task.c:273 | taskEXIT_CRITICAL | STATUS 更新出临界区 |
| esp32/components/tcs_link/link_task.c:282 | taskENTER_CRITICAL | 0x11 PARAM 帧入 s_last_param 临界区 |
| esp32/components/tcs_link/link_task.c:293 | taskEXIT_CRITICAL | PARAM 更新出临界区 |
| esp32/components/tcs_link/link_task.c:351 | taskENTER_CRITICAL | 注册表扫描拷贝入临界区 |
| esp32/components/tcs_link/link_task.c:355 | taskEXIT_CRITICAL | 注册表扫描出临界区 |
| esp32/components/tcs_link/link_task.c:379 | taskENTER_CRITICAL | link_param_value 读参数临界区 |
| esp32/components/tcs_link/link_task.c:389 | taskEXIT_CRITICAL | 读参数出临界区 |
| esp32/components/tcs_link/link_task.c:407 | vTaskDelay+pdMS_TO_TICKS | link 任务主循环空闲 500ms 节拍 |
| esp32/components/tcs_link/link_task.c:410 | xQueueReceive+pdMS_TO_TICKS | UART 事件队列 50ms 超时收事件 |
| esp32/components/tcs_link/link_task.c:463 | xTaskCreate | 建 tcs_link 任务（4096，优先级 5） |
| esp32/components/tcs_link/link_task.c:502 | vTaskDelay+pdMS_TO_TICKS | LPBK 回环自测帧间隔 20ms |
| esp32/components/tcs_link/link_task.c:518 | vTaskDelay+pdMS_TO_TICKS | LPBK 站点 1 发帧后等 50ms |
| esp32/components/tcs_link/link_task.c:525 | vTaskDelay+pdMS_TO_TICKS | LPBK 站点 2 等 50ms |
| esp32/components/tcs_link/link_task.c:533 | vTaskDelay+pdMS_TO_TICKS | LPBK 站点 3 等 50ms |
| esp32/components/tcs_link/link_task.c:546 | vTaskDelay+pdMS_TO_TICKS | LPBK 收尾等结果 100ms |
| esp32/components/tcs_m207/m207_service.c:5 | vTaskDelayUntil（注释） | 文件头注描述 50ms 节流拍（非调用） |
| esp32/components/tcs_m207/m207_service.c:145 | xQueueSend | 写入口投递消息队列（满即丢返 -1） |
| esp32/components/tcs_m207/m207_service.c:242 | xQueueReceive | 统计路径清空残留队列计 drop |
| esp32/components/tcs_m207/m207_service.c:432 | xTaskGetTickCount | m207 任务节拍基准 |
| esp32/components/tcs_m207/m207_service.c:435 | vTaskDelayUntil+pdMS_TO_TICKS | M207_TICK_MS 节流绝对节拍 |
| esp32/components/tcs_m207/m207_service.c:443 | xQueueReceive | UART 事件队列收事件（0 超时） |
| esp32/components/tcs_m207/m207_service.c:474 | xQueueReceive | 每拍至多消费 1 帧发送仲裁 |
| esp32/components/tcs_m207/m207_service.c:500 | uxTaskGetStackHighWaterMark | 栈水位巡检（自诊断） |
| esp32/components/tcs_m207/m207_service.c:538 | pdMS_TO_TICKS | uart_wait_tx_done 超时 50ms 换算 |
| esp32/components/tcs_m207/m207_service.c:541 | xQueueCreate | 建消息队列（M207_Q_LEN×msg） |
| esp32/components/tcs_m207/m207_service.c:552 | xTaskCreatePinnedToCore | 建 m207_svc 任务（M207_TASK_STACK，核 1） |
| esp32/components/tcs_m207/m207_service.c:733 | vTaskDelay+pdMS_TO_TICKS | 上电自测命令间 20ms 间隔 |
| esp32/components/tcs_m207/m207_service.c:771 | vTaskDelay+pdMS_TO_TICKS | 自测时序 10ms |
| esp32/components/tcs_m207/m207_service.c:778 | vTaskDelay+pdMS_TO_TICKS | 自测等应答窗口 60ms |
| esp32/components/tcs_m207/m207_service.c:780 | vTaskDelay+pdMS_TO_TICKS | 自测时序 10ms |
| esp32/components/tcs_m207/m207_service.c:788 | vTaskDelay+pdMS_TO_TICKS | 自测等回显 60ms |
| esp32/components/tcs_m207/m207_service.c:801 | vTaskDelay+pdMS_TO_TICKS | 自测收尾 10ms |
| esp32/components/tcs_m207/m207_service.c:872 | vTaskDelay+pdMS_TO_TICKS | sl_wait_ms 自环应答器延时原语 |
| esp32/components/tcs_m207/m207_service.c:969 | vTaskDelay+pdMS_TO_TICKS | 自环脚本帧内 2ms 微延时 |
| esp32/components/tcs_m207/m207_service.c:972 | vTaskDelay+pdMS_TO_TICKS | 自环脚本 5ms |
| esp32/components/tcs_m207/m207_service.c:975 | vTaskDelay+pdMS_TO_TICKS | 自环脚本 2ms |
| esp32/components/tcs_m207/m207_service.c:994 | vTaskDelay+pdMS_TO_TICKS | [RP] 等 m207_online 上线（100ms×≤60） |
| esp32/components/tcs_m207/m207_service.c:1006 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 500ms |
| esp32/components/tcs_m207/m207_service.c:1008 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 500ms |
| esp32/components/tcs_m207/m207_service.c:1010 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 500ms |
| esp32/components/tcs_m207/m207_service.c:1012 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 500ms |
| esp32/components/tcs_m207/m207_service.c:1014 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 500ms |
| esp32/components/tcs_m207/m207_service.c:1016 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 500ms |
| esp32/components/tcs_m207/m207_service.c:1018 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 500ms |
| esp32/components/tcs_m207/m207_service.c:1020 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 500ms |
| esp32/components/tcs_m207/m207_service.c:1024 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1026 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1028 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1030 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1032 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1034 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1036 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1038 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1040 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1042 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1044 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1046 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 800ms |
| esp32/components/tcs_m207/m207_service.c:1050 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 4000ms |
| esp32/components/tcs_m207/m207_service.c:1058 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 4000ms |
| esp32/components/tcs_m207/m207_service.c:1068 | vTaskDelay+pdMS_TO_TICKS | 取证脚本阶段延时 5000ms |
| esp32/components/tcs_tmp117/tmp117_service.c:83 | vTaskDelay+pdMS_TO_TICKS | 采样周期 period ms 等待 |
| esp32/components/tcs_tmp117/tmp117_service.c:180 | xTaskCreatePinnedToCore | 建 tmp117_svc 任务（4096，核 1） |
| esp32/components/tcs_web/websocket.c:39 | taskENTER_CRITICAL | fds_add 入 fd 表临界区 |
| esp32/components/tcs_web/websocket.c:41 | taskEXIT_CRITICAL | 已存在早退出临界区 |
| esp32/components/tcs_web/websocket.c:42 | taskEXIT_CRITICAL | 拿空位写入出临界区 |
| esp32/components/tcs_web/websocket.c:44 | taskEXIT_CRITICAL | 表满路径出临界区 |
| esp32/components/tcs_web/websocket.c:49 | taskENTER_CRITICAL | fds_del 入临界区 |
| esp32/components/tcs_web/websocket.c:52 | taskEXIT_CRITICAL | fds_del 出临界区 |
| esp32/components/tcs_web/websocket.c:75 | taskENTER_CRITICAL | 快照广播锁 fd 集合 |
| esp32/components/tcs_web/websocket.c:78 | taskEXIT_CRITICAL | 解锁 fd 集合 |
| esp32/components/tcs_web/websocket.c:152 | taskENTER_CRITICAL | 注册表 JSON 缓存命中读（s_reg_mux） |
| esp32/components/tcs_web/websocket.c:156 | taskEXIT_CRITICAL | 缓存命中拷贝出临界区 |
| esp32/components/tcs_web/websocket.c:198 | taskENTER_CRITICAL | 换入新缓存串临界区 |
| esp32/components/tcs_web/websocket.c:203 | taskEXIT_CRITICAL | 换缓存出临界区（旧串锁外延后释放） |
| esp32/components/tcs_web/websocket.c:241 | taskENTER_CRITICAL | def 事件使缓存代失效（s_reg_gen++） |
| esp32/components/tcs_web/websocket.c:243 | taskEXIT_CRITICAL | 代失效出临界区 |
| esp32/components/tcs_web/websocket.c:267 | vTaskDelay+pdMS_TO_TICKS | ws 推送任务 WS_PUSH_MS 节拍 |
| esp32/components/tcs_web/websocket.c:278 | xTaskCreate | 建 tcs_ws 推送任务（4096，优先级 4） |
| esp32/main/r1e_probe.c:55 | vTaskDelay+pdMS_TO_TICKS | R1e 取证时序 250ms |
| esp32/main/r1e_probe.c:71 | vTaskDelay+pdMS_TO_TICKS | 等 m207_online（100ms×≤80） |
| esp32/main/r1e_probe.c:80 | vTaskDelay+pdMS_TO_TICKS | R1e 取证时序 100ms |
| esp32/main/r1e_probe.c:98 | vTaskDelay+pdMS_TO_TICKS | R1e 取证时序 1500ms |
| esp32/main/r1e_probe.c:107 | vTaskDelay+pdMS_TO_TICKS | R1e 取证时序 1300ms |
| esp32/main/r1e_probe.c:109 | vTaskDelay+pdMS_TO_TICKS | R1e 取证时序 1500ms |
| esp32/main/r1e_probe.c:195 | vTaskDelay+pdMS_TO_TICKS | R1e 取证时序 300ms |
| esp32/main/r1e_probe.c:211 | vTaskDelay+pdMS_TO_TICKS | 软复位前让日志走完 200ms |
| esp32/main/main.c:69 | vTaskDelete | ota 无对槽→删自身退出拷贝任务 |
| esp32/main/main.c:76 | vTaskDelete | esp_ota_begin 失败删任务退出 |
| esp32/main/main.c:82 | vTaskDelete | esp_ota_write 失败删任务退出 |
| esp32/main/main.c:85 | vTaskDelete | esp_ota_end 失败删任务退出 |
| esp32/main/main.c:88 | vTaskDelay+pdMS_TO_TICKS | ota 完成 1s 后 esp_restart |
| esp32/main/main.c:102 | vTaskDelay+pdMS_TO_TICKS | 回滚路径 1s 后 esp_restart |
| esp32/main/main.c:105 | xTaskCreate | 建 ota_copy 任务（6144，优先级 4） |
| esp32/main/tcs_snap.c:46 | xTaskGetTickCount | 快照任务节拍基准 |
| esp32/main/tcs_snap.c:49 | vTaskDelayUntil+pdMS_TO_TICKS | SNAP_PERIOD_MS 快照绝对节拍 |
| esp32/main/tcs_snap.c:115 | xTaskCreatePinnedToCore | 建 tcs_snap 任务（3072） |
| esp32/main/tcs_console.c:152 | vTaskGetRunTimeStats | perf 命令打 CPU 运行时统计表 |
| esp32/main/tcs_console.c:154 | vTaskList | perf 命令打任务状态表 |
| esp32/main/tcs_console.c:190 | vTaskDelay+pdMS_TO_TICKS | ledblink 命令低电平 300ms |
| esp32/main/tcs_console.c:191 | vTaskDelay+pdMS_TO_TICKS | ledblink 命令高电平 300ms |
| esp32/main/tcs_console.c:211 | vTaskDelay+pdMS_TO_TICKS | reset 命令延时 1s 再复位 |
| esp32/main/tcs_console.c:258 | vTaskList（help 文本） | perf 命令 help 字符串提及（非调用） |

## ④ 对照结论

1. **栈单位=字节 ✓（W10 #25-28）**：4 处 `xTaskCreatePinnedToCore`/`xTaskCreate` 的栈参（4096/4096/4096/3072/6144）按 idf_additions.h L51-52 原文"NUMBER OF BYTES"口径书写，全部为字节数非字数；tcs_snap 3072B 构建实测无溢出（复审表 #28）。
2. **W3-A MessageBuffer 官方化=评估拒绝 ✓（不迁移）**：`docs/results/W3_txq_评估.md` §5 结论 **NO-GO（维持）**——主判据=H743/PC 端无 FreeRTOS 金测不可达，次判据=安全链路零收益全量重认证；F7 定论"FreeRTOS 队列族官方等价候选=评估拒绝"。故本工程 comm txq 保持自研环形队列，FreeRTOS 队列使用面仅剩 m207_service.c 的 xQueueCreate/Send/Receive（SPSC/单消费者场景，语义匹配官方 Queue）。
3. **延时宏口径 ✓**：全部阻塞延时经 `pdMS_TO_TICKS()` 换算（65 处），无一裸 tick 字面量；周期任务用 `vTaskDelayUntil` 绝对节拍（control/m207/snap 三服务），一次性等待用 `vTaskDelay`，划分与官方 basic_freertos_smp_usage 示例一致。
4. **临界区 ✓**：taskENTER/EXIT_CRITICAL 共 26 行（ENTER 12 / EXIT 14）：EXIT 多 2 行源于 websocket.c:39 fds_add 单入口带 3 条退出路径（:41 重复早退 /:42 入空位 /:44 表满收尾），逐 ENTER 核对退出路径无遗漏；保护对象均为多任务共享 POD 快照/缓存代，模式同官方 lock_example（任务间共享数据加锁）。
5. **uxTaskGetStackHighWaterMark ✓**：m207 任务内自诊断巡检（:500），与 real_time_stats 示例的观测用法同型。
6. **偏差：无**。xSemaphore/xEventGroup/xMessageBuffer/xStreamBuffer 本库 0 调用（W3-A 拒绝后无新增），无需整改项。
