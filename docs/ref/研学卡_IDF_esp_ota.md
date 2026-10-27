# 研学卡 · IDF esp_ota（app_update 组件）（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **12** == 表行数 12 ✓

## ① 官方文档与版本章节

- 组件头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/app_update/include/esp_ota_ops.h`（组件名=app_update，API 前缀=esp_ota_）
- **头注原文摘录（逐字，esp_ota_ops.h L236，`esp_ota_set_boot_partition` 注）**：
  > `* @note If this function returns ESP_OK, calling esp_restart() will boot the newly configured app partition.`
- **头注原文摘录（逐字，esp_ota_ops.h L240-243，`esp_ota_get_next_update_partition`）**：
  > `* @brief Return the next OTA app partition which should be written with a new firmware.`
  > `*`
  > `* Call this function to find an OTA app partition which can be passed to esp_ota_begin().`
  > `* Finds next partition round-robin, starting from the current running partition.`

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/ota/native_ota_example/main/native_ota_example.c`（本地分区读写 begin/write/end/set_boot 链——本库 ota_copy_task 同型）
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/ota/simple_ota_example/main/simple_ota_example.c`（HTTP 流式下载参照）
- 族实存：`advanced_https_ota/`、`otatool/`、`partitions_ota/`

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rn "esp_ota_" esp32/components esp32/main --include=*.c
```
**命中 12 行 == 下表 12 行**（域：自研 main/main.c，无 managed_components；触发面在 tcs_web/websocket.c 的 ota 分支，经 `app_ota_trigger` 间接进入，见 ④-2）。

| 文件:行 | API | 语义 |
|---|---|---|
| esp32/main/main.c:20 | esp_ota_ops.h（include） | 头引入（非调用） |
| esp32/main/main.c:67 | esp_ota_get_running_partition | ota_copy_task 取当前运行槽 |
| esp32/main/main.c:68 | esp_ota_get_next_update_partition | 取对槽（round-robin）作写入目标 |
| esp32/main/main.c:74 | esp_ota_handle_t（类型声明） | OTA 写句柄声明（非调用） |
| esp32/main/main.c:75 | esp_ota_begin | 对槽开启 OTA 写（自算镜像长 len） |
| esp32/main/main.c:81 | esp_ota_write | 8KB 块循环写入 |
| esp32/main/main.c:84 | esp_ota_end | 结束写（校验镜像头/长度） |
| esp32/main/main.c:86 | esp_ota_set_boot_partition | 切 boot 到对槽 |
| esp32/main/main.c:96 | esp_ota_get_running_partition | 回滚分支取当前槽 |
| esp32/main/main.c:97 | esp_ota_get_next_update_partition | 回滚分支取对槽 |
| esp32/main/main.c:101 | esp_ota_set_boot_partition | 回滚=boot 指回对槽（不拷贝） |
| esp32/main/main.c:118 | esp_ota_get_running_partition | app_main 启动日志打运行槽名 |

## ④ 对照结论

1. **OTA 写链完整 ✓（平台附录 §9-8 处方）**：`get_next→begin→write→end→set_boot_partition→esp_restart`（main.c:68-88）逐环无缺、任一环失败 vTaskDelete 退出不动 boot 槽；`esp_ota_end` 后必接 `set_boot_partition` 再重启，杜绝"end 后直接 restart 仍启旧版"经典坑。镜像长自算（ota_image_size 对齐+checksum+hash 尾，:60-64），拷贝源=运行槽本体（自拷贝式升级）。
2. **回滚位 ✓（websocket.c ota 分支）**：`ws_on_recv` 收 `{"t":"ota","m":1}` → `app_ota_trigger(1)`（websocket.c ota 分支，WP-F.4）→ main.c:96-102 取对槽 `esp_ota_set_boot_partition(next)` + 1s 后 esp_restart——对上头注 L240-243"round-robin, starting from the current running"：next 恒为非运行槽，boot 指回即回滚；m=0 走完整拷贝切换。语义与官方 native_ota_example 的 set_boot+restart 收尾一致。
3. **官方"待验证回滚"机制未启用=注记非偏差**：sdkconfig 实测 `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is not set`（esp32/sdkconfig:424；`CONFIG_APP_ROLLBACK_ENABLE` 亦未置 :2335）——官方 pending-verify 自动回滚（esp_ota_mark_app_valid_cancel_rollback 族）未启用，本库以"网页 m=1 手动回滚对槽"等效替代且可控性更好；如二期要求首启自证，再评估开启该 CONFIG（不立项）。
4. **启动可观测 ✓**：app_main 用 get_running_partition 打 boot 槽名+版本（:118），OTA 生效与否有日志直接证据（观察驱动原则）。
5. **偏差：无；待整改：无**。
