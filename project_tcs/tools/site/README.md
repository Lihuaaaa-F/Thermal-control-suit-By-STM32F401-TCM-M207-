# 现场工具（`project_tcs/tools/site/`）

真机取证用的小工具。都从仓库根直接运行：

```bash
python project_tcs/tools/site/<脚本>.py ...
```

它们**不进固件**，也不参与 `run_all.sh`。依赖：Windows + Git Bash、pyserial、Pillow、`arm-none-eabi-nm`、`probe-rs`。

| 脚本 | 作用 | 备注 |
|---|---|---|
| `esp_bridge.py` | ESP32 控制台常驻桥（后台跑） | 桥占着串口：**烧 ESP32 前先停它**。打开串口会让 ESP32 复位一次 |
| `esp_cmd.py` | 经桥发一条命令并打印回显 | `esp_cmd.py status` |
| `tap.py` | 探针注入一次触摸 | **先写坐标、最后写序号**，脚本已回读自检 |
| `wvar.py` | 探针写 32 位变量（符号名或地址） | `wvar.py g_lvgl_freeze 1` |
| `rtt_live.py` | 连续读 H743 RTT 日志落盘 | 不连续读会丢日志 |
| `dlg_timing.py` | 量"注入点击到像素变色"的时间 | 每次探测约 0.35 秒，分辨率就是 0.35 秒 |
| `hist_read.py` | 读曲线历史环（60 点 × 30 秒） | 不截屏核对曲线页有没有数据 |
| `mask_lattice.py` | 弹窗点阵遮罩像素核对 / 两张截图逐像素比对 | |
| `site_web_check.py` | 网页服务端真机检查（W1–W11，自带 WebSocket 客户端） | **运行前机器要先连上设备热点**；脚本不改 WiFi |
| `test_ws_client.py` | 上面那个 WebSocket 客户端的本机回环自测 | 已进 `run_all.sh` |

`g_tap_inject`、`g_lvgl_freeze` 这类是**诊断钩子**：PROD 交付档里没有（见 C01），要用带钩子的构建。

环境变量：`TCS_SITE_DIR`（桥的日志与命令文件目录，默认 `%TEMP%\tcs_site`）、`ESP_PORT`（默认 COM3）、
`TCS_ELF`（H743 ELF，默认 `project_tcs/build/tcs_main.elf`）。

日志与命令文件：`%TEMP%\tcs_site\esp_out.log`（串口日志）、`esp_cmd.txt`（桥看到就取走）。
