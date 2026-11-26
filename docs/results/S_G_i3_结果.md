# S-G i3 结果（集成收口批次）— 2026-11-26

分支 `fix/s-i3`，基座 = 集成分支 `fix/s-u-int2` @ `bfd8725`（之上另有协调者合并的 08862c9 / fcfda86 / 4f4943d，只影响台架档构建、屏端版本串取宏、研学卡与 docs；本分支未包含它们，合并时以集成分支为准）。
本批次**不碰设备**：没有 probe-rs、没有开串口、没有烧录；真机步骤见文末「协调者要做的事」。

## 一、提交列表（按先后）

| # | 提交 | 条目 | 一句话 |
|---|---|---|---|
| 1 | `24553cf` | 1 | P1-09 的另一半：`m207_tec_force`（离线记保持位、复联第一帧补发、读回到关才解除）+.tec_force 接线+复联判据读命令值 |
| 2 | `21d303e` | 3 | 快照补 `tec_cmd/tec_act/tec_known`，`TCS_PROTO_VER` 2→3；回显改独立字段；屏端"未知"显示；`control_tec_dir` 离线回暂停 |
| 3 | `89f7f42` | 2 | link/web 启动失败进汇总（`esp_err_t`）；ALM_SERVICE 在屏上有文案 |
| 4 | `4035b91` | 4 | 注入源在生效的那一路豁免卡死判据（hook + `water` 注入 + 默认/SIM 档构建验证） |
| 5 | `372fee1` | 8 | 诊断/仿真档 `mstat`：模块保护状态 + 读数"逐位不变"最长连续时长（`control_stuck_max_ms`） |
| 6 | `6de8f58` | 10 | WS 的 ota `m` 只认精确 0/1，其余（1.5 / 缺字段 / 字符串 / true）一律 `bad_m` |
| 7 | `1c96139` | 9 | 状态迁移环（8 条，`.noinit`，magic+CRC）+ 交付档"记录器必须在 .noinit"正向门禁 |
| 8 | `e1cf175` | 11 | 协议版本不配对：屏端拒收计数（`stat` 的 `vr=`）+ 横幅换文案；规格写 OTA 发布规则；字库补 4 字 |
| 9 | `acfdab8` | 7 | README 不再声称现行固件版本号，指向 `protocol/tcs_version.h` |
| 10 | `d146d10` + `f4a7bf0` | 12 | 真机脚本护栏 `site_lib.sh`（guard / FLASH / REBUILT）进仓库 + 现场工具页写明规则 |
| 11 | `bdfc5fc` | 5 | 规格写明 M09-F-06 布防语义；"参数校验失败"横幅写清后果 |
| 12 | `（收尾）` | — | 研学卡重锚、审计报告与文件清单重新生成（单独提交，合并时由协调者统一再生成） |

## 二、逐条判定

判定只用五种。关单条件里"真机……"的部分一律没做（本批次不碰设备），写进"遗留"。

### 1. P1-09 的另一半：安全关断真正到执行器 —— **已实施待真机**

- **现状核实（与复核 R7-09 一致）**：基座上 `m207_tec_force` 并不存在（只在 control.c/control.h 的注释里）；`main.c` 没接 `.tec_force`，SAFE 的关断退化为 `m207_tec()`——它第一行就是"离线 → 回 -2"，离线期关断连旁路槽都进不了；复联补发只看与命令混用的 `g_tcs.tecN_on`，关断之后晚到的 `TCSW=1` 应答会让它漏发。
- **做了什么**：`m207_tec_force(ch,on)`（离线记保持位、在线等同关断、读回到关或更新的"开"才解除）；`m207_go_online()` 的复联判据改为"保持位 ∨ 命令意图是关 ∨ 回显是关"，先补发关断再放行队列；N-01 超温路径的服务层关断也走它；`main.c` 接 `.tec_force`；`control_tec_act()` 改为独立回显字段（`control_tec_echo_set()`，M207 服务回 `TCSW?` 时写）。
- **怎么验证的**：新用例 `esp32/tests/test_m207_force.c`（F1–F7，"控制 + 服务内核"按 main.c 装配联调）：未修的 `m207_core.c`（`bfd8725`）配"退化为 m207_tec"的力通道桩 → 红 11（F2×2 / F3×4 / F6×3 / F7×2）；修后 **74/0**。`test_control_esp32.c` P9-6 加"离线期关断每 500ms 照样进安全关断通道"。变异：`mutation_control.py` M55 CAUGHT；新脚本 `mutation_m207_force.py` F01–F10 全按预期（9 抓 + F05 等价存活，F07 把两道一起删被抓）。
- **遗留（真机）**：拔掉 M207 后触发急停，重连后 TEC 必须为 0；离线期快照 `tec_known` 为 0。

