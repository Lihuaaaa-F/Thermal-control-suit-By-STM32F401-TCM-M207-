# 网页端第二轮修复结果（V1–V12 + F1–F16）

日期：2026-11-28
基线：`d53070e`（第二轮盘点，只盘点未改）
范围：`esp32/components/tcs_web`（前端 + /api/ws 数据面）、`tcs_core/param_registry.c`、`tcs_link`、`tcs_wifi`、`project_tcs/Core/Src/main.c`（STATUS.rfu）

## 0. 结论

第二轮盘点的 **12 条视觉 + 16 条功能全部落地**；复测中又发现并修掉 3 个连带问题（见 §3）。
主机门禁、H743 台架构建、ESP32 三档构建全绿；网页在 mock 上按 320/360/390/844×390/1280 实渲染复核，
无 JS 报错、无横向溢出。**仍未上真机**（ESP32 不在，见 §6）。

## 1. 视觉（V1–V12）

| ID | 改动 | 验证 |
|---|---|---|
| V1 | 纵轴不再为 48 °C 阈值撑到 50；阈值在视图外时只在顶/左边画“▲ 48 °C 阈值↑（图外）”，不再画整条虚线（虚线贴 40 刻度会被读成阈值=40） | 390/桌面截图；canvas 轴 20–40，水温线回到图内 |
| V2 | 执行器改为两行 `tec-row`（名称 + 制冷/暂停/制热三段 + 功率），行高统一 45 px；≤320 不再出现“贴身/TEC”换行 | 320 量测：行宽 271、三段按钮 46/47/47、scrollWidth=271 无溢出 |
| V3 | 桌面内容列 `max-width:860px`；参数滑条 683 px（原约 900 且值框贴最右） | 1280 量测 page=860、range=683；截图 |
| V4 | 日志去掉重复的事件列，改两列 `60px 1fr`；事件码只留在 title | 390 量测 grid=60px 260px，`.le` 不存在 |
| V5 | 页签圆点按严重度分色：`has-alarm` 红、`has-warn` 黄 | alarm=2（模块失联）→ `has-warn`；alarm=10（超温+模块）→ `has-alarm` |
| V6 | 顶栏过期文案缩为“◐ 数据过期”；**根因修复**：报警 chip 不再吃到报警卡的 12 px 内边距（44→32 px），顶栏不再长高；“解除报警”按钮缩为“解除”（aria-label 保留全称） | 390/320 报警+过期并存：topH 52.7、单行；修复前 64.8 两行 |
| V7 | 参数值框 96 px；聚焦时蓝框 + 3 px 浅蓝 ring（`:focus`） | 实测 focus：border `rgb(37,99,235)`、shadow `0 0 0 3px`；截图 |
| V8 | 曲线图例改白底描边 chip（可点隐藏），不再是三行静态文字 | 390/桌面截图 |
| V9 | “无报警，运行正常”去掉卡片外框，改成透明 48 px 单行提示条（不再是一整张空卡）；日志空态保持居中短文案 | 390 量测：h=48、背景透明、无边框；截图 |
| V10 | 桌面底栏 `justify-content:flex-start` + 左内边距 224，急停左缘与内容列左缘对齐 | 1280 量测 estop.x=224 = 内容列 x |
| V11 | 阈值标签移到绘图区左上并与刻度线脱开（并入 V1 的出界标记） | 390/桌面截图 |
| V12 | 开关按下态：绿描边 + 浅绿底 `#ECFDF5` + 绿字，一眼可分 | 实测按下 `background rgb(236,253,245)`、文字 `rgb(21,128,61)` |

## 2. 功能（F1–F16）

