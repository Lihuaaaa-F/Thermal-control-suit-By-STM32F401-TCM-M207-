# F 模式页（批次 m1）结果 — 2026-11-26

一句话结论：用户 2026-11-26 拍板的 12 条全部实现，分五层提交在 `feat/mode-page`（基座 `fix/s-u-int2` @ `fddfe57`）；
主机用例、变异、门禁全绿，H743 四档 / ESP32 三档从零构建见 §5。**真机一项没做**（不碰设备）——
§9 是给用户的上机步骤。与并行批次 e3a 的重叠见 §6，其中有两处合并后必须人工复核的语义交互。

判定口径：**已修并验证** = 有判别性主机用例 / 构建证据、不依赖真机；**已实施待真机** = 代码与主机侧验证完成，
关单条件里的真机部分没做；**未做** = 附原因。

## 0. 分支与提交

| 提交 | 层 | 内容 |
|---|---|---|
| `bf1c528` | 1 控制内核 | `control_manual.[ch]`、`link_loss_guard.[ch]`、参数 0x09 / 0x0A、`control.c` 调用点、`control_service.c` 接线、链路层记对端帧时刻；用例 `test_control_manual.c`、`test_link_peer.c`、门禁 `test_m1_gates.py` |
| `b456586` | 2 M207 轮询 | `m207_core.c` 轮询 `TCx:TCACTVOL?` / `TCx:TCACTCUR?`；用例 `test_m207_elec.c`；黄金样本加 4 种帧 |
| `e80938c` | 3 协议 | `TCS_PROTO_VER` 2；0x20 追加 9 字节、0x21 两个新 mask 位；屏端镜像；两端用例、`proto_ref.py`、`proto_layout.h` |
| `c50e807` | 4 界面 | `tui_p4_mode.c`、首页模式键、`ui_model` 模式页逻辑、曲线页体温线、字库、`estop_matrix.py` 加页；用例 `test_ui_model.c` F 段、`test_hist_body.c` |
| `18e434a` | 4 界面 | 控制台 `linkmute` 回显与帮助说明"静默 ≥3 s 触发断链急停" |
| `f19e6c2` | 5 文档 | 三份现行文档增补 |
| `708620c` | 文档 | cherry-pick 集成分支 `3936d1d`（研学卡重锚；本分支基座上 `[card_lines]` 本来就红） |
| `4bb03bb` | 文档 | 研学卡按本分支再锚一次（`Core/Src/main.c` 多了 19 行） |
| `ec46549` | 1 控制内核 | 变异体脚本 `esp32/tests/mutation_control_manual.py`（34 处） |
| 见最终回复 | 结果 | 本文档（含 §5 原始输出） |
| 见最终回复 | 审计 | 官方库审计清单与机器报告重新生成（分支上最后一个提交；合并时请丢弃后统一重新生成） |

## 1. 对照用户拍板的 12 条

| # | 用户原话要点 | 做了什么 | 怎么验证的 | 判定 | 遗留 |
|---|---|---|---|---|---|
| 1 | 首页"切自动"换成显示当前状态的按钮；点击进二级页 | `tui_p0.c`：键上文字恒为"手动"/"自动"（断链置灰）；点击 `tui_request(TUI_MODE)`；首页不再调 `tui_dlg_mode()` | `test_ui_model.c` F1；H743 四档构建 | 已实施待真机 | 观感需截图确认 |
| 2 | 手动模式能看水箱温度和体温，可直接点制冷 / 制热 / 暂停 | `tui_p4_mode.c` 三张温度卡 + 每路三个键；点键 = 写 0x09 / 0x0A（等 ACK 的 0x12） | F3–F10、M01–M12；H743 构建 | 已实施待真机 | — |
| 3 | 两路 TEC 分别控制（TEC1 / TEC2） | 两路各一个方向参数；`control_manual.c` 逐路映射输出 | M02、M06；变异 A01 / A12 / C01 | 已实施待真机 | — |
| 4 | 手动时目标温度不显示、不生效 | 手动输出用固定目标 15.0 / 45.0（与自动状态机同一对常量）；界面没有目标温度 | M02（目标 15.0 / 45.0）；页面无设定值控件 | 已实施待真机 | — |
| 5 | 手动、自动两页都显示功率 | 两页的 TEC 行都画功率与电压电流（主控轮询 `TCACTVOL` / `TCACTCUR`） | `test_m207_elec.c`（61 条）、协议用例、F9 | 已实施待真机 | 关着时的读数待真机 |
| 6 | 自动页不做舒适区间条；温度卡与手动页同一种卡片 | 自动页复用同一组温度卡控件；TEC 行整行只读 | H743 构建 | 已实施待真机 | — |
| 7 | 断链必须安全优先，等于发急停 | `link_loss_guard.c` + `control_service.c` 每拍判定 → `tcs_emergency_stop_src` | L01–L03；变异 B01–B06；`test_m1_gates.py` 逐字核对接线 | 已实施待真机 | 见 R1、D4 |
| 8 | 网页端先不改 | `tcs_web/` 一个字节没动；新参数不带 `PF_WEB` | `test_web_status.c` 230 条全过；`web_gz_check` | 已修并验证 | 网页会收到 0x09 / 0x0A 的变更推送，已核对 `app.js` 对未知参数号判空 |
| 9 | 其余按默认值 | 见 §3 | 见 §3 | — | — |
| 10 | 界面上叫 TEC1 / TEC2 | 页面与参数名都用 TEC1 / TEC2 | 页面源码 | 已实施待真机 | — |
| 11 | 温度卡不要趋势箭头 | 没有画 | 页面源码 | 已实施待真机 | — |
| 12 | 三张温度卡都能点进曲线页；曲线页加体温曲线 | 三张卡与首页水箱卡同一条路径 `tui_request(TUI_CHART)`；历史环加体温采样；曲线页第三条线 + 图例 | `test_hist_body.c` 70 条；H743 构建 | 已实施待真机 | 观感需截图确认 |

