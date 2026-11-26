# S-G r9f 结果（独立复核 R9 的剩余 2 条 P2 + 4 条 P3）

- 批次：r9f。工作树 `D:\Espressif\ws\tcs-g-r9f`，分支 `fix/s-r9f`。
- 基座：集成分支 `fix/s-u-int2` @ `fc12c04`（tag `s-gate-20261126-2039`，全量门禁通过）。
- 全程**未碰设备**：没有 probe-rs、串口、烧录，没有任何驱动 TEC / 加热的操作。证据 = 读代码 + 主机用例 + 源码门禁 + 构建矩阵。
- 范围：R9 的 8 项（2 条 P2 + 4 条 P3 + 收尾；R9-06 只记录）。R9 的发现清单见 `%TEMP%\tcs_site\R9_findings.md`。

## 0. 一页结论

| 编号 | 严重度 | 判定 | 提交 | 一句话 |
|---|---|---|---|---|
| R9-02 | P2 | **已修并验证** | `bcb6bb6` | 锁存期间继续看水温自己的 10 分钟窗口，满了记进涉事通道；体温单独恢复不放行 |
| R9-01 | P2 | **已实施待真机** | `c51d54a` | 断链期间网页 `c=9` 如实回 `link_down`，不清位、不落盘 |
| R9-04 | P3 | **已修并验证** | `c51d54a` | 只含 STUCK 的锁存不再产生 NVS 写（0 次，原 2 次） |
| R9-03 | P3 | **已修并验证** | `ba33e01` | 水温预冻改成开沿后 30 秒确认窗；窗口内动过就撤销 |
| R9-05 | P3 | **已修并验证** | `c2c4603` | 任意屏可写参数被拒后立刻回退到权威值；被拒过的参数同值可再发一次 |
| R9-07 | P3 | **已修并验证** | `d2fd0af` + `4d7ceeb` | 动机注释按事实改写（两端原本都是 16），并避开源码门禁的旧模型字面量 |
| R9-08 | P3 | **已修并验证** | `3883722` | 屏端 ALM_PROTO 不再虚增"还有 N 条"，有横幅与确认原因 |
| R9-06 | P3 | **已修并验证**（只记录） | `8d8bc69` | 两处非等价在代码处写明"为什么保持现状"，行为不变 |

判定计数：已修并验证 6 / 已实施待真机 1（R9-01）/ 未做 0 / 不适用 0 / 已修未记 0。

## 1. R9-02（P2）两路同时冻住：水温按自己的长窗口进涉事通道

**问题（R9 原文）**：r8f 撤掉 `tc1_pre` 之后，体温先判出 STUCK 时不再记水温。两路**同时**冻住时
体温一恢复（2 秒自愈）设备就退出 SAFE、重开 TEC，而水温仍冻着——要等它自己的 10 分钟窗口满才被
`tc1_stuck` 判出来。R9 在主机上量到输出在"水温读数冻着"下开了 **557.9 秒**；48°C 硬切断读的正是那个
冻住的值，全程失明。

**改法（`esp32/components/tcs_core/control.c`）**：
- STUCK 锁存期间继续复查两路水温的**长窗口**（`stuck_water_is`，10 分钟——不是 R8-02 撤掉的 30 秒短窗口）：
  满了就 OR 进 `s_stuck_chn` 并报借来的 `ALM_SENSOR_OPEN`。`stuck_recovered` 因此要求它重新变化，
  体温单独恢复不再放行。
- 只复查"这次锁存时**正被驱动**"的水温通道（`s_stuck_watch`，trip 那一拍记）：从没用过的 TC2
  读数冻着与 48°C 切断无关，不该被拖进涉事通道。**第一版实现漏了这条，被新用例 K26 抓红**
  （A4 红：bit4 把设备永久按在 SAFE），已修正。
- 体温那一拍同时按长窗口记一次（R9 建议原文；与 `tc1_stuck` 在同一拍通常等价，真正起作用的是
  锁存期间的复查）。

**红/绿证据**：
- 新用例 **K26**（`esp32/tests/test_control_esp32.c`）：两路冻住 → 体温先判 → 水温窗口满 →
  `ALM_SENSOR_OPEN` 出现 → 体温单独恢复 3 秒内 `tec_cmd` 恒 0、状态仍 SAFE → 水温也动了才 2.1 秒自愈回 HEAT。
- 修前（基线 `control.c`）：`FAIL K26 [R9-02] the water channel is recorded ...` +
  `FAIL ... body recovery alone does not resume output`（实测 `on_ticks=1`，A4 红）。
