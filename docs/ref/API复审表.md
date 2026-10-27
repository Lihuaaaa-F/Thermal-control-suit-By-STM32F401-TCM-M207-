# W10 官方 API 调用正确性复审表（v13.4 §7d；整改 v2=红队 D1/D2/D3/D4/D6/D7 采纳）

日期：2026-10-26。范围=册定首批 10 项+grep 扩全。**规则（红队最差执行者测试采纳）**：一行=一个调用点（禁分组合并）；每行出处列=可点开的官方源路径；门禁=行数 ≥ 基线命中数。

**基线命令（附录 B 全文）实测命中 40；本表数据行 41（40 命中逐行对应+红队 D3 增补 link 错误路径 1 行）≥ 40 ✓**

官方语义出处缩写：〔UART-HAL〕= Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_uart.c；〔I2C-HAL〕= 同目录 _i2c.c；〔IWDG-HAL〕= Drivers/STM32H7xx_HAL_Driver/Inc/stm32h7xx_hal_iwdg.h（本工程未拉 iwdc.c 源，Inc 头为语义权威）；〔httpd〕= IDF v5.5.5 components/esp_http_server/include/esp_http_server.h；〔nvs〕= components/nvs_flash/include/nvs.h；〔tasks〕= IDF FreeRTOS（components/freertos/FreeRTOS-Kernel/include/freertos/idf_additions.h："NUMBER OF BYTES…differs from vanilla FreeRTOS"）；〔wdt〕= components/esp_system/include/esp_task_wdt.h；〔ledc〕= components/esp_driver_ledc/include/driver/ledc.h；〔wifi〕= components/esp_wifi/include/esp_wifi_types.h；〔uart-drv〕= components/esp_driver_uart/include/driver/uart.h。

## 一、H743 侧（17 行，#1-17）

| # | 调用点 | 官方语义出处 | 我们用法 | 结论 |
|---|---|---|---|---|
| 1 | App/tcs/console.c:108 console_rx_complete 内重挂 | 〔UART-HAL〕Receive_IT 完成回调后需再挂续收 | 单字节连环挂 | ✅正确 |
| 2 | App/tcs/console.c:116 错误恢复重挂（**AbortReceive 金标准=L111-116，console_uart_error_recover 整函数**） | 〔UART-HAL〕错误后 RxState 可仍 BUSY_RX，直接重挂得 HAL_BUSY；官方恢复模式=Abort 后重启 | ORE 清+**AbortReceive**+重挂（"防 BUSY 导致重挂失败"注释在 L115） | ✅正确（金标准本体） |
| 3 | App/tcs/console.c:539 console_init 初挂 | 同 #1 语义 | 启动期首挂（非错误路径） | ✅正确 |
| 4 | App/tcs/console.c:59 Transmit(...,10u) | 〔UART-HAL〕Timeout 单位=ms | n≤32B@115200≈2.8ms<10ms | ✅正确 |
| 5 | App/tcs/m207.c:313 Transmit(...,20u) | 同上 | 15B@57600≈2.6ms，7.7×裕度 | ✅正确 |
| 6 | App/tcs/m207.c:346 启动挂 | 同 #1 | 同款 | ✅正确 |
| 7 | App/tcs/m207.c:543 错误恢复重挂（**原缺 Abort**） | 同 #2 | 原仅清 ORE+重挂——错误态 BUSY_RX 时返 HAL_BUSY=RX 静默死 | ⚠偏差→**已整改（并入 W10）**：+HAL_UART_AbortReceive 对齐 #2 金标准 |
| 8 | App/tcs/m207.c:550 完成重挂 | 同 #1 | 正常路径 | ✅正确 |
| 9 | Core/Src/main.c:104 链路 Receive_IT | 同 #1 | 链路 RX 单字节挂 | ✅正确 |
| 10 | **Core/Src/main.c:108-113 链路错误恢复（红队 D3 增补审）** | 同 #2 | link 错误路径：清标志+**AbortReceive（L111）**+重挂——三路自愈对照齐（console/m207/link 各 1，配对 grep 门禁见附录 C） | ✅正确（第三路本已在案，v1 表漏审） |
| 11 | Core/Src/main.c:139 Transmit(&huart_link,...,20u) | 同 #4 | 最大帧 254B（proto.h:30 宏展开）@230400 8N1≈11.0ms，20ms=1.8×裕度（W3-A 重估注记） | ✅正确（裕度偏紧注记） |
| 12 | App/tcs/tmp117.c:111 Mem_Read(DEVID,20u) | 〔I2C-HAL〕DevAddress=8bit 移位地址/MemAddSize/Timeout ms/返 HAL_OK 判 | ADDR7<<1 ✓ 8BIT ✓ 返查 ✓ | ✅正确（W1 ft5446 复制本模式） |
| 13 | App/tcs/tmp117.c:147 Mem_Read(TEMP,20u) | 同上 | 同上 | ✅正确 |
| 14 | App/iwdg/iwdg.c:34 KR=0xCCCC（启动） | 〔IWDG-HAL〕HAL_IWDG_Init 首键 | 同键 | ✅（F15 豁免 1/4，见下） |
| 15 | App/iwdg/iwdg.c:35 KR=0x5555（解锁） | 同上 | 同键 | ✅（2/4） |
| 16 | App/iwdg/iwdg.c:40 KR=0xAAAA（装载） | 同上+Refresh 语义 | 同键 | ✅（3/4） |
| 17 | App/iwdg/iwdg.c:45 KR=0xAAAA（喂狗） | 同上 | 同键=HAL_IWDG_Refresh 等价 | ✅（4/4）——**F15 定论=豁免留档（红队独立裁决=豁免成立）**：序列逐键等价+CMSIS 官方符号+WP-C.1/E3/E-20 历史证；iwdg.c 头查证注释在案 |

