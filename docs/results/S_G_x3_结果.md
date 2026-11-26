# S-G x3 结果（门禁加固：弱断言、可绕过点、今天新暴露的三类坑）

分支 `fix/s-g-x3`，基座 `85a009c`（集成分支 `fix/s-u-int2`，2026-11-26 18:06 全量门禁通过）。
本批次只动测试、门禁脚本、`project_tcs/tools/` 下的工具与文档；**没有改任何固件逻辑**
（`App/`、`Core/`、`esp32/components/`、`esp32/main/` 下的 .c/.h 一行未动），也没有碰设备。

提交：

| 提交 | 内容 |
|---|---|
| `7d90639` | X06-F-06：修 4+1 处恒真/弱断言，加恒真扫描 + 断言数基线两道门 |
| `5e7711a` | X06-F-07 余项：每道门禁最小样本数、环境变量覆盖报出处、rtt_count 改区间+必需点 |
| `30113da` | 条目 3：执行器点击禁区收成唯一实现 `tap_guarded` + 主机自检 + 静态门禁（D7 事故） |
| `40e5f8e` | 条目 4：构建脚本回归门禁（GLOB CONFIGURE_DEPENDS / 台架档无条件配置 / proto_sync 第 9 项） |
| `599c5f4` | 条目 5：`tools/s_gate.sh` 集成门禁收进仓库 |
| `f969318` | 条目 6：真机脚本操作三条规则（追加到 `docs/复验指引卡_v4.md`） |
| 最后一个提交 | 官方库审计清单与机器报告（单独提交，合并时由协调者统一重新生成） |

---

## 1. X06-F-06 — 4 处弱断言 / 恒真断言

**判定：已修并验证**

### 改了什么

| 位置 | 修前 | 修后 | 为什么是这个值 |
|---|---|---|---|
| `tests/test_proto_tcs.c`（修前 :255） | `comm_txq_count(&L2) == 0 \|\| 1`（恒真） | `== 3` | 3 次超时各重传一帧、这段没调用 `comm_poll_tx`，所以队列里正好压着 3 帧；第 4 次 tick 只清 pending 不再入队（由下一行 `link_ok == 0` 覆盖） |
| 同上（修前 :108） | `CHECK(st > 0)` | `CHECK(st == 10)` | `proto_self_test` 有 10 个子项（3 CRC + 3 定点换算 + 2 下行编帧 + 2 RX 状态机），逐项列在注释里；增删子项必须同步这个数（有意为之：逼一次复核） |
| `tests/test_post.c`（修前 :76） | `post_arm_step(...) >= 0`（枚举恒非负） | `== POST_ARM_ARMED_ONLINE` | 上一段循环已把 `healthy_run` 记满 10（ack=0 不迁移），这一轮第一次带 ack 就升 ARMED，10 次全部返回 ARMED |
| `tests/test_ws_json_escape.c`（修前 :59） | `(unsigned char)*p >= 0x20 \|\| *p == '\0'`（死项） | `(unsigned char)*p >= 0x20` | 循环条件就是 `*p`，进到体内必然非 0，那个析取项永远不成立；去掉后断言才是它名字说的那件事 |
| `tests/test_perf_hist.c`（修前 :42）**审查未列的第 5 处** | `CHECK(1, "对空指针加样本不崩")` | `h.n == 0 && h.over_thresh == 8000u` | 空指针路径真正该成立的是"什么都没写"：隔壁窗口的样本数仍 0、预算没被改 |

另外给三个不打印计数的用例（`test_touch_session.c` / `test_qr.c` / `test_ws_json_escape.c`）补了
"执行了多少条断言"的输出，作为下面断言数门禁的取数口。

### 怎么验证的

新增两道常驻门禁（都接进 `tests/run_all.sh`，带 `# S-G x3` 注释）：

1. `tests/check_tautology.py` — 恒真断言静态扫描（`|| 1`、`|| true`、`CHECK(1,`、
   对无符号/枚举值 `>= 0`、`for(;*p;)` 体内 `|| *p == '\0'` 死项；含 python `assert ... or 1`）。
2. `tests/assert_count.py` + `tests/baseline/assert_counts.tsv` — 逐个运行 27 个主机单测，
   取它自己打印的 `PASS=<n>`（或语料/模糊测试的 `corpus=` / `fed=`），低于基线即红。

