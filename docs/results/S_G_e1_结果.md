# S-G 实现批次 e1 结果 — ESP32 链路层其余条目

- 批次：S 线 S-G e1（M11-F-09 / M11-F-13 / M08-F-07 / M11-F-15 / X01-F-03 / X01-F-04 / X01-F-05）
- 工作树 / 分支：`D:\Espressif\ws\tcs-g-e1` / `fix/s-g-e1`
- 基座：`85a009c`（`fix/s-u-int2`，2026-11-26 18:06 全量门禁通过；协议版本 3）。开工第一步
  `git merge --ff-only 85a009c` 成功（无冲突、无 rebase）。
- 日期：2026-11-26　环境：Windows + Git Bash、MSVC cl（主机单测）、ESP-IDF v5.5.5（构建）
- **不碰设备**：没开串口、没烧录、没驱动任何 TEC / 加热。真机项写在 §4。

## 1. 逐条结果

判定只用五种：已修并验证 / 已实施待真机 / 已修未记 / 不适用 / 未做。

| ID | 判定 | 改了什么 | 怎么验证的（命令与结果） | 遗留与原因 |
|---|---|---|---|---|
| **M11-F-09**（P2）收帧不校验 `ver`，网页 ver 硬编码 | 已实施待真机 | 提交 `959a0d3`。① `link_dispatch.c`：0x10 STATUS 比对 `ver == TCS_PROTO_VER`，不符 → `proto_mismatch++` + `ESP_LOGW` + 新增提示位 **`ALM_PROTO`（0x0080）**；版本对上后提示位清 0、计数只增。不符的帧**不再 publish**（ver 变了就可能布局变了，按当前布局解出来的字段写进 s_status 就是拿错位数据当真值），只刷新判活时间戳。② `link_stats_t` 增 `peer_ver`（实际收到的对端版本）与 `ack_bad`，`linkstat` 打 `ver=` / `ackbad=`。③ `tcs_snap.c` 组 0x20/0x21 时把 `link_alarm_hint()` 按位或进 alarm（不改 `g_tcs`：控制任务每拍重建 alarm_bits，外部写进去最多活一拍，会闪） | `esp32/tests/test_link_dir.c` 新增 N 段 12 条断言：`[N] 版本不符：verg=1 peer_ver=4 hint=0x0080 tc1_act=25.5 alive=1` → `[N] 版本恢复：verg=1 peer_ver=3 hint=0x0000 tc1_act=26.0`；`test_link_dir: PASS=89 FAIL=0`。判别性：修复前不比对 ver，hint 恒 0、tc1_act 会被伪造成 99.0 → N 段三条红 | **网页那一行没改**：`esp32/components/tcs_web/websocket.c:249` 仍写 `cJSON_AddNumberToObject(root, "ver", TCS_PROTO_VER)`——`tcs_web/` 归 e3b。精确改法：`web_status_t` 加 `uint8_t ver;`，`gather()` 里 `link_status_t ls; link_status_copy(&ls); s->ver = ls.valid ? ls.ver : 0;`，然后 JSON 用 `s->ver ? s->ver : TCS_PROTO_VER`。`ALM_PROTO` 的屏/网页文案也归界面与 e3b（位本身不参与安全判定） |
| **M11-F-13**（P2）`tcs_snap.c` 用 int32 承接 64 位 `esp_timer` | 已修并验证 | 提交 `ce1cec2`。新增 `esp32/main/snap_time.c/.h`（零 IDF 依赖的纯算术）：`snap_ms_from_us(int64_t)`、`snap_elapsed_us(t0, now)`（无符号减法）、`snap_late_ms()`。`tcs_snap.c` 的 `t0` 改 `int64_t`、`ms` 与 `enc_max` 的 `dt` 都走它们 | `esp32/tests/test_snap_time.c` 21/0：修复前写法（先截 int32 再除）在同一组时刻上 `ms @2^31us` = 4292819813、`@2^32us` = 0、int32 差为负（红）；新实现 2147483 / 4294967 / 连续（绿）。覆盖 2^31 / 2^32（71.6 分钟）/ 49.7 天 / 50 天、tick 回绕、钳位 | 真机 90 分钟连续跑的读数见 §4（`[SNAPP]`/`enc_max` 不跳变） |
| **M08-F-07**（P2）链路侧：200ms 节拍无迟到统计 | 已实施待真机 | 提交 `ce1cec2`。`snap_task` 醒来后记 `xTaskGetTickCount()` 与 `last_wake` 的差（`snap_late_ms`，无符号、跨 tick 回绕安全），10s 窗口取最大；`[SNAPP]` 行改为 `enc_max=…us (budget 500) late_max=…ms (period 200ms) snap=… delta=… drop=… skip=…` | `test_snap_time.c` 第 4 节 5 条（准时 0 / 晚 3 tick=30ms / 晚一整拍=200ms / 跨回绕=50ms / 极大值钳位）；ESP32 默认档增量构建 0 告警 | **OTA 栈与 OTA 缓冲没做**（归 e3b，已在改 OTA）。`late_max` 只有在真机上才有意义（主机测的是算术）：见 §4 的读数与判据 |
| **M11-F-15**（P2）`link_task.c` 六类职责同文件 | 已修并验证 | 提交 `9f29c73`（纯重构）。916 行拆成 `link_task.c` 214 / `link_dispatch.c` 128 / `link_registry.c` 162 / `link_demo.c` 162 / `link_selftest.c` 156 / `link_send.c` 105 / `link_status.c` 105 + 内部头 `link_internal.h`；对外 API 仍只在 `include/link_task.h`。`CMakeLists.txt` 与 `esp32/tests` 的 linkdir 一行同步 | 拆分前后同一个用例 `esp32/tests/test_link_dir.c`（链整个组件）**输出逐行相同**：`PASS=89 FAIL=0`，含 `[LPBK] RESULT pass=4 fail=0 PASS`、`[N]` 两行。符号归属：`dumpbin /symbols` 对 7 个 .obj 统计，**62 个 link_* 符号各只有 1 个定义者**；拆分前 24 个公开函数一个不少（清单见 §5.2）。`mutation_link_dir` 24 个变异体 0 异常（锚点跟着新文件改过）。ESP32 默认档构建 0 告警；`CONFIG_TCS_DEMO=1` 档编译通过 | 文件数从 3 个变 9 个（`nm` 里多出的 `link_internal_*` / `link_dispatch_*` 等是组件内部符号，前缀已区分）。演示档的行为没有真机验证过（它本来就只在 `CONFIG_TCS_DEMO` 档存在） |
| **X01-F-03**（P2）0x90 的 `status/ret` 两端都不解析 | 已修并验证 | 提交 `68c3b58`。① 协议：`TCS_ACK_STATUS_MASK 0x0F`（l1 的方向位在**消息号字节** bit6，status 字节本身没有方向位；掩码保证将来有人往高位塞标志时不被当成未知状态码）。② 两端 `comm.c`：`comm_on_ack_ex(seq, mt, status, ret)` —— 结果落进 pending 条目（`ack_status/ack_ret/ack_have`）再清 `in_use`；非 OK 计数 `ack_nonok_cnt`（含未匹配）；匹配时在临界区外调 `on_ack_result`；`comm_last_ack()` 供排障。0x90 收帧改用 `tcs_scan_ack`；GS 遗留 0x04/0x84（4 字节）仍走旧入口；`comm_on_ack()` 保留为 status=OK 的包装。③ 屏端 `link_send.c`：`(seq,mt)→参数 id` 跟踪表（深=COMM_MAX_PENDING），按 BAD_ID / BUSY / CLAMP 分桶并回调上层；`main.c` 的 `on_param_write_result`：CLAMP 采用 `ret`（主控实际写入值），BAD_ID/BUSY 打 `[WARN] param write rejected …` 并把设定值回退到最近快照的权威值（其它参数由 0x11 镜像泵 ~41s 内回正）。④ `linkstat` / `stat` 增 `ackbad=`（屏端另有 `wbad/wclamp/wbusy`） | 新用例 `tests/test_link_ack.c`（H743 comm.c+link_send.c）36/0；`esp32/tests/test_link_ack_comm.c`（主控 comm.c，同一份用例跳过 link_send 几节）20/0。判别性：把 comm.c 的 0x90 分支换回修复前写法（只取 seq/mt）→ 同一用例 **8 条断言红**（非 OK 计数、status/ret 到上层、link_send 找回被拒参数等）。既有 `test_link_dir` 89/0、`test_link_send` 30/0、`test_link_peer` 22/0、`proto_sync --check` OK | 无（web 侧 `link_last_ack` 的消费端 M11-F-08 不属本条） |
| **X01-F-04**（P2）0x20 的 `badge/fresh_ms` 无消费者 | 已实施待真机 | 提交 `da7c753`。`link_snap.h` 新增 `tcs_link_fresh_t` 与纯判定：`tcs_link_fresh_apply()`（0x20 整块）、`tcs_link_fresh_badge()`（0x21 的 `TCS_D_BADGE`）、`tcs_link_stale_at(fresh_ms, snap_age, have)`——陈旧 = `0xFFFF` 或 `>3000ms`（3 个 500ms 轮询周期）或快照 `>2s` 没来。`main.c` 的 `snap_apply` 调 apply、0x21 分支处理 badge、新增 `tcs_link_fresh_copy()`；`stat` 增 `fresh= / bdg= / stl=` | `tests/test_snap_fresh.c` 17/0（新鲜 → 0xFFFF 陈旧 → 超龄陈旧 → 恢复；3000/2000ms 两个边界；badge 增量/掩码/失联报警原值 2 保留）。判别性：把 apply 换回"不判"（stale 恒 0）→ 2 条红。接线门禁 `tests/check_snap_fresh.py` 7 项（snap_apply 与 0x21 分支逐字核对、0xFFFF 原值保留、不许接 POST 角标） | **界面没改**（`project_tcs/App/lvgl_ui/` 不许动）："屏上出现数据陈旧"要界面层按 §3 的方案落地；真机读数见 §4 |
| **X01-F-05**（P2）`MAX_TXQ` 两端不同、注释口径过期 | 已修并验证 | 提交 `6d97085`。值只在 `protocol/tcs_proto.h` 写一次（`TCS_MAX_TXQ 16u` = 8 条等 ACK 在途的重传 + 8 条免 ARQ 周期帧/应答余量）；两端 `comm.h` 都 `#define MAX_TXQ TCS_MAX_TXQ` 并各带两条 `_Static_assert`（相等；`>= COMM_MAX_PENDING + 8`）。注释改写容量模型并说明为什么不能按 `param_count()` 静态推导（注册表条数是运行期常量；注册表下发按 1 帧/拍节拍化，队列稳态 1~2 帧） | 新用例 `tests/test_link_contract.c`：两端 comm.h 各编一次，5/5 与 5/5；源码门禁 `tests/test_link_contract.py` 16 项（两份 comm.h 的宏形式/断言/include 顺序/旧注释零残留）。判别性：把 ESP32 comm.h 改成字面量 8 → 编译报 **两条 C2338 static_assert**（输出见 §5.1） | 无 |

