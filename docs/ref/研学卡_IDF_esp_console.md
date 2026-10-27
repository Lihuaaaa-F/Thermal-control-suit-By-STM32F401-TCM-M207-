# 研学卡 · IDF esp_console（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **8** == 表行数 8 ✓

## ① 官方文档与版本章节

- 组件头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/console/esp_console.h`（**v5.5 布局事实**：console 组件重构后头文件在组件根，非 include/ 子目录——制卡时 ls 复核；实现源 commands.c/esp_console_common.c 等同目录）
- **头注原文摘录（逐字，esp_console.h L228-229，`esp_console_cmd_register`）**：
  > `* @brief Register console command`
  > `* @param cmd pointer to the command description; can point to a temporary value`
- **头注原文摘录（逐字，esp_console.h L443-445，`esp_console_new_repl_usb_serial_jtag` 注——all-in-one 口径）**：
  > `* @note This is an all-in-one function to establish the environment needed for REPL, includes:`
  > `*       - Initializes linenoise`
  > `*       - Spawn new thread to run REPL in the background`
  > `*`
  > `* @attention This function is meant to be used in the examples to make the code more compact.`
  > `*            Applications which use console functionality should be based on`
  > `*            the underlying linenoise and esp_console functions.`

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/console/advanced/main/console_example_main.c`
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/console/advanced/main/console_settings.c`（命令分文件组织参照）
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/console/basic/main/`（族实存）

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rn "esp_console_" esp32/components esp32/main --include=*.c
```
**命中 8 行 == 下表 8 行**（域：自研 main/tcs_console.c，无 managed_components）。

| 文件:行 | API | 语义 |
|---|---|---|
| esp32/main/tcs_console.c:218 | esp_console_repl_config_t（类型声明） | REPL 配置（ESP_CONSOLE_REPL_CONFIG_DEFAULT） |
| esp32/main/tcs_console.c:222 | esp_console_dev_usb_serial_jtag_config_t（类型声明） | USB-Serial-JTAG 设备配置声明 |
| esp32/main/tcs_console.c:225 | esp_console_repl_t（类型声明） | REPL 句柄声明（非调用） |
| esp32/main/tcs_console.c:226 | esp_console_new_repl_usb_serial_jtag | all-in-one 建 REPL（免驱 USB-S-JTAG 口） |
| esp32/main/tcs_console.c:232 | esp_console_register_help_command | 注册 help 命令（官方内建） |
| esp32/main/tcs_console.c:233 | esp_console_cmd_t（类型声明） | 自研命令表 cmds[] 声明 |
| esp32/main/tcs_console.c:264 | esp_console_cmd_register | 循环注册自研命令表 |
| esp32/main/tcs_console.c:268 | esp_console_start_repl | 启动 REPL 后台线程 |

## ④ 对照结论

1. **官方组件+自研命令表 ✓（本卡主锚点）**：REPL 骨架全用官方 esp_console（new_repl_usb_serial_jtag/register_help/start_repl），业务命令=自研 cmds[] 静态表（:233）+ 循环 esp_console_cmd_register（:264）——"自研必须参考官方示例"落实为：命令注册模式逐行对照 console/advanced 示例的 cmds 注册循环。
2. **命令面归属清晰 ✓**：自研命令（perf/ledblink/ledtest/reset 等，help 文本在 :255-262 段）全部经官方 esp_console_cmd_t 描述符注册，无一绕过框架直挂 stdin——对照清单"M207 无官方件合法自研"同口径：console 有官方件，用官方件。
3. **all-in-one 使用口径=注记（非偏差）**：头注 @attention 原文说明 all-in-one"meant to be used in the examples"，建议应用基于底层 linenoise+esp_console 组合。本项目取 all-in-one 便利路径+自研命令表：功能面（注册/帮助/行编辑）无缺口，USB-Serial-JTAG 与日志同口共存（main.c:156 注释在案）；若二期需自定义提示符管线/非阻塞集成，再转底层组合（不立项，留档）。
4. **错误处理 ✓**：register 失败 ESP_LOGW 降级、start_repl 失败 ESP_LOGE（:264-268），无静默吞错。
5. **偏差：无；待整改：无**。