**红线（修前）** — 扫描器在未修的代码上跑：

```
[tautology] FAIL project_tcs\tests\test_perf_hist.c:42  T2 恒真条件（CHECK(1 / CHECK(true） | CHECK(1, "对空指针加样本不崩");
[tautology] FAIL project_tcs\tests\test_post.c:76       T3 对不可能为负的值断言 >= 0（函数 post_arm_step() 返回无符号/枚举） | CHECK(post_arm_step(&c, 0, 1, 1, 1) >= 0, "counting");
[tautology] FAIL project_tcs\tests\test_proto_tcs.c:255 T1 恒真析取项（|| 1 / || true） | CHECK(comm_txq_count(&L2) == 0 || 1, "txq sane");
[tautology] FAIL project_tcs\tests\test_ws_json_escape.c:59 T4 死项：循环条件就是 *p，体内 || *p == '\0' 永不成立
[tautology] FAIL（4 处）
```

`CHECK(st > 0)` 不是字面恒真（`st` 是 int），扫描器抓不到，靠"改成精确的 10"这一条覆盖。

**绿线（修后）**：

```
[tautology] SELFTEST PASS（5 个样例）
[tautology] PASS（0 处）
[assert_count] PASS（27 个测试可执行行：12863 条断言 + 46951 例语料样本，均不低于基线）
```

断言数不下降：`test_proto_tcs` 46→46、`test_post` 81→81、`test_perf_hist` 29→29（改的是条件，不是条数）。

### 遗留

- 断言数是"执行次数"口径（循环里重复执行也计数），所以 `touchsession` 的 10497 与 `ws_json` 的 1060
  里大部分来自逐点/逐字符循环。它防的是"少跑断言"，不防"断言写得松"——后者归恒真扫描器。
- `run_all.sh` 里用 heredoc 现场生成的 `test_tmp117.c` 没有自己的文件，不在断言数门禁的 27 个之列（已写在脚本注释里）。

---

## 2. X06-F-07 余项 — 门禁可绕过点

**判定：已修并验证**

### 改了什么（`tests/run_all.sh`，逐处带 `# S-G x3`）

| 门禁 | 修前 | 修后 |
|---|---|---|
| `vendor_sha` | 清单删空也 PASS，`(5 files)` 是写死的字面量 | 数清单条数，< 5 判红；PASS 行打印真实条数 |
| `card_paths` | 研学卡全移走 → `0 checked, 0 bad` 仍 PASS | `n < 1` 判红 |
| `golden_corpus`（新增） | 两个 golden CSV 清空后逐条比对 0 条，测试 fail=0 | `tests/m207_golden.csv`、`tests/m207_golden_rx.csv` 各 ≥ 16 条 |
| `rtt_count` | 硬编码 `== 16`（加日志要改数字，删日志改数字就放行） | 6 条必需调用点必须在（`FAULT CFSR=` / `ltdc init rv=` / `ft5446 ver=` / `i2c4 up pclk4=` / `eeprom online magic=` / `[POST] done pass=`）+ 总数落在 [16,24] |
| `[env]` 覆盖 | `GCC=${GCC:-gcc}`、`TCS_PYBIN`、`IDF_CJSON` 静默生效 | 三处都打印"实际取值 + 来源" |

`rtt_count` 的区间怎么来的（写进脚本注释）：下界 16 = 当前业务面调用点数（`App/rtt/` 的 vendor 与薄壳不计）
——"一条都不许少"；上界 16+8 留给确实要新增日志的场合，同时挡住宏展开/循环里打日志的刷屏式增长。

### 怎么验证的（把每道门禁的语料人为抽空 → 必须红，恢复后绿）

跑的是从 `run_all.sh` **原样抽出**的代码块（不是复写的逻辑）：

