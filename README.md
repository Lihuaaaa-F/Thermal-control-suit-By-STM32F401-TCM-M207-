# 调温服 TCS（STM32H743 地面站板 + ESP32-S3 无线端）

主控 STM32H743IIT6（WKS 7" 1024×600 RGB + FT5446 触摸，M207 温控模块 / TMP117 /
风扇直连），ESP32-S3 WiFi 热点+网页。仓库同时含 H743 固件（`project_tcs/`）
与 ESP32 端（`esp32/`）。**现行版本 fw/网页 v1.1.0。**

> 📖 **规格正典：`docs/现行规格说明书_v1.1.md`**（唯一现行规格；架构/参数表/协议/
> FMEA/性能实测全部以此为准，历史任务书冲突时以正典为准）。
> 用户侧入口：`docs/用户手册_v1.0.md`；交付明细：`docs/交付清单_v1.1.0.md`。

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
| 字库重生成 | 改 UI/注册表文案后：`python tools/gen_font_cn.py && python tools/font_audit.py` |
| 活性体检 | `bash tools/wp_check.sh`（reset→loop_passes 增长判定） |
| 夜间回归 | `bash tools/nightly.sh`（双构建/单测/活性/基准帧 diff/fuzz/压力快照/丢包注入；`--init` 生成帧基线）|
| 答辩彩排 | `python tools/rehearsal.py <遍数>`（附录 A 20 项自动评分，板上 sim 固件）|
| 台架一键 | `bash tools/bench.sh`（构建→单测→烧录→复位→活性→体检）|
| 出厂复位 | `bash tools/factory_reset.sh` |
| ESP32 | `source ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/activate_idf.sh && cd esp32 && bash .../esp_loop.sh all` |

## 硬性纪律

- **pyocd 禁用**（退出留核 HALT）；halt 类调试禁用于温控回路（加热安全）。
- IWDG=2s（WP-C.1）；`wdttest` 命令可验证（复位后 `reset-src=IWDG`，IWDG1RSTF 判定优先于 PIN）。
- 架构：注册表=唯一数据源；三层单向 `驱动(ltdc/uart/i2c/eeprom) ← 服务(m207/tmp117/control/registry/post) ← 视图(tcs_ui/console/link)`；协议 MsgType 只加不改。
- **H6 POST+动态布防**：开机六项自检（TMP117 ID/M207 一拍/EEPROM CRC/参数表/屏触/版本），角标 3s（`post badge [ms]` 重显）；布防三态机 0x08=0 auto/1 强制布防/2 强制不布防；合理性滤波体温 25~45°C |Δ|≤2°C/s、M207 |Δ|>10°C/s 按失联。`post` 命令看六项结果与布防态。
- 上屏字符串改动必须重生成字库并过 font_audit（H3 教训）；UI 改动必须帧缓冲取证入 `docs/results/`。
- 失败 2 次停：按 embedded-dev-loop §4 格式报告并记结果文档。

## 目录

- `project_tcs/` — H743 固件（CMake + arm-none-eabi + Ninja）
- `esp32/` — ESP32-S3（IDF v5.5；tcs_link 链路 / tcs_web 网页 / tcs_wifi 热点）
- `docs/results/` — 各 WP 结果文档与总文档（验收入口）
- `docs/evidence/` — 运行时证据（帧缓冲 PNG、日志）