## 二、ESP32 侧（24 行，#18-41）

| # | 调用点 | 官方语义出处 | 我们用法 | 结论 |
|---|---|---|---|---|
| 18 | tcs_web/websocket.c:69 fds_send ← ws 推送任务（websocket.c:161-168 自有循环） | 〔httpd〕L1770-1783 头注："should rarely be called directly, with an exception of asynchronous send using httpd_queue_work" | 非 httpd 线程直调=违约；E-17 未爆=常态运气 | ⚠偏差→**分流 W2**（§3.3 落实 httpd_queue_work/同步 API+锁） |
| 19 | tcs_web/websocket.c:192 **ws_on_open**（L177）握手处理器内 async | 同上（handler=httpd 上下文=合法域） | 握手推注册表，malloc/free 配对 | ✅正确 |
| 20 | tcs_core/nvs_store.c:21 nvs_get_blob | 〔nvs〕length 入=容量/出=实长 | 置容量→调后校验 len==EE_IMG_SIZE | ✅正确 |
| 21 | tcs_core/nvs_store.c:33 nvs_set_blob | 〔nvs〕写后需 commit | 紧跟 #22 | ✅正确 |
| 22 | tcs_core/nvs_store.c:34 nvs_commit | 同上 | 成功才 commit+close | ✅正确 |
| 23 | tcs_core/control_service.c:56 esp_task_wdt_add(NULL) | 〔wdt〕NULL=订阅当前任务；超时=CONFIG（3s） | ctrl 任务自订阅 | ✅正确 |
| 24 | tcs_core/control_service.c:66 esp_task_wdt_reset | 〔wdt〕刷新当前订阅 | 500ms<3s；E-20 故意停喂 | ✅正确（常驻任务无需 delete） |
| 25 | tcs_core/control_service.c:77 xTaskCreatePinnedToCore(...,4096,...) | 〔tasks〕IDF usStackDepth=字节 | 4096B 核 1 返查 | ✅正确 |
| 26 | tcs_m207/m207_service.c:552 同上（4096 宏） | 同上 | 同上 | ✅正确 |
| 27 | tcs_tmp117/tmp117_service.c:180 同上（4096） | 同上 | 同上 | ✅正确 |
| 28 | main/tcs_snap.c:115 同上（3072） | 同上 | 构建实测无溢出 | ✅正确 |
| 29 | tcs_fan/fan_service.c:123 ledc_channel_config（init） | 〔ledc〕绑通道/引脚/timer+初始 duty | timer 先配+返查 | ✅正确 |
| 30 | tcs_fan/fan_service.c:78 ledc_channel_config（WS2812 借脚后重绑） | 〔ledc〕重复 config=合法改绑路径 | 借脚后重绑（timer 不变） | ✅正确 |
| 31 | tcs_fan/fan_service.c:140 ledc_set_duty | 〔ledc〕set 后必须 update 才生效 | 与 #32 紧邻 | ✅正确 |
| 32 | tcs_fan/fan_service.c:141 ledc_update_duty | 同上 | 配对完整 | ✅正确 |
| 33 | tcs_wifi/softap.c:52 esp_wifi_set_config(AP) | 〔wifi〕ssid_len/max_connection/channel/authmode 语义 | len✓ max=4 ch=6 WPA2 | ✅正确 |
| 34 | tcs_link/link_task.c:458 uart_driver_install（服务 init） | 〔uart-drv〕返 ESP_OK 才可用；重复 install 返错 | ESP_ERROR_CHECK | ✅正确 |
| 35 | tcs_link/link_task.c:498 install（回环自测）+delete L555 | 同上 | 诊断路径+配对 delete | ✅正确 |
| 36 | tcs_m207/m207_service.c:530 install（服务 init） | 同 #34 | ESP_ERROR_CHECK | ✅正确 |
| 37 | tcs_m207/m207_service.c:764 install（上电自测）+delete L800 | 同上 | 返值显式判+配对 delete | ✅正确 |
| 38 | main/r1e_probe.c:48 nvs_commit | 〔nvs〕同 #22 | 探针写后 commit | ✅正确 |
| 39 | main/r1e_probe.c:166 nvs_commit | 同上 | 同上 | ✅正确 |
| 40 | main/r1e_probe.c:178 nvs_commit | 同上 | 同上 | ✅正确 |
| 41 | main/r1e_probe.c:205 nvs_commit | 同上 | 同上 | ✅正确 |