### 2. P1-12 的另一半：启动失败要能被看见 —— **已实施待真机**

- **做了什么**：`link_start()` / `web_start()` 返回 `esp_err_t`（`ws_init()` 带出推送任务结果；任务没建成 → 卸 UART 驱动 / 返 `ESP_ERR_NO_MEM`）；`main.c` 用 `svc_note(&svc, SVC_BIT_LINK/WEB, …)`，不再 `svc_unverified`；屏端新横幅 `UI_BN_SERVICE`："功能启动失败 / 部分功能不可用 · 请断电重启"（红底、点击只给提示，确认不掉），`ui_ack_reason` 也认这一位。
- **怎么验证的**：`test_link_dir.c` 加两条（任务建成 → `ESP_OK`；没建成 → `ESP_ERR_NO_MEM`，桩加可控失败）：未改的 `link_task.c`（void）上编译红，现 79/0。新 `test_ui_svc.c` 15/0：未改的 `ui_model` 上编译红。源码门禁 `test_m1_gates` 第 8 项：对这 4 个文件改动前的源文件跑 → 红 10；现 PASS（还钉住"web_start 只能调一次 ws_init"）。`run_m207_tests.sh` ALL GREEN；默认档 / SIM 档构建 0 告警。
- **遗留（真机）**：人为让 `m207_service_start` / WS 推送任务失败 → 设备不 abort、日志与屏上都能看到。

### 3. 快照补三个量（tec_cmd / tec_act / tec_known）+ 协议抬到 3 —— **已实施待真机**

- **做了什么**：`protocol/tcs_proto.h` 0x20 末尾追加 3B（`tec_cmd` 命令意图 / `tec_act` 模块回显 / `tec_known` 是否可信，bit0=tec1 bit1=tec2），49→52B，`TCS_PROTO_VER` 2→3；`proto_layout.h` 契约（sizeof 56→60、偏移 54/55/56）；`tcs_snap.c` 同一拍取样；屏端 `link_snap.h` 镜像 + `ui_model` 按 known 位画"未知"（首页胶囊、模式页状态文字转灰，版面不变）；`post.c` 的 VER 自检项 == 3u；`control_tec_dir()` 在该路 known 位为 0 时回暂停。字节向量的三处对照同步（两端 C 用例 + `proto_ref.py` selftest）。
- **怎么验证的**：未改协议前两端都是编译红（结构体没有这三个字段；屏端 `test_proto_m1` 与 esp32 `test_tcs_proto`）；改后 esp32 74/0、屏端 45/0、`proto_ref.py selftest OK`。屏端新 `test_ui_known.c` 19/0（未改 ui_model 编译红）；既有 `test_ui_model` 182/0。`test_control_esp32.c` K19（回显独立于命令）旧耦合下红 3 → 286/0；`test_control_manual.c` M16（离线时方向回暂停）未修红 1 → 169/0。变异：`mutation_control.py` M56、`mutation_control_manual.py` C13、`mutation_ui_model.py` M30–M34（脚本加"可选第 4 个元素指定用例文件"）——`mutation_ui_model` ALL AS EXPECTED（34 抓）。`proto_sync --check` OK。
- **遗留**：复联后到第一次 `TCSW?` 回读之前（约 0.1–0.3 s）`tec_act` 是上一次的回显而 `tec_known` 已为 1（如实写进风险节）；真机看屏端"未知"的实际呈现与协议 3 两端联调。