**判定计数（7 条）**：已修并验证 4 / 已实施待真机 3 / 已修未记 0 / 不适用 0 / 未做 0。

## 2. 与说明不一致 / 按自己判断的地方（如实）

1. **M11-F-09 说明里的"方向位在 status 字节的最高位"与实际 l1 实现不符**：l1 把方向位放在**消息号字节**的 bit6
   （`TCS_MT_DIR_ESP32 0x40`），status 字节只有 0..3。因此解析时不需要掩掉方向位；我仍加了
   `TCS_ACK_STATUS_MASK 0x0F` 作为前向兼容（将来若真往 status 高位塞标志，不会被当成未知状态码），
   并在协议头写清两种口径。
2. **M11-F-09 的"网页用实际 ver"没做**：`tcs_web/` 归 e3b（本批次禁止改）。给了精确的一行方案（见上表）。
3. **0x10 STATUS 在 v2 仍在使用**：H743 每 500ms 发一帧，ESP32 用它做链路判活与网页状态，所以按说明
   "别给不再收发的帧写校验"核对后，校验就写在这一帧上（0x20 快照的 ver 早由 H743 侧校验）。
4. **M08-F-07 只做链路侧**：OTA 栈 / OTA 缓冲按说明归 e3b。
5. **X01-F-04 的数据落在 `link_snap.h` 的镜像结构**（不是 `g_tcs`）：`g_tcs` 的 `tcs_state_t` 在
   `esp32/components/tcs_core/control.h`（e3a 归属），且它每拍被控制任务重建；把"数据龄/角标"放进
   每帧重建的 `g_tcs` 会被抹掉。镜像结构是说明允许的"等价的镜像结构"，界面经 `tcs_link_fresh_copy()` 读。
