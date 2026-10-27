# 研学卡 · IDF nvs_flash（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **41** == 表行数 41 ✓

## ① 官方文档与版本章节

- 组件头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/nvs_flash/include/nvs_flash.h`（init/erase）与 `.../components/nvs_flash/include/nvs.h`（open/get/set/commit——len 与 commit 语义权威）
- **头注原文摘录（逐字，nvs.h L201-202，set 族函数注）**：
  > `Note that the actual storage will not be updated`
  > `until \c nvs_commit is called.`
- **头注原文摘录（逐字，nvs.h L480-483，`nvs_get_str` 的 length 参数注；`nvs_get_blob` 注明"behaves the same as \c nvs_get_str, except for the data type"）**：
  > `In case out_value a zero, will be set to the length`
  > `required to hold the value. In case out_value is not`
  > `zero, will be set to the actual length of the value`
  > `written. For nvs_get_str this includes zero terminator.`

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/storage/nvs/nvs_rw_blob/main/nvs_blob_example_main.c`（blob 读写+commit 参照）
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/storage/nvs/nvs_rw_value/main/nvs_value_example_main.c`（标量读写参照）

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rn "nvs_" esp32/components esp32/main --include=*.c
```
**命中 41 行 == 下表 41 行**（域：全部自研 tcs_core/tcs_wifi/main，无 managed_components）。

| 文件:行 | API | 语义 |
|---|---|---|
| esp32/components/tcs_core/nvs_store.c:2 | nvs_store（注释） | 文件头注"NVS 持久化实现（R1a）"（非调用） |
| esp32/components/tcs_core/nvs_store.c:6 | nvs_store.h（include） | 引自建头（非 API 调用） |
| esp32/components/tcs_core/nvs_store.c:14 | nvs_flash_init（注释） | 注释说明 NVS 恒可用依据（非调用） |
| esp32/components/tcs_core/nvs_store.c:18 | nvs_handle_t（类型声明） | 读句柄声明（非调用） |
| esp32/components/tcs_core/nvs_store.c:19 | nvs_open | 只读打开命名空间 NS_TCS |
| esp32/components/tcs_core/nvs_store.c:21 | nvs_get_blob | 读注册表镜像（len 入=sizeof 出校验==EE_IMG_SIZE） |
| esp32/components/tcs_core/nvs_store.c:22 | nvs_close | 关只读句柄 |
| esp32/components/tcs_core/nvs_store.c:26 | nvs_registry_commit（函数定义名） | 写入口函数定义行（含 nvs_ 前缀，非 API 调用） |
| esp32/components/tcs_core/nvs_store.c:28 | nvs_handle_t（类型声明） | 写句柄声明（非调用） |
| esp32/components/tcs_core/nvs_store.c:29 | nvs_open | 读写打开 NS_TCS |
| esp32/components/tcs_core/nvs_store.c:33 | nvs_set_blob | 写注册表镜像 EE_IMG_SIZE 字节 |
| esp32/components/tcs_core/nvs_store.c:34 | nvs_commit | set 成功才 commit（断电不丢） |
| esp32/components/tcs_core/nvs_store.c:35 | nvs_close | 关写句柄 |
| esp32/components/tcs_wifi/softap.c:8 | nvs_flash.h（include） | 头引入（非调用） |
| esp32/components/tcs_wifi/softap.c:24 | nvs_handle_t（类型声明） | 覆盖读句柄（非调用） |
| esp32/components/tcs_wifi/softap.c:25 | nvs_open | 只读打开 "tcs" 命名空间 |
| esp32/components/tcs_wifi/softap.c:27 | nvs_get_str | 读 ssid（len 入=sizeof(ssid)） |
| esp32/components/tcs_wifi/softap.c:29 | nvs_get_str | 读 pass（失败回退默认 "12345678"） |
| esp32/components/tcs_wifi/softap.c:32 | nvs_close | 关句柄 |
| esp32/main/tcs_console.c:10 | nvs_store.h（include） | 头引入（非调用） |
| esp32/main/main.c:16 | nvs_flash.h（include） | 头引入（非调用） |
| esp32/main/main.c:29 | nvs_store.h（include） | 头引入（非调用） |
| esp32/main/main.c:110 | nvs_flash_init | app_main 首步初始化默认 nvs 分区 |
| esp32/main/main.c:112 | nvs_flash_erase | NO_FREE_PAGES/NEW_VERSION 分支擦除恢复 |
| esp32/main/main.c:113 | nvs_flash_init | 擦除后重 init（官方恢复序） |
| esp32/main/main.c:125 | nvs_registry_commit（函数引用） | param_hooks.eeprom_commit 装配（自研函数名命中） |
| esp32/main/r1e_probe.c:36 | nvs_handle_t（类型声明） | 探针常驻句柄（非调用） |
| esp32/main/r1e_probe.c:41 | nvs_get_u8 | 读探针 stage |
| esp32/main/r1e_probe.c:47 | nvs_set_u8 | 写探针 stage |
| esp32/main/r1e_probe.c:48 | nvs_commit | stage 写后落盘 |
| esp32/main/r1e_probe.c:125 | nvs_open | 读写打开 "r1e" 命名空间 |
| esp32/main/r1e_probe.c:130 | nvs_get_u8 | 读 done 标志 |
| esp32/main/r1e_probe.c:139 | nvs_set_u8 | 复位 done=0（重跑取证） |
| esp32/main/r1e_probe.c:165 | nvs_set_u8 | 阶段完成置 done=1 |
| esp32/main/r1e_probe.c:166 | nvs_commit | 随写落盘 |
| esp32/main/r1e_probe.c:177 | nvs_set_u8 | 分支完成置 done=1 |
| esp32/main/r1e_probe.c:178 | nvs_commit | 随写落盘 |
| esp32/main/r1e_probe.c:186 | nvs_stats_t（类型声明） | 统计结构声明（非调用） |
| esp32/main/r1e_probe.c:187 | nvs_get_stats | 查空间余量取证 |
| esp32/main/r1e_probe.c:204 | nvs_set_u8 | 终局置 done=1 |
| esp32/main/r1e_probe.c:205 | nvs_commit | 随写落盘 |

## ④ 对照结论

1. **len 入出语义 ✓（W10 #20）**：nvs_store.c:21 读 blob 前 len 置 sizeof(img)、调后校验 len==EE_IMG_SIZE；softap.c:26-29 读 str 同款（len 置 sizeof 后传址），逐字对上 nvs.h L480-483"入=容量/出=实长"。
2. **set→commit 配对 ✓（W10 #21-22）**：nvs_store.c:33-34 set 成功才 commit；r1e_probe.c 全部 4 组 set_u8→commit 紧邻（:47-48/:165-166/:177-178/:204-205，复审表 #38-41），对上 nvs.h L201-202"actual storage will not be updated until nvs_commit is called"。
3. **init 恢复序 ✓**：main.c:110-113 对 ESP_ERR_NVS_NO_FREE_PAGES/NEW_VERSION_FOUND 走 erase→re-init，即官方 pitfall 处方（平台附录 §9-3 同款），无静默吞错。
4. **open/close 配对 ✓**：3 处 open（nvs_store×2、softap、r1e 各路径）均有对应 close；只读路径无 set 即无需 commit，符合语义。
5. **注记（非整改）**：softap.c:27 nvs_get_str 返值未显式判，但以 `ssid[0]` 判空回退默认 SSID，功能等效防御，可在后续触达时顺手补 `== ESP_OK` 判（不立项）。
6. **偏差：无；待整改：无**。
