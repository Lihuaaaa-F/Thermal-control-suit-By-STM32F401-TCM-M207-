# S-G r8f 结果（独立复核 R8 的 4 条 P2：卡死通道 × ack、体温拖水温、体温预冻、超温断链）

分支 `fix/s-r8f`，基座 = 集成分支 `fix/s-u-int2` @ `f3b46d2`（全量门禁头 + i3 + r7f + x3）。
本批次只动 ESP32 控制核与它的主机用例 / 变异表：`esp32/components/tcs_core/control.c`、
`link_loss_guard.[ch]`、`control_service.c`（只改注释）、`esp32/tests/` 下的两个用例与两个变异脚本、
`test_m1_gates.py`（逐字门禁同步）。**没有碰设备**：没有 probe-rs、没有串口、没有烧录、没有任何驱动
TEC / 加热的操作。H743 端一行未动。

R8 的发现清单（只读）：`C:\Users\ShiYuFang\AppData\Local\Temp\tcs_site\R8_findings.md`；
复核人的复现用例在 `D:\Espressif\ws\tcs-r8\esp32\tests\r8\`（只读参考，本批次把它改写成断言正确行为的用例）。

## 0. R8 编号 × 提交

| R8 编号 | 严重度 | 提交 | 一句话 |
|---|---|---|---|
| R8-01 | P2 | `3536dd9` | ack 清“借来的”`ALM_SENSOR_OPEN` 的判据按 bit1 / bit2 两路看（原先只认 TC1） |
| R8-02 | P2 | `da96cb4` | 撤掉 `tc1_pre`：体温先判出来时不再按 30 秒短窗口把稳态水温记进涉事通道 |
| R8-03 | P2 | `070f7cb` | 撤掉体温的预冻判据，恢复“开 TEC 后 30 秒”宽限；水温预冻（10 分钟长窗口）保留 |
| R8-04 | P2 | `d47a1f3` | 超温安全态里断链补上急停位；锁存被确认清空后、链路仍断 → 下一拍重新锁急停 |

判定全部是 **已实施待真机**：代码与主机侧验证完成；R8 的关单条件里“真机”部分按批次约定没做
（见 §5）。

---

## 1. R8-01 — TC2-only 卡死：ack 把借来的 `ALM_SENSOR_OPEN` 清掉

**判定：已实施待真机**（主机侧已修并验证）

### 问题

通道归属已经扩到 bit2（判据 `control.c:824`、自愈 `:182`、ack 的 ok 判据 `:560` 三处都认 `4u`），
只有“报警位清除”那一处还只认 bit1（`!(stuck_chn & 2u)`）。手动模式 TC2 探头冻住 → 开 TC2 当拍
判卡死（SAFE + `ALM_SENSOR_OPEN`）→ 按一次“解除”：STUCK 锁存正确留着（设备仍在 SAFE），
但 `ALM_SENSOR_OPEN` 被清掉（R8 主机实测 `alarm 0x0020 → 0x0000`）。屏/网页一条横幅都没有，
屏还回“已解除”；STUCK 不落盘，操作员的下一步通常是断电重启，重启后这个仍冻着的 TC2 探头
不再有任何保护。

### 改法

`control.c` 的报警位清除判据改成 `!(stuck_chn & (2u | 4u))`（体温借用的是另一行，未动）。
通道归属四处（判据 / 自愈 / ack ok / 报警位清除）现在口径一致。

### 红 / 绿证据

新增 K25（`esp32/tests/test_control_esp32.c`）：TC1 活着、TC2 冻满 10 分钟 → 开 TC2 当拍判卡死 →
一次 ack 后 `SAFETY_BIT_STUCK` 与 `ALM_SENSOR_OPEN` **都**还在、仍在 SAFE；TC2 读数恢复变化后
2 秒自愈退出。

```
# 修前（同一份用例、同一命令）
FAIL: K25 [R8-01] ALM_SENSOR_OPEN survives the ack (the only visible reason for this SAFE) (line 1358)
test_control: PASS=337 FAIL=1
# 修后
test_control: PASS=338 FAIL=0
```

回归变异 `mutation_control.py` 的 **M77**（把这处改回只认 bit1）：
`CAUGHT by 1 checks, e.g. K25 [R8-01] ALM_SENSOR_OPEN survives the ack`。

### 遗留

主机侧无遗留（判据纯逻辑，不依赖真机，输出门控没动）。真机只多一步目视：屏 / 网页的
报警横幅在 ack 之后仍在（亮的是同一位 `ALM_SENSOR_OPEN`，见 §5）。

---

## 2. R8-02 — 体温先判出来时按 30 秒短窗口拖上水温

**判定：已实施待真机**（真机部分是 F2 那组分段数据的复测，见 §5）

### 问题

r7f R7-04 为了补“体温遮住水温”的盲窗，加了 `tc1_pre = s_tec_cmd_on[0] && stuck_is(water…)`
（30 秒窗口），在体温先判出 STUCK 的那一拍把“同拍也没动”的水温一并记进 `s_stuck_chn`
并报 `ALM_SENSOR_OPEN`。但健康水温读数的稳态本来就会 30–53 秒逐位不变（真机 F2 实测），于是：
体温探头一次卡死之后，体温恢复、水温“正常地不动”，2 秒自愈要求涉事通道都活 → 设备退不出 SAFE
（R8 实测 60 秒），ack 同样无效，屏上报警还写“水温探头断线”（错通道）。注入档里这条支路也没有被
`stuck_src_injected(2)` 豁免：台架上体温一卡死就只能重启。

### 改法

撤掉 `tc1_pre` 支路三处（变量声明、借位报警、涉事通道 OR）。这条路在语义上已经冗余：
`tc1_pre` 的判据（TEC 开 + `stuck_is`）⊆ `tc1_stuck` 的前半，再叠加水温长窗口后，只要水温真的
冻满 10 分钟就由 `tc1_stuck` 自己记进 bit2 / `ALM_SENSOR_OPEN`，注入豁免也照旧走 `tc1_stuck`。
水温自己的判据（10 分钟）不变（K9c 继续覆盖）；R7-04 想补的盲窗在“水温真的冻着”的情形下仍然成立
（新增 K24b 钉住）。

### 红 / 绿证据

改写 K24 + 新增 K24b（`test_control_esp32.c`）：

- K24：体温冻住、水温只是稳态不动 → 报警只写体温通道（`ALM_SENSOR_OPEN` 不得出现）；
  体温恢复后 2 秒自愈退出 SAFE（水温仍稳态）。
- K24b（对照）：水温在 TEC 开之前已冻 580 秒（未过长窗口，开沿不预判），开 TEC 后 30 秒与体温
  同一拍判出 → 两路都记；体温单独恢复不放行（6 秒内 TEC 命令恒 0），水温也动了才 2 秒自愈。

```
# 修前
FAIL: K24 [R8-02] a merely steady water reading is not blamed (30 s is not stuck for water) (line 1193)
FAIL: K24 [R8-02] the body recovering releases SAFE although the water reading stayed steady (line 1202)
test_control: PASS=342 FAIL=2
# 修后
test_control: PASS=344 FAIL=0
```

回归变异 `mutation_control.py` 的 **M78**（把 30 秒短窗口拖拽加回去）：
`CAUGHT by 2 checks, e.g. K24 [R8-02] a merely steady water reading is not blamed`。
r7f 的 M67 / M68 锚点随 `tc1_pre` 一起删除，覆盖改由 M78 承担（写在变异表注释里）。

### 遗留

水温“时冻时动”（每不到 10 分钟动一次）判不出卡死——10 分钟窗口的既有代价（K21），未变。

---

## 3. R8-03 — 预冻判据把误报风险提前到“开 TEC 当拍”

**判定：已实施待真机**

### 问题

r7f 的 `stuck_prefrozen` 让“TEC 开沿那一刻读数已冻满窗口”直接判卡死，体温那一路的窗口只有
30 秒。健康读数在闭环稳态本来就可能 30 秒逐位不变（F2 实测 30–53 秒），自动模式进 HEAT 的当拍
就会判 STUCK + `ALM_SENSOR_OPEN`（报警写“水温探头断线”）并进 SAFE。旧判据（`stuck_is`）要求
`now - on_since ≥ 30 秒`：开 TEC 之后读数只要在 30 秒内变过一次就不判——那 30 秒正是唯一给被控
对象留响应时间的机会，预冻判据把它拿走了。

### 改法

删掉体温那一路的预冻判据（`body_stuck` 的第二段），体温恢复“从 TEC 开沿起算满 30 秒”的原设计
（K9 的语义）。水温两路的预冻判据保留，且本来就要求冻满长窗口（10 分钟），远大于健康读数的
连续不变段，不会误报（K9c 继续覆盖）。

### 红 / 绿证据

改写 K9b：体温在 TEC 开之前已冻 60 秒 → 开沿后 29.9 秒仍在 HEAT（宽限在），30.0 秒判 STUCK。

```
# 修前
FAIL: K9b [R8-03] a long-frozen body reading still gets the 30 s grace from TEC turn-on (line 1168)
test_control: PASS=344 FAIL=1
# 修后
test_control: PASS=345 FAIL=0
```

回归变异 `mutation_control.py` 的 **M79**（把体温预冻加回去）：
`CAUGHT by 1 check, e.g. K9b [R8-03] a long-frozen body reading still gets the 30 s grace`。

### 遗留 / 代价（如实写）

R7-04 想补的盲窗在体温这一路又回来了：体温读数在 TEC 开之前就冻住时，要等开 TEC 后 30 秒才判
卡死。这 30 秒里**没有**保护缺口：体温读数不参与 48°C 硬切断（只有两路水温参与），自动模式在
这 30 秒里仍按冻结的体温制热——这正是 K9 钉住的原设计，代价是“冻结的体温最多多驱动 30 秒输出”。
真机发生率取决于“TEC 关着时体温读数能连续 30 秒逐位不变”的概率（TMP117 分辨率 0.0078 °C，
正常稳态几乎不会）。

---

## 4. R8-04 — 超温安全态里断链只记“已处理”，网页 ack 后在屏仍断时恢复制热

**判定：已实施待真机**

### 问题

R7-01 把“已在安全态”收窄到急停 / 超温之后，断链在这一档只记 `tripped` 就沉默，而且要等屏回来
才重新武装。超温锁存是**可以被人在网页上解除**的（不需要屏）：自动制热中超温 → SAFE；屏断链
5 秒（只记 `tripped`）；水温降下来后在网页上按解除 → 状态机按体温重新开 TEC，屏仍断着、
`peer_age` 继续涨，但 `tripped` 还是 1，再也不会判急停（R8 主机实测 `peer_age=8200 ms`、
`tec1_cmd=1`、`estops=0`）。I2“屏断链 = 急停”在这条路上不成立。

### 改法

1. `link_loss_already_safe()` 收窄到**急停位**：超温安全态里断链照常走
   `tcs_emergency_stop_src` 把急停位补上（输出已停，幂等）；急停位锁上之后才“不重复触发”。
2. `link_loss_guard_step()` 去掉“`tripped` 就提前返回”：`tripped` 只作诊断。锁存里没有急停位
   （空锁存 / 只有超温 / 只有会自愈的位）时，这一拍就要触发。
3. `control_service.c` 的注释与 `test_m1_gates.py` 的逐字门禁同步到新口径。

r7f 的 L04 矩阵里“超温态断链不触发”那一行按新口径改了：断链 → **出现急停位**；降温 + 网页 ack →
锁存空的那一拍下一拍立刻重新锁急停，TEC 命令始终 0；屏回来 + ack 才回 IDLE（手动）。

### 红 / 绿证据

- 新增 **L01b**（`already_safe` 取值矩阵：空 / 超温 / 模块失联 / 体温失联 / 卡死 → 0；急停 →
  1；急停+超温 → 1）。
- 改写 **L01**（“锁存被清空、链路仍断 → 必须重新触发”）。
- 改写 **L04 超温行**、新增 **L05**（复现 R8-D 的时序：超温 SAFE → 断链 5 秒 → 降温 + 网页 ack）。

```
# 修前
FAIL: L01 [R8-04] the latch was cleared while the link is still lost -> trigger again (line 466)
FAIL: L01b [R8-04] overtemp alone is not already safe (it can be acknowledged) (line 478)
FAIL: L04 overtemp row: link lost 3 s -> the estop bit is added (R8-04) (line 585)
FAIL: L04 overtemp row: both bits latched (overtemp + estop) (line 588)
FAIL: L04 overtemp row: [R8-04] the estop is re-latched on the tick after the ack (line 595)
FAIL: L04 overtemp row: [R8-04] ack while the screen is still lost never brings the TEC back (line 598)
FAIL: L04 overtemp row: cooled down + link back -> still SAFE until acknowledged (line 602)
FAIL: L05 [R8-04] link loss in an overtemp SAFE adds the estop bit (line 690)
FAIL: L05 [R8-04] the estop is re-latched on the tick after the ack (line 696)
FAIL: L05 [R8-04] the web ack does not restart the TEC while the screen is still lost (line 698)
FAIL: L05 [R8-04] still SAFE with both TEC off while the screen is lost (line 701)
FAIL: L05 link back -> still SAFE until acknowledged (line 705)
FAIL: L05 acknowledged after the link is back -> IDLE in manual mode, nothing restarted (line 708)
test_control_manual: PASS=220 FAIL=13
# 修后
test_control_manual: PASS=233 FAIL=0
```

变异 `mutation_control_manual.py`：B07–B11 全部被抓（**B11** 就是把“`tripped` 提前返回”加回去 =
R8-04 的缺陷本体，`CAUGHT by 7 checks`）；B03 的锚点随缺陷本体一起删除；B05 / B06 只剩诊断字段
读写，按“等价”入库并写明理由。

### 遗留 / 需要人看的点（最危险的一处）

网页 ack 与“下一拍重新锁急停”之间有一个控制节拍（100 ms）的窗口：那一拍锁存是空的，状态机在
手动模式（急停把模式切成手动、两路手动参数已清零），`control_tec_cmd` 仍为 0；要在那一拍把 TEC
开起来，需要有人在同一个 100 ms 内再下一条“开”命令。要做到零窗口，得让 ack 在屏链路不新鲜时
不清急停位（给控制核加一个链路新鲜度输入，`control.h` 的 ops 或一个 setter），这会扩大接口面，
超出本批次“按证据、保守改”的范围，留给下一轮决策。

---

## 5. 本批次验证总结果

全部在 `D:\Espressif\ws\tcs-g-r8f` 上跑，提交见 §0（最后一次全量在 `d47a1f3` 之后）。

| 入口 | 结果（原样汇总行） |
|---|---|
| `esp32/` `bash tests/run_m207_tests.sh` | `esp32-tests: ALL GREEN` |
| `project_tcs/` `bash tests/run_all.sh` | `run_all: ALL GREEN`；`[tautology] PASS（0 处）`；`[assert_count] PASS（27 个测试可执行行：12863 条断言 + 46951 例语料样本，均不低于基线）` |
| `esp32/` `python tests/mutation_control.py` | `mutation_control: ALL AS EXPECTED`（M77 / M78 / M79 均被抓） |
| `esp32/` `python tests/mutation_control_manual.py` | `mutation_control_manual: ALL AS EXPECTED`（B07–B11 被抓，B05/B06 等价） |
| 仓库根 `python tools/proto_sync.py --check` | `proto_sync: OK (...)` |
| `esp32/` `python tests/test_m1_gates.py` | `[m1gates] PASS (...)` |
| `project_tcs/` `bash tools/build_matrix.sh --esp32 --diag --site --twice` | `build_matrix: PASS`；H743 四档 `exit=0 error=0 warning=4`（PROD/BENCH/SIM/SIMWG，等于基线）；ESP32 默认 / 诊断 / 现场三档 `exit=0 error=0 warning=0`；可复现：H743 剔除调试段后 md5 `a947f3c9…` 两次相同、原始 bin md5 相同，ESP32 默认档两次从零构建 app / bootloader / 分区表 sha256 相同；交付档干净核对 PASS |

逐条的红证据都是在“该条修之前、前一条已修”的树上门控出来的（每条的 K/L 断言相互独立），
命令与修后 green 完全相同（见各节）。原始日志在 `/tmp/r8f_*.log`（不入库）。

### 真机部分（未做，按批次约定）

- 本批次**不碰设备**：没有 probe-rs、没有串口、没有烧录、没有任何驱动 TEC / 加热的操作。
- R8-01 / R8-02 / R8-03 / R8-04 的判据全部是主机可测的纯逻辑（锁存 / 报警位 / 状态机 / 断链判定），
  真机只多一层“屏与网页的呈现”：R8-01 的关单条件“屏/网页上报警横幅仍在”需要在真机上目视一次
  （本批次只钉住 `tcs_alarm_active(ALM_SENSOR_OPEN)`，屏侧 `ui_banner_select` 读的就是这一位）。
- R8-02 的真机关单条件（“体温探头卡死后水温稳态不动，设备能自己退出 SAFE”）需要在真机上复测
  F2 那组读数形态；本批次用 K24 在主机上按 F2 的读数形态（水温 25.0 稳态）复现。

## 6. 风险与需要人看的点

1. **R8-04 的 100 ms 窗口**（§4 遗留）：ack 之后、下一拍重新锁急停之间锁存为空。目前 TEC 仍是 0
   （手动模式 + 手动参数已清零），但严格说 I2 在这 100 ms 内只靠“没有人在同拍下命令”兜底。
   要零窗口必须给控制核加链路新鲜度输入，建议下一轮决定。
2. **R8-02 撤掉 `tc1_pre` 的取舍**：体温先判出来时不再“抢记”水温；水温真冻着时仍由它自己的
   10 分钟窗口记进去（K24b）。代价是：如果水温在体温卡死后的 30 秒到 10 分钟之间才真正冻住，
   它要等到自己的窗口才被记——这本来就是水温判据的固有延迟，不是新增缺口。
3. **R8-03 恢复 30 秒宽限**：体温读数在 TEC 开之前冻住时，最多多驱动 30 秒输出（原设计，K9）。
   体温不参与 48°C 硬切断，所以这 30 秒没有“超温失明”的问题。
4. **变异表的三处调整**（都已写明理由）：M67 / M68 随 `tc1_pre` 删除（覆盖改由 M78）；
   B03 随“`tripped` 提前返回”删除（反向由 B11 钉住）；B05 / B06 标“等价”（`tripped` 只作诊断）。
   这三处不是“削弱”，是因为被测代码的那条路径被有意删掉 / 变成纯诊断；判别力转移到新变异上。
5. **R8-05 / R8-06（P3）不在本批次范围**：R8-05（急停路径里的同步 NVS 写）需要改
   `param_persist_now` 的调用点与 persist_worker 的语义，R8-06 是诊断口径 / 文档，另批处理。
6. TC1 / TC2 的水路拓扑仍未确认（沿用 r7f“两路独立水体”的口径）；若实际同一水体，多出来的是
   保守（可能误报），不是缺口。
