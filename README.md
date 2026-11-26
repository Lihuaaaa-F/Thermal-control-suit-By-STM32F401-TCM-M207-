# 调温服 TCS（STM32H743 屏幕终端 + ESP32-S3 主控）

STM32H743IIT6 = **纯屏幕终端**（WKS 7" 1024×600 RGB + FT5446 触摸 + 急停入口，渲染/触摸/急停）；
ESP32-S3 = **主控**（M207 温控模块 / TMP117 / 风扇直连，控制闭环 / 注册表真值 / NVS / WiFi 热点+网页）。
两端经 UART2 230400 成对通信（快照下行、指令上行）。仓库同时含 H743 固件（`project_tcs/`）
与 ESP32 端（`esp32/`）。**固件版本以 `protocol/tcs_version.h` 为唯一源**（应用版本与数值化的 `fw_ver` 都从它推导；定版后 `python tools/version_bump.py --set X.Y.Z[-后缀]` 一处改全仓，`--check` 核对没有第二处写死的固件版本号）。下文提到的 v1.1.0 / v1.2.0 指某一轮的规格快照，不是当前固件版本。

> 📖 **规格正典：`docs/现行规格说明书_v1.2.md`**（唯一现行规格；架构/参数表/协议/
> 📖 **UI 规范正典：`docs/现行UI规范_v1.2.md`**（视觉/交互/文案唯一权威；v9.5 K13）
> FMEA/性能实测全部以此为准，历史任务书冲突时以正典为准）。
> 用户侧入口：`docs/用户手册_v1.2.md`；交付明细：`docs/交付清单_v1.1.0.md`（v1.1.0 历史快照，现行版本见 `protocol/tcs_version.h`）。

## 工具纪律（每日开工必读）

| 环节 | 命令（Git Bash，仓库根） |
|---|---|
| 编译 H743 | `cd project_tcs && cmake --build build` |
| 烧录 | `probe-rs download build/tcs_main.elf --chip STM32H743II --probe 0d28:0204` |
| **烧后必做** | `probe-rs reset --chip STM32H743II --probe 0d28:0204`（download 不自动复位；**必须核对 uptime 确认真复位**） |
| 读写内存 | `probe-rs read/write --chip ... b32 <地址> <字数>`（WORDS 必填；变量地址用 `arm-none-eabi-nm build/tcs_main.elf` 现查） |
| RTT 日志 | `probe-rs attach --chip ...`（非交互管道可能无输出；固件内 `tools/mb_console.py` 直读 RTT 环） |
| 无线控制台 | `python tools/mb_console.py "ver" "stat" "perf"`（RAM 邮箱注入命令，无需 COM/USB-TTL） |
| 屏幕取证 | `python tools/fb_dump.py out.png`（读前缓冲转 PNG，像素级"拍照"） |
| PC 单测 | `bash tests/run_all.sh`（含 font_audit 字库门禁，必须全绿才能提交） |
| 字库重生成 | 改 UI/注册表文案后：`python tools/gen_font_ui.py && python tools/font_audit_lvgl.py`（`run_all.sh` 已含字库门禁）|
| 活性体检 | `bash tools/wp_check.sh`（reset→loop_passes 增长判定） |
| 构建矩阵 | `bash tools/build_matrix.sh [--esp32] [--diag] [--site] [--twice]`（H743 PROD/BENCH/SIM/SIMWG 从零构建 + 告警基线核对 + **交付档干净核对**；`--esp32 --diag --site` 再做 ESP32 三档；`--twice` 比对可复现 md5；约 6 分钟）|
| 夜间回归 | `bash tools/nightly.sh`（先 source IDF：构建矩阵全部档位与核对 + PC 单测；约 6 分钟）|
| 答辩彩排 | `python tools/rehearsal.py <遍数>`（附录 A 20 项自动评分，板上 sim 固件）|
| 台架一键 | `bash tools/bench.sh [--device]`（PROD 增量构建→单测；`--device` 才烧录→回读校验→活性→体检）|
| 烧录脚本 | `bash tools/flash_lvgl.sh [ELF] [--check-only]`（probe-rs 下载→多点回读校验→复位→核对版本串与主循环计数）|
| 台架档构建 | `bash tools/build_bench.sh`（构建 BENCH 到 `build-bench/` 并核对钩子齐全——现场探针工具要烧这一档）|
| 现场工具 | `python project_tcs/tools/site/<脚本>.py`（串口桥、点击注入、急停入口矩阵、调参页重建检查、网页服务端检查等，见该目录 README）|
| ESP32 | `source ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/activate_idf.sh && cd esp32 && bash .../esp_loop.sh all` |