```
---- vendor 正常: FAIL=0                 [vendor_sha] PASS (5 files)
---- vendor 只剩 2 条（应红）: FAIL=1     [vendor_sha] FAIL: 清单里只有 2 条（应 ≥ 5）——语料被抽空时这道门禁会空过
---- vendor 恢复: FAIL=0                 [vendor_sha] PASS (5 files)
---- card_paths 正常: FAIL=0             [card_paths] 70 checked, 0 bad
---- card_paths 研学卡全部移走（应红）: FAIL=1
                                        [card_paths] 0 checked, 0 bad
                                        [card_paths] FAIL 一条路径都没检查到（研学卡被移走或格式变了？）
---- card_paths 恢复: FAIL=0             [card_paths] 70 checked, 0 bad
---- golden_corpus 正常: FAIL=0          [golden_corpus] tests/m207_golden.csv: 16 条 / m207_golden_rx.csv: 16 条
---- golden_corpus 只剩 10 条（应红）: FAIL=1
                                        [golden_corpus] FAIL tests/m207_golden.csv 只有 10 条（应 ≥ 16）
---- golden_corpus 恢复: FAIL=0
---- rtt_count 正常: FAIL=0              [rtt_count] PASS (16 个调用点，必需点齐全，区间 [16,24])
---- rtt_count 必需日志点被改名（应红）: FAIL=1  [rtt_count] FAIL: 必需日志点缺失： [[POST] done pass=]
---- rtt_count 恢复: FAIL=0
---- rtt_count 加了 10 个调用点（应红）: FAIL=1  [rtt_count] FAIL: SEGGER_RTT_printf 调用点 26 不在 [16,24]
---- rtt_count 最终恢复: FAIL=0
```

环境变量覆盖打印（本机实跑）：

```
[env] python=...\Python312\python.exe（来源：环境变量 TCS_PYBIN）      ← TCS_PYBIN 覆盖
[env] python=...\Python312\python.exe（来源：默认候选顺序（TCS_PYBIN=未设置））
[env] CJSON_DIR=/d/tmp/cjson（来源：环境变量 IDF_CJSON）
[env] CJSON_DIR=/d/Espressif/frameworks/esp-idf-v5.5.5/components/json/cJSON（来源：默认值）
[env] GCC=arm-none-eabi-gcc（来源：环境变量 GCC，覆盖了默认值 gcc）
```

### 遗留

- 本机没装 gcc，`run_all.sh` 走 MSVC `cl` 分支，`[env] GCC` 的"默认值"分支没法在本机跑到
  （覆盖分支已单独抽出验证）。
- `rtt_count` 的 6 条锚点是"排障信息源"清单，新增日志点只要不动这 6 条、总数不超 24 就不用改门禁；
  超过 24 要人来看是不是刷屏。

---

## 3. 条目 3（X3-03）— 现场工具的点击禁区做成公共函数 + 主机自检

**判定：已修并验证（主机侧）；真机上未复跑（本批次不碰设备）**

### 起因（D7 事故）

2026-11-26 D7 第一次跑 `estop_matrix`：模式页"点按"那一行用 `tap(632,388)` 去点确认框的"取消"，
确认框已经不在了，这一下落在 TEC2"制冷"上——主控向 M207 发了一条**真实的 TEC2 制冷命令**
（约 1 秒后被同一行的"按住 2 秒"急停关掉）。当时的禁区只在 `estop_matrix` 自己那份 `tap()` 里，
`tune_refresh_check` / `tap` / `dlg_timing` 各自直接写 `g_tap_inject`，完全绕过。

### 改了什么

- `project_tcs/tools/site/site_common.py` 成为**唯一实现**：
  - `ACTUATOR_RECTS`（6 个矩形：两行制冷/制热 + 风扇 + 切自动），坐标来源写在旁边（真机截图 `shots7/02_mode_manual.png`）；
  - `TapRefused` / `tap_guarded(mb, x, y, mode=1, page=None)`：模式页（页码 4）落在矩形里 → 抛错、
    **一个字节都不写探针**；写入仍是 mode/x/y 先、序号最后，写后回读校对；`page=None` 时从固件 `s_page` 现读（默认就拦）。
- `estop_matrix.py` / `tune_refresh_check.py` / `tap.py` / `dlg_timing.py` 全部改用它；`estop_matrix` 里那份矩形表删除。
- `tools/site/README.md` 写明"注入点击必须走 `tap_guarded`"。

### 怎么验证的

`tests/test_tap_guard.py`（纯 Python：假页号读取函数 + 假探针对象，不碰真设备）：

**红（修前）** — 同一个测试放到基座 `85a009c` 的 `site_common.py` 上：

```
AttributeError: module 'site_common' has no attribute 'ACTUATOR_RECTS'   → exit 1
```

**绿（修后）**：

```
test_tap_guard: PASS=93 FAIL=0
```