- 变异 **M80**（删掉这两处复查）CAUGHT by 2 checks；`mutation_control: ALL AS EXPECTED`。

**遗留（重要）**：体温在**水温窗口满之前**就恢复的情形，输出仍可对着冻住的水温读数开到窗口满
（约 557 秒，与 R9 的实测一致）。要缩短只能给"同时冻住"一个比 10 分钟短的确认窗，而那会与 R8-02
（健康水温稳态 30–53 秒，F2 实测）冲突——这是需要用户裁决的取舍，见第 9 节。

## 2. R9-01（P2）+ R9-04（P3）断链期间网页"解除" + NVS 写放大

**问题**：屏链路断开 → 3 秒后急停锁存。网页按"解除"：`tcs_alarm_ack` 清掉急停位 → 下一拍
`link_loss_check` 又锁上。网页先收到"成功"、随后报警回来，没有任何原因文案；每点一次还写 2 次
NVS（ack 清位 + 重锁；按 10 Hz 算一小时 72k 次）。屏永久损坏时设备再也进不了运行态，唯一出口是断电重启。

**改法**：
- `esp32/components/tcs_web/websocket.c` 的 `handle_cmd`：`c=9`（ALARM_ACK）在屏链路断开期间
  （`link_peer_age_ms` 已连过 且 `age >= TCS_LINK_LOST_ESTOP_MS`，与断链急停同一常量、同一阈值）
  **不执行**，回 `{"t":"err","code":"link_down","msg":"屏链路断开，急停只能在屏上确认"}`，并在网页事件
  日志留一条"网页：确认恢复被拒绝（屏链路断开）"。只拦 c=9；急停/模式/风扇/保存不受影响；
  屏从没连上过的台架不触发断链急停，ack 照常放行。**本仓库没有 HTTP `/api/cmd`**（网页命令只有
  WS 这一条路），所以只改了 WS 分支。
- `control.c`：`safety_trip` 只在置上的位属于 `SAFETY_PERSIST_MASK` 时落盘；
  `safety_ack_latched` 只在"落盘视图"（`latch & SAFETY_PERSIST_MASK`）真的变了时落盘。
- 规格追加（不改写原裁决记录）：`docs/现行规格说明书_v1.2.md`（§5 裁决记录后新增一条）、
  `esp32/components/tcs_web/WS_PROTOCOL.md`（4.1 命令码 9 与 err 码表）。

**红/绿证据**：
- **K28**（`test_control_esp32.c`）：只含 STUCK 的 trip 与 ack 的 persist 调用次数都是 **0**
  （修前分别是 1 与 1；变异 **M81** 把两处改回去 → CAUGHT by 2 checks）。
- `esp32/tests/test_web_gates.py` 新增 7 条源码门禁：回绝存在、位置在 `control_apply_cmd_src`
  **之前**、只覆盖 `TCS_CMD_ALARM_ACK`、阈值与急停同一常量、include 单一源、文案。
  基座 `websocket.c` 上 **6 条红**；修后全绿。
- `run_m207_tests.sh` ALL GREEN；`mutation_control` ALL AS EXPECTED。

**遗留**：
- **产品口径待用户裁决**：屏永久损坏时设备不能进入运行态，唯一出口是断电重启（重启后"上电后屏
  从没连上过"重新成立，网页 ack 才能清掉持久化的急停锁存）。要不要给"屏坏了也能确认急停"的出口
  （例如长按物理键 / 网页二次确认 + 明确风险提示）见第 9 节。
- **前端冻结**：`app.js` 对所有 `err` 帧只弹通用文案（"设备无法解析上一条指令"）；`c=9` 的回执走
  `ack` 帧，我们回的是 `err`，所以前端的 pending 会在 `T.ackMs` 后自己超时并弹"设备未确认 解除"。
  两句都不如"屏链路断开，急停只能在屏上确认"贴切——专门文案要等前端解冻后补；准确原因已经进串口
  日志与网页事件日志（"网页：确认恢复被拒绝（屏链路断开）"）。
- R9-01 的关单条件里"真机拔屏线 → 网页确认被拒"的部分需要真机。

## 3. R9-03（P3）水温预冻的 30 秒确认窗

**问题**：水温在 TEC 开沿**当拍**就判卡死（读数在开 TEC 前已冻满 10 分钟 → 当拍 STUCK + SAFE +
"水温探头断线"）。10 分钟的健康余量来自 TEC 关着、环境 0.2°C/10min 漂移的台架数据；保温水箱 /
恒温室里 0.01°C 分辨率下 10 分钟逐位不变完全可能，表现就是"按制热就报探头断线"，用户会去拔插探头。