### 4. 卡死判据在注入档的口径（M09-F-09）—— **已实施待真机**

- **口径（按 i3b 默认做法）**：`control_ops_t.stuck_exempt(ch)`——**该路注入源在生效时**才豁免这一路的卡死判据（ch1 = `tmp117_sim_active()`，ch2 = `m207_sim_water_active()`）；生效 / 失效各打一次 W 级日志；交付档不接（NULL）→ 行为不变：真实读数逐位不变满窗口（体温 30 s / 水温 10 min）仍然进安全态。
- **做了什么**：control.c 只在 `body_stuck/tc1_stuck` 之后各加一行门控（四个变异锚点原文**未动**，门禁逐条核对）；main.c 在 `#if CONFIG_TCS_SIM` 下接线；tcs_m207 新增台架水温注入（`m207_sim_water/_off/_active`，注入值每拍在 tick_end 覆盖读数）+ console `water <ch> <°C|off>`（仅 SIM 档）；`verify_prod_clean.py` 的 SIM 命令单列（默认档不得有、`--esp32-expect` 可点名），`build_matrix.sh` 现场档点名 `water`。
- **怎么验证的**：K22（`test_control_esp32.c`）：体温注入生效 → 冻 35 s 不进 SAFE；注入关掉 → 30 s 内进 SAFE；水温注入生效 → 冻 630 s 不进 SAFE；关掉 → 满 10 分钟窗口进 SAFE。未修的 control.c（`89f7f42`）上红 2；改后默认档 291/0、SIM 档 296/0。新 `test_m207_sim.c`（SIM 档编译）14/0。变异 M57/M58/M59。默认档 bin 查不到 `water` 的帮助文本，SIM 档在（`verify_prod_clean` 两方向核过）。三档构建 0 告警。
- **遗留（真机）**：现场档用 `water` 明确打开恒定水温注入时，台架流程不再被卡死判据打断；确认交付档（无注入）的冻结保护仍在。

### 5. M09-F-06 的布防语义写进文档 —— **已修并验证**

- **做了什么**：规格说明书 §6 追加一条（不改写前文）：自动布防 = 双源健康 **且** 上电参数镜像完好；镜像损坏 / NVS 持续写失败的设备不会自动布防，此期间传感器类故障只记状态沿、不进安全态，只有 `ALM_EEPROM`（"参数校验失败"）一条提示——**参数存储故障期间没有传感器保护**；超温与急停不受布防影响；成功落盘一次（且双源健康）即恢复。屏端该横幅副标题改为"已恢复默认参数 · 重新设定后恢复保护"（字库覆盖率核对：缺 0）。
- **怎么验证的**：源码门禁 `test_m1_gates` 第 14 项（规格条目三句话 + 横幅文案逐字）；`font_audit_lvgl` PASS（缺 0）；H743 PROD 增量构建 0 告警。

### 6. X04-F-05 待定清单 → 给用户/协调者的决策表 —— **已修并验证（只出表，不拍板）**

机制保持"只记日志、只提供读数，不进安全态"（未改）。决策表见本文第六节。

### 7. X06-F-09 的文档漂移余项 —— **已修并验证（版本号本身留给用户）**

- `python tools/version_bump.py --check` → `version_check: PASS`；剩两条 NOTE：README（已在本批次处理）与 `docs/现行规格说明书_v1.2.md` 里的 v1.1.0 / v1.2.0。后者是**规格文档的版本**（评审记录的一部分），不是固件版本号，不改写（改写会篡改评审历史）；`--check` 继续以 NOTE 报出。README 第 5 行的"现行版本 fw/网页 v1.2.0"是对当前固件的声称，已改为"固件版本以 `protocol/tcs_version.h` 为唯一源"（并写明 `--set` 一处改全仓 / `--check` 查第二处写死）。
- **没做的**：版本号本身（`1.3.0-rc1`）没动——按说明由用户定。

### 8. M207 保护状态的读取入口 —— **已实施待真机**