6. **`ALM_PROTO` 只进 0x20/0x21 的 alarm 字段，不写 `g_tcs.alarm_bits`**：控制任务每拍
   `g_tcs.alarm_bits = 0` 再按锁存重建，外部写进去最多活一拍（会闪）。副作用：网页（读 g_tcs）暂时看不到
   这一位（e3b 可把它与 `peer_ver` 一起映射）；屏在版本不符时本来就会整帧丢弃快照，所以这一位当前的实际
   消费者是日志 + 未来的界面。

## 3. X01-F-04 的界面方案（交界面层；本批次不动 `lvgl_ui/`）

数据源（只读，一行调用）：`tcs_link_fresh_t f; tcs_link_fresh_copy(&f);` 与
`uint32_t age, seq; uint8_t have; tcs_link_snap_info(&age, &seq, &have);`
现算"陈旧"：`tcs_link_stale_at(f.fresh_ms, age, f.have)`。

| 元素 | 用什么 | 显示规则 |
|---|---|---|
| 温度卡（贴身/环境水温、体温） | `tcs_link_stale_at(...) == 1` | 整卡沿用现有"连接中断"的灰化样式 + 文案"**数据陈旧**"；不显示具体数字。`fresh_ms == 0xFFFF` 时文案改"**温度未知（主控离线）**"，**不得**把它换算成 65.5s 显示 |
| 布防角标 | `f.badge`（协议 `post_arm_t`） | 独立控件，**不许**接 `post_badge_state()`（那是本地 POST 角标，0/1/2 语义不同：未开始/显示中/已淡出）。`badge==2` → 红底"布防失联"（优先级低于急停/超温横幅，高于普通提示）；`==1` → "已布防"；`==0` → 不显示 |
| 何时恢复 | 下一帧新鲜 0x20 | `stale` 自动回 0（用例覆盖"新鲜 → 陈旧 → 恢复"），角标随 badge 字段 |