## 2. "要做的"五节逐项

### 2.1 控制内核（`esp32/components/tcs_core/`）

| 项 | 做了什么 | 验证 | 判定 |
|---|---|---|---|
| 两个参数 | 0x09 `tec1_manual`、0x0A `tec2_manual`：U8、0..2、初值 0、`PF_RW\|PF_SCREEN`（不持久化、不带 `PF_WEB`） | M01；变异 D05 / D06 | 已修并验证 |
| 写前校验 | `param_set` 前置检查块：取值不是 0/1/2 → `-5`；非手动 → `-3`；安全态 / 急停 / 超温锁存 → `-4`（经 `on_check` 钩子问 `control_param_check`，原因码原样透传）；内核自己清回暂停（值 0 + `PARAM_SRC_KERNEL`）不经校验 | M04 / M05 / M09 / M10；变异 A09 / A10 / C06 / D01–D04 | 已修并验证 |
| 输出映射，只在变化时下发 | 制冷 = `TCADJTEMP 15.0` + `TCSW=1`；制热 = `TCADJTEMP 45.0` + `TCSW=1`；暂停 = `TCSW=0` | M02、M03；变异 A01–A03 | 已修并验证 |
| 风扇联动与恢复 | 任一路制冷 / 制热 → 风扇开；两路都暂停 → 恢复进入联动前记下的用户选择；解除安全态时复原的也是用户的选择 | M07、M08；变异 A04–A06 / A11 / C10 | 已修并验证 |
| 上电默认暂停 | 两参数初值 0，`control_init` 清手动模块状态 | M01；D05 | 已修并验证 |
| 进安全态 / 急停清零 | `enter_safe` 调 `control_manual_reset(0)`；急停登记请求、节拍顶执行；确认解除后输出仍是暂停 | M08、M09、M10、L02；变异 A08 / C02 / C05 | 已修并验证 |
| 切到自动清零 | 登记请求 → 节拍顶：两路参数回暂停、两路关断、第一路交给状态机 | M06；变异 C04 / C05 | 已修并验证 |
| 模块回读关断 | 已下发"开"而模块回读是关（模块断电重启后复联重同步、网页把它关了）→ 不自动重开，参数置回暂停并广播 0x11 | M11、M12；变异 A07 | 已修并验证 |
| 输出方向给快照 | `control_tec_dir()`：通道关 = 暂停；自动第一路看状态机；手动看已下发方向；开着但不是模式页设的 = 3 | M02 / M06 / M09 / M12；变异 C07 / C08 | 已修并验证 |
| 断链 = 急停（判定） | `link_loss_guard_step()`：满 3000 ms 才触发；从未连上不触发；已在安全态不触发；一次断开只触发一次，链路恢复后重新武装 | L01：2.9 s / 2.999 s 不触发、3.0 s 触发、不重复、恢复后重新武装、从未连上永远不触发；变异 B01–B06 | 已修并验证 |
| 断链 = 急停（路径与后果） | 2.9 s 输出原样；3.0 s 触发后两路 TEC 关、风扇开、`ALM_ESTOP`、强制手动；恢复后 6 秒内保持安全态；确认后输出仍是暂停 | L02、L03 | 已修并验证 |
| 断链 = 急停（接线） | `control_service.c` 每个控制节拍调 `link_loss_check()`，触发走 `tcs_emergency_stop_src(PARAM_SRC_KERNEL)`，日志 `SCREEN LINK LOST` | `test_m1_gates.py`（该文件依赖 RTOS，主机编译不了，逐字核对） | 已修并验证（接线）／已实施待真机（行为） |
| 对端帧时刻 | `comm.c` 只在方向闸门之后记；回声、坏 CRC、杂字节、方向不符的帧都不算；ACK 与重复帧算 | `test_link_peer.c` 22 条 | 已修并验证 |
| 既有行为不变 | `test_control_esp32.c` 187 条、`mutation_control.py` ALL AS EXPECTED | 见左 | 已修并验证 |

变异：`python esp32/tests/mutation_control_manual.py` → 34 处，33 处被抓，1 处（急停不登记清零）按预期存活
（下一拍 `enter_safe` 同样清零；删掉 `enter_safe` 那道防线由 C02 单独抓）。

说明两处与说明文件的字面差异：
- 用例放在新文件 `test_control_manual.c` 而不是追加进 `test_control_esp32.c`——后者 e3a 也在改（上游 +484 行），追加必冲突。
- 变异体放新脚本 `mutation_control_manual.py`，同样是为了不与 e3a 在 `mutation_control.py` 上的改动冲突。
- "手动输出回暂停"除了急停 / 超温 / 安全态 / 断链 / 切自动，还多了一种：模块回读变关。这是为了让"参数 = 实际输出"
  在模块断电重启后仍然成立，且不自动重开（与既有"模块侧状态未知时不自动重开"的口径一致）。