- **做了什么**：诊断 / 仿真档新命令 `mstat`：两路各打印 `TCERRORSTATUS` / `TCOTPSTATUS` 原值、故障位掩码与位名（`m207_errstat_fault_mask` / `m207_fault_bit_name`）、数据龄、valid；末行打印体温 / 水温读数"逐位不变"的最长连续时长（含当前这一段）。只读：不报警、不进安全态、不动执行器；提醒一行"TCERRORSTATUS 含上电锁存位"。
- **control.c 的只读导出**：`control_stuck_max_ms(which, now_ms)`；实现体与导出都在 `#if CONFIG_TCS_DIAG || CONFIG_TCS_SIM` 里（交付档不编译）。`verify_prod_clean.py` 把 mstat 的帮助文本（"读数抖动"）列进禁入清单；`build_matrix.sh` 现场档点名 `mstat`。
- **怎么验证的**：`test_control_esp32.c` S1（SIM 档编译）：水温每拍在变 → 最长连续时长短；体温冻 10 s → 含当前这一段；收尾后最大值保留、当前段更长时接管；非法通道 0。默认档 291/0（S1 不编译）、SIM 档 296/0（新增 `ctlstat` 一行跑同一用例）。默认档 bin 无 mstat 文本、SIM 档在。源码门禁第 10 项。
- **遗留（真机）**：`mstat` 采 TCERRORSTATUS 极性与锁存位读数（供第 6 条决策表）；TEC 实测时量读数抖动。

### 9. HardFault 记录器收尾 —— **已实施待真机**

- **做了什么**：`project_tcs/App/tcs/control.c` 新增状态迁移环 `tcs_trace_t`（magic `'TRCT'` + 累计条数 + 8 条 + CRC32），落 `.noinit`（`g_trace_ring`）；纯逻辑五个函数（reset/push/valid/count/at）进 control.h；写入点 `goto_state()`（ev=0）与 `tcs_emergency_stop()`（ev=1）；`fault` 命令 / 启动打印在 HardFault 记录后接着打环。门禁：`verify_prod_clean.py --expect-recorder`（PROD ELF 里必须有 `g_fault_rec` 与 `g_trace_ring`、都在 `.noinit`、`.noinit` 是 NOBITS），`build_matrix.sh` 的 PROD 核对带上它。
- **怎么验证的**：`test_control.c` T1–T13（回绕、顺序、越界；一位翻转 / 坏 magic / 0xFF 垃圾 → 整环无效；对无效环 push 先重置；急停写 ev=1 + 迁移两笔且时间戳正确）。未改的 control.c 上链接红（LNK1120：7 个 `tcs_trace_*` 未解析），改后 86/0。`objdump -t` 显示两个符号都在 `.noinit`；`--expect-recorder` PASS。门禁反向自证（解析逻辑自测）：符号挪进 `.bss` / `.noinit` 带 CONTENTS 各报一条。
- **遗留（真机）**：软复位后 `fault` 命令应打出复位前的迁移（`.noinit` 跨复位存活的真机确认）；掉电仍丢（H743 没有不掉电的存储——方案第五条）。

### 10. WS 的 `{"t":"ota","m":1.5}` 仍被当成回滚 —— **已修并验证**

- **做了什么**：`web_status.c` 新增纯函数 `web_ota_mode_json(const cJSON *m)`：只认精确的整数 0 / 1；缺字段、非数字、1.5、2、-1、0.001、字符串 `"1"`、true/false、null、数组、对象一律 -1。`websocket.c` 的 ota 分支改用它，-1 → `{"t":"err","code":"bad_m",…}`（两种模式都不触发）。只动了 `m` 的解析几行（与 r7f 的"输出已停"检查不重叠）；HTTP 侧 `web_ota_mode_value()` 原样。
- **怎么验证的**：`test_web_hardening.c` 新增 `test_ota_mode_json`（真实 cJSON，2 合法 + 12 非法 + NULL）：未改的 `web_status.c` 上链接红（LNK1120），改后 31/0。`test_web_gates.py` 新增门禁：ws ota 必须走 `web_ota_mode_json`、不得碰 `valueint`、必须有 `bad_m`、不得用查询串那版。`run_m207_tests.sh` ALL GREEN；默认 / SIM 档构建 0 告警。