**改法（`control.c`）**：新增 `s_water_pre_ms[2]` 与 `stuck_water_prefrozen()`：预冻成立时先挂起，
TEC 开沿后 **30 秒**内读数动过一次立刻撤销；一次都没动（且冻结总时长仍满它自己的 10 分钟）才判。
`control_init` 复位。

**红/绿证据**：
- **K9c / K25** 按新口径改写：开沿 +29.9 s 不判、+30.1 s 判（旧断言"当拍判"按设计不再成立）。
- 新增 **K27**：确认窗内读数动一下（+5 s）→ 撤销，再跑 60 s 不判。
- 变异 **M82**（预冻回到"开沿当拍"判）CAUGHT by 1 check（K9c）。
- **M66** 由"必须被抓"改标"等价"：30 秒确认窗把预冻的收益吸收掉了——撤掉预冻走 `stuck_is` 的
  30 秒宽限，两条路径都在开沿 +30 s 判，K9c 的 29.9/30.1 断言在变异前后一模一样；预冻路径本身由
  M82 钉住。**这不是削弱，是一处新产生的真实等价**（如实记录在变异表里）。

**遗留**：真机"TEC 关、热稳态能否连续 10 分钟逐位不变"仍未量（R9 的挂账），本批次不碰设备。

## 4. R9-05（P3）屏可写参数被拒的回退与同值重试

**问题**：X01-F-03 的回退只覆盖 `tc1_set` / `tc2_set`；其余屏可写参数被拒后，被拒值留在屏上直到
0x11 镜像泵下一轮（每 800ms 一条、51 条约 **41 秒**）。更糟：被拒值已写进本地表，用户再点同一个值会
被 `param_set` 的"相等短路"判成"没变"——**根本发不出帧**。

**改法**：
- `project_tcs/App/tcs/param_registry.c`：两份按表下标的旁路状态——权威值影子（`param_link_sync` /
  `param_mirror_apply` 写入；来源 = 0x20 快照 / 0x11 镜像泵 / CLAMP 回执）与"上次写被拒"标记；
  新接口 `param_revert_to_authority` / `param_note_write_result`。
- `param_set` 的相等短路：被拒过的参数放行一次（发出去后清标记）。
- `project_tcs/Core/Src/main.c` 的 `on_param_write_result`：CLAMP 用回执值并清标记；
  REJECT/BUSY 立刻回退到权威值（没有影子时退回老行为），并置"被拒"标记；OK 清标记。

**红/绿证据**（`project_tcs/tests/test_param_registry.c`，PASS=91 FAIL=0）：
- 新增 6 条断言：改值→广播；被拒→立刻回退权威值；被拒后同值再写**真的发帧**；只放行一次；
  OK/CLAMP 清标记；没有权威值时不回退；影子对任意屏可写参数生效（body_cold）。
- 判别性：单独回退 `param_set` 的相等短路 → `FAIL: R9-05 [retry] after a rejection the same value is
  sent again`（PASS=90 FAIL=1）；基线没有这两个新接口，用例连编译都过不去（红）。
- 回退路径的 main.c 接线由 `build_matrix`（H743 四档）编译验证。

**遗留**：主控把 `rv==2`（"值已记下、但模块没受理（离线/忙）"）映射成 `TCS_ACK_BUSY`
（`esp32/main/main.c`），屏端一律当拒绝回退——主控其实已经收下了这个值，两边短时间反向不一致，
等镜像泵纠正。要区分需要一个新 status 或 CLAMP+ret 的语义决定，涉及协议与两端，本批次未改。
屏端 LVGL 调参页的显示路径（点击回调）仍主机编译不了（R9 的"未能验证"之一）。

## 5. R9-07（P3）TCS_MAX_TXQ 的动机注释

**问题**：注释说"原先一边 16 一边 8"，与事实不符：`f3b46d2` 的两份 `comm.h` 都是 `MAX_TXQ 16`。
那个"8"来自 H743 的一句注释，是把注释当成了代码。

**改法**：只改注释（值不动、两条 `_Static_assert` 不动）。第一版直接引用了旧注释原文
"注册表 8 条目 + …"，撞上 `project_tcs/tests/test_link_contract.py` 的一条源码门禁（禁止该字面量
出现在协议头里，它代表废弃的容量模型）→ 补正为转述（`4d7ceeb`）。

