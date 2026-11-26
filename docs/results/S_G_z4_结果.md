# S-G z4 结果（收尾补做批次）— 2026-11-26

分支 `fix/s-z4`，基座 = 集成分支 `fix/s-u-int2` @ `b61c193`（`git merge --ff-only` 报 Already up to date，无新增）。
本批次**不碰设备**：没有 probe-rs、没有开串口、没有烧录，也没有任何驱动 TEC 的操作。关单条件里"真机……"的部分
一律没做，写进各条的「遗留」。

本批次的任务是"未做"清单里能在不碰设备前提下做掉的几条。开工核对的结果是：**7 条里有 4 条审查之后已被别的提交
修掉**（M08-F-11、R5-11、M02-F-07、M03-F-09），本次只补门禁 / 补证据；真正动代码的是 M08-F-13（OTA 两条路径）、
R8-06、X03-F-11。

## 一、提交列表（按先后）

| # | 提交 | 条目 | 一句话 |
|---|---|---|---|
| 1 | `b5e07ec` | M08-F-13 | OTA 拷贝完成 / 回滚两条重启路径在 `esp_restart()` 前调 `param_persist_now()`（失败也重启，只记日志）+ `test_web_gates.py` 三条重启路径门禁 |
| 2 | `559b756` | M08-F-11 | `gedge` 忙等循环的让出与 2000ms 上限门禁（代码已在 6a7b704 修过，本次只补回归网） |
| 3 | `32aaafe` | M02-F-07 | `tests/check_keypad_pd5.py`：PD5 只归 USART2_TX，矩阵键盘复活即红 |
| 4 | `5d6138b` | R8-06 | `control_stuck_max_ms` 扩到 3 路（体温 / TC1 / TC2），`mstat` 分开报；屏端 `stat` 加 `vrlast=` |
| 5 | `c6d51a9` | X03-F-11 | `perf` 打 7 个任务的栈余量（DIAG/SIM）+ 源码门禁 |
| 6 | `b91c466` | 收尾 | 研学卡行号重锚（console.c / main.c 增行后 40 处红） |
| 7 | （收尾） | — | 本文件 + 官方库审计机器报告与文件清单重新生成（单独提交，合并时由协调者统一再生成） |

## 二、逐条判定

判定只用五种。**统计：已修并验证 0、已实施待真机 3、已修未记 4、不适用 0、未做 1。**

### 1. M08-F-13 `reset` / OTA 重启不落盘 —— **已实施待真机**

- **现状核实**：`cmd_reset` 在 R1 已修（`esp32/main/tcs_console.c:468-477`，先 `param_persist_now()` 再
  `esp_restart()`）；**OTA 两条重启路径没有**：`ota_copy_task`（拷贝完成后）与 `app_ota_trigger` 的回滚分支都是
  `vTaskDelay(1000)` 直接 `esp_restart()`。参数的常规落盘是 2 秒防抖 + 500ms 冲洗（`param_persist_flush`），
  这 1 秒里改的参数就随重启丢了。
- **改了什么**：两条路径在 `esp_restart()` 之前各调一次 `param_persist_now()`；返回值进日志（`rv<0` → `ESP_LOGE`），
  **落盘失败也照样重启**（boot 分区已经切了，停在运行态只会让网页/屏以为 OTA 没生效——按 z4 说明的口径，
  不是审查方案里"失败拒绝重启"）。
- **怎么验证的**：`esp32/tests/test_web_gates.py` 新增 M08-F-13 门禁：三条重启路径（`cmd_reset` / `ota_copy_task` /
  `app_ota_trigger` 回滚）各查"`param_persist_now()` 恰好一次、在 `esp_restart()` 之前、两者之间没有
  return/goto/break（失败不拦重启）"。对基座 `b61c193` 导出跑 → 两条 OTA 路径红；修后 `test_web_gates: PASS`。
- **遗留（真机）**：改一个参数后 100ms 内 `reset`，重启后值仍为新值；OTA 前后各改一次参数同理。