### 11. 主控与屏协议版本不配对时的表现 —— **已实施待真机**

- **做了什么**：① 规格 §5 追加 OTA 发布规则：OTA 包不得改变 `TCS_PROTO_VER`，除非屏固件同步升级；必须升协议时先烧屏、再 OTA 主控，两步在同一停机窗口内连续做。② 屏端被版本校验拒掉的快照单独计数（`Core/Src/main.c`，`stat` 第二行末尾 `vr=`）；界面纯逻辑 `ui_vr_mismatch()`（首拍建基准、计数为 0 永不触发、2 s 窗口）+ 新横幅 `UI_BN_VER_MISMATCH`"主控固件与屏不匹配 / 请同步升级后再用"——只在"链路不新鲜 + 计数在涨"时替换"主控连接中断"，其余安全行为一丝不动（链路仍判为不新鲜、执行器/模式未知、按钮不可点）；急停没送到仍急停优先。字库补 4 字（匹/升/同/级）。
- **怎么验证的**：新 `test_ui_mismatch.c` 18/0（横幅换文案 / 行为不变 / 急停优先 / 计数不涨回退 / 跟踪器六种情形）；未改的 ui_model 上编译红。既有 UI 用例 182+19+15 全绿；`font_audit` PASS；源码门禁第 11 项；H743 PROD 构建 0 告警。
- **遗留（真机）**：按 D6 第八节重做"两端不同版本"：屏上应出现"主控固件与屏不匹配"，`stat` 的 `vr=` 增长。

### 12. D6 的教训落成门禁 —— **已修并验证**

- **做了什么**：`project_tcs/tools/site/site_lib.sh`（`guard <期望HEAD> [档名]`：HEAD 等于期望、已跟踪文件干净、固件不早于源码；`FLASH <档> <标签>`：烧录前后各算 sha256，变了打 `REBUILT`；`IDF` / `ts` 包装；工作树/串口经 `TCS_SITE_REPO` / `TCS_SITE_PORT` 覆盖）；`project_tcs/tools/site/README.md` 新增「真机脚本的护栏」四条规则（运行期间不改同一个工作树；先 guard；用 FLASH 并认 REBUILT；证据以两端自报版本串与 sha256 为准）。
- **怎么验证的**：`bash -n` 语法通过；源码门禁 `test_m1_gates` 第 13 项逐条核对。

## 三、本批次验证总结果

（见文末「验证记录」：两份主机全量、两份变异脚本、`proto_sync --check`、`build_matrix.sh --esp32 --diag --site --twice` 的原样汇总行。）

## 四、风险与需要人看的点

1. **快照 `tec_act` 的复联窗口**：复联后到第一次 `TCSW?` 回读之前（约 0.1–0.3 s，最坏含一次快照周期），`tec_known` 已是 1 而 `tec_act` 还是上一次的回显。屏上表现为"未知 → 短暂旧状态 → 真状态"。没有为它加"回显新鲜度"位：那会把 `tec_known` 的语义收紧成"在线且回读过"，P9-6/P9-7 与 m1 的一批用例都要改，收益不足。
2. **协议抬到 3 的发布窗口**：本分支与集成分支合并后，主控（OTA）与屏（探针）必须成对升级；屏端 `vr=` 与新横幅是这次专门为"忘记成对升级"留的现场判据，但**它不能替代发布纪律**（规格 §5 已写）。
3. **`m207_tec_force` 的契约与 e3a 文档原写法有出入**：e3a 写"每 500ms 重试到读回一致"，实际是"服务侧不自带定时器，重试节拍由 control 的锁存重申给出"。e3a 结果文档已追加第十节更正。
4. **卡死豁免只在"该路注入在生效"时成立**：现场档用 `water` 明确打开注入才豁免水温这一路；交付档不受影响。若有人在现场档用恒定水温做台架、又忘了打开 `water`，10 分钟后仍会进安全态（这是有意的：宁可误报，不可漏报真实冻结）。
5. **字库 24px 链的用字约束**：新增界面文案受 `font_audit_lvgl` 门禁约束（服务/台/掉/备/版本/升级/同步等字不在链上）。本批次因此把两处文案换成了"功能启动失败 / 功能没能恢复 / 有功能没能启动 / 请断电重启"这类写法；ext24 由 `regen_ext_fonts.py` 重新生成（171 字，+4）。