| ID | 实现 | 验证 |
|---|---|---|
| F1 | 网页切自动模式先弹确认（与屏端 `tui_dlg_mode` 同口径），确认后才发 `c=6` | mock：点模式 → 弹“切到自动模式？”→ 确认后 `mode=1` |
| F2/F7 | `setLocked()`：安全态 / 数据过期 / 连接断开时锁执行器与参数；`requestSet`、参数事件、模式/风扇开关统一拒绝并提示 | 实测 locked=true 时 45 个控件 disabled、`#lockhint` 显示；解除后恢复 |
| F3 | `0x09/0x0A` 加 `PF_WEB`；执行器每路“制冷/暂停/制热”三段（写 set id=9/10） | 实测点“制冷”：`tec_dir=1`、按钮 aria-pressed、参数页同步 |
| F4 | status 增 `tec1/2_ok`、`tec1/2_v`、`tec1/2_i`；前端算功率 | 实测 TEC1 开：8.23 V × 3.10 A → “25.5 W” |
| F5 | 诊断档“固件维护”卡：GET `/api/ota` 返回非 404 才显示（交付档 404 隐藏；诊断档 POST-only 路由 GET→405 显示） | mock（200）隐藏；diag 档构建通过；按钮 POST `?m=0/1` |
| F6 | 顶栏“解除”chip：有可确认报警（4/8/16）时出现，确认后发 `c=9` | 实测 alarm=4 → 点解除 → 确认 → alarm=0、state=0、解锁 |
| F8 | 参数搜索框：按名称/键过滤 `.prow`，分组自动展开/隐藏 | 实测搜索“温度”命中 3/4 行，清空恢复 |
| F9 | 日志级别过滤（全部/警告及以上/仅报警）+ CSV 导出（时间/级别/事件/消息） | 实测 `alarm` → 0 条；CSV `text/csv`、表头+2 行、中文引号转义 |
| F10 | 顶栏“屏链路断开”chip（`st.screen=0`） | mock `/__fault?screen=0` → chip 显示 |
| F11 | 点“设定 xx °C”切参数页并滚动/闪烁对应行 | 实测 tab=params、row flash；修复了冒泡回总览（§3） |
| F12 | 设备页“修改热点”表单（当前口令/新名/新口令）→ WS `{t:'wifi'}`；后端校验当前口令、长度，写 NVS 并 `esp_wifi_set_config` 立即生效 | mock 实测确认弹窗、`wifi_ok` toast、口令框清空；后端编译/主机测试通过 |
| F13 | “恢复默认”：按注册表默认值逐条 `set`（60 ms 间隔），保护类一次性确认 | 实测全部 pending 归零（每条都收到 pv 回读） |
| F14 | STATUS 的 `rfu` 字节带屏固件码（v1.3=0x13）：屏端填 → `link_status.peer_fw` → `status.screen_fw` → 设备页显示 `v1.3` | mock 显示 v1.3；旧屏 rfu=0 → “未上报”，向后兼容 |
| F15 | 多报警时显示“全部解除（已恢复项）”；仅当 ≥2 条且含可确认位 | 实测 alarm=10、两张卡 → 按钮出现；单条自动恢复报警不出现 |
| F16 | 模式页对等：两路方向 + 功率 + 风扇/模式，可在网页完成屏端模式页的操作 | 同 F3/F4 实测 |

## 3. 复测中额外发现并修掉

| # | 问题 | 修法 |
|---|---|---|
| A1 | 顶栏报警 chip 同时带 `.alarm`，吃到报警卡的 `padding:12px`，高度 44 px，任何报警都把顶栏撑高（V6 的真根因，缩短文案治不了） | CSS 改 `.alarm:not(.chip)`；chip 回到 32 px，topH 52.7 |
| A2 | F11 点“设定”后事件冒泡到 `.vcard` 的“看曲线”，页面又被切回总览 | 设定链接的 click/keydown `stopPropagation()` |
| A3 | 48 °C 出界虚线正好压在 40 刻度线上（V1 的第一版修法），视觉上像“阈值=40” | 出界只画三角+文字，不画线 |
| A4 | 终检发现 V9 第一版只动了文案，“无报警，运行正常”仍是带边框的整张卡 | 去掉 `card` 类，只留透明 48 px 提示条（h=48、无边框） |

## 4. 后端 / 协议改动

