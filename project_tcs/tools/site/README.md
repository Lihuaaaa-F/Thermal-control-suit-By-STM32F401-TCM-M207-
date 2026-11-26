# 现场工具（`project_tcs/tools/site/`）

真机取证用的小工具。都从仓库根直接运行：

```bash
python project_tcs/tools/site/<脚本>.py ...
```

它们**不进固件**。除标注"已进 run_all"的三项自测（本目录的 test_ws_client.py、tests/ 下的 test_tap_guard.py 与 check_site_tap_guard.py）外，也不参与 `run_all.sh`。

**注入点击必须走 `site_common.tap_guarded`（S-G x3，2026-11-26 D7 事故后）**：模式页（页码 4）上
落在制冷/制热/风扇/切自动矩形里的点会直接抛 `TapRefused`，一个字节都不写探针。
矩形表与坐标来源只在 `site_common.py` 一处；`tests/check_site_tap_guard.py` 禁止别处再写一份。
依赖：Windows + Git Bash、pyserial、Pillow、`arm-none-eabi-nm`、`probe-rs`。

## 先看这个：这些工具要配台架档固件

2026-11-24（S 线 C01）起，**交付档（PROD）固件里没有调试/测试钩子**：`g_tap_inject`、`g_lvgl_freeze`、
`g_lvgl_drain`、`g_tap_drop`、`g_estop_calls`、`g_estop_dbg`、`g_ui_cmd`、`g_dbg_rx_ring`、屏链波形采集、
走查用的诊断页，都只在台架/仿真档（`TCS_PROFILE=BENCH|SIM`）里编译。所以：

```bash
# ① 构建台架档（产物在 project_tcs/build-bench/）——脚本最后会核对钩子确实在
bash project_tcs/tools/build_bench.sh
# ② 烧录（probe-rs 下载 → 多点回读校验 → 复位 → 核对版本串与主循环计数）
bash project_tcs/tools/flash_lvgl.sh project_tcs/build-bench/tcs_main.elf
# ③ 让工具用这份 ELF 取符号地址
export TCS_ELF=project_tcs/build-bench/tcs_main.elf
```

对着交付档固件跑这些工具，会在查符号那一步停下，并提示你烧台架档。交付档本身的核对用
`verify_prod_clean.py`（见下表）：它证明的是"交付镜像里没有这些钩子"，而不是"交付镜像上测过"。
在台架档上量出来的行为要能代表交付档，靠的是两档共用同一份源码，只有钩子那几段不同。

## 两个每次构建都会自动跑的核对

| 脚本 | 作用 | 什么时候跑 |
|---|---|---|
| `verify_prod_clean.py` | **交付档干净核对**：查 PROD 产物的符号表与只读数据段，不得有调试/台架钩子与命令名；反过来核对台架/仿真/诊断档里它们还在（证明查得出来） | `tools/build_matrix.sh` 每次构建后 |
| `test_mb_fwcheck.py` | `tools/mb_console.py` "板上固件身份核对"的离线自测：同一份构建的 ELF 取样与自己的 .bin 逐字相符；换一档的镜像必有不符 | 同上 |

对 ESP32 的核对：默认配置档不得含 `linkmute / linkloop / webtest / linkrepin / gedge / ltx / m207 raw /
mode / tec / fan`；诊断档（`CONFIG_TCS_DIAG=y`）必须全含；现场档（`CONFIG_TCS_SIM=y`）只含前三条。

## 写 RAM 之前的固件身份核对

`tools/mb_console.py` 的 `pr_write()`（所有探针写操作的出口）在每个进程第一次写之前，会从板载 Flash
回读 3 处各 8 个字，与 `TCS_ELF` 指向的 ELF 逐字比对，对不上就拒绝写。原因：2026-11-24 板上跑着
U 线固件，工具按 S 线的 ELF 取了地址，往一个不确定的 RAM 位置写了 16 字节。没有跳过开关；
只读操作（`pr_read`、RTT 读、截图）不受影响。

## 真机取证工具