### 2.2 M207 轮询（`esp32/components/tcs_m207/`）

| 项 | 做了什么 | 验证 | 判定 |
|---|---|---|---|
| 每秒轮询四条查询 | 子状态机 `m207_elec_poll()` 只在温度轮询的空档发帧：温度应答到了之后、下一次温度轮询之前，且应答截止早于下一次温度轮询 | `test_m207_elec.c` 用例 2：10 秒内温度轮询 20 次、间隔 500 ms 一步不让；每个量 10 秒 ≥14 次；没有一条电学查询落在温度轮询前 300 ms 的窗口里 | 已修并验证 |
| 应答按数值解析 | 按参数名匹配入缓存，与 `TCACTTEMP` 同一套解析；负零吃掉；负电流、超量程按坏值丢弃并保留上一次的好值 | 用例 1、5；变异"负零不吃掉"被抓 | 已修并验证 |
| 不支持就停 | 回码或不应答连续 3 次 → 判不支持、停发，只留一条日志；复联后清零重试；不计链路失败、不被当成传感器开路 | 用例 3、4、7 | 已修并验证 |
| 与别的帧交错 | 本拍名额给了别的帧时作废在途的电学等待，那一帧的回码不会被记到电学查询头上 | 用例 8；变异 M-A 被抓 | 已修并验证 |
| 取值接口 | `m207_elec_get(ch, out)`：电压、电流、数据龄、`valid`（龄 ≤ 3000 ms）、`fresh`；`m207_elec_supported()`（说明里写的 `m207_last Volt/Cur(ch)`，实现成一个带龄的结构体） | 用例 6 | 已修并验证 |
| 既有节拍不被拖慢 | e2 的全部 M207 用例与对端模型直驱（`test_m207_fw_adv.py`）保持全绿 | `run_m207_tests.sh` ALL GREEN | 已修并验证 |

变异（对 `m207_core.c` 的电学量轮询逐个改坏一处）6 处全部被抓：不作废在途等待 / 不看下一次温度轮询 / 被拒不累计 /
回码落回温度归属 / 不吃负零 / 超时不计失败。

### 2.3 协议（`protocol/tcs_proto.h`）

| 项 | 做了什么 | 验证 | 判定 |
|---|---|---|---|
| 0x20 追加 | 末尾 9 字节：`tec_dir`（低半字节第一路、高半字节第二路）、`tec1_v`（i16，0.01 V）、`tec1_i`（u16，0.01 A）、`tec2_v`、`tec2_i`；`TCS_SNAP_PL_LEN` 40 → 49 | ESP32 `test_tcs_proto.c` 65 条、屏端 `test_proto_m1.c` 38 条、`proto_ref.py` selftest——三处同一组字节 | 已修并验证 |
| 0x21 新 mask 位 | `TCS_D_TEC_DIR` = 0x0100（1 字节）、`TCS_D_ELEC` = 0x0200（8 字节），字段排在低 8 位之后 | 同上（含字段顺序、截断帧被拒） | 已修并验证 |
| 未知值 | 电压 0x8000 / 电流 0xFFFF；越界打包成未知 | `tcs_elec_pack_v / _i` 用例 | 已修并验证 |
| 版本 | `TCS_PROTO_VER` 2；屏端 POST 的 VER 自检项同改；`proto_layout.h` 契约同步（结构体 56 / 28 字节、偏移、线长） | 构建（两端 `proto.c` 都 include 契约头）；`test_post` 81 条 | 已修并验证 |
| 门禁 | 方向表注释更新；`proto_ref.py` 字节对照 | `python tools/proto_sync.py --check` → OK | 已修并验证 |
| 屏端落镜像 | `Core/Src/main.c` 在 0x20 与 0x21 两个入口调 `tcs_link_tec_set_dirs / set_elec`（纯逻辑在 `link_snap.h`） | `test_proto_m1.c` 的镜像换算用例；`test_m1_gates.py` 逐字核对两个入口 | 已修并验证 |
| 主控取数 | `tcs_snap.c` 取 `control_tec_dir()` 与 `m207_elec_get()`；顺手修掉 0x21 载荷缓冲只有 24 字节（全字段含新增共 31）放不下的隐患 | `test_m1_gates.py` | 已修并验证 |
| 网页状态帧 | 不动 | `test_web_status.c` 230 条 | 已修并验证 |

### 2.4 屏端界面（`project_tcs/App/lvgl_ui/`）