## 三、发现汇总与分流

| 发现 | 级别 | 分流 |
|---|---|---|
| m207 错误恢复缺 AbortReceive（#7） | 一般 | **已整改并入 W10**（金标准 #2 对齐） |
| ws 推送任务直调 async（#18） | 一般 | **W2**（§3.3） |
| 链路 Transmit 裕度 1.8×（#11，帧长实为 254B） | 微瑕 | **W3-A** 重估 |
| F15 iwdg（#14-17） | — | **豁免留档定论**（红队独立裁决=成立；条件"措辞弱化"已落 iwdg.c v2 注释） |

## 附录 B：基线命令（复跑）
```bash
grep -rnE 'HAL_UART_Receive_IT[ ]*\(|HAL_UART_Transmit[ ]*\(|HAL_I2C_Mem_Read[ ]*\(|httpd_ws_send_frame_async[ ]*\(|nvs_get_blob[ ]*\(|nvs_set_blob[ ]*\(|nvs_commit[ ]*\(|xTaskCreatePinnedToCore[ ]*\(|esp_task_wdt_add[ ]*\(|esp_task_wdt_reset[ ]*\(|ledc_channel_config[ ]*\(|ledc_set_duty[ ]*\(|ledc_update_duty[ ]*\(|esp_wifi_set_config[ ]*\(|uart_driver_install[ ]*\(|IWDG1->KR[ ]*=' project_tcs/App project_tcs/Core/Src esp32/components esp32/main --include=*.c | wc -l
# → 40（2026-10-26 实测）；本表数据行 41 ≥ 40（40 命中逐行有对应+D3 增补 1）
```

## 附录 C：三路 UART 错误自愈 Abort 配对门禁（红队建议采纳，W 线新增）
```bash
grep -c "HAL_UART_AbortReceive" project_tcs/App/tcs/console.c project_tcs/App/tcs/m207.c project_tcs/Core/Src/main.c
# 期望 1/1/1（console L115 / m207 L542 整改后 / main.c L111）
```
