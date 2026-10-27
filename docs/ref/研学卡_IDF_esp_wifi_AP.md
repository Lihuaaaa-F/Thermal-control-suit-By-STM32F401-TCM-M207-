# 研学卡 · IDF esp_wifi（SoftAP 面）（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **9** == 表行数 9 ✓

## ① 官方文档与版本章节

- WiFi 驱动头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/esp_wifi/include/esp_wifi.h`（AP 配置结构体在 `.../components/esp_wifi/include/esp_wifi_types.h`；netif 侧 `.../components/esp_netif/include/esp_netif.h`）
- **头注原文摘录（逐字，esp_wifi.h L407-410，`esp_wifi_start`）**：
  > `* @brief  Start WiFi according to current configuration`
  > `*         If mode is WIFI_MODE_AP, it creates soft-AP control block and starts soft-AP`
- **头注原文摘录（逐字，esp_wifi.h L379-381，`esp_wifi_set_mode`）**：
  > `* @brief     Set the WiFi operating mode`
  > `*            Set the WiFi operating mode as station, soft-AP, station+soft-AP or NAN.`

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/wifi/getting_started/softAP/main/softap_example_main.c`
- 佐证文件：同目录 `CMakeLists.txt`、`Kconfig.projbuild`、`idf_component.yml`

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rn "esp_wifi_\|esp_netif_" esp32/components esp32/main --include=*.c
```
**命中 9 行 == 下表 9 行**（域：全部自研 tcs_wifi/main，无 managed_components）。

| 文件:行 | API | 语义 |
|---|---|---|
| esp32/components/tcs_wifi/softap.c:40 | esp_netif_create_default_wifi_ap | 建默认 AP netif（DHCP server 随附） |
| esp32/components/tcs_wifi/softap.c:42 | esp_wifi_init | 按 WIFI_INIT_CONFIG_DEFAULT 装驱动 |
| esp32/components/tcs_wifi/softap.c:43 | esp_wifi_set_mode | 设 WIFI_MODE_AP 纯 AP 模式 |
| esp32/components/tcs_wifi/softap.c:52 | esp_wifi_set_config | 写 AP 配置（WIFI_IF_AP：ssid/len/pass/authmode/max_connection/channel） |
| esp32/components/tcs_wifi/softap.c:53 | esp_wifi_start | 起 soft-AP 控制块 |
| esp32/components/tcs_wifi/softap.c:55 | esp_netif_ip_info_t（类型声明） | IP 信息结构体声明（非调用） |
| esp32/components/tcs_wifi/softap.c:56 | esp_netif_get_handle_from_ifkey | 按 "WIFI_AP_DEF" 取 AP netif 句柄 |
| esp32/components/tcs_wifi/softap.c:57 | esp_netif_get_ip_info | 读 AP IP 打启动日志 |
| esp32/main/main.c:115 | esp_netif_init | TCP/IP 协议栈初始化（softap_start 前置） |

配套（同初始化序、非本 grep 模式词）：`esp32/main/main.c:116 esp_event_loop_create_default()`——官方 softAP 示例的默认事件循环步骤，本库在 main.c 统一创建，softap.c 仅 include esp_event.h（:12）不自建。

## ④ 对照结论

1. **初始化顺序 ✓**：`nvs_flash_init→esp_netif_init→esp_event_loop_create_default→esp_netif_create_default_wifi_ap→esp_wifi_init→set_mode→set_config→start`，与官方 softAP 示例及平台附录 §7 铁律完全同序（main.c:108-116 + softap.c:40-53）。
2. **esp_wifi_set_config AP 域 ✓（W10 #33）**：ssid_len 显式置 strlen、max_connection=4（≤官方上限）、channel=6、authmode=WPA2_PSK，逐字段对上 esp_wifi_types.h 语义（复审表 #33 ✅）。
3. **启动确证 ✓**：官方示例用事件回调打 GOT_IP；本库无 STA 需求，改用 esp_netif_get_handle_from_ifkey+get_ip_info 直读 AP IP 打日志（softap.c:55-60），等效取证且不空挂事件回调——**注记**：AP 模式无 IP_EVENT_AP_STAIPASSIGNED 消费属预期（无 STA 接入依赖）。
4. **SSID/密码 NVS 覆盖 ✓**：SSID 默认 TCS-<mac 后 4 hex>（esp_read_mac ESP_MAC_WIFI_SOFTAP），NVS "tcs" 命名空间 ssid/pass 可覆盖（网页设置入口二期预留），读取走只读 open→get→close，无写无 commit 需求。
5. **偏差：无；待整改：无**。esp_wifi_deinit/stop 未调用=AP 与设备同生命周期（常驻），与官方示例同型，不计违约。