时序：主控 200ms 一帧，`fresh_ms` 正常 ≤1000ms（500ms/通道、取较旧一路）。断链 2s 后 `age > 2000` → 陈旧；
M207 掉线时主控给 `0xFFFF` → 立即陈旧。两者都会在 1 帧内恢复。

## 4. 真机验证（协调者做；两端都必须是新固件）

### 4.1 期望读数

| 位置 | 命令 / 日志 | 期望 | 看到什么算不通过 |
|---|---|---|---|
| ESP32 `linkstat` | 新增字段 | `ver=3`（实际收到的 H743 版本）、`verg=0`、`ackbad=0`；`arq=1`、`wdir=0`、`txwdir=0` | `ver=0` 而屏明明在发 0x10；`verg` 持续增长；`ackbad` 在没有被拒写入时增长 |
| ESP32 `[SNAPP]` | 每 10s 一行 | `enc_max=<500us`、`late_max=0～10ms`（准时醒来 0～1 tick）、`snap≈50/10s`、`delta` 视变化、`drop=0 skip=0` | `late_max` 持续 >100ms（快照任务被抢占，"每 200ms 一帧"不成立）；`enc_max` 出现 4.29e9 级伪值（M11-F-13 回归） |
| H743 `stat` | 第二行新增 | `ackbad=0 wbad=0 wclamp=0 wbusy=0`、`fresh=<1000 bdg=<0/1/2> stl=0` | 屏上改参数被拒却 `wbad=0`（ACK 结果没到上层）；`fresh=65535` 且 M207 明明在线；数据新鲜时 `stl=1` |
| H743 `stat` | 在屏上写一个**不存在的 id**（台架档 `p set` 越权 id 或改表后一端未更新） | `[WARN] param write rejected id=0x.. st=2` 一行 + `wbad+1`；设定值在下一帧快照后回到权威值 | 被拒了却没有任何日志/计数（F-03 回归） |
| 网页 | `st` 消息的 `ver` | 改完 e3b 那一行后应为 **3**（与屏一致）；两端版本不同时显示的是对端真实版本 | 恒为 1 / 恒为本地常量 |

