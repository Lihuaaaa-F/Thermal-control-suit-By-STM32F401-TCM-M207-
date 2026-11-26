# S-G r7f 结果 — 独立复核 R7 的修复批次（两条 P1 + R7-08 按 P1 做）

- 分支 `fix/s-r7f`（工作树 `D:\Espressif\ws\tcs-g-r7f`），基座 = 集成分支 `fix/s-u-int2` @ `08862c9`；
  本批次最终提交见文末"六"（9 条 + 结果文档 + 研学卡重锚）。
- 全程未碰设备：没有 probe-rs、没有串口、没有烧录，没有任何驱动 TEC / 加热的操作。全部证据来自读代码 +
  主机用例（MSVC cl，与 `esp32/tests/run_m207_tests.sh` / `project_tcs/tests/run_all.sh` 同一套编译方式）+
  ESP32 默认/诊断档与 H743 PROD/BENCH 档的构建。真机步骤见"五"，由协调者执行。
- 依据：`R7_findings.md`（复核发现清单，基座 `bfd8725`）与本批次说明 `impl_brief_r7f.md`。行号以本分支为准。

## 一、提交列表（按先后 = 按批次说明的顺序）

| 提交 | 条目 | 一句话 |
|---|---|---|
| `076c547` | R7-01（P1） | 断链判定的"已在安全态"只认急停 / 超温锁存的安全态 |
| `70359ac` | R7-02（P1） | 急停"模式 = 手动"与急停锁存一起落盘；上电读到急停锁存先回手动 |
| `ec0916d` | R7-08（P1）+ R7-11 | TC2 的水温进超温硬切断与卡死判据；服务层超温旁路关断两路 |
| `73804d8` | R7-03（P2） | 自动模式 COOL / HEAT 下每拍重申风扇开 + 拒绝关风扇 |
| `b44e84b` | R7-04（P2） | TEC 开沿预判"读数早就冻住"；体温先触发时也记水温 |
| `ccb6fae` | R7-06（P2） | 首页风扇卡片 / 模式页风扇键在链路不新鲜时不发命令 |
| `219a36c` | R7-07（P2） | OTA / 回滚触发前要求两路 TEC 命令都为关（诊断档） |
| `eef187a` | R7-12（P3） | 交付档邮箱 `p set` 拒执行器参数（0x06 / 0x09 / 0x0A） |
| `9548c12` | R7-13.3（P3） | 规格 §3 / §8 追加"通信类 2 秒自愈"的更正说明 |
| 见"六" | R7-10 | 不做，只记录（0x21 增量帧在屏端活不过一圈主循环，行为无害） |

## 二、判定表

判定只用五种。本批次碰不到真机，所以凡是"要在板子上看一眼才算关单"的，一律标 **已实施待真机**。