| 项 | 做了什么 | 验证 | 判定 |
|---|---|---|---|
| 页枚举 | `TUI_MODE` 排在 `TUI_DIAG` 之前；走查命令 17 = 进模式页 | 构建（`tcs_lvgl.c` 里按 `% 4` 循环走查的代码不受影响） | 已修并验证 |
| 首页模式键 | 见 §1 第 1 条 | F1 | 已实施待真机 |
| 模式页手动版面 | 三张温度卡（x 32 / 360 / 688，y 128，304×128，可点 → 曲线页，无趋势箭头）；TEC 两行（y 272 / 376，高 88；信息卡 x 32–488；三个键 x 504 / 676 / 848、宽 144）；底栏风扇 32–264、切换模式 280–744、急停与首页同位同尺寸 | H743 四档构建 | 已实施待真机 |
| 模式页自动版面 | 标题"自动模式"；右上"切换到手动 ›"；TEC 两行整行只读（x 32–992）；底栏"切换到手动模式" | H743 四档构建；F5 | 已实施待真机 |
| 高亮以回读为准 | `ui_mode_btn_style()`：只有主控回读里当前生效的方向高亮（制冷蓝 / 制热暖色 / 暂停灰）；通道开着但不是模式页设的 → 三个都不高亮 | F7 | 已修并验证（逻辑）／已实施待真机（观感） |
| 点了之后 | `ui_mode_cmd_*`：回读已是该方向 → 成了；链路断或 800 ms 没新快照 → 没响应；报警位 → 安全态；自动模式 → 只能手动调；其余 → 没接受 | F10；变异 U-B / U-D / U-G / U-H | 已修并验证 |
| 点之前的拦截 | 断链 / 安全态 / 自动模式下点方向键只提示不发帧；联动期间点风扇只提示 | F3–F6；变异 U-A / U-C / U-E | 已修并验证 |
| 功率格式 | W 一位小数；V、A 各两位；没数或太旧 →"未知"转灰；不出现 `-0.00` | F9；变异 U-F | 已修并验证 |
| 曲线页体温线 | 第三条线 + 图例；探头没接 / 链路不在的格子 NaN | `test_hist_body.c` 70 条 | 已修并验证（逻辑）／已实施待真机（观感） |
| 字库 | `tools/regen_ext_fonts.py` 重生成 ext24（+4 字：先 启 工 节）；新文案一律 24px（32px 字库只有既有的字；功率主数字 32px，"未知"两个字换 24px 画） | 字体门禁：重生成前 FAIL（缺 4 字），生成后 PASS | 已修并验证 |
| 急停矩阵 | `tools/site/estop_matrix.py` 页面循环加页序 4"模式"（首页进入坐标 376,520）；模式页上 (940,520) 就是急停键——点按只弹确认框、按住 2 秒界面在处理不触发，松开后若有弹窗关掉 | `py_compile` 通过、`--help` 自检通过 | 已实施待真机 |

`test_ui_model.c`：112 → 182 条，全过；既有 `tests/mutation_ui_model.py` 仍 ALL AS EXPECTED（新代码避开了它的锚点原文）。
对本批 ui_model 逻辑的 8 处变异全部被抓。

### 2.5 文档

| 文档 | 做了什么 |
|---|---|
| `docs/现行UI规范_v1.2.md` | 末尾增补一章（F 首页模式键、G 模式页版面 / 按钮语义 / 状态文案 / 数据口径、H 曲线页体温线、I 裁决记录 12–17）；既有条文一字未动 |
| `docs/现行规格说明书_v1.2.md` | 参数表加 0x09 / 0x0A 与语义说明；§5 加 `TCS_PROTO_VER` 2 的新字段与"屏链路断开等于发急停（用户 2026-11-26），取代断链只报告"的裁决记录；变更史加第 10 条 |
| `docs/现行M207协议_v1.md` | §8.2 给 `TCx:TCACTVOL` / `TCx:TCACTCUR` 补详注（来源、真机实录、负零、轮询口径）；修订加一条 |
| 本文档 | 上机卡见 §9 |

## 3. 默认值逐项

| 项 | 默认 | 落实 | 验证 |
|---|---|---|---|
| 手动的目标温度 | 固定值，沿用 `TEC_COOL_TARGET_C` / `TEC_HEAT_TARGET_C`，两路相同，界面不显示 | `control_manual.c` | M02 |
| 风扇联动 | 任一路制冷 / 制热 → 开；都暂停 → 恢复原选择 | `fan_link()` | M07；A04–A06 |
| 切换模式 | 切到自动二次确认；切到手动立即 | `tui_p4_mode.c` 的 `clk_switch` | 逻辑简单，未单独做主机用例（依赖弹窗与 LVGL） |
| 急停 / 超温 / 安全态 / 断链之后 | 两路回暂停；确认后回到手动、两路暂停，不自己恢复 | `enter_safe` / `control_manual_reset` | M08、M09、M10、L02 |
| 断链判急停 | 3 秒防抖；只对"连上过之后断开"生效 | `link_loss_guard.c` | L01、L03；B01 / B02 |
| 功率刷新 | 1 秒 | 四条查询每秒一轮（响应正常时每个量约 0.7 秒） | `test_m207_elec.c` 用例 2 |
| 网页端 | 不改 | `tcs_web/` 未改；新参数无 `PF_WEB` | `test_web_status.c` |

## 4. "没做之前红、做了之后绿"记录

