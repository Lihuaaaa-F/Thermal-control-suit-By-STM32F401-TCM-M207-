# 研学卡 · IDF driver/uart（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **55** == 表行数 55 ✓（含变量名/函数名/注释的子串命中行，逐行标注）

## ① 官方文档与版本章节

- 组件头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/esp_driver_uart/include/driver/uart.h`（v5.5 起 uart 驱动实体在 esp_driver_uart 组件，`components/driver/` 仅存兼容壳——复审表 D4 已勘正组件归属口径）
- **头注原文摘录（逐字，uart.h L100-101，`uart_driver_install`）**：
  > `* @brief Install UART driver and set the UART to the default configuration.`
  > `*`
  > `* UART ISR handler will be attached to the same CPU core that this function is running on.`
- **头注原文摘录（逐字，uart.h L107，Rx/Tx 缓冲约束）**：
  > `* @note  Rx_buffer_size should be greater than UART_HW_FIFO_LEN(uart_num). Tx_buffer_size should be either zero or greater than UART_HW_FIFO_LEN(uart_num).`

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/peripherals/uart/uart_events/main/uart_events_example_main.c`（事件队列模式——tcs_link 同型）
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/peripherals/uart/uart_async_rxtxtasks/main/uart_async_rxtxtasks_main.c`（m207_service.c:19 头注自述参照）
- 同目录族实存：`uart_echo/`、`uart_echo_rs485/`、`uart_select/`、`uart_repl/`、`nmea0183_parser/`、`uart_dma_ota/`

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rn "uart_" esp32/components esp32/main --include=*.c
```
**命中 55 行 == 下表 55 行**（域：全部自研 tcs_link/tcs_m207，无 managed_components）。tcs_link=UART1 屏链路（LINK_UART），tcs_m207=UART2 温控模块（M207_UART）。