### 4.2 回归项（按计划跑 `linkloop 1` 与 `linkmute`）

- `linkloop 1` 20s：`echo=` 增量 ≈ 期间本端发出帧数（与 l1 的 §2.6 表一致），`rx_f` / `dup` **不变**，
  不出现 `PARAM id=`，不出现 `H743 SET id=`；`ackbad` 不因回声增长。
- `linkmute`：静默期 `txfail`/`uartdrop` 增长是预期；静默结束后快照恢复、`late_max` 无长期尖峰。
- `restart` / 拔插：`verg` 不增长（两端都是新固件）；`fresh` 在 M207 复联后回到 <1000。

## 5. 判别性证据原文（摘要）

### 5.1 X01-F-05（编译期断言有牙）

把 ESP32 `comm.h` 的 `MAX_TXQ` 改成字面量 8 后编译：

```
comm.h(47): error C2338: static_assert failed: 'MAX_TXQ must follow protocol/tcs_proto.h TCS_MAX_TXQ'
comm.h(48): error C2338: static_assert failed: 'MAX_TXQ must hold a full retransmission burst + 8 frames'
```

### 5.2 M11-F-15 符号归属（`dumpbin /symbols`，按 .obj 统计）

62 个 `link_*` 符号各只有 1 个定义者；拆分前 `link_task.c` 的 24 个公开函数全部仍存在：

| 文件 | 行数 | 公开符号（示例，全量见下表） |
|---|---|---|
| `link_task.c` | 214 | `link_start` `link_alive` `link_peer_age_ms` `link_crit_enter/exit` `link_ctx` `link_internal_now_ms` `link_core_*` |
| `link_dispatch.c` | 128 | `link_dispatch_frame` `link_on_cmd` `link_on_setparam` `link_dispatch_proto_mismatch/peer_ver/alarm_hint` |
| `link_registry.c` | 162 | `link_registry_init/apply_entry/progress_reset/done/note_param` `link_get_registry` `link_request_registry` `link_param_value` `link_last_param_copy` |
| `link_demo.c` | 162 | `link_demo_takeover/step/cmd/suppressed/boot/note_param/running` |
| `link_selftest.c` | 156 | `link_loopback_run` `link_muted` `link_mute_ms` `link_mute_rxdrop` `link_loopback` |
| `link_send.c` | 105 | `link_frame_build` `link_send_ack` `link_send_frame_mt/param/info/cmd` |
| `link_status.c` | 105 | `link_status_buf/note_alive/copy` `link_internal_lock/unlock` `link_notify_event` `link_on_event` `link_stats_get` `link_alarm_hint` `link_proto_mismatch` `link_rx_bytes/frames` |