### 2. M08-F-11 `gedge` 忙等最长 5s 无 yield —— **已修未记**

- **现状核实**：提交 `6a7b704` 已把上限收到 2000（`esp32/main/tcs_console.c:377`）并在循环体里
  `vTaskDelay(1)`（`:384`），与审查方案一致。审查时"5000 + 纯忙等"的代码在基座上已不存在。
- **改了什么**：只补门禁（`esp32/tests/test_m207_gates.py` 新增 M08-F-11）：上限 2000、循环体（截到配平的右花括号）
  每轮有 `vTaskDelay`/`vTaskDelayUntil`/`taskYIELD`、时间界用 `esp_timer_get_time()`。
- **怎么验证的**：门禁绿；把 `vTaskDelay(1);` 临时删掉 → 该门禁两条转红（已实测，随即还原）。
- **遗留（真机）**：`gedge 36 2000` 连跑不再触发 TWDT（3s，PANIC）复位；这条只能在板上看。

### 3. R5-11 代码注释保留提交说明已撤回的"约 2s" —— **已修未记**

- **现状核实**：`project_tcs/App/lvgl_ui/ui_common.c:313-319` 已由提交 `11037bd` 更正为现场实测的
  182ms → 82ms，并明写"原先写的'弹窗约 2s 才画完整 / 瓦片数约 1100'两处都不对……（2026-11-25 更正，
  独立复核 R5-11）"。全仓（`project_tcs/App/lvgl_ui` + `esp32/`）再查 `约 2s` / `约2s`，只剩这一处更正说明
  与两处与弹窗无关的看门狗说明（`iwdg.h`、`Core/Src/main.c` 的"约 2s 后看门狗复位"）。
- **改了什么**：无（不重复修）。核对方法：逐字读这两段 + `git log -S` 确认修正提交。
- **遗留**：无。

### 4. M02-F-07 keypad 公开 API 会把 PD5 从 USART2_TX 改成 GPIO —— **已修未记**

- **现状核实**：`App/keypad.c` 已由提交 `b14a22f` 删除（`project_tcs/CMakeLists.txt:66-67`、`Core/Src/main.c:22`、
  `Core/Inc/main.h:17` 都留了说明）；PD5 在 `MX_UART_Init` 里显式配成 AF7
  （`project_tcs/Core/Src/main.c:886`）。
- **改了什么**：只补源码门禁 `project_tcs/tests/check_keypad_pd5.py`（接在 `run_all.sh` 末尾，`# S-G z4`）：
  App/ 下没有 keypad 文件、剥注释后全仓没有 `keypad_init/scan/poll` 调用、USART2 初始化里 PD5 显式 AF7、
  App/ 里没有把 GPIOD pin5 配成普通 GPIO 的 `HAL_GPIO_Init`。
- **怎么验证的**：门禁绿；临时在 App/ 放一个 `keypad_tmp_revive.c`（含 `keypad_init()` 调用 + PD5 输入配置）
  → 三条红，删掉即恢复绿（已实测）。
- **遗留**：无（纯源码判据）。

### 5. M03-F-09 字体门禁漏掉注册表上屏的参数名 / 单位 —— **已修未记**

- **现状核实**：提交 `8a0d246` 已把 `tools/font_audit_lvgl.py` 的扫描面扩到两份参数注册表
  （`App/tcs/param_registry.c` 与 `esp32/.../param_registry.c`），并且加了"扫不到就判红"的样本下限。
- **怎么验证的（判别性）**：把 H743 注册表里的 `环境水箱设定温度` 临时改成 `环境水箱设定温度龘` →
  `[A] 字面量 561 条 / 字符 365 个 → 24px 链缺 1 个`、`param_registry.c(H743): 缺 1 → 龘`、
  `FONT_AUDIT_LVGL: FAIL`；还原后 PASS。门禁确实覆盖注册表，不是只扫界面目录。
- **遗留**：无。