## 五、协调者要做的事（真机；本批次不碰设备）

1. **P1-09**：设备正常运行、TC1 手动加热中 → 拔掉 M207 通信线（模块仍供电）→ 按急停 → 屏幕上应显示连接中断/安全态；插回 M207 → **复联后 1 s 内** TEC 1/2 都应为 0（`status` 的 `tec1/tec2`，`mstat`/快照 `tec_known` 应为 3）；断开期间 `tec_known` 为 0（屏上"未知"）。
2. **P1-12**：人为让 `m207_service_start` 失败（例如先占住 UART2）→ 设备**不 abort**；串口有 E 级"services FAILED to start: m207"；屏上出现"功能启动失败 / 部分功能不可用 · 请断电重启"；不再出现 "all up"。再试一次让 `ws_init` 的推送任务创建失败（堆压到很低）→ 同样的横幅与日志名单里有 web。
3. **协议 3 联调**：两端用同一提交重建、烧录（屏先烧、主控后烧或同时）→ 屏上无"不匹配"横幅、`stat` 的 `vr=0`；然后把主控单独烧**旧**协议（2）→ 屏上应出现"主控固件与屏不匹配"、`stat` 的 `vr=` 持续增长，且安全行为与"连接中断"一样。
4. **`mstat`（采集第 6 条决策表要的读数）**：TEC 关闭静置 → `mstat`（两路 errstat/otpstat/age/valid 与 stuck 最长连续时长）；TEC 手动制冷/制热再各采一次；记录 "stuck: body/water max" 的变化（这是 F2 §4.3 要量的"TEC 开着时读数抖动"）。
5. **HardFault 环**：用台架档的受控触发（或软复位）→ `fault` 命令应打出复位前的迁移（`state trace:` 行，`(estop)` 标记可见）；确认 `.noinit` 跨复位存活。
6. **注入档口径**：现场档 `water 1 31.5` 打开恒定水温 + TEC 开 >10 分钟 → 不应进安全态（日志里有 "stuck check exempt on water"）；`water off` 后同样条件 → 应进安全态。
7. **真机脚本护栏**：以后的真机脚本按 `project_tcs/tools/site/site_lib.sh` 的规则写（先 `guard`、用 `FLASH`、认 `REBUILT`）。

## 六、X04-F-05 决策表（条目 6；用户/协调者拍板，本批次不动机制）