拆分前后 `test_link_dir` 输出逐行相同（89 条断言，含 `[LPBK]` 与 `[N]`）。

### 5.3 X01-F-03（换回修复前的 0x90 解析 → 8 条红）

```
FAIL: 1: 非 OK 应答计数 +1
FAIL: 1: 完成回调带 status/ret（修复前 ret 恒 0、status 不参与判定）
FAIL: 1: comm_last_ack 可取到最近一条应答的结果
FAIL: 3: 0x80|BUSY 按 BUSY 解释（掩码 0x0F）
FAIL: 4: 未匹配的非 OK 应答也计数
FAIL: 6: link_send 按 (seq,mt) 找回参数 id 并上报表决（CLAMP: 实际 44.5 / 请求 50）
FAIL: 7: 上层能提示/回退被拒的那一条写入
FAIL: 8: BUSY 也交给上层
test_link_ack: PASS=28 FAIL=8
```

### 5.4 M11-F-13 / M08-F-07（同一组时刻，旧写法 vs 新写法）

```
      ms @2^31us: new=2147483 old=4292819813
      ms @2^32us: new=4294967 old=0
  ok   修复前的 int32 差为负（红）：-2147479648
  ok   修复后的间隔 = 2147487648 us（2^31+4000）
test_snap_time: PASS=21 FAIL=0
```

### 5.5 M11-F-09（N 段原样输出）

```
  [N] 版本不符：verg=1 peer_ver=4 hint=0x0080 tc1_act=25.5 alive=1
  [N] 版本恢复：verg=1 peer_ver=3 hint=0x0000 tc1_act=26.0 alarms=0x0000
test_link_dir: PASS=89 FAIL=0
```

## 6. 本批次验证总结果

| 门禁 | 结果 |
|---|---|
| H743 主机全量 `bash tests/run_all.sh` | `run_all: ALL GREEN`（含新接的 `[linkcontract]` `[linkack]` `[snapfresh]` 与源码门禁；`[mut-link]` 24 个变异体 0 异常；`[card_lines] PASS`） |
| ESP32 主机全量 `bash tests/run_m207_tests.sh` | `esp32-tests: ALL GREEN`（`linkdir` 89/0、`linkpeer` 22/0、`linkack` 20/0、`snaptime` 21/0、`linkcontract` 5/0） |
| H743 构建矩阵四档（一次 `tools/build_matrix.sh --esp32 --diag --site`） | PROD / BENCH / SIM / SIMWG：`exit=0 error=0 warning=4`（= 基线，全在第三方 qrcodegen）；`[prod-clean] PASS`（交付档不含钩子、台架档含钩子、nm_addr 提示正确）；PROD 可复现比对的自测 PASS |
| ESP32 三档 | default / diag：`exit=0 error=0 warning=0`；**site：第一次 `exit=2`，是 GCC 内部错误（`esp_lcd_panel_rgb.c:722 internal compiler error: Segmentation fault`，4 并发构建时的编译器崩溃，与本批次代码无关）**；把并发降到 `IDF_BUILD_JOBS=2` 单独重跑同一个 `build-matrix-site` → `exit=0`、**0 告警**、镜像生成 |
| 协议门禁 | `python tools/proto_sync.py --check` → OK（组帧函数改名 `mt_frame→link_frame_build` 已同步 `TX_PAT`） |
| 审计 | `official_lib_audit.py --update-manifest` → 306 文件、0 未覆盖、0 未分类；报告与清单在最后一个单独提交 |

## 7. 风险 / 需要人看的点