> **构建档位（2026-11-15 起；2026-11-24 补齐钩子隔离）**：H743 用 `-DTCS_PROFILE=PROD|BENCH|SIM`（默认 **PROD**）。
> 交付/日常 = PROD：不含 `pindrv/pinrel/pinscan/wdttest/wave/txwave/rxraw`、`mode/tec/fan`、`m207 send`，
> **也不含调试/测试钩子**（`g_ui_cmd` 走查命令、`tui_diag` 诊断页、`g_tap_inject`/`g_tap_drop` 合成触摸与丢帧、
> `g_lvgl_freeze`/`g_lvgl_drain`、`g_estop_calls`/`g_estop_dbg`、屏链波形采集、`g_dbg_rx_ring`）和 LVGL demos。
> 台架/诊断 = BENCH（`cmake ... -DTCS_PROFILE=BENCH`，或直接 `bash tools/build_bench.sh`）。
> 只读遥测（`g_lvgl_guard`、`g_tl8`、`g_budget`、`g_dbg_snap_gap_*`）三档都留。
> **交付档里没有什么是靠产物核对证明的**：`python project_tcs/tools/site/verify_prod_clean.py --elf build/tcs_main.elf`，
> 构建矩阵每次都跑。ESP32 同理：`m207 raw`、`mode/tec/fan`、`linkrepin`、`gedge`、`ltx` 需 `CONFIG_TCS_DIAG=y`；
> `linkmute`/`linkloop`/`webtest` 需 `CONFIG_TCS_DIAG` 或 `CONFIG_TCS_SIM`；注入 `body` 需 `CONFIG_TCS_SIM=y`；
> 演示模式需 `CONFIG_TCS_DEMO=y`（三者默认 n，menuconfig 里开）。
> **照本文档的台架命令操作前先确认档位，否则会得到 `unknown command`。**
> 条件编译的代码必须在让它生效的那一档上编译验证（2026-11-24 教训：默认档 0 告警、SIM/DIAG 档编译失败）。

## 硬性纪律

- **pyocd 禁用**（退出留核 HALT）；halt 类调试禁用于温控回路（加热安全）。
- IWDG=2s（WP-C.1）；`wdttest` 命令可验证（复位后 `reset-src=IWDG`，IWDG1RSTF 判定优先于 PIN）。
- 架构：注册表=唯一数据源；三层单向 `驱动(ltdc/uart/i2c/eeprom) ← 服务(m207/tmp117/control/registry/post) ← 视图(lvgl_ui/console/link)`；协议 MsgType 只加不改。
- **H6 POST+动态布防**：开机六项自检（TMP117 ID/M207 一拍/EEPROM CRC/参数表/屏触/版本），角标 3s（`post badge [ms]` 重显）；布防三态机 0x08=0 auto/1 强制布防/2 强制不布防；合理性滤波体温 25~45°C |Δ|≤2°C/s、M207 |Δ|>10°C/s 按失联。`post` 命令看六项结果与布防态。
- 上屏字符串改动必须重生成字库并过 font_audit（H3 教训）；UI 改动必须帧缓冲取证入 `docs/results/`。
- 失败 2 次停：按 embedded-dev-loop §4 格式报告并记结果文档。

## 目录

- `project_tcs/` — H743 固件（CMake + arm-none-eabi + Ninja）
- `esp32/` — ESP32-S3（IDF v5.5；tcs_link 链路 / tcs_web 网页 / tcs_wifi 热点）
- `docs/results/` — 各 WP 结果文档与总文档（验收入口）
- `docs/evidence/` — 运行时证据（帧缓冲 PNG、日志）