### 6. R8-06 `mstat` 的 water max 混 TC1/TC2；`vr=` 看不到对端版本 —— **已实施待真机**

- **现状核实**：`control.c` 里 TC1 与 TC2 两路水温的 `stuck_feed(..., 1u)` 共用 `s_stuck_max_ms[1]`，
  读的人分不清是哪一路冻着；屏端 `stat` 只有版本拒收计数 `vr=`，看不出对端报的是哪一版。
- **改了什么**：① `control_stuck_max_ms` 的 `which` 扩到 3（1=体温、2=TC1 水温、3=TC2 水温），
  `s_stuck_max_ms[3]`、TC2 走 idx=2 单独统计；`mstat` 打 `body / water TC1 / water TC2` 三个数。
  ② H743 记下最近一帧 0x20 的 `ver` 原值（`tcs_link_snap_ver_last()`，`0xFFFFFFFF` = 还没收到过），
  `stat` 行加 `vrlast=`；两个数的语义写进 `link_snap.h` 与 `console.c` 的注释。
- **怎么验证的**：`test_control_esp32.c` 在"TC1 一直动、TC2 冻 10 秒"的窗口里断言 `which=3` ≥9.9s 且
  `which=2 < which=3`；对基座 `control.c` 实测 **PASS=362 FAIL=2**（两条新断言红），修后 **PASS=364 FAIL=0**。
  `test_m1_gates.py` 的锚点同步到 `[3]` 并新增"TC2 走 idx=2"；`verify_prod_clean.py` 的禁入清单没松
  （mstat 仍在，三档核对全 PASS）。
- **遗留（真机）**：TEC 开着时 `mstat` 两路水温的实际秒数；两端协议版本真的不同时 `vrlast` 的实际取值。

### 7. X03-F-11 任务栈水位没有观测点 —— **已实施待真机**

- **改了什么**：`cmd_perf` 在 `vTaskList` 之后打一行
  `[perf] hwm(bytes): tcs_snap=… tcs_link=… ctrl_svc=… m207_svc=… tcs_ws=… tmp117_svc=… p_persist=…`；
  句柄用 `xTaskGetHandle(名字)` 取（不动各组件、不改优先级与栈大小）。整块进
  `#if CONFIG_TCS_DIAG || CONFIG_TCS_SIM`，交付档不编译。
  **单位**：IDF v5.5.5 的 `uxTaskGetStackHighWaterMark` 返回**字节**（`task.h:1499` 原文 "in bytes"，
  不是标准 FreeRTOS 文档里的字数），与 `xTaskCreate` 的栈参同单位——注释与打印标签都按字节写。
- **怎么验证的**：`test_m207_gates.py` 新增 X03-F-11 门禁——两个 API 都在、覆盖 5 个必查任务、
  打印行位于 DIAG/SIM 的 `#if` 内（把 `#if` 改成 `#if 1` 实测转红，随即还原）。
- **实测到的栈水位数值**：**本批次拿不到**（不碰设备，没有运行时）。可控的是预算：`tcs_snap` 3072 B、
  `tcs_link` 4096 B、`ctrl_svc` 4096 B、`m207_svc` 4096 B、`tcs_ws` 4096 B、`tmp117_svc` 4096 B、
  `p_persist` 4096 B。**第一次上机敲一次 `perf` 即可填上这 7 个数**；审计方案里的"余量 <25% 就抬栈"
  （`tcs_snap` 3072→4096）也等这批读数再决定——按 z4 约束本次不改栈大小。
- **遗留（真机）**：`perf` 的 7 个 hwm 读数（尤其 `tcs_snap`）。

### 8. R9-02 的挂账项 —— **未做**

- 10 分钟窗口 vs 盲窗的取舍是用户决定，本批次不碰；**仍待用户决定**。

## 三、验证总结果