**红/绿证据**：`test_link_contract.py` 基座绿 → 第一版注释红（`FAIL protocol header has no stale
'registry 8 entries' model`）→ 补正后 `PASS=16 FAIL=0`。`python tools/proto_sync.py --check` OK。

**遗留**：`6d97085` 的提交信息里同样的错误陈述无法改写（不改写历史），以协议头注释为准。

## 6. R9-08（P3）ALM_PROTO 的文案与"还有 N 条"

**问题**：`ALM_PROTO`（对端协议版本不一致）在协议头有定义、ESP32 会 OR 进快照 alarm，但屏端
`ui_banner_select` 没有这一位：版本不配对时急停/超温/功能启动失败横幅会写"还有 1 条"，而没有任何
横幅能指出是协议版本；`ui_ack_reason` 对它返回 NULL（点"解除"后只说"仍未解除"、没有原因）。

**改法（只动判定与文案，不动版面、不新增控件）**：
- `project_tcs/App/lvgl_ui/ui_model.c`：`alarm_count` 先剥掉 `UI_ALARM_TIPS_MASK`（= `ALM_PROTO`）。
- `ui_banner_select` 追加 `ALM_PROTO` 横幅，复用现有 `UI_BN_VER_MISMATCH` 与
  "主控固件与屏不匹配 / 请同步升级后再用"（字库覆盖率门禁限定了用字，不引入新字）；位置在安全类与
  通信类之后。
- `ui_ack_reason` 对 `ALM_PROTO` 返回"主控固件与屏不匹配"。

**红/绿证据**（`project_tcs/tests/test_ui_model.c`，PASS=190 FAIL=0）：
- 新增 3 条断言（ESTOP+PROTO 时 extra=0；PROTO 单独出 VER_MISMATCH 横幅；`ui_ack_reason` 非空）。
- 判别性：基线 `ui_model.c` 上这 3 条红（PASS=187 FAIL=3）。
- 变异 **M35**（把 ALM_PROTO 加回计数）CAUGHT by 1 check；`mutation_ui_model` ALL AS EXPECTED。

**遗留（网页前端冻结）**：网页 `app.js` 的 `ALARMS` 表只有 1/2/4/8/16/32 六位，没有 128（ALM_PROTO）
与 64（ALM_SERVICE）；网页端**没有**"还有 N 条"计数器（逐位出卡片），所以不存在虚增，但这两个提示位
在网页上仍然不可见。补卡片需要改 `app.js`，按"网页前端先不改"留给前端解冻后处理。

## 7. R9-06（P3）link_task 拆分里的两处非等价——只记录

**结论**：两处都不改行为，在代码处写明"为什么保持现状"：
- `link_registry_note_param` 多写 `s_param_val / s_param_valid`：`link_param_value()` 当前没有任何
  消费者，无可观察差异；"收到 0x12 就记进注册表缓存"语义上更自洽，为一个没人读的缓存回退更奇怪。
- `link_alive` 的 `< 3000` → `<= 3000`：1 ms，无实际影响；断链急停判定走 `link_peer_age_ms` 与同一
  常量，本就不依赖这个边界。

**红/绿证据**：只加注释（不参与编译）；`esp32` 全量主机套件在收尾门禁里绿。

## 8. 本批次验证总结果

命令与结果（原样汇总行）：

| 命令（工作目录） | 结果 |
|---|---|
| `bash tests/run_m207_tests.sh`（esp32/） | `esp32-tests: ALL GREEN`；`test_control: PASS=362 FAIL=0`、`test_web_*`、`test_link_*`、`test_snap_time` 全绿 |
| `python -I -X utf8 tests/mutation_control.py`（esp32/） | `[current] PASS=357 FAIL=0`；`mutation_control: ALL AS EXPECTED`（M80/M81/M82 CAUGHT；M66 如实标等价） |
| `bash tests/run_all.sh`（project_tcs/） | 见 §8.1（含 H743 全套件、x3 恒真/断言数门禁、两套变异、源码门禁） |
| `python -I -X utf8 tests/mutation_ui_model.py`（project_tcs/） | `mutation_ui_model: ALL AS EXPECTED`（M35 CAUGHT） |
| `python -I -X utf8 ../esp32/tools/official_lib_audit.py --check-report` | `[audit] PASS (311 files, 0 uncovered, 1 unclassified)`（收尾提交里重新生成后） |
| `python -I -X utf8 tools/proto_sync.py --check`（仓库根） | `proto_sync: OK (...)` |
| `bash tools/build_matrix.sh --esp32 --diag --site --twice`（project_tcs/） | 见 §8.2 |

