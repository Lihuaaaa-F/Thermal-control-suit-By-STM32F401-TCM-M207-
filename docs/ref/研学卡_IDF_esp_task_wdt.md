# 研学卡 · IDF esp_task_wdt（任务看门狗）（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **5** == 表行数 5 ✓

## ① 官方文档与版本章节

- 组件头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/esp_system/include/esp_task_wdt.h`（**v5.5 归属事实**：esp_task_wdt 实体在 esp_system 组件内，无独立 components/esp_task_wdt 目录——制卡时 ls 复核）
- **头注原文摘录（逐字，esp_task_wdt.h L80-83，`esp_task_wdt_add`）**：
  > `* @brief Subscribe a task to the Task Watchdog Timer (TWDT)`
  > `*`
  > `* This function subscribes a task to the TWDT. Each subscribed task must periodically call esp_task_wdt_reset() to`
  > `* prevent the TWDT from elapsing its timeout period. Failure to do so will result in a TWDT timeout.`
- **头注原文摘录（逐字，esp_task_wdt.h L85，NULL 参数语义）**：
  > `* @param task_handle Handle of the task. Input NULL to subscribe the current running task to the TWDT`

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/system/task_watchdog/main/task_watchdog_example_main.c`

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rn "esp_task_wdt" esp32/components esp32/main --include=*.c
```
**命中 5 行 == 下表 5 行**（域：自研 tcs_core/main，无 managed_components）。

| 文件:行 | API | 语义 |
|---|---|---|
| esp32/components/tcs_core/control_service.c:16 | esp_task_wdt.h（include） | 头引入（非调用） |
| esp32/components/tcs_core/control_service.c:56 | esp_task_wdt_add(NULL) | ctrl 任务自订阅 TWDT（超时 3s，F3/F12） |
| esp32/components/tcs_core/control_service.c:66 | esp_task_wdt_reset | 每拍喂狗（500ms 间隙 <1s 铁律；E-20 悬停喂） |
| esp32/main/r1e_probe.c:19 | esp_task_wdt.h（include） | 头引入未直调（经 control_service_hang_test 触发 E-20） |
| esp32/main/main.c:156 | esp_task_wdt（注释） | main.c 初始化注释提及订阅（非调用） |

## ④ 对照结论

1. **add(NULL)/reset 配对 ✓（W10 #23-24）**：`esp_task_wdt_add(NULL)` 在 control_task 入口自订阅（:56），逐字对上头注 L85"NULL=订阅当前运行任务"；循环内 `esp_task_wdt_reset()`（:66）按头注 L82-83"must periodically call"口径周期喂狗。
2. **喂狗周期 ✓**：控制拍 500ms（vTaskDelayUntil 节拍，见 FreeRTOS 卡 :58 行）远小于 3s 超时（CONFIG_ESP_TASK_WDT_TIMEOUT），且"间隙 <1s 铁律"注记在案。
3. **E-20 故意停喂 ✓**：`if (!s_hang_test) esp_task_wdt_reset()`（:66）——E-20 看门狗自驱取证经 `control_service_hang_test(1)`（r1e_probe.c:212 调用）悬停喂狗制造超时，属有意测试钩子非缺陷。
4. **无 delete=正确口径 ✓（复审表 #24）**：ctrl 任务常驻不退出，订阅随任务生命周期，无需 esp_task_wdt_delete；对照官方 task_watchdog 示例（示例含 add/reset/超时路径，临时任务场景才需 delete）。
5. **偏差：无；待整改：无**。订阅面收敛于温控任务单一入口，其余任务（link/m207/ws/snap）未订阅=各服务自报健康（offline/alarm 路径），TWDT 定位为"控制闭环卡死"哨兵，范围设计如此。