1. **M11-F-09 的网页 ver 一行还在 e3b 手里**（§1 表里给了精确改法）。在它改完之前，网页上的 ver 仍是本地常量；
   ESP32 侧的 `peer_ver` / `verg` / `ALM_PROTO` 已经可用。
2. **ALM_PROTO 目前只出现在快照的 alarm 字段**，屏在版本不符时会整帧拒收快照、网页读的是 `g_tcs`——
   这一位现在实际只体现在日志里。要在屏/网页上有可见效果，需要界面层（文案）与 e3b（网页映射）各一步。
3. **X01-F-03 的行为变化**：屏写参数被拒（BAD_ID/BUSY）时现在会打 `[WARN] param write rejected …` 并把
   设定值拉回最近快照的权威值；`CLAMP` 时采用主控回的 `ret`。真机上如果看到屏上数值"跳一下"，
   先看这条日志——它是被拒/被限幅的正常表现。
4. **M11-F-15 让组件从 3 个 .c 变 9 个**：`esp32/tests/run_m207_tests.sh` 的 linkdir 一行与
   `mutation_link_dir.py` 的源文件表都已同步；如果别的分支还按 `link_task.c` 单独编译，会缺符号。
   另外 `tools/proto_sync.py` 的 `TX_PAT` 加了 `link_frame_build`——其它批次若再改组帧函数名要一起改。
5. **构建矩阵的 site 档第一次是 GCC ICE**：本机 16GB、并发 4 时编译器段错误。重跑（并发 2）通过。
   以后并发建议不超过 2；`build_matrix.sh` 本身没有并发上限，靠环境变量。
6. **0x10 STATUS 版本不符时不再 publish 字段**（只判活）：如果现场真出现"屏在发但网页一直显示离线/旧值"
   并且 `verg` 在涨，那就是版本不匹配的既定表现，不是链路坏。
7. **`link_stats_t` 增了两个字段、`stat` / `linkstat` 的输出字符串变了**：现场的日志解析脚本若按列切分要跟着改。

## 8. 过程中的事故（如实）

1. **`python -I` 会忽略 `PYTHONUTF8`/`PYTHONIOENCODING`**（`-I` 隐含 `-E`）：手工跑 `study_cards_check.py`
   时在 cp1252 控制台上因中文括号编码失败。这不是代码问题；`run_all.sh` 里没有 `-I`，门禁本身正常。
   `reanchor_study_cards.py` 同理必须用 `PYTHONUTF8=1 python tools/reanchor_study_cards.py`（它靠 grep 子进程，
   `-I` 会让子进程按 cp1252 解码而崩）。
2. **拆分后第一次构建暴露两个新文件的 `TAG` 未使用**（`link_status.c`、`link_demo.c` 的 `#if` 分支）——
   0 告警基线因此变 2，已修（demo 的 TAG 移进 `#if`，status 删掉），重编 0 告警。
3. **m1 的源码门禁 `test_m1_gates.py` 假设 `link_peer_age_ms` 在 `link_task.c`**：拆分把它挪到
   `link_status.c` 后门禁红。我把它**挪回** `link_task.c`（并改用 `comm_peer_last_ms()`，本来就是门禁要求的
   口径）而不是改门禁——m1 的门禁是别人的，改它风险更大。`link_task.c` 因此从 202 → 214 行。
4. **`mutation_link_dir.py` 的锚点与源文件表**必须跟着拆分改（L1/L3/S1/S3 四处文本 + esp_dir 的源文件列表），
   不改就是"未变异的实现自己就没过（COMPILE_FAIL）"。改完 24 个变异体 0 异常。
5. **研学卡重锚**：本批次改了 `console.c` / `main.c` / 新增 9 个文件，`[card_lines]` 漂了 46 处；
   用 `PYTHONUTF8=1 python tools/reanchor_study_cards.py` 重锚（UART 43/43、GPIO 58/58、PWR/RCC 61/61，
   新增 5 行），基线文件与三张卡随文档提交。