| 对象 | 没做之前 | 做了之后 |
|---|---|---|
| 控制内核 `test_control_manual.c` | 对着接线前的内核跑：PASS=116 FAIL=40 | PASS=156 FAIL=0 |
| 控制内核变异 `mutation_control_manual.py` | — | 34 处：33 处被抓，1 处按预期存活，ALL AS EXPECTED |
| 对端帧时刻 `test_link_peer.c` | 接口不存在，链接失败 | PASS=22 FAIL=0 |
| M207 电学量 `test_m207_elec.c` | 接口不存在，链接失败 | PASS=61 FAIL=0；6 处变异全部被抓 |
| ESP32 协议 `test_tcs_proto.c` | `snap len 40`、新增断言红 | PASS=65 FAIL=0 |
| 屏端协议 `test_proto_m1.c` | 对着旧头文件编译失败（缺 `TCS_D_TEC_DIR`、`TCS_TEC_DIR_*`、`TCS_ELEC_*_UNKNOWN`、`tec_dir` / `tec1_v` 成员） | PASS=38 FAIL=0 |
| 屏端参数表 `test_param_registry.c` | 对着旧 `param_registry.c`：PASS=74 FAIL=5 | PASS=79 FAIL=0 |
| `ui_model` `test_ui_model.c` | 12 个 `ui_mode_*` 未解析，链接失败 | PASS=182 FAIL=0；8 处变异全部被抓 |
| 历史环体温 `test_hist_body.c` | 对着旧 `control.c`：`tcs_hist_snapshot_body` 未解析 | PASS=70 FAIL=0 |
| 字体门禁 | FAIL（缺 先 启 工 节） | PASS |
| `proto_ref.py` selftest | 旧实现没有新字段 | OK |
| `proto_sync.py --check` | — | OK |
| `estop_matrix.py` | 页面循环只有四页 | 五页；`py_compile` 与 `--help` 通过 |
| 研学卡 `study_cards_check.py` | 基座 `fddfe57` 上红 43 处（集成分支后来在 `3936d1d` 修了）；本分支 `main.c` 多 19 行又漂 34 处 | PASS（3 张卡 / 162 行） |

## 5. 验证总结果

### 5.1 构建矩阵

命令（先 `source activate_idf.sh`；`CMAKE_BUILD_PARALLEL_LEVEL=2`、`IDF_BUILD_JOBS=2`；在 `ec46549` 的干净工作树上，运行期间未动任何文件）：

```
bash tools/build_matrix.sh --esp32 --diag --site --twice      # 11:07:05 → 11:17:13
```

原样输出（已去掉进度行）：

```
MATRIX_START 11:07:05
[PROD] exit=0 error=0 warning=4 FLASH: 458188 2; DTCMRAM: 95336 128; AXIRAM: 205952 320
[BENCH] exit=0 error=0 warning=4 FLASH: 476964 2; DTCMRAM: 99032 128; AXIRAM: 205952 320
[SIM] exit=0 error=0 warning=4 FLASH: 476964 2; DTCMRAM: 99032 128; AXIRAM: 205952 320
[SIMWG] exit=0 error=0 warning=4 FLASH: 516116 2; DTCMRAM: 99080 128; AXIRAM: 205952 320
[prod-clean] PASS（交付（PROD）档产物不得含调试/台架钩子）
[prod-clean] PASS（台架档产物必须含钩子）
[prod-clean] PASS（台架档产物必须含钩子）
[prod-clean] PASS（台架档产物必须含钩子）
PROD ELF vs PROD 镜像      采样点 3，不相等 0
PROD ELF vs BENCH 镜像     采样点 3，不相等 3
BENCH ELF vs PROD 镜像     采样点 3，不相等 3
BENCH ELF vs BENCH 镜像    采样点 3，不相等 0
自测：PASS（同源 0 处不符；异源至少 1 处不符）
nm_addr 缺钩子时的提示正确（提到烧台架档）
[esp32-default] exit=0 error=0 warning=0
[esp32-diag] exit=0 error=0 warning=0
[esp32-site] exit=0 error=0 warning=0
[esp32-repro] tcs_wireless.bin  sha256 fd8227c6e408aad1  相同
[esp32-repro] bootloader/bootloader.bin  sha256 ad532789dd342643  相同
[esp32-repro] partition_table/partition-table.bin  sha256 456e1ceb0b963c9d  相同
[esp32-repro] 默认档两次从零构建 sha256 相同
[prod-clean] PASS（交付（PROD）档产物不得含调试/台架钩子；ESP32 命令集=无台架命令）
[prod-clean] PASS（交付（PROD）档产物不得含调试/台架钩子；ESP32 命令集=全部台架命令）
[prod-clean] PASS（交付（PROD）档产物不得含调试/台架钩子；ESP32 命令集含 linkloop,linkmute,webtest）
[esp32] 干净核对 PASS（默认档无台架命令；诊断档 / 现场档命令集相符）
================ build_matrix 汇总 ================
可复现构建：剔除 .comment 与调试信息后 md5 1fab94d62460d8b5accd069e08c7d739 / 1fab94d62460d8b5accd069e08c7d739 → 相同；原始 bin md5 相同
交付档干净核对：PASS
ESP32：见上（日志在 esp32/build-matrix-*.log）
build_matrix: PASS
MATRIX_RC=0
MATRIX_DONE 11:17:13
```

**本批次一共跑了两次矩阵**，如实记录：
- 第一次（03:44，在 `18e434a` 上启动）：H743 四档与交付档干净核对已通过，但两项没过——
  ① `[repro] FAIL`：我在矩阵运行途中提交了文档（03:47、03:49 两次提交），两次 PROD 从零构建的 HEAD 不同、版本串不同，
  **是我的操作问题，不是代码问题**；② `[esp32-default] exit=2`：`esp_lcd_panel_rgb.c` 报
  `internal compiler error: Segmentation fault`（IDF 自带组件，不在本批次改过的文件里）。原样输出：