| 项 | 命令 | 结果 |
|---|---|---|
| ESP32 主机全量 | `esp32/` 下 `bash tests/run_m207_tests.sh` | `esp32-tests: ALL GREEN` |
| H743 主机全量 | `project_tcs/` 下 `bash tests/run_all.sh` | `run_all: ALL GREEN`（含 tautology / assert_count / 字库 / 审计 / 研学卡门禁） |
| 变异（H743） | run_all 内 | mut-estop 20 / mut-link 24 / mut-follow 8 / mut-txq 6 / mut-audit 7 / mut-perf 5 —— 全部 `0 个异常` |
| 变异（ESP32） | `python esp32/tests/mutation_control.py` / `mutation_control_manual.py` / `mutation_core.py` / `mutation_web.py` | 四套 `ALL AS EXPECTED` |
| 变异（ESP32，既有瑕疵） | `mutation_m207_force.py` | `HAS PROBLEMS (1)`：F09 锚点 `(void)m207_tec_force(1, 0);` 在 `m207_core.c` 里出现 2 次 → SKIP「变异表过期」。**基座即如此**（`m207_core.c` 本批次零改动），不是本次引入；本批次不动它（属 i3 的变异表维护） |
| 协议同步 | `python tools/proto_sync.py --check` | `proto_sync: OK`（单一源 + 2 包装一致；版本取自 `TCS_PROTO_VER`） |
| 构建矩阵（唯一一次） | `bash tools/build_matrix.sh --esp32 --diag --site --twice` | `build_matrix: PASS`：PROD / BENCH / SIM / SIMWG 各 `error=0 warning=4`（基线 4，余下全在第三方 qrcodegen）；ESP32 默认 / 诊断 / 现场三档 `error=0 warning=0`；`[prod-clean] PASS` ×3；两次从零构建 md5 相同（`7ff8c4a9…`）、ESP32 app/bootloader/分区表 sha256 相同 |
| 交付档干净核对 | 矩阵内 `verify_prod_clean.py` | 默认档无台架命令；诊断档命令集相符；现场档含 `linkloop,linkmute,mstat,water,webtest` → 三档 PASS（`mstat` / `perf` 的 hwm 行都不进交付档） |

## 四、风险与需要人看的点

1. **`sdkconfig.site` 与 coredump 门禁冲突（新发现，非本批次引入）**：按 z4 说明把
   `D:\Espressif\ws\tcs\esp32\sdkconfig.site` 复制进工作树跑 `--site`，构建 PASS；但这份文件里
   `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH is not set`、`CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y`，
   `esp32/tests/check_layout.py` 第 6 项因此判红（"现场档就没有故障证据"）。本次验证矩阵用的是这份配置，
   跑完已把副本删掉（它不入库），`run_all` 复跑 ALL GREEN。**需要决定**：现场档到底要不要 coredump——
   要的话，协调机上的那份 `sdkconfig.site` 得改（另改 `check_layout` 的期望也行，但那是松门禁）。
2. **`mutation_m207_force.py` F09 锚点过期**（`m207_core.c` 里同一句出现 2 次）：基座即红，与 z4 无关。
   建议下一批把它改成带上下文的一小段锚点。
3. **栈水位没有实测值**：X03-F-11 的观测点建好了，但"余量 <25% → `tcs_snap` 抬到 4096"这个决定仍然悬着，
   等第一次上机 `perf` 的 7 个数。
4. **OTA 落盘失败仍然重启**：这是 z4 说明的口径（与审查方案相反）。理由是 boot 分区已经切了，
   停在运行态会让网页/屏以为 OTA 没生效；代价是极端情况下（NVS 写坏）这一次参数变更丢失。
   如果希望"落盘失败就不重启、留在本槽并报错"，要改回审查方案——这需要协调者定。
5. **`perf` 的 hwm 行是逐任务查名字**：任务改名会让那一路打 `-`（不会崩）。名字是 `tcs_snap` / `tcs_link` /
   `ctrl_svc` / `m207_svc` / `tcs_ws` / `tmp117_svc` / `p_persist`，门禁里钉住了前 5 个。