| 文件 | 改动 |
|---|---|
| `esp32/components/tcs_core/param_registry.c` | `0x09/0x0A` flags 加 `PF_WEB`（网页可写方向；仍不持久化） |
| `esp32/components/tcs_web/include/web_status.h` | 结构体加 `tec_dir`、`tec1/2_ok`、`tec1/2_v`、`tec1/2_i`、`screen_fw`（业务字段 22→30） |
| `esp32/components/tcs_web/web_status.c` | JSON 补 8 字段；`web_status_changed` 纳入比较 |
| `esp32/components/tcs_web/websocket.c` | gather 填方向/电参/屏固件；新增 WS `wifi` 消息（校验当前口令→写 NVS→应用 AP） |
| `esp32/components/tcs_web/WS_PROTOCOL.md` | 同步 `st` 新字段、`wifi`/`wifi_ok`/`wifi_auth`、报警位 64/128 |
| `esp32/components/tcs_link/include/link_task.h`、`link_dispatch.c` | `link_status_t.peer_fw`（取自 STATUS.rfu） |
| `esp32/components/tcs_wifi/include/softap.h`、`softap.c` | `softap_set_credentials()`：长度校验、NVS("tcs")、`esp_wifi_set_config`，返回码 -1..-4 |
| `project_tcs/Core/Src/main.c` | STATUS `.rfu = (MAJOR<<4)|MINOR`（v1.3=0x13；旧网页/旧主控忽略） |
| `esp32/tests/test_control_manual.c`、`test_web_status.c` | 断言 0x09/0x0A 含 `PF_WEB`；status 字段数 30 |
| `esp32/tools/web_mock.py` | 模拟 tec_dir/电参/screen_fw/wifi_ok |
| `project_tcs/tools/reanchor_study_cards.py` | `grep` 子进程显式 `encoding="utf-8"`（Windows cp1252 下原本直接崩，研学卡重锚工具不可用） |

## 5. 门禁

| 门禁 | 结果 |
|---|---|
| `node --check app.js / chart.js` | PASS |
| `python tools/web_gz_check.py` | PASS（4 个资源与 .gz 一致，mtime=0） |
| `project_tcs/tests/run_all.sh` | **ALL GREEN**（含研学卡重锚后 card_lines 162 行） |
| `esp32/tests/run_m207_tests.sh` | **ALL GREEN** |
| `tools/build_bench.sh`（H743 台架） | build OK + prod-clean PASS |
| `build_matrix.sh --esp32-only --diag --site --twice` | 默认/诊断/现场 3 档 0 error 0 warning；twice 两次从零构建 app sha256 相同（`45d319d4e972cadb`），bootloader/分区表相同 |

## 6. 未上真机 / 残留风险

- **ESP32 从 11-27 起不在（COM3 消失）**，本轮网页与固件**未烧录**；设备热点里仍是旧页面。要看新页面需接回 ESP32 后烧本提交产物。
- 未实测：F12 真改热点（手机断开重连）、F5 诊断档 OTA、F14 旧屏（rfu=0）兼容、真手指在 320 小屏上的点击。
- TEC 相关实测按用户要求**未做**（涉及真实制冷/制热）。
- F5 的“显示条件”依赖 httpd 对 POST-only 路由的 GET 返回 405；若 IDF 版本改为 404，卡片会不显示（不影响交付档，诊断档需复验）。

## 7. 复现与说明

- 证据截图：`docs/evidence/网页视觉盘点_20261128/第二轮/修复后_*.png`（10 张，320/390/横屏/桌面/安全态/过期/聚焦/阈值标记）。
- 第一次矩阵曾报“app sha 不同”：原因是矩阵运行期间我仍在改 `web_files`（两次构建嵌入了不同资源），不是构建系统问题；冻结代码后重跑相同。
- 网页资源体积：`app.js.gz` 17.4→20.1 KB、`index.html.gz` 2.8→3.7 KB、`style.css.gz` 4.1→4.4 KB，仍在分区预算内。