| # | 待定项 | 选项 | 风险 | 我的建议 |
|---|---|---|---|---|
| 1 | 哪些 `TCERRORSTATUS` 位算"故障" | A. 只把 over-temp / over-current / load-short 当故障（其余显示为历史）；B. 7 位全算；C. 一位都不进安全态，只显示 | B：真机 TC1 恒回 253（10 月自检把开路保护上限写成 1°C 留下的锁存位）→ 上电即永久 SAFE；A：漏掉 external / sensor-open 两路本可用的证据 | **A**；且首版只报警不进 SAFE（见 #2） |
| 2 | 故障位要不要进 SAFE / 报警位 | A. 只记日志与 `mstat` 读数（现状）；B. 置 ALM_EXEC（需新报警位 + 两端文案 + 屏横幅）；C. 直接 `safety_trip` | C：锁存位会一上电就停设备；B：新报警位要两端界面改（本批次界面层已冻结） | **A 先跑一段真机**，用 `mstat` 采到"哪些位会变、什么时候变"再定 B |
| 3 | 锁存位怎么对待 | A. 显示为"历史"、要求模块断电后清；B. 提供清除流程（下发参数把保护上限改回正常值再断电）；C. 不管 | A：用户看到 253 会以为坏；C：真机诊断价值丢失 | **A + 一条文档化的"断电复位"操作**；B 的单列为后续项 |
| 4 | `TCOTPSTATUS` 极性 | A. 与 TCERRORSTATUS 一样"正常=1"（按位取反）；B. 相反；C. 只在真机核实前当不透明原值 | 猜错极性会把"正常"当"过温"或反之 | **C（现状）**，用 `mstat` 读到实际值再定 A/B |
| 5 | 风扇弱校验（`fan_weak_check`）阈值与条件 | A. 300 s / 0.5°C（现默认）；B. 放宽到 600 s / 1.0°C；C. 不接 | 恒温稳态下误判（制冷到目标后水温本就不动） | **C 暂不接**；等 TEC 实测有数据再标定 |
| 6 | 新报警位 `ALM_EXEC` | A. 在 `ALM_*` 段末尾加一位（0x0080）；B. 复用 `ALM_M207`；C. 不加 | A：两端界面文案 + 屏横幅都要改（需评审）；B：语义混淆 | **A 但单独评审**（本批次只留位记录，不用） |
| 7 | **水温冻结的 10 分钟里 48°C 硬切断失明**：模块自己的保护（`TCRTOCPHT`=120°C 等）是否独立于这个探头 | A. 独立（模块内自测热电偶/自身温度）；B. 不独立（同一探头） | 若 B：探头冻结 10 分钟窗口中，既没有软件 48°C 切断、也没有模块保护 → 只有 PID 目标与电压上限兜底 | **必须先做一次真机实验**（把探头短路/恒温模拟冻结、看模块在 120°C 设定下是否自停）；在此之前建议把水温卡死窗口从 10 分钟缩到 5 分钟（F2 的余量 5.7 倍）作为折中 |

## 七、验证记录

（原样汇总行，命令均在 `project_tcs/` 或 `esp32/` 下执行。）

**1) 主机全量 —— `esp32/`：`bash tests/run_m207_tests.sh`**

```
esp32-tests: ALL GREEN
```
关键行：`test_tcs_proto: PASS=74 FAIL=0`、`test_control: PASS=291 FAIL=0`（SIM 档 `ctlstat` 296/0）、
`test_control_manual: PASS=169`、`test_m207_force: PASS=74`、`test_m207_sim: PASS=14`、
`test_link_dir: PASS=79`、`test_web_hardening: PASS=31`、`test_web_status: PASS=230`；
门禁 `[m207gates]` `[m1gates]` `[webgates]` 全 PASS。

**2) 主机全量 —— `project_tcs/`：`bash tests/run_all.sh`**

```
[audit] PASS (报告与代码一致: 290 files, 0 uncovered, 0 unclassified)
...
esp32-tests: ALL GREEN
...
run_all: ALL GREEN
```
关键行：`test_control: PASS=86`（含 T1–T13）、`test_proto_m1: PASS=45`、`test_ui_model: PASS=182`、
`test_ui_known: PASS=19`、`test_ui_svc: PASS=15`、`test_ui_mismatch: PASS=18`、
`test_link_dir(H743): PASS=154`、`test_post: PASS=81`；`[card_lines] PASS（3 张卡 / 162 行）`、
`[audit] PASS`、`[m1gates]/[webgates]/[m207gates] PASS`；内嵌的 esp32 套件 ALL GREEN。

**3) 变异（每个都必须让单测变红；等价的注明理由并提供"两道一起删"的变异）**

```
$ cd esp32 && python tests/mutation_control.py
[current] PASS=291 FAIL=0
[old impl] rc=1 PASS=229 FAIL=62 (W2 fails: 16)
... （56 个变异：55 CAUGHT，1 个按预期等价存活）
mutation_control: ALL AS EXPECTED

$ python tests/mutation_control_manual.py     → mutation_control_manual: ALL AS EXPECTED
$ python tests/mutation_m207_force.py         → mutation_m207_force: ALL AS EXPECTED（9 抓 + F05 等价，F07 把两道一起删被抓）
$ python tests/mutation_web.py                → mutation_web: ALL AS EXPECTED
$ python tests/mutation_core.py               → mutation_core: ALL AS EXPECTED
$ cd ../project_tcs && python tests/mutation_ui_model.py → mutation_ui_model: ALL AS EXPECTED（34 抓，含 M30–M34）
```
H743 侧的 `mutation_link_dir / mutation_link_follow / mutation_m207_txq / mutation_audit_ring /
mutation_perf_hist / mutation_estop_gate` 随 `run_all.sh` 跑，全 PASS。