| 文件:行 | API | 语义 |
|---|---|---|
| esp32/components/tcs_link/link_task.c:39 | s_uart_queue（变量名） | 驱动事件队列句柄声明（子串命中，非调用） |
| esp32/components/tcs_link/link_task.c:216 | link_uart_send（函数名） | 自研发送函数定义行（子串命中，非 API 调用） |
| esp32/components/tcs_link/link_task.c:218 | uart_write_bytes | link 层写帧字节 |
| esp32/components/tcs_link/link_task.c:398 | uart_event_t（类型声明） | 事件结构声明（非调用） |
| esp32/components/tcs_link/link_task.c:410 | s_uart_queue（变量引用） | xQueueReceive 收驱动事件（子串命中） |
| esp32/components/tcs_link/link_task.c:412 | uart_read_bytes | DATA 事件读载荷（0 超时非阻塞） |
| esp32/components/tcs_link/link_task.c:419 | uart_flush_input | 错误恢复清 RX |
| esp32/components/tcs_link/link_task.c:448 | link_uart_send（函数引用） | comm_init 装配发送钩子（子串命中） |
| esp32/components/tcs_link/link_task.c:450 | uart_config_t（类型声明） | 生产路径波特率配置声明（非调用） |
| esp32/components/tcs_link/link_task.c:458 | uart_driver_install | 服务初始化装驱动（RX 缓冲+事件队列，ESP_ERROR_CHECK） |
| esp32/components/tcs_link/link_task.c:459 | s_uart_queue（变量引用） | install 第 5 参传出事件队列（子串命中） |
| esp32/components/tcs_link/link_task.c:460 | uart_param_config | 应用波特率/数据位配置 |
| esp32/components/tcs_link/link_task.c:461 | uart_set_pin | 绑 TXD/RXD 引脚 |
| esp32/components/tcs_link/link_task.c:474 | uart_write_bytes | LPBK 自测发帧（诊断路径） |
| esp32/components/tcs_link/link_task.c:481 | uart_read_bytes | LPBK 收回显对拍 |
| esp32/components/tcs_link/link_task.c:493 | uart_config_t（类型声明） | 自测路径配置声明（非调用） |
| esp32/components/tcs_link/link_task.c:498 | uart_driver_install | 回环自测装驱动（诊断路径，返值未包 ERROR_CHECK） |
| esp32/components/tcs_link/link_task.c:499 | uart_param_config | 自测参数配置 |
| esp32/components/tcs_link/link_task.c:500 | uart_set_pin | 自测引脚绑定 |
| esp32/components/tcs_link/link_task.c:501 | uart_set_loop_back | 开内部回环（真机无杜邦线） |
| esp32/components/tcs_link/link_task.c:517 | uart_write_bytes | LPBK 站点帧发送 |
| esp32/components/tcs_link/link_task.c:524 | uart_write_bytes | LPBK 站点帧发送 |
| esp32/components/tcs_link/link_task.c:532 | uart_write_bytes | LPBK 站点帧发送 |
| esp32/components/tcs_link/link_task.c:554 | uart_set_loop_back | 关内部回环收场 |
| esp32/components/tcs_link/link_task.c:555 | uart_driver_delete | 卸自测驱动（与 :498 配对） |
| esp32/components/tcs_m207/m207_service.c:19 | uart（注释） | 头注自述参照官方 uart_async_rxtxtasks（非调用） |
| esp32/components/tcs_m207/m207_service.c:135 | uart_write_bytes | 发 AT 命令字节 |
| esp32/components/tcs_m207/m207_service.c:368 | uart_read_bytes | 非阻塞读应答载荷 |
| esp32/components/tcs_m207/m207_service.c:433 | uart_event_t（类型声明） | 服务任务事件声明（非调用） |
| esp32/components/tcs_m207/m207_service.c:448 | uart_flush_input | 事件循环错误恢复清 RX |
| esp32/components/tcs_m207/m207_service.c:522 | uart_config_t（类型声明） | 服务初始化配置声明（非调用） |
| esp32/components/tcs_m207/m207_service.c:530 | uart_driver_install | 服务初始化装驱动（RX/TX 缓冲+事件队列，ESP_ERROR_CHECK） |
| esp32/components/tcs_m207/m207_service.c:532 | uart_param_config | 57600 8N1 配置 |
| esp32/components/tcs_m207/m207_service.c:533 | uart_set_pin | 绑 M207_TXD/RXD GPIO |
| esp32/components/tcs_m207/m207_service.c:537 | uart_write_bytes | 发 "\r" 唤醒探测 |
| esp32/components/tcs_m207/m207_service.c:538 | uart_wait_tx_done | 等 TX 完成（50ms 超时）再收 |
| esp32/components/tcs_m207/m207_service.c:539 | uart_flush_input | 收前清残留 |
| esp32/components/tcs_m207/m207_service.c:732 | uart_write_bytes | 探针写文本命令 |
| esp32/components/tcs_m207/m207_service.c:736 | uart_read_bytes | 排空读回显 |
| esp32/components/tcs_m207/m207_service.c:759 | uart_config_t（类型声明） | 上电自测配置声明（非调用） |
| esp32/components/tcs_m207/m207_service.c:764 | uart_driver_install | 上电自测装驱动（诊断路径，返值显式判） |
| esp32/components/tcs_m207/m207_service.c:768 | uart_param_config | 自测波特率配置 |
| esp32/components/tcs_m207/m207_service.c:769 | uart_set_pin | 自测引脚绑定 |
| esp32/components/tcs_m207/m207_service.c:774 | uart_set_loop_back | 开内部回环做回显基准 |
| esp32/components/tcs_m207/m207_service.c:777 | uart_write_bytes | 回环态发 "\r" |
| esp32/components/tcs_m207/m207_service.c:779 | uart_flush_input | 切物理路前清缓冲 |
| esp32/components/tcs_m207/m207_service.c:784 | uart_set_loop_back | 关内部回环转物理短接探测 |
| esp32/components/tcs_m207/m207_service.c:785 | uart_flush_input | 转路后再清 |
| esp32/components/tcs_m207/m207_service.c:787 | uart_write_bytes | 发探测串（物理短接才有回显） |
| esp32/components/tcs_m207/m207_service.c:790 | uart_read_bytes | 读回显判定物理链路 |
| esp32/components/tcs_m207/m207_service.c:799 | uart_set_loop_back | 收场确保回环关闭 |
| esp32/components/tcs_m207/m207_service.c:800 | uart_driver_delete | 卸自测驱动（与 :764 配对） |
| esp32/components/tcs_m207/m207_service.c:825 | uart_write_bytes | 补发挂起应答行 |
| esp32/components/tcs_m207/m207_service.c:879 | uart_set_loop_back | selfloop 取证：借常驻驱动开回环注入应答 |
| esp32/components/tcs_m207/m207_service.c:944 | uart_set_loop_back | selfloop 收场关回环（不卸常驻驱动） |

## ④ 对照结论

1. **driver_install 双诊断路径+delete 配对 ✓（W10 #34-37）**：生产路径 2 处（link :458、m207 :530）ESP_ERROR_CHECK 包裹、常驻不卸载；诊断路径 2 处（link :498→:555、m207 :764→:800）install→delete 严格配对（复审表 #34-37 全 ✅）；m207 :764 返值显式判、link :498 未包 ERROR_CHECK 属自测容错口径（失败即自测 FAIL 输出），与复审表 #35 描述一致。
2. **事件队列模式 ✓**：install 传 queue → xQueueReceive 收 uart_event_t → DATA 读/错误 flush_input（link :398-419、m207 :433-448），与官方 uart_events 示例逐环节同型；m207 头注（:19）自述另参照 uart_async_rxtxtasks（异步收发+环形缓冲）。
3. **写后同步 ✓**：m207 :537-539 唤醒序列 write→wait_tx_done(50ms)→flush_input→read，时序完整；wait_tx_done 超时经 pdMS_TO_TICKS 换算（FreeRTOS 卡 :538 行同源）。
4. **loop_back 用法 ✓**：三处 set_loop_back(true) 均有对应 false 收场（:501→:554、:774→:784/:799、:879→:944）；selfloop（:879/:944）在常驻驱动上开关回环做取证注入、不 delete，属有意设计（服务不中断），注记非偏差。
5. **RX 缓冲约束 ✓**：生产 install 的 rx_buffer_size（LINK_RXBUF/M207_RXBUF）大于硬件 FIFO 长度，满足头注 L107 约束；LPBK 路径 2048 同样满足。
6. **偏差：无；待整改：无**。