覆盖：六个矩形 × 5 个点全部被拒且 `mb.writes == []`；急停键 (790,482)、返回键 (84,28)、首页四个导航键放行；
左闭右开边界（左上角拒、右/下边界外侧放行）；页码 0/1/2/3 上同样的执行器坐标放行；不传 `page` 时从 `s_page` 现读也拦；
注入参数与序号递增正确。

`tests/check_site_tap_guard.py`（静态门禁）：R1 除 `site_common.py` 外不许再取 `g_tap_inject` / 写探针
（`verify_prod_clean.py`、`test_mb_fwcheck.py` 两个只读审计例外，理由写在表里）；R2 矩形表只能有一份；
R3 四个会注入的工具必须调用 `tap_guarded`。

**红（修前）** — 对 `85a009c` 的 `tools/site` 快照：`FAIL（15 个 site 工具，9 处违规）`
（estop_matrix R1/R2/R3；tap / dlg_timing / tune_refresh_check 各 R1+R3）。
**绿（修后）**：`PASS（15 个 site 工具，0 处违规）`。

### 遗留

- 真机上没跑过 `estop_matrix`。禁区矩形来自 2026-11-26 的模式页截图，页面若改版，矩形表要重量并同步
  `test_tap_guard.py` 的断言。
- `test_tap_guard.py` 只证明"矩形表 + 拒绝逻辑"，不证明"模式页上真的只有这 6 个执行器控件"——
  后者需要真机截图复核（已登记为真机遗留）。

---

## 4. 条目 4（X3-04）— 构建脚本的回归门禁

**判定：已修并验证**

### 起因

2026-11-26 集成日：305856d 上 H743 四档 / ESP32 三档 / 主机全量都过，**只有台架档增量构建失败**——
模式页新增 `tui_p4.c`，而 `App/lvgl_ui/*.c` 的 GLOB 没带 `CONFIGURE_DEPENDS`，旧 `build-bench/`
不会因为多了一个源文件而重新配置，链接时 `tui_p4_*` 未定义。同族坑还有：`build_bench.sh` 曾经
"目录不存在才配置"（版本串里的提交号会过期、新增源文件也编不进来）；`proto_sync.py` 第 9 项只在运行时才验。

### 改了什么

新增 `tests/check_build_scripts.py`（接进 `run_all.sh`），三条源码级判据：
① `CMakeLists.txt` 里覆盖 `App/lvgl_ui` 的 `file(GLOB …)` 必须带 `CONFIGURE_DEPENDS`；
② `tools/build_bench.sh` 的 `cmake -S . -B build-bench` 不能被 `-d/-f build-bench` 这类条件包住；
③ 跑 `tools/proto_sync.py --check`，输出里必须有"对外报的协议版本取自 TCS_PROTO_VER"。

### 怎么验证的（判别性：三处临时改回旧写法，不提交，随后 `git checkout` 精确还原）

```
---- 修后（应绿）: exit=0                       [build_scripts] PASS（3 条判据…）
---- ① GLOB 去掉 CONFIGURE_DEPENDS: exit=1      FAIL ① App/lvgl_ui 的 GLOB 没带 CONFIGURE_DEPENDS（新增 .c 不会触发重新配置）
---- ② 台架档回到条件配置: exit=1                FAIL ② 配置行被条件包住了（第 13 行：if [ ! -d build-bench ]; then）
---- ③ proto_sync 第 9 项结论消失: exit=1        FAIL ③ proto_sync 输出里没有第 9 项结论（…取自 TCS_PROTO_VER）
---- 恢复后（应绿）: exit=0                     [build_scripts] PASS（3 条判据…）
```

### 遗留

本批次按约定不跑 H743 四档矩阵（只改测试与门禁脚本），本门禁只证明"脚本不会再退化成旧写法"，
不替代真正的台架档构建——台架档真构建由条目 5 的 `s_gate.sh` 第 2 步覆盖。

---

## 5. 条目 5（X3-05）— 集成门禁脚本收进仓库

**判定：已修并验证**

### 改了什么

新增 `project_tcs/tools/s_gate.sh <上一个已过门禁的提交>`：

1. H743 四档 `tools/build_matrix.sh --twice`；
2. 台架档 `tools/build_bench.sh`；
3. `tests/run_all.sh`；
4. ESP32 三档（默认/诊断/现场）——**只在** `esp32/` 下有非文档改动时跑。