| ID | 判定 | 改了什么 | 怎么验证的（命令与结果） | 遗留与原因 |
|---|---|---|---|---|
| R7-01 | 已实施待真机 | 新增纯函数 `link_loss_already_safe()`（只看 `SAFETY_BIT_ESTOP\|SAFETY_BIT_OVERTEMP`）；`control_service.c` 的 `already_safe` 改为 `link_loss_already_safe(control_safety_latched())` | 新增 L04"断链 × 安全态原因"矩阵（急停/超温/模块失联/体温失联/读数卡死 × 断链 5 s，第 1.5 s 起故障源恢复）。修前（already_safe 取修前的 `control_state() == CTRL_SAFE`）：`test_control_manual: PASS=182 FAIL=16`，三个会自愈的行都是"estop 判定 0 次，锁存=0x00，状态=HEAT，TEC1 命令=1"；修后 `PASS=198 FAIL=0`。变异 B07–B10 全部被抓；`test_m1_gates.py` 第 1 项逐字核对（修前接线 FAIL、改后 PASS） | 真机（见五-1）。行为变化：屏在、模块掉线期间屏重启 / 断链 ≥3 s 会留一个要人工确认的急停（用户裁决"断链 = 急停"的本意） |
| R7-02 | 已实施待真机 | ① `control_init` 读到急停锁存 → `estop_boot_force_manual()`（`param_set(MODE,0,KERNEL)` + `param_persist_now()`）；② `tcs_emergency_stop_src` 在 `safe_outputs()` 之后同步落盘模式 | `test_control_esp32.c` 新增 R7-02a…e（NVS 桩：参数镜像 + 安全锁存各一份"掉电也在"的副本，上电顺序与 main.c 同）。修前 `PASS=290 FAIL=6`，扫描打印"急停后 0~3000ms 掉电重启 31 次：带回自动模式 25 次，确认后 TEC 被开 25 次"；修后 `PASS=296 FAIL=0`，"0 次 / 0 次"。变异 M57–M59 被抓 | 真机（见五-2）。同类窗口未堵：普通"切回手动"仍是 2 s 防抖，自动→手动后 2.5 s 内掉电会回到自动（与"自动模式掉电重启自动恢复"同一设计，未改） |
| R7-08 | 已实施待真机 | ① `water_overtemp()`（tc1_act 或 tc2_act > 48）：硬切断加 TC2 分支，退出 / ack / 通信类自愈 / 报警位清除四处同口径；② `s_stuck_tc2` + TC2 卡死判据（TC2 命令开着 + 冻满 10 分钟），`s_stuck_chn` 扩到 bit2（体温 / TC1 / TC2）；③ `m207_core.c` 超温置锁存处旁路关断两路（R7-11） | `test_control_esp32.c` K22（TC2 水温 90 °C → 当拍锁存 + 报警 + 两路关；TC2 降温前 ack 不清）/ K23（TC2 冻 600 s → STUCK + ALM_SENSOR_OPEN；仍冻时 ack 不能越过，重新变化后 2 s 自愈）。修前 `PASS=301 FAIL=10`；修后 `PASS=311 FAIL=0`。服务层：`test_m207_core.c` 新增 `test_overtemp_trip_tc2`、`test_overtemp_trip` 补 `TC2:TCSW=0`。修前 `PASS=89 FAIL=6`（5 条真红 + 1 条草稿构建器的相对路径问题）；修后从 esp32/ 跑 `PASS=96 FAIL=0`。变异 M60、M62–M65、D02 被抓；M61 记为等价（TC2 超温本身会锁存 OVERTEMP，锁存没清之前到不了退出判据），并用 D02 把三道显式判据一起删、必须被抓（成立） | 真机（见五-3）+ **水路拓扑待用户确认**（见三） |
| R7-03 | 已修并验证 | `control_poll`：自动模式 COOL / HEAT 下每拍 `out_fan(1)`；`control_apply_cmd_src` 的 FAN_TOGGLE 在自动 COOL / HEAT 下拒绝"关"（-2） | `test_control_manual.c` M07b：自动模式 3 状态 × 5 命令的不变量扫描 + HEAT/COOL 下 FAN_TOGGLE 回 -2 + 被外力关掉后下一拍重申。修前 `PASS=204 FAIL=7`；修后 `PASS=211 FAIL=0`。变异 E01 / E02 被抓 | 真机只差一眼（五-4）。**第一版用"任一路 TEC 命令为开 ⇒ 风扇开"把既有 W2-20 抓红**（自动 IDLE 下 TEC2 复原后风扇归状态机管），改成按状态判；自动 IDLE 下网页旧命令仍能开 TEC2（F 模式页 D3 已知项，不在本条） |
| R7-04 | 已修并验证 | 新增 `stuck_prefrozen()`：TEC 开沿时读数已冻满自己的窗口（体温 30 s / 水温 10 min）→ 当拍判卡死；体温先触发时把同拍也冻着的水温记进 `s_stuck_chn`（并报 ALM_SENSOR_OPEN） | `test_control_esp32.c`：K9 措辞改准确（"读数在 TEC 开之前**不久**冻住"才从开沿起算一个窗口；冻结时长 60 s→10 s，语义不变）；新增 K9b（开 TEC 前已冻 60 s → 当拍判）、K9c（水温冻 11 min → TC1 一开就当拍判）、K24（两路同时冻住 → 体温恢复不重开 TEC，水温也动了才 2 s 自愈）。修前 4 条红；修后 `PASS=320 FAIL=0`。变异 M66–M68 被抓；M35 仍由改写后的 K9 抓住 | 真机（五-5）。"动一下又冻住"的传感器每次仍能换一个窗口的输出（e3a 已记，10 分钟窗口是有意的代价） |
| R7-06 | 已实施待真机 | `ui_mode_fan_block` 先按链路判（"主控连接中断，现在不能操作"）；新增 `ui_home_fan_block`；`tui_p0.c` 的 `clk_fan` 现读快照年龄再决定发不发 | `test_ui_model.c`：F4 补一条 + E 段三条（断开 / 从未连上 → 提示；链路在 → 不拦）。修前 `PASS=184 FAIL=3`；修后 `PASS=187 FAIL=0`。变异 R06-1 / R06-2 被抓。H743 BENCH 档构建通过（`tui_p0.c` 的接线只靠编译 + 模式页同款写法） | 真机（五-6）：点击回调依赖 LVGL，主机只测到纯函数层 |
| R7-07 | 已修并验证 | 新增纯判据 `web_ota_outputs_stopped()`；`/api/ota` 与 WS 的 ota 帧在解析 m 之后、触发之前查两路 TEC 命令，不是都关 → HTTP 409 `reason=outputs_on` / WS `code=outputs_on`，不触发不重启 | `test_web_hardening.c` 四条（修前 3 红 → 修后 `PASS=18 FAIL=0`）；`test_web_gates.py` 新增四项（两处入口都查、且查在触发之前、两个 code 都在）：还原两处入口 → 4 项 FAIL，改后 PASS。默认档与**诊断档** IDF 构建均通过（这两个端点只在诊断档编译） | 真机（五-7）。网页前端没改（web_files 归 U 线）：`outputs_on` 是新 err code，前端未知 code 按既有逻辑提示 |
| R7-12 | 已修并验证 | 新增 `param_mailbox_blocked(id, prod)`；`console.c` 的 `p set` 在 `TCS_PROFILE < 1` 里拒 0x06 / 0x09 / 0x0A（整段落进 `#if`，不是运行时常量——否则台架档 rodata 里也留着这句文本）；`verify_prod_clean.py` 新增 `H743_PROD_ONLY`（交付档必须有、台架档必须没有） | `test_param_registry.c` 5 条：修前（旧实现 + 永远放行替身）3 红 → 修后 `PASS=84 FAIL=0`。交付档干净核对：修前 PROD elf `[prod-clean] FAIL`（拒绝文本不在）→ 改后 `PASS`；BENCH `--expect-hooks` `PASS`。核实过 `tools/site/` 下没有现场工具在交付档用邮箱 `p set`（`tune_refresh_check.py` 用的是 ESP32 诊断档控制台） | 真机（五-8）。非执行器参数照旧放行（屏幕调参页本来就能写，不新增能力）——这是我的判断，若要"交付档邮箱只读"请说一声 |
| R7-13.3 | 已修并验证 | 规格 §3 / §8 各**追加**一条更正（不改写原条款）：2 秒自愈只对通信类故障、只清通信类位；急停位（含断链急停）/ 超温位只有人工确认能清；急停会把模式切回手动 | 逐字核对：`git diff` 只新增 12 行、未改任何原条款；`test_web_gates.py` 的"正典"三项仍 PASS | R7-13.1（WS ota 的 `m` 不严格）与 R7-13.2（卡死借用的报警位文案）不在本条：前者 i3 在做，后者属界面文案（U 线） |
| R7-10 | 未做（只记录） | — | — | 0x21 增量帧在屏端活不过一圈主循环（`snap_reassert` 用上一帧全量整体回灌），显示最多滞后一个 200 ms 节拍，功能无害；改它要动屏端快照路径，风险大于收益，留待以后清理 |