### 8.1 run_all / 变异

`bash tests/run_all.sh`（project_tcs/）最终输出：`run_all: ALL GREEN`。其中：

- H743 全套件：`test_param_registry: PASS=91`、`test_ui_model: PASS=190`、`test_control: PASS=86`、
  `test_post: PASS=81`、`test_proto_tcs: PASS=46`、`test_touch_session: PASS=10497` 等全绿。
- x3 门禁：`[tautology] PASS（0 处）`、`[assert_count] PASS（27 个测试可执行行：12873 条断言 +
  46951 例语料样本，均不低于基线）`。
- 审计：`[audit] PASS (报告与代码一致: 311 files, 0 uncovered, 1 unclassified)`。
- 研学卡：`[card_lines] PASS（3 张卡 / 162 行，文件、行号、接口名逐行对得上）`（本次改动后重锚）。
- run_all 内置的 6 套变异（estop_gate / link_dir / link_follow / m207_txq / audit_ring / perf_hist）
  全部通过（日志在 `project_tcs/build/mut_*.log`）。
- esp32 套件（run_all 内也会跑一遍）：`esp32-tests: ALL GREEN`。

单独跑的变异：`mutation_control.py` → `ALL AS EXPECTED`（M80/M81/M82 CAUGHT，M66 等价）；
`mutation_ui_model.py` → `ALL AS EXPECTED`（M35 CAUGHT）。

### 8.2 构建矩阵

`bash tools/build_matrix.sh --esp32 --diag --site --twice`（一次，约 20 分钟）：

- H743 四档从零构建：PROD / BENCH / SIM / SIMWG 全部 `exit=0 error=0 warning=4`
  （与基线 4 条一致）；FLASH 461796 / 480476 / 480476 / 519612 B。
- 交付档干净核对：`[prod-clean] PASS` ×4；`交付档干净核对：PASS`。
- 可复现比对：`PROD ELF vs PROD 镜像 采样点 3，不相等 0`；自测 PASS（异源至少 1 处不符）。
- ESP32 三档从零构建：默认 / 诊断 / 现场 全部 `exit=0 error=0 warning=0`；
  `[esp32] 干净核对 PASS（默认档无台架命令；诊断档 / 现场档命令集相符）`。
- ESP32 默认档两次从零构建：app / bootloader / partition-table 的 sha256 相同。
- 汇总：`build_matrix: PASS`。

## 9. 风险与需要用户 / 协调者决定的事

1. **R9-01 的产品口径（最需要裁决）**：当前口径是"急停只能在屏上确认"，因此**屏永久损坏时设备不能
   进入运行态，唯一出口是断电重启**。可选：(a) 维持现状，写进用户手册；(b) 给一条"无屏也能确认"
   的出口（例如面板物理键长按 / 网页二次确认 + 明确的风险提示），那就要改 `link_loss_already_safe`
   的触发条件为"断链 + 本次断链还没被确认过"，并单独评审（安全判定）。
2. **R9-02 的残余盲窗（~557 秒）**：体温在 10 分钟内恢复时，输出仍可对着冻住的水温读数开到窗口满。
   缩短它需要一个比 10 分钟短的"同时冻住"确认窗（例如 120 秒，是 F2 实测健康段 53 秒的 2.2 倍），
   代价是健康稳态下更可能误报一次 STUCK。这是安全 / 可用性的取舍，需要用户定。
3. **`rv==2` 的语义（R9-05 第三点，未做）**：主控"收下了但没送到模块"被屏端当拒绝回退，两边短时间
   反向不一致。要区分需要新 status 或 CLAMP+ret 的协议决定。
4. **网页前端冻结**：`link_down` 的专门文案与 ALM_PROTO/ALM_SERVICE 两张报警卡片要等前端解冻。
5. **M66 改标"等价"**（R9-03 的直接后果）：确认窗把预冻的收益吸收掉了，这是真实等价而非削弱；
   已在变异表写明，若要保留判别力只能删掉预冻判据（那是行为变更，本批次不做）。
6. **现场档配置**：本批次用的是 `tcs-int2` 的 `esp32/sdkconfig.site`（含 X04-F-03 的 coredump 两行）；
   `D:\Espressif\ws\tcs\esp32\sdkconfig.site` 仍是陈旧副本，属共享环境问题。
7. **真机部分（按批次约定未做）**：R9-01（拔屏线 → 网页确认被拒）、R9-05（屏上调参被拒后的显示
   回退）、R9-03（热稳态 10 分钟读数是否真的逐位不变）都需要真机。