前置：工作树干净（连 untracked 也算脏）、`<prev>` 与 HEAD 都可见。日志只写自己命名的
`project_tcs/build-gate/s_gate_<步骤>.log`。结论只打印一行：全过 `GATE: ALL GREEN`，
任何一步非零 `GATE: FAIL` + 非零退出。默认限并发 2（内存纪律）。

细节：ESP32 那一步的改动探测用 `git -C <仓库顶层> diff --name-only <prev> HEAD -- esp32 ':!*.md'`——
**用 `-C` 钉住仓库顶层**，否则在 `project_tcs/` 里跑时 pathspec 永远匹配不到，这一步会被静默跳过（等于没跑）。

### 怎么验证的

- `bash -n` 通过；
- 无参数 → 用法 + `GATE: FAIL`，rc=2；
- 脏工作树 → 列出脏文件 + `失败步骤：工作树不干净`，rc=1（不会白跑十几分钟）；
- 干净工作树上的完整实跑：见第 7 节。

### 遗留

- 脚本自己没单测（它测的是别人）。
- ESP32 分支要求 `esp32/sdkconfig.site` 存在（该文件不入库），缺文件时判红而不是静默跳过；
  本批次分支没有 `esp32/` 改动，所以那次实跑的第 4 步走的是"跳过"路径（这本身也验证了改动探测没误判）。

---

## 6. 条目 6（X3-06）— 真机操作规则（文档）

**判定：已修并验证**

在 `docs/复验指引卡_v4.md` 末尾**追加** E 节（前 51 行未改），三条规则各写起因：

1. 真机脚本运行期间不改同一个工作树；烧录前后核对固件 sha256 与设备自报版本串
   —— `idf.py flash` 会先构建再烧；当天踩过"改了源码没重建、烧的还是旧 bin"。
2. 会在有执行器按钮的页面上点击的工具必须用带禁区的点击；新页面第一次上真机只做"进去、截图、返回"
   —— D7 事故（同上）。
3. 停后台任务后要核对进程真的没了；真机脚本输出文件名带提交号与时间
   —— 串口桥/探针停掉后进程仍占串口；当天多份 `esp_out.log` 分不清对应哪个提交。

i3 已把 `site_lib.sh`（烧录护栏）收进 `project_tcs/tools/site/`，这里只引用、不重复实现。

验证：`git diff --stat` 显示只新增、无删除（25 行新增 / 0 行删除）；文件统一 CRLF、无孤立 CR。

---

## 7. 本批次验证总结果

### 7.1 `s_gate.sh` 实跑（干净工作树）

在提交 `c80129a` 的**临时 worktree** 上跑——该提交 = 本分支除"结果文档"外的全部内容
（之后那份审计提交被重做成最后一个独立提交，内容不变）。跑之前 `git status --porcelain` 为空。

```
$ bash project_tcs/tools/s_gate.sh 85a009c
GATE: ALL GREEN            （rc=0）
```

分步证据（日志在 `project_tcs/build-gate/s_gate_<步骤>.log`，该目录被 `.gitignore` 的 `build-*/` 覆盖，不入库）：

| 步骤 | 结论 |
|---|---|
| 1 H743 四档 `--twice` | PROD / BENCH / SIM / SIMWG 全部 `exit=0 error=0 warning=4`（= 告警基线）；`可复现构建：剔除 .comment 与调试信息后 md5 4372590b… / 4372590b… → 相同；原始 bin md5 相同` |
| 2 台架档 | `[bench-build] build OK` + `[prod-clean] PASS（台架档产物必须含钩子）` |
| 3 主机全量 | `run_all: ALL GREEN`（含本批次新增的 6 道门禁） |
| 4 ESP32 三档 | 按设计跳过：`git -C <仓库顶层> diff --name-only 85a009c HEAD -- esp32 ':!*.md'` 为空（本分支没动 `esp32/`） |

`run_all.sh` 原样汇总行：

```
run_all: ALL GREEN
```

本次实跑里，本批次新增/加固的门禁输出：