```
MATRIX_START 03:44:27
[PROD] exit=0 error=0 warning=4 FLASH: 458188 2; DTCMRAM: 95336 128; AXIRAM: 205952 320
[BENCH] exit=0 error=0 warning=4 FLASH: 476964 2; DTCMRAM: 99032 128; AXIRAM: 205952 320
[SIM] exit=0 error=0 warning=4 FLASH: 476972 2; DTCMRAM: 99032 128; AXIRAM: 205952 320
[SIMWG] exit=0 error=0 warning=4 FLASH: 516116 2; DTCMRAM: 99080 128; AXIRAM: 205952 320
[repro] FAIL：同一源码两次从零构建产物不同
[esp32-default] exit=2 error=2 warning=0
build_matrix: FAIL
MATRIX_RC=1
MATRIX_DONE 03:51:21
```

- 第二次即上面这一次（最后一次），全部通过。

### 5.2 主机单测全量

命令：`cd project_tcs && bash tests/run_all.sh`。原样汇总行：

```
test_m207_parse: PASS=35 FAIL=0
test_param_registry: PASS=79 FAIL=0
test_control: PASS=73 FAIL=0
test_post: PASS=81 FAIL=0
test_site_logic: PASS=119 FAIL=0
test_proto_tcs: PASS=46 FAIL=0
test_ui_model: PASS=182 FAIL=0
test_link_info: PASS=30 FAIL=0
test_ws_client: PASS=13 FAIL=0
FONT_AUDIT_LVGL: PASS — 字面量全覆盖，三档无 fallback 字体白名单齐全，字形度量为比例步进
[audit] PASS (报告与代码一致: 268 files, 0 uncovered, 0 unclassified)
[card_paths] 70 checked, 0 bad
[card_surface] PASS (7 cards)
[card_lines] PASS（3 张卡 / 162 行，文件、行号、接口名逐行对得上）
src_bytes: PASS（无 NUL / 无孤立 CR）
test_m207_proto: PASS=66 FAIL=0
test_tmp117_pure: PASS=15 FAIL=0
test_fan_pure: PASS=142 FAIL=0
test_tmp117_core: PASS=65 FAIL=0
test_tmp117_core: PASS=70 FAIL=0
test_tcs_proto: PASS=65 FAIL=0
test_control: PASS=187 FAIL=0
test_control_manual: PASS=156 FAIL=0
test_m207_core: PASS=87 FAIL=0
test_m207_queue: PASS=53 FAIL=0
test_m207_link: PASS=19 FAIL=0
test_m207_lb: PASS=54 FAIL=0
test_m207_status: PASS=40 FAIL=0
test_m207_elec: PASS=61 FAIL=0
test_link_dir: PASS=77 FAIL=0
test_link_peer: PASS=22 FAIL=0
test_web_status: PASS=230 FAIL=0
[m1gates] PASS (屏链路断开 3 秒 = 急停的接线：控制节拍 → 纯判定 → 急停路径；对端帧时刻在方向闸门之后；协议 ver2：主控快照带输出方向与电学量、屏端 0x20 / 0x21 两个入口落镜像)
esp32-tests: ALL GREEN
[prodlink] PASS（test_prod_link_only: PASS=32 FAIL=0）
test_drv_pure: PASS=69 FAIL=0
test_link_dir(H743): PASS=154 FAIL=0
test_link_send: PASS=30 FAIL=0
proto_sync: OK (单一源 + 2 包装一致，无重复定义；方向表与两端收发分支一致；两端闸门相同；屏端 ISR 不碰 comm 层)
test_link_follow_prod: PASS=30 FAIL=0
test_m207_txq: PASS=30 FAIL=0
[estop_yield] PASS (主循环最先看屏急停入口；4 个阻塞循环体内让位；7 条登记的例外)
test_audit_ring: PASS=46 FAIL=0
test_perf_hist: PASS=29 FAIL=0
test_proto_m1: PASS=38 FAIL=0
[histbody] PASS（test_hist_body: PASS=70 FAIL=0）
run_all: ALL GREEN
```

没有进 `run_all.sh`、单独跑的变异脚本：
`esp32/tests/mutation_control.py` → ALL AS EXPECTED（既有 31 处）；
`esp32/tests/mutation_control_manual.py` → ALL AS EXPECTED（本批 34 处：33 处被抓，1 处按预期存活）；
`project_tcs/tests/mutation_ui_model.py` → ALL AS EXPECTED。

## 6. 与并行批次 e3a 的重叠（给合并人）

试合并（`git merge-tree --write-tree HEAD fix/s-u-int2`，对 `de997cb`，不动工作树）：冲突 7 个文件。

| 文件 | 冲突 | 怎么合 |
|---|---|---|
| `esp32/components/tcs_core/CMakeLists.txt` | `SRCS` 一行两边各追加（我加 `"control_manual.c" "link_loss_guard.c"`，上游加 `"boot_record.c" "svc_status.c"`） | 两段都留 |
| `esp32/components/tcs_core/control.c` | ① include 行两边各加一行；② `control_init` 尾部两边各追加一块 | 两边都留 |
| `esp32/components/tcs_core/param_registry.c` | 上游把 `param_set` 的函数体改名成 `param_set_impl(id, v, src, out)` 并加了锁；我的 `param_kernel_pause()` 与前置检查块加在旧函数头前面 | 把 `param_kernel_pause()` 留在 `param_set_impl` 之前，前置检查块留在函数体开头；`on_check` 那一行的 `!param_kernel_pause(p, c, src)` 条件照搬 |
| `docs/现行规格说明书_v1.2.md` | "0x08 三态语义"那一条上游改写过，我的 0x09 / 0x0A 说明紧跟其后 | 两条都留 |
| `docs/ref/研学卡_*.md` ×3 | 两边 `main.c` 行数不同 | 合并后跑 `python tools/reanchor_study_cards.py` |
| 审计清单与机器报告 | — | 丢弃我的审计提交，合并后统一重新生成 |