**4) 协议单一源**：`python tools/proto_sync.py --check`

```
proto_sync: OK (单一源 + 2 包装一致，无重复定义；方向表与两端收发分支一致；两端闸门相同；屏端 ISR 不碰 comm 层)
```

**5) 版本**：`python tools/version_bump.py --check`

```
version: protocol/tcs_version.h = 1.3.0-rc1 (fw_ver BCD 0x010300)
version_check: PASS
```

**6) 构建矩阵**：`bash tools/build_matrix.sh --esp32 --diag --site --twice`

```
=== [PROD] 从零构建：rm -rf build-matrix/PROD && cmake && build
[PROD] exit=0 error=0 warning=4 FLASH: 460516 2; DTCMRAM: 95504 128; AXIRAM: 205952 320
=== [BENCH] 从零构建
[BENCH] exit=0 error=0 warning=4 FLASH: 479316 2; DTCMRAM: 99208 128; AXIRAM: 205952 320
=== [SIM] 从零构建
[SIM] exit=0 error=0 warning=4 FLASH: 479316 2; DTCMRAM: 99208 128; AXIRAM: 205952 320
=== [SIMWG] 从零构建
[SIMWG] exit=0 error=0 warning=4 FLASH: 518460 2; DTCMRAM: 99256 128; AXIRAM: 205952 320
[prod-clean] PASS（交付（PROD）档产物不得含调试/台架钩子）
[prod-clean] PASS（台架档产物必须含钩子）×3
=== [esp32-default] 从零构建（配置取自 sdkconfig.matrix.seed，原文件不动）
[esp32-default] exit=0 error=0 warning=0
=== [esp32-diag] 从零构建（配置取自 sdkconfig.matrix.diagbase，原文件不动）
[esp32-diag] exit=0 error=0 warning=0
=== [esp32-site] 从零构建（配置取自 sdkconfig.site，原文件不动）
[esp32-site] exit=0 error=0 warning=0
=== [esp32-repro] 默认档再从零构建一次（可复现构建）
[esp32-repro] tcs_wireless.bin  sha256 4387fcb57e8eca12  相同
[esp32-repro] bootloader/bootloader.bin  sha256 ad532789dd342643  相同
[esp32-repro] partition_table/partition-table.bin  sha256 137dde9623233f24  相同
[esp32-repro] 默认档两次从零构建 sha256 相同
[prod-clean] PASS（…ESP32 命令集=无台架命令）
[prod-clean] PASS（…ESP32 命令集=全部台架命令）
[prod-clean] PASS（…ESP32 命令集含 linkloop,linkmute,mstat,water,webtest）
[esp32] 干净核对 PASS（默认档无台架命令；诊断档 / 现场档命令集相符）
================ build_matrix 汇总 ================
可复现构建：剔除 .comment 与调试信息后 md5 b48276d311847140cee06417fd751619 / b48276d311847140cee06417fd751619 → 相同；原始 bin md5 相同
交付档干净核对：PASS
ESP32：见上（日志在 esp32/build-matrix-*.log）
build_matrix: PASS
```

> 第一次跑矩阵（17:06 启动）遇到两件都不是代码问题的事，如实记录：
> ① `[esp32-diag]` 撞上 IDF 自带 `esp_lcd_panel_rgb.c:722` 的 GCC 内部编译器错误（brief 预告过）——
> 从断点续编一次即过（续编日志 0 个 ICE，diag 档 `verify_prod_clean --esp32-expect-all` PASS）；
> ② 跑的过程中我重锚了研学卡（改动已跟踪文件），H743 的 `[repro]` 因此把两次从零构建的产物判成不同
> （版本串一次干净、一次 `-dirty`）。把审计产物重新生成、其余改动提交后重跑整个矩阵，就是上面这次
> （`build_matrix: PASS`）。