```
[tautology] SELFTEST PASS（5 个样例）
[tautology] PASS（0 处）
[assert_count] PASS（27 个测试可执行行：12863 条断言 + 46951 例语料样本，均不低于基线）
[golden_corpus] tests/m207_golden.csv: 16 条
[golden_corpus] tests/m207_golden_rx.csv: 16 条
[tap_guard_gate] PASS（15 个 site 工具，0 处违规）
[build_scripts] PASS（3 条判据：GLOB CONFIGURE_DEPENDS / build_bench 无条件配置 / proto_sync 第 9 项）
[env] python=…\Python312\python.exe（来源：默认候选顺序（TCS_PYBIN=未设置））
[env] CJSON_DIR=/d/Espressif/frameworks/esp-idf-v5.5.5/components/json/cJSON（来源：默认值）
[env] GCC=…（本机无 gcc，走 MSVC 分支，该行不打印）
```

`[tap_guard]` 成功时不打 `[tap_guard] PASS`（与其它 `run` 不同，它是纯 Python 自检），它自己的输出是
`test_tap_guard: PASS=93 FAIL=0`。

### 7.2 本批次新加 / 加固的门禁清单（名字 + 它防的是什么）

| 门禁 | 位置 | 防什么 |
|---|---|---|
| `[tautology_selftest]` / `[tautology]` | `tests/check_tautology.py` | 恒真断言（`\|\| 1`、`CHECK(1,`、无符号/枚举值 `>= 0`、`for(;*p;)` 体内 `\|\| *p == '\0'` 死项）——防"假绿"，比缺测试更危险 |
| `[assert_count]` | `tests/assert_count.py` + `tests/baseline/assert_counts.tsv` | 删断言 / 缩小循环次数 / 抽掉语料造成的静默减配（按"执行了多少条断言/多少例样本"比基线） |
| `[golden_corpus]` | `run_all.sh`（内联） | 两个 golden CSV 被抽空/减条后"逐条比对 0 条也全绿" |
| `[vendor_sha]`（加固） | `run_all.sh` | vendor 清单被删空也 PASS；PASS 行不再写死 `(5 files)` |
| `[card_paths]`（加固） | `run_all.sh` | 研学卡被移走后 `0 checked, 0 bad` 仍 PASS |
| `[rtt_count]`（改写） | `run_all.sh` | 硬编码 `==16` 既能被"改数字"绕过、又挡住正当新增；改成"6 条必需锚点在 + 总数 ∈ [16,24]" |
| `[env]`（新增） | `run_all.sh` | `GCC` / `TCS_PYBIN` / `IDF_CJSON` 被环境变量覆盖时静默生效 |
| `[tap_guard]` | `tests/test_tap_guard.py` | 禁区逻辑被改坏、矩形表被挪走后，模式页的点击会打到执行器按钮（D7 事故） |
| `[tap_guard_gate]` | `tests/check_site_tap_guard.py` | `tools/site` 下再出现第二份写 `g_tap_inject` 的代码或第二份矩形表 |
| `[build_scripts]` | `tests/check_build_scripts.py` | GLOB 丢 `CONFIGURE_DEPENDS` / 台架档回到"目录不存在才配置" / `proto_sync` 第 9 项被删（305856d 台架档构建失败那一类） |
| `tools/s_gate.sh` | 脚本本身 | 集成门禁只存在于协调者 `%TEMP%` 里、换台机器就没有 |

## 8. 风险与需要人看的点

1. **断言数基线是"防倒退"而不是"防松"**：`assert_count` 用执行次数做口径，一个 `for` 循环里
   重复执行的 `CHECK` 会计很多次（touchsession 10497、ws_json 1060）。如果有人把循环次数从 10000 改到 100，
   门禁会红；但如果他把断言写得更松，门禁不会红——那一层由 `check_tautology.py` 的 4 类模式兜。
2. **`rtt_count` 的 6 条锚点是我选的"排障信息源"**：`FAULT CFSR=`（硬故障寄存器）、`ltdc init rv=`、
   `ft5446 ver=`、`i2c4 up pclk4=`、`eeprom online magic=`、`[POST] done pass=`。如果有人有理由删其中一条，
   得同时改门禁并说明——这是有意设的门槛。
3. **执行器矩形表来自一张截图**：模式页若改版（按钮位置/尺寸变化），禁区就会失效而门禁仍绿
   （`test_tap_guard` 断言的是表内一致性，不是表与页面的一致性）。真机上第一次跑模式页工具前，
   请先"进去、截图、返回"，用新截图核一遍矩形。