自动合并但要人工看一眼语义的三处（**重要**）：

1. **e3a 的"读数卡死"判据在手动模式下同样生效。** 上游 `control_poll` 里
   `tc1_stuck = s_tec_cmd_on[0] && stuck_is(&s_stuck_tc1, now, s_tec_on_since[0])`：TEC1 的命令意图为开、
   `tc1_act` 逐位不变满 30 秒、且 TEC1 从那之后一直开着 → 锁 `SAFETY_BIT_STUCK` 进安全态。模式页的手动输出是
   **固定目标**（制冷 15.0 ℃、制热 45.0 ℃），水温稳到目标后读数很可能 30 秒不变——合并后手动"制冷 / 制热"可能在
   水温稳定后被它停掉（我的 `enter_safe` 钩子随之把两路参数清回暂停，要用户重新点）。自动模式自己的 COOL / HEAT
   同样以 15 / 45 为目标，理论上也有这个问题，e3a 应已在真机上看过。**这是合并后第一个要上机确认的点**；若成立，
   需要决定手动模式是否豁免 `tc1_stuck`（见 D7）。
2. **输出方向的"开 / 关"取值来源。** 我的 `control_tec_dir()` 读 `g_tcs.tecX_on`；e3a 之后 `g_tcs` 多了 `tec_known`
   （模块离线时为 0），并把"命令意图"（`s_tec_cmd_on`）与"模块回显"分开。合并后建议 `control_tec_dir()` 在
   `g_tcs.tec_known` 的对应位为 0 时直接报暂停（模块离线时方向不可信）。
3. **`control_manual` 的下发入口。** 我经 `out_tec_sw()` 下发（安全门控的唯一收敛点）；e3a 给 `out_tec_sw` 加了
   `out_tec_sw_ex(ch, on, force)` 外壳，意图计时靠它。我的调用点不用改，但合并后请确认 `s_tec_on_since[]` 对模式页
   设的两路都计时（原逻辑只见 `s_tec_on_since[0]`，即只看 TEC1）。

`esp32/main/main.c`、`project_tcs/Core/Src/main.c` 我没有和 e3a 重叠的改动需要说明：前者我一行没改（钩子沿用既有装配），
后者是屏端的（`snap_apply` 落镜像、`tcs_link_write_param`），上游若也动过，按两边都留处理。

## 7. 风险

最危险的三个：

- **R1 断链 = 急停是新增的自动安全动作。** 屏端任何超过 3 秒的静默都会让主控进安全态并锁存（锁存落 NVS，断电也在）：
  屏幕重启、调试时 halt、以后加的耗时操作把主循环挡住 3 秒以上。误触发的方向是"停机 + 要人工确认"，不是危险方向，
  但会打断使用。上电后屏从没连上过不触发。上机请先量一下屏幕正常重启要多久（§9 第 11 步）。
- **R2 与 e3a 合并后的交互**（§6 的三点）：卡死判据可能停掉手动输出；方向取值在模块离线时的口径；TEC 意图计时只看第一路。
- **R3 手动输出是开环的固定目标。** 制冷 15.0 ℃、制热 45.0 ℃ 不可调，只靠 48 ℃ 超温硬切断与模块自身保护兜底；
  没有低温保护；体温探头没接（未布防）也能手动；TEC2 同理。首次上机建议先不接 TEC 负载，只看功率读数。

其它：
- 页面观感（字号、对齐、右上"切换到 ›"按钮与状态胶囊之间只有十几像素间隙）没有经真机确认。
- 模式页上"点了没成"的原因是按快照里的报警位与模式推断的——主控的拒绝码（-3 / -4 / -5）不透传到界面层（comm 层消费了 ACK 状态）。
- 诊断 / 仿真 / 现场档的 `linkmute` 静默 ≥3 秒现在会触发急停；走查脚本若依赖"静默后自动恢复、无急停"需要调整。
- 功率"未知"时主数字从 32px 换成 24px 画（"未知"两个字只有 24px 字库里有）。

## 8. 需要用户决定的点

| # | 问题 | 现状 |
|---|---|---|
| D1 | 网页的旧"TEC 开 / 关"命令与新方向参数并存：网页开 TEC 后，模式页该行显示"输出中"、三个键都不高亮 | 网页未改（本批要求）；要不要下一批把网页也改成方向控制？ |
| D2 | TEC 工作期间，网页或台架把风扇关掉会被联动在 100 ms 内开回 | 按默认值"任一路制冷 / 制热 → 风扇开" |
| D3 | 自动模式下网页仍能用旧命令开关 TEC2（旧命令没按模式拦） | 未改 |
| D4 | 断链 3 秒 = 急停对"屏幕重启 / 调试 halt / 主循环阻塞"一视同仁 | 3 秒是用户给的防抖值；要不要更长，或给屏幕重启留例外（固件侧无法区分重启与卡死）？ |
| D5 | 模式页右下角就是急停键，但按住 2 秒（界面在处理）不直触发，与其它二级页一致；首页是按住 0.6 秒直触发 | 按说明文件做；要让模式页也像首页，需改 `tcs_lvgl.c` 的急停入口判定（本批不许改，需单独评审） |
| D6 | 通信类故障 2 秒自愈后，模式页设的输出一律不复原；网页旧命令开的 TEC2 仍按老规则复原 | 按"安全优先"做 |
| D7 | 合并 e3a 之后：手动模式要不要豁免"读数卡死"判据（§6 第 1 点） | 待合并后上机确认再定 |
| D8 | 功率显示电压照实带符号（制热为负，如 `-8.23 V`） | 说明只说"各两位"；要不要显示绝对值？ |