## 三、R7-08：按什么水路拓扑做的、用户要确认什么

- **按"两路独立水体"做的**（仓库里 TC1 / TC2 写作"贴身水箱 / 环境水箱"，各有一路 TCACTTEMP 探头；用户确认接线之前
  按最坏情况处理）：TC2 的水温单独判超温（48 °C）、单独判卡死（10 分钟窗口），任一成立 → 进 SAFE → **两路全停**
  （SAFE 没有"只停一路"的形态）；服务层解析到任一路超温时也旁路关断两路（防控制任务不在）。
- **代价**：TC2 超温时 TC1 贴身水箱也停（偏保守；恢复要人工确认）。
- **若实际是同一路水、TC1 探头测得到 TC2 的热量**：R7-08 的超温部分变成冗余保护（不会误报——两路读数本来就一起
  动；但两路读数都不动时，谁先到窗口都会进 SAFE，语义仍然正确）。此时可撤销 TC2 那一份，代价是回到"只有 TC1 探头
  看得见"的单一依赖。
- **用户要确认三件事**：① TC1 / TC2 是不是同一路水体（管线怎么接的）；② 两路 TEC 的水箱 / 探头是否各自独立；
  ③ 若共用一路，是否接受"任一探头超温都停两路"的口径。
- 屏上文案区分不了 TC1 / TC2：两路卡死都借 `ALM_SENSOR_OPEN`（"水温探头"），两路超温都用 `ALM_OVERTEMP`。
  这是既有报警位不够用的限制（新增位要两边界面一起改，不在本批次）。