| 脚本 | 作用 | 备注 |
|---|---|---|
| `esp_bridge.py` | ESP32 控制台常驻桥（后台跑） | 桥占着串口：**烧 ESP32 前先停它**。打开串口会让 ESP32 复位一次 |
| `esp_cmd.py` | 经桥发一条命令并打印回显 | `esp_cmd.py status` |
| `tap.py` | 探针注入一次触摸 | **先写坐标、最后写序号**，脚本已回读自检 |
| `wvar.py` | 探针写 32 位变量（符号名或地址） | `wvar.py g_lvgl_freeze 1` |
| `estop_matrix.py` | 急停屏入口矩阵（12 项）：每页点/按住、LVGL 冻结、触摸掉帧，读 `g_estop_dbg` 的毫秒数 | 需台架档 |
| `tune_refresh_check.py` | 调参页外部参数变化是否就地刷新（不整页重建） | 需台架档 |
| `rtt_live.py` | 连续读 H743 RTT 日志落盘 | 不连续读会丢日志 |
| `dlg_timing.py` | 量"注入点击到像素变色"的时间 | 每次探测约 0.35 秒，分辨率就是 0.35 秒 |
| `hist_read.py` | 读曲线历史环（60 点 × 30 秒） | 不截屏核对曲线页有没有数据 |
| `mask_lattice.py` | 弹窗点阵遮罩像素核对 / 两张截图逐像素比对 | |
| `site_web_check.py` | 网页服务端真机检查（W1–W11，自带 WebSocket 客户端） | **运行前机器要先连上设备热点**；脚本不改 WiFi |
| `test_ws_client.py` | 上面那个 WebSocket 客户端的本机回环自测 | 已进 `run_all.sh` |

ESP32 侧对应的诊断命令（也只在 SIM 或 DIAG 档）：`linkmute <秒>`（链路静默：不收不发，快照也停发；
不拔线诱导断链再恢复）、`linkloop <0|1>`（芯片内部 TX→RX 回环，验证回声防护）、`webtest`
（网页端点自检）。要 ESP32 诊断档：`idf.py menuconfig` 里打开 `CONFIG_TCS_DIAG`，或
`idf.py -B build-diag -D SDKCONFIG=sdkconfig.diag set-target esp32s3` 后在该文件里加 `CONFIG_TCS_DIAG=y`。

环境变量：`TCS_SITE_DIR`（桥的日志与命令文件目录，默认 `%TEMP%\tcs_site`）、`ESP_PORT`（默认 COM3）、
`TCS_ELF`（H743 ELF，默认 `project_tcs/build/tcs_main.elf`，即交付档）。

日志与命令文件：`%TEMP%\tcs_site\esp_out.log`（串口日志）、`esp_cmd.txt`（桥看到就取走）。

## 真机脚本的护栏（`site_lib.sh`，S 线 i3 条目 12）

D6 第八节的事故（2026-11-26）：真机脚本跑到一半、有人在同一个工作树里合并了新代码，而
`idf.py flash` **会先构建再烧**——"烧回交付档"实际烧的是新源码；两端协议版本从此不配对，
屏拒收全部快照（表现为"主控连接中断"），脚本输出里的"已烧回交付档"也不成立。

**规则（写在这里，所有真机脚本照做）：**

1. **真机脚本运行期间不改同一个工作树**：不合并、不改源码、不切分支、不 `git checkout`。
2. 烧录前先 `guard <期望HEAD> [档名] || exit 1`（`site_lib.sh`）：HEAD 必须等于期望值、已跟踪文件必须干净；
   给了档名时还要求 `esp32/build-matrix-<档>/tcs_wireless.bin` 不早于会进固件的源码。
3. 用 `FLASH <档> <标签>` 烧录：它烧前烧后各算一次固件 sha256，变了就打印 `REBUILT`——
   这一步"自动重建 + 烧录"的结果**不能当证据**，要重新登记。
4. 证据里"烧回了什么"以屏 / 主控自己报的版本串和 sha256 为准，不以脚本里的字样为准。

```bash
. project_tcs/tools/site/site_lib.sh          # 或 . "$(dirname "$0")/site_lib.sh"
guard "$(git -C "$R" rev-parse --short=9 HEAD)" default || exit 1
FLASH default d7-step0
```

环境变量：`TCS_SITE_REPO`（工作树根，缺省=本文件所在仓库）、`TCS_SITE_PORT`（串口，缺省 `COM3`）、
`TCS_SITE_LOG`（日志目录）、`TCS_SITE_BAUD`（烧录波特率，缺省 921600）。

