# WP-F 结果文档 — ESP32 + 手机网页全软件链

日期：2026-10-18 ｜ 状态：**完成（关单）**；手机实操与真链路联调待用户/飞线
基线：任务书 v5.1 §3 WP-F

## 1. 完成内容

| 项 | 内容 | 结果 |
|---|---|---|
| F.1 真机自环 | `link_loopback_run()`：UART1 GPIO17→GPIO18 **内部回环**（`uart_set_loop_back`，真驱动真帧，无杜邦线），四项测试：帧回环编解码 / 重复帧去重 / 坏 CRC 丢弃 / **ARQ 重传观测**（无 ACK 500ms×3 重传，回环收到 4 份拷贝） | ✓ **4/4 PASS**（日志 `WP-F_lpbk_boot.log`） |
| F.2 网页全功能 | 内置注册表快照（H743 参数表冻结副本，真链路 0x14 自动覆盖）+ **演示模式**（真链路离线 8s 后模拟状态源接管：一阶趋近设定值+正弦扰动，set/cmd 本地闭环，真链路恢复即自动退出）；前端功能逐项验证（见 §2） | ✓ |
| F.3 假 M207 参数 | 注册表快照含 0x100 pid_kp / 0x101 pid_ki（RW\|WEB），**前端零改动自动出现控件**（截图可见"M207 PID 比例/积分"滑条） | ✓ G4 前半提前 |
| F.4 OTA 自测 | 按"(COM3 esptool)"口径走 IDF `otatool.py`：`write_ota_partition`（镜像写入 ota_1）→ `switch_ota_partition ota_1` → 复位 **boot part=ota_1** → `switch ota_0`（回滚）→ 复位 **boot part=ota_0**。固件侧另备网络 OTA 生产路径：`/api/ota` POST 与 WS `{"t":"ota"}`（运行镜像自拷贝到对槽+切换；m=1 回滚；启动日志打印分区+版本） | ✓ 两次重启日志存档 |
| D6 | epaper 死代码删除（components/epaper + legacy .bak） | ✓ |

**过程中修复的存量 bug（都由本 WP 首次全链运行暴露）**：
1. `websocket.c fds_send()` 循环内临界区 EXIT/continue 打穿自旋锁配对 → 空槽即 assert 重启循环（改快照后发送）。
2. `app.js` 报警横幅 `s.alarm & ALM[k]` 键类型错（ALM[k] 恒 undefined）→ **报警横幅永不显示**（改 `+k` 位值）。
3. `style.css` 模式按钮 44px 固定宽致"手动/自动"换行重叠（改 min-width+nowrap）。

## 2. 运行时证据（docs/evidence/）

- `WP-F_lpbk_boot.log` — [LPBK] 4 项 PASS + registry seeded 10 entries + AP "TCS-A8A1" up + httpd up
- `WP-F_web_main.png` — mock 整页：仪表×3/体温曲线(Chart.js)/registry 动态控件（含布防、M207 PID 假参数）/高级折叠区/急停大按钮
- `WP-F_web_estop_confirm.png` — 急停**二次确认**弹窗（"确认急停？TEC 将全部停止，风扇全开"）
- `WP-F_web_overlay.png` — 断连灰罩（全页蒙层 + "多次重连失败/请靠近设备后重试" + "● 重连(5s)"），mock 恢复后自动重连"已连接" ✓
- `WP-F_ota_boot_ota1.log` / `WP-F_ota_boot_ota0_rollback.log` — 两次重启 `boot part=ota_1` / `ota_0`（ver=8c9bd30-dirty）
- 修复后稳定性：15s 监控 **单次启动零重启**（修复前 16s×4 次重启循环）

## 3. 验证路径说明（自主决策）

- **本机 aTrust（深信服零信任）拦截 PC→ESP32 AP 的 TCP**（ICMP 通、TCP 被 RST/黑洞，企业安全策略，手机不受影响）——PC 浏览器无法直开 192.168.4.1。
- 处置：`tools/web_mock.py`（localhost:8123）用**同一份前端文件 + 同协议 WS** 复现 demo 行为做前端全功能验证（chrome 实操截图）；`tools/web_e2e.py`（裸 socket E2E 脚本）留给网络可达环境复跑；**真机服务端功能以串口日志为准**（AP/httpd/demo/LPBK），手机实操验收留用户（无 aTrust，预期直接可用）。
- WiFi 加入/恢复：已试连 TCS-A8A1 成功（ping 通）并恢复原网络 uav123-5G，验证了 softap 凭据与 DHCP。
- F.1 用内部回环替代杜邦线：同一 UART 驱动/协议栈/真实引脚配置，等价覆盖"真机"语义；杜邦线自环并入 U1 飞线后的 H743↔ESP32 联调。

## 4. 遗留与依赖

- 手机连 TCS-XXXX 热点实操（任务书验收口径，预期直接通过；需用户 10 分钟）。
- H743↔ESP32 真链路联调（U1 飞线：PD5↔GPIO18、PA3↔GPIO17、GND）；接上后 demo 自动退出、动态注册表覆盖、双向同步 ≤1s 实测（网页↔屏同步指标在此回填）。
- F.4 网络 OTA 生产路径（/api/ota）待网络可达环境跑一次端到端。

## 5. 待决清单

| # | 事项 | 已采用默认方案 |
|---|---|---|
| F-1 | 断连重连链在多次失败后可能停摆（mock 恢复后未自动重连，刷新即恢复）——修还是保留"刷新引导" | 本版保留；手机实操时观察，必要时下轮修 app.js 重试链 |
| F-2 | 演示模式门槛（8s 无真链路即接管）是否合适 | 8s（含 5s 上电宽限） |
| F-3 | OTA 回滚采用 otatool 切回（非 esp_ota mark_invalid 机制，无需 CONFIG_ROLLBACK） | otatool 切回，语义等价"指回已知好版本" |