## 四、本批次验证总结果

条目内的红/绿输出已在"二"里逐条写明，这里只汇总最终全量（都是最后一个提交之前跑的实跑结果）。

- **构建矩阵**（`project_tcs/` 下 `bash tools/build_matrix.sh --esp32 --diag --site --twice`，先 source IDF）：
  `build_matrix: PASS`。
  - H743 四档从零：`PROD / BENCH / SIM / SIMWG  exit=0 error=0 warning=4`（告警基线四档均为 4）；
    `FLASH: 458620 / 477260 / 477260 / 516404`。
  - 交付档干净核对：`[prod-clean] PASS` ×4（含本批次新增的 R7-12 项：交付档必须有 `p set` 拒绝文本、台架三档必须没有）；
    ELF 与镜像采样自测 `自测：PASS`。
  - 可复现：`PROD` 两次从零构建 md5 相同（`115245dc5055b4c27a8fc2beaea33aaf`）。
  - ESP32 三档从零：`esp32-default / esp32-diag / esp32-site  exit=0 error=0 warning=0`；
    干净核对 `[esp32] 干净核对 PASS`（默认档无台架命令；诊断档 / 现场档命令集相符）。
  - ESP32 默认档两次从零构建：`tcs_wireless.bin` / `bootloader.bin` / `partition-table.bin` sha256 相同。
- **ESP32 主机全量**：`esp32/` 下 `bash tests/run_m207_tests.sh` → `esp32-tests: ALL GREEN`；
  `test_control` 320/0、`test_control_manual` 211/0、`test_m207_core` 96/0、`test_m207_queue` 53/0、
  `test_m207_link` 19/0、`test_m207_lb` 54/0、`test_m207_status` 40/0、`test_m207_elec` 61/0、
  `test_link_dir` 77/0、`test_link_peer` 22/0、`test_tcs_core` 102/0、`test_ota_pure` 29/0、
  `test_web_status` 230/0、`test_web_hardening` 18/0；源码门禁 `[m207gates] / [m1gates] / [webgates] / check_layout / version_check` 全 PASS。
- **变异脚本**：`python tests/mutation_control.py` → `[current] PASS=320 FAIL=0` + `mutation_control: ALL AS EXPECTED`
  （含新增 M57–M68、D02；M61 记等价并配 D02）；`python tests/mutation_control_manual.py` → `PASS=211` + `ALL AS EXPECTED`
  （含 B07–B10、E01–E02）；`project_tcs/tests/mutation_ui_model.py` → `PASS=187` + `ALL AS EXPECTED`（含 R06-1/R06-2）。
- **H743 主机全量**：`project_tcs/` 下 `bash tests/run_all.sh` → `run_all: ALL GREEN`
  （`[card_lines] PASS（3 张卡 / 162 行）`、`[proto_m1] PASS`、`esp32-tests: ALL GREEN` 等）。
- **协议单一源**：仓库根 `python tools/proto_sync.py --check` → `proto_sync: OK`。
- **审计清单**：本批次没有新增 / 删除代码文件（只改了既有文件 + 新增一个 `docs/results/*.md`，
  不进审计清单），所以没有动 `docs/审计文件清单.txt` 与官方库审计报告。

## 五、协调者要做的真机步骤（每条：怎么做 / 应看到什么）

1. **R7-01 断链 × 自愈型安全态**：屏接上、已布防、自动制热中 → 拔 M207 通信线（进 M207_LOST 安全态）→ 把屏重启
   （链路断 ≥3 秒）。应看到：控制台 `CTRL: SCREEN LINK LOST: no frame ... -> ESTOP`；锁存里有急停位；模块接回后
   设备**不**自己恢复输出（TEC 命令保持 0）；屏连上、点"解除"之后仍停在手动、无输出。
2. **R7-02 急停后立即掉电**：自动制热中按急停 → 1 秒内拔电 → 重新上电。应看到：设备是**手动 + 急停 + SAFE**
   （网页 / 控制台 `p list` 看 mode=0）；点"解除"后无任何 TEC 输出；再明确切一次自动才恢复。对照：急停后等 3 秒再
   掉电，结果一样。