## 9. 真机待做清单（照着做）

准备：
1. 两端固件都烧本分支——**必须成对**（协议 v2；只换一端屏不出数）。
   H743 台架档：`cd project_tcs && bash tools/build_bench.sh`（或直接用矩阵产物 `project_tcs/build-matrix/BENCH/tcs_main.{elf,hex}`）。
   ESP32 诊断档：矩阵产物 `esp32/build-matrix-diag/`（`idf.py -B build-matrix-diag -p COMx flash`）。
2. 前提：当前无报警、手动模式。第一次先不接 TEC 负载。
3. ESP32 控制台（USB 串口）全程开着；以下坐标都是屏幕像素。

走查：

4. 上电。首页模式键（272..480 × 472..568）显示"手动"。点它 (376,520) → 进模式页：标题"手动模式"，三张温度卡，
   TEC1 / TEC2 两行，"暂停"键灰底高亮，功率有数。TEC 关着时预期 `0.0 W` 附近；若显示"未知"，在 ESP32 敲
   `m207 raw TC1:TCACTVOL?` 看模块认不认这条。
5. 对照：`p list`（0x09 / 0x0A 都是 0）、`m207 raw TC1:TCACTVOL?`、`m207 raw TC1:TCACTCUR?`、`status`。
6. 点 TEC1 行"制冷"(576,316)：半秒内"制冷"变蓝底、"制冷中"、风扇键变"风扇 开"、功率与电压电流有数。
   ESP32 日志：`H743 SET id=0x09 v=1.00 rv=0 -> ack st=0 val=1.00`；`p list` 里 0x09 = 1。
7. 点 TEC2 行"制热"(920,420)：同理（0x0A = 2）。
8. 制冷中点风扇 (148,520)：只出提示"TEC 工作时风扇保持开启"，风扇不变。
9. 点"暂停"(748,316)、(748,420)：回"暂停"高亮；`p list` 两路都是 0。
10. 点三张温度卡 (184,192) / (512,192) / (840,192)：都进曲线页，图例有"体温"；没接体温探头时体温线是断的、图例显示 `--.-`。
    点"‹ 返回"(84,28) 回首页（返回键统一回首页）。
11. 切自动：模式页点"切换到自动模式"(512,520) → 确认框 → 点"切换"(392,388) → 自动页（没有方向键，两行整行只读）。
    点右上"切换到手动 ›"(564,28) → 立即回手动页，两路都是暂停。
12. **断链急停（不拔线）**：先点 TEC1"制冷"。ESP32 敲 `linkmute 5` → 屏上立刻出"主控连接中断"横幅、模式页转灰；
    约 3 秒后 ESP32 日志出 `[CTRL] SCREEN LINK LOST: no frame from the display for 30xxms (connected before) -> ESTOP now=…`；
    5 秒后链路恢复 → 屏上红色横幅"急停已触发"，模式页两路都是"暂停"（不是制冷）。点横幅 (428,92) 确认 → 解除；
    两路仍是暂停，风扇回到原来的选择。再点"制冷"才会重新输出。拔屏线 ≥3 秒再插回是同一条路径。
13. **从未连上不触发**：屏不上电（或屏线不接），只给 ESP32 上电，等 30 秒：`status` 不在安全态、没有急停；
    接上屏后一切正常。
14. **屏幕重启要多久**：模式页上让 TEC1 制冷，然后给屏幕单独断电再上电，计时。超过 3 秒就会看到第 12 步的急停——
    这决定 D4 怎么定。
15. 急停矩阵：`TCS_ELF=build-bench/tcs_main.elf python project_tcs/tools/site/estop_matrix.py` → 五页，
    "模式"页的"点按""按住2s"两行应为 PASS。
16. 网页：手机连热点，控制台"屏间协议"一栏显示 v2；网页点"贴身水箱 TEC 开" → 模式页该行显示"输出中"，
    三个键都不高亮；网页日志里能看到"屏幕：TEC1 手动输出 改为 1"这类记录。

## 10. 遗留

- 真机全部没做（不碰设备）：见 §9。
- "切换到自动二次确认 / 切到手动立即"这段只有构建证据，没有主机用例（依赖弹窗与 LVGL）。
- 模式页 `set_banner_room` 是空实现：版面已经给横幅留了 64–120，不需要挪。
- 说明里的"必要时 `mutation_control.py` 加变异体"没有改那个文件（e3a 也在改），改用独立脚本 `mutation_control_manual.py`。
- M207 电学量轮询的 6 处变异与 `ui_model` 的 8 处变异是用临时脚本跑的（不在仓库里）；需要的话可以照
  `mutation_control_manual.py` 的样子固化。