3. **R7-08 TC2 保护**：模式页让 TEC2 单独制热 → 把 TC2 那一路的水加热 / 注入到 49 °C。应看到：当拍锁存超温、
   两路 TCSW=0、ALM_OVERTEMP；TC2 降温之前"解除"不清，降温后解除可退出。（若拓扑确认是同一路水，本条降级为冗余保护，
   只确认不误报即可。）
4. **R7-03 自动模式风扇**：自动制冷 / 制热运行中，在网页点风扇、在模式页点风扇键。应看到：网页回 BUSY（拒绝），
   屏上提示"TEC 工作时风扇保持开启"，风扇保持转；自动 IDLE（体温在带内）时风扇键照旧可用。
5. **R7-04 起步盲窗**：用注入档让水温读数恒定不变 ≥10 分钟（TC1 关着）→ 再开 TC1 制热。应看到：当拍（≤0.2 秒）
   进 SAFE、报警位是"水温探头"，而不是等 30 秒之后。
6. **R7-06 屏端风扇入口**：屏与主控链路断开 >0.5 秒（屏上已显示"主控连接中断"）→ 点首页风扇卡片 / 模式页风扇键。
   应看到：弹"主控连接中断，现在不能操作"，不发 0x15；链路恢复后风扇状态未被改变。
7. **R7-07 OTA 前输出已停**（诊断档）：自动制热中在网页点 OTA（`m=0`）或回滚（`m=1`）。应看到：HTTP 409
   `{"t":"ota","started":false,"reason":"outputs_on"}` / WS `{"t":"err","code":"outputs_on",...}`，设备**不**重启；
   把两路 TEC 都停了再点 → 正常开始拷贝并重启。
8. **R7-12 交付档邮箱**：交付固件 + 调试器读邮箱执行 `p set 0x06 1` / `p set 0x09 2`。应看到：
   `rejected (prod: mode / TEC outputs are not writable from this mailbox)`、参数与输出都不动；
   `p set 0x03 37.0` 仍然可写。台架固件上 `p set 0x06 1` 仍可写。

## 六、最终全量与提交

- 分支 `fix/s-r7f` 上的提交（按先后）：`076c547`、`70359ac`、`ec0916d`、`73804d8`、`b44e84b`、`ccb6fae`、`219a36c`、
  `eef187a`、`9548c12`，最后是**本提交**（研学卡重锚 + 行数基线 + 本文档）——最终提交号见交付说明。
- `build_matrix.sh --esp32 --diag --site --twice`：`build_matrix: PASS`（明细见"四"）。
- `tests/run_all.sh`：`run_all: ALL GREEN`。
- `tools/proto_sync.py --check`：`proto_sync: OK`。
- 研学卡：`reanchor_study_cards.py` 重锚后 `[card_lines] PASS`（`研学卡_HAL_UART` 43/43、`研学卡_HAL_GPIO_EXTI` 58/58、
  `研学卡_HAL_PWR_RCC` 61/61；触发重锚的是 `console.c` 的 `p set` 新增 13 行）。

## 七、风险与需要人看的点

1. **R7-01 的行为变化**（需要现场习惯）：以前"模块掉线 + 屏恰好重启"会自己好；现在会留一个急停，要人工确认。
   这是"断链 = 急停"的裁决落地，但现场维护流程（换屏 / 屏刷机）会多一步确认。
2. **R7-08 的拓扑假设**（见三）：若实际是一路水，多出来的是冗余保护；若是两路独立水，这条是本批次最关键的补强。
3. **R7-02 的同步 NVS 写**：急停路径多了一次 flash 写（模式落盘），在控制任务里最坏几十~几百毫秒（与既有的锁存
   落盘同一量级）；已放在 `safe_outputs()`（两路 TCSW=0）之后。控制任务的 TWDT 是 3 s，余量足够，但真机上值得看一眼
   急停到 TCSW=0 的时延（五-2 顺带看）。
4. **R7-04 与 F2 的边界**：10 分钟窗口仍保护水温；"动一下又冻住"的传感器每次换一个窗口的输出，是 e3a 已记的代价。
5. **R7-03 第一版被既有用例抓红**（W2-20）：说明"任一路 TEC 命令为开 ⇒ 风扇开"与既有设计（自动 IDLE 下 TEC2 复原、
   风扇归状态机管）冲突。现在按状态判；若以后要统一"TEC 开着风扇必开"，得先改 W2-20 那条设计。
