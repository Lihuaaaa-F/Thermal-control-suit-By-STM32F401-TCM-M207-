# W_终审 报告（§1f 腿二：独立终审会话；全查非抽查）

**结论：不通过（must-fix 9 项，全部为证据/门禁/文档级 + 1 项死代码卫生；未发现功能/安全缺陷）。**
按 §1f 原文"缺一行=终审失败"及"must-fix 清零才放行"，本册当前不得记 `v13 线终审 PASS`。
三腿中腿三（用户终裁卡）与板窗批为客观待办，**不构成本次不通过的理由**，在 §7 明列。

| 项 | 内容 |
|---|---|
| 审计对象 | `docs/调温服任务书_v13.4_官方库对齐修复专册.md` 全册 + `docs/results/W_终审_符合性矩阵.md`（v2）+ 全部 W 报告/红队/证据 |
| 代码基线 | 分支 `feature/lvgl-port`，HEAD `ec93db0`（=tag w1a）；十包关单 commit 为该 HEAD 祖先；G 线未提交改动不属对象 |
| 干净树构建 | `git archive ec93db0` → `/tmp/tcs_audit`，arm-none-eabi-gcc 13.2.1 + Ninja 全量构建（0 error） |
| 审计环境 | Windows + Git Bash；**未使用 cmd/bat/vcvars**；`grep` 经 rtk 钩子被改写到 ugrep，本报告所有门禁重跑一律显式 `/usr/bin/grep`（GNU grep 3.0）以匹配册内语义 |
| 审计方法 | 矩阵 A 节 42 行逐行核验（证据路径存在性 100% 机器检查 + 内容级复现）；§1c 18 条整批重跑；自枚举 DoD 与矩阵 diff；跨 W 集成缝 7 项（4 项点名 + 3 项新发现）；Q12 独立性声明 |
| 结论去向 | 台账追加终审事件；must-fix 清零后按 §1f 重走受影响 W 的轻量红队复验 + 重终审 |

---

## 0 执行摘要

### 0.1 矩阵核验统计（42 行全查）

| 判定 | 行数 | 行号 |
|---|---|---|
| ✓ 复现（证据存在且内容支撑） | 33 | W0-1..5、W4.1-1/2、W5-1..3、W7-1..3、W10-1/2、W9-1..3、W4.2-1/2/3/5、W2-1..4、W3A-2/3/4、W1a-1/3/4、W8-1 |
| △ 部分支撑/证据失实 | 3 | W4.1-3、W3A-1、W1a-2 |
| ✗ 断言被证伪 | 2 | **W8-2**（三卡 ③ 失配）、**W10-3**（W1 新调用面漏审） |
| ⏸ 客观挂账（板窗/前置） | 3 | W4.2-4、W2-5、W1b-g |
| N/A（合理） | 1 | W3B（NO-GO 关闭） |

### 0.2 §1c 重跑统计（18 条，含终审行；合计 10+5+2+1=18）

- **✓（10 条）**：W1-nm、W2-pu、W2-snprintf、W2-转义、W4-%f、W5-探针、W7-黄灯、W8-卡数、W9-清单、W10-表（数值；逐行对应另见 MF-2）；
- **按字面非绿 / △（5 条）**：W1-mem（HAL 半行 ✓、`GPIO_PIN_1[23]` 半行 3≠0）、W1-TIMINGR（无 `cubemx_timing.json`，变体复算 ✓）、W3-枚举（命令不可执行，重定义后 15=15）、W4-SHA（仓库 ✓ / 干净树必红）、终审行（矩阵 42 行 < 可枚举 DoD；"整批全绿"不成立）；
- **⏸ 挂账（2 条）**：W1-对拍、W1-perf（板）；
- **N/A（1 条）**：W3-金测（NO-GO）。

### 0.3 三个核心新发现

1. **W8-2 "22/22 零失配" 在 HEAD 不成立**（W8 红队复跑时点早于 W1a，其 22/22 当时可能成立；W1a 之后失效）：W1a 改 `ft5446.c` 后，`研学卡_HAL_I2C` ③ 声称 5 行、实 10 命中；`HAL_GPIO_EXTI` 53 vs 实 54；`HAL_PWR_RCC` 40 vs 实 42；GPIO 卡另有 7 处行号因 W1a 插入而指向错误代码行。提交 `38d0692` 声称"补 W1a 调用面 2 行"，实际只改了 ④ 节散文，③ 表未动。
2. **W10 复审表漏审 W1a 新增 API 面**：§7d 第 3 条明文要求复审"HAL_I2C_Mem_Read（W1 新调用面）"，HEAD 上 `ft5446.c:345 Mem_Write / :369 Mem_Read / :389 IsDeviceReady` 三个调用点无对应行；基线 grep 已由 40 涨到 41，与表行数 41 碰巧相等，机器门禁假绿。
3. **F16 "%l 门禁归零" 在 HEAD 已为 1**：`ft5446.c:134` 新日志 `%lu×2+%08lx`（W1a 加入）。功能无损（官方 vendor printf 在 printf.c L349-360 **显式过滤 l/h**，`%lu` 按 `%u` 工作，已实证），但 W4.1-3 的"三条=0/0/0"、W4.2 差异表"无 i/l"、facade 注释"W4 grep 三条门禁"三处表述失实；且 `%f/%l` 门禁从未进入 run_all（只有 rtt_count），回归探测器缺位。

---

## 1 矩阵 A 节逐行核验（42/42）

> 判定口径：✓=证据路径存在且打开核对内容支撑；△=内容部分支撑或数字失实；✗=断言被证伪；⏸=客观挂账；N/A=关闭不欠账。
> "路径存在性"另做 100% 机器检查：矩阵 A/B 引用的全部 `docs/…/project_tcs/…/esp32/…` 路径 → **0 缺失**（脚本见附录 A.2）。

| # | 行 | 判定 | 我复核的方式与结果 |
|---|---|---|---|
| 1 | W0-1 | ✓ | `git log b741ef2 -1` = led_strip v3.1.1 替换 commit；`esp32/main/idf_component.yml:17` `^3.0.0`；`managed_components/.../idf_component.yml:11 version: 3.1.1`；v11.3:151 有 R7 E-26 复跑责任行 |
| 2 | W0-2 | ✓ | v11.3:148 R4 删除清单含 eeprom；`grep -ic "eeprom\|at24" docs/接线表_v1.md`=0；致 R4 两条门禁在台账 W0 关单行 |
| 3 | W0-3 | ✓ | 打开 `docs/evidence/官方库检索/2026-10-26_components_espressif_com.md`：ft5x06 官方 1.1.1 + TMP117 唯一件 GPL-3.0-only，与清单断言一致 |
| 4 | W0-4 | ✓ | `docs/官方库对照清单.md:29` F13 行"考虑过并拒绝"在案 |
| 5 | W0-5 | ✓ | 核销表 4 数据行+机器复核命令节齐 |
| 6 | W4.1-1 | ✓ | tag `w4.1` 存在；diff 显示 4 处 `%f`→`snprintf+WriteString`、2 处 `%lu` 同模式（F16） |
| 7 | W4.1-2 | ✓ | 打开 evidence txt：BEFORE=字面量 `%.1f/%.2f/%lu`、AFTER=真值；F14 4+4、F16 2+2 共 12 条数据行成对 |
| 8 | W4.1-3 | △ | `%f` 全库复扫=0 ✓；但行内"门禁三条=0/0/0"在 HEAD 为 **0/0/1**（`%l` 见 §4 S-1），且三条 grep 均未入 run_all |
| 9 | W5-1 | ✓ | `nightly.sh:115 step audit python ../esp32/tools/official_lib_audit.py`，`step()` 透传 EXIT；run_all 尾部 audit 段在 |
| 10 | W5-2 | ✓ | **亲手重放探针协议**：塞 tmp_probe.c→exit=1 且报告点名 `['project_tcs/tmp_probe.c']`；删→exit=0（见 §2 G13） |
| 11 | W5-3 | ✓ | 清单头 3 行 SOP + `#` 行被脚本忽略（实跑 exit=0 自证） |
| 12 | W7-1 | ✓ | 7 文件 `grep -c "官方查证："` 各=1 |
| 13 | W7-2 | ✓ | audit 脚本 VERIFY_COMMENT_FILES+黄灯节；重跑报告 `黄灯数: 0` |
| 14 | W7-3 | ✓ | 黄灯 0（重跑）；W8 红队修复已落（uart 示例名现为 `uart_async_rxtxtasks_main.c`） |
| 15 | W10-1 | ✓ | 脚本核验：41 个数字行、ID 1..41 连续、出处列 41/41 非空 |
| 16 | W10-2 | ✓ | `iwdg.c` 头注 F15 豁免段逐键/历史证/CMSIS 符号齐 |
| 17 | W10-3 | ✗ | 首批 10 项中第 3 项"W1 新调用面"未覆盖（见 §4 S-6）；基线命中已 41≠表注 40 |
| 18 | W9-1 | ✓ | 脚本核验：14 数据行×6 列全非空（矩阵"14 行"正确；W9 完成报告 §1 的"12 行"、§8 的"13 行"为陈旧数） |
| 19 | W9-2 | ✓ | manifest `^3.0.0` + lock `version: 3.1.1` + 实装 3.1.1；`git check-ignore`→IN-GIT、`ls-files`→TRACKED |
| 20 | W9-3 | ✓ | CHANGELOG 4 项 + 唯一 manifest + CMSIS V1.10.7 + tree hash；HEAD 复算 Drivers/qr 两 hash 与清单逐字一致 |
| 21 | W4.2-1 | ✓ | 五文件双列哈希复算全中（disk=CRLF 列、`tr -d \r`=LF 列）；UPSTREAM/LICENSE 在库 |
| 22 | W4.2-2 | ✓ | Conf：UP=1024/DOWN=0/缓冲 1+1/不覆盖模式（官方默认 NO_BLOCK_SKIP） |
| 23 | W4.2-3 | ✓ | facade 守卫名隔离 + 一参/两参宏壳；`_SEGGER_RTT` 符号在 DTCM |
| 24 | W4.2-4 | ⏸ | 板窗，报告 §6.1 明示 |
| 25 | W4.2-5 | ✓ | 第三任红队报告存在（153 行，纪律核查+7 发现+覆盖重写 R6），结论"整改后放行" |
| 26 | W2-1 | ✓ | `PrintUnformatted`=0、`snprintf`=0；6 点位逐段代码在 `ws_status_json/ws_registry_json/push_pv/fds_send` |
| 27 | W2-2 | ✓ | `cJSON_PrintBuffered` 256/8192/128 三处；reg 代缓存 `g0` 快照+portMUX 换入、锁外重建 |
| 28 | W2-3 | ✓ | `fds_send`→`httpd_queue_work`；直调 async 仅 `ws_on_open`（httpd 上下文）；队列深度 6=`CONFIG_LWIP_UDP_RECVMBOX_SIZE`（报告误写 RECVMDB，红队已记录不立案） |
| 29 | W2-4 | ✓ | run_all `test_ws_json_escape` 7 向量全 OK（真实 IDF cJSON 源编译） |
| 30 | W2-5 | ⏸ | 板窗（E-02/E-17×5/E-19） |
| 31 | W3A-1 | △ | 调用点级 15 行（H743 6+ESP32 6+LPBK 3）与 §1 表一致；**但文档内 grep 命令在 GNU/ugrep 下均不可直接执行**（BRE 未配对括号），且 H743 行号是 G 线工作区快照（main.c:156/187/282/311/599/608），HEAD 实为 153/184/279/308/592/601 |
| 32 | W3A-2 | ✓ | 六操作表与代码一致；push_front 0 真调用坐实（4 命中=定义+注释） |
| 33 | W3A-3 | ✓ | 双端 comm.c 仅 W7 注释差；MAX_TXQ 16(H743)/8(ESP32) 亲验 |
| 34 | W3A-4 | ✓ | NO-GO 两腿判据成立（H743/PC 无 FreeRTOS→金测不可达；零收益全量重认证） |
| 35 | W3B | N/A | NO-GO 前置未满足，不欠账 |
| 36 | W1a-1 | ✓ | **独立复算 TIMINGR**：官方计算器单进程单次调用 120MHz→400k=`0x20E21F33`、100k=`0x70D04648`，与报告逐字一致；vendor 计算器与 docs/ref 副本 SHA256 相同 |
| 37 | W1a-2 | △ | 0 error、新增警告 0 ✓（基线/HEAD 均 6 条 warning 行，同集）；map 体积：干净树复测 163592→164584=**+992B**，报告为 163600→164520=+920B（bss 68512→71160 完全一致）；72B 差异未归因，量级不影响结论 |
| 38 | W1a-3 | ✓ | nm ct_iic=0；`HAL_I2C_Mem_`=2；位bang TU 单编 0 error/0 warning（6.2KB .o） |
| 39 | W1a-4 | ✓ | cubemx 两轮 CLI 日志+AN4235 PDF 在库；红队"偏差可豁免"附条件在报告；但 §2.4 三件非板材料未做（见漏列 M-01..03） |
| 40 | W1b-g | ⏸ | 板窗/前置客观成立；但其中 **W1f 与非板材料不需要板**，挂账理由过宽（见 MF-5） |
| 41 | W8-1 | ✓ | 22 卡、每卡 4 节标题齐（22×4=88）；`card_paths` 70 checked/0 bad |
| 42 | W8-2 | ✗ | 三卡 ③ 失配 + GPIO 7 处行号陈旧 + I2C 卡 main.c 行号仅在工作区成立（§4 S-4） |

---

## 2 §1c 机器门禁整批重跑（18 条；命令原文+输出）

> 环境：干净树 `/tmp/tcs_audit`（ec93db0）；GNU grep 3.0；arm-none-eabi 13.2.1。以下输出为原文摘录。

**G1（W1-nm）**
```bash
$ arm-none-eabi-nm build/tcs_main.elf | grep -c ct_iic
0
```

**G2（W1-mem / 引脚文本）**
```bash
$ grep -c "HAL_I2C_Mem_" Core/Src/ft5446.c
2
$ grep -c "GPIO_PIN_1[23]" Core/Src/ft5446.c
3
```
判定：Mem_ ✓；文本门禁 **3≠0**。W1a 红队 F3 已裁定"nm 符号门禁为真值、文本门禁与 §2.2 条4 保留条款自相矛盾"，裁定合理，但**册 §1c 该行文字至今未改**（MF-5）。

**G3（W1-对拍）**：`docs/evidence/W1/` 仅 AN4235 PDF，无 20 行 A/B CSV → ⏸（W1b 未执行，板）。

**G4（W1-TIMINGR）**
```bash
$ ls docs/evidence/cubemx_timing.json
No such file or directory          # 册内门禁所指证据文件不存在
# 变体复算（官方 i2c_timing_utility.c 零改动，单进程单次调用）：
PCLK4=120000000Hz target=400000Hz timing=0x20E21F33
PCLK4=120000000Hz target=100000Hz timing=0x70D04648
```
判定：按字面 ✗（无 json）；按 W1a 变体 ✓（数值精确复现）。另发现计算器静态累加器副作用：**同进程第二次调用**会得到 `0x60F00C16`（400k），与代码注释"每启动单次调用"约束一致，但该约束仅注释、无机器门禁（潜在风险，见 §4 S-7）。

**G5（W1-perf）**：无 T-12 W1 双组数证据 → ⏸（W1c）。

**G6 / G7（W2）**
```bash
$ grep -c "cJSON_PrintUnformatted" esp32/components/tcs_web/websocket.c
0
$ grep -n "snprintf" esp32/components/tcs_web/websocket.c | wc -l
0
$ grep -n "cJSON_PrintBuffered" esp32/components/tcs_web/websocket.c
131,193,222
```

**G8（W2-转义）**：run_all 内 `test_ws_json_escape: PASS`（7/7 向量 OK）。

**G9（W3-金测）**：N/A（NO-GO）。

**G10（W3-枚举）**
```bash
$ grep -rn "comm_send\(\|comm_txq_push\|..." project_tcs esp32 --include=*.c | grep -v build
/usr/bin/grep: Unmatched ( or \(          # 册内命令原文不可执行
$ grep -rnE "comm_send\(|comm_txq_push|..." ... | wc -l
48                                        # 含定义/注释/二进制匹配
# 调用点级人工分类：H743 6 + ESP32 6 + LPBK 3 = 15 == 表行 15
```

**G11（W4-%f）**
```bash
$ grep -rn "SEGGER_RTT_printf.*%[0-9.]*f" project_tcs --include=*.c | wc -l
0
$ grep -rn "SEGGER_RTT_printf.*%[0-9.]*l[a-z]" project_tcs --include=*.c | wc -l
1     # ft5446.c:134（F16 扩展门禁，HEAD 已非 0；官方 printf 过滤 l，功能无损）
```

**G12（W4-SHA）**
```bash
# 当前工作区：
[vendor_sha] PASS (5 files)               # 双列哈希均亲算核对
# 干净树（git archive ec93db0 → autocrlf=true 检出）：
sha256sum: 'App/rtt/vendor/SEGGER_RTT.c'$'\r': No such file or directory
[vendor_sha] FAIL ×5 → run_all: HAS FAILURES
```
判定：仓库内 ✓；**fresh clone / autocrlf=true 必红**（MF-7）。

**G13（W5-探针）**
```bash
$ echo test > project_tcs/tmp_probe.c && python ../esp32/tools/official_lib_audit.py; echo $?
report: … (135 files, 0 uncovered, 1 unclassified)
1
# 报告正文：- **未归类: 1 ['project_tcs/tmp_probe.c']**
$ rm tmp_probe.c && python …; echo $?
0
```

**G14（W7-黄灯）**：`- **黄灯数: 0** （7 文件全带官方查证注释）`。

**G15（W8-卡数）**
```bash
$ ls docs/ref/研学卡_*.md | wc -l
22
$ 每卡四节标题计数 → 22×4 全在
$ ③ 行数 vs 卡内自报：I2C 5→实 10；GPIO 53→实 54；PWR/RCC 40→实 42   ← 三卡失配
```

**G16（W9-清单）**：14 行×6 列全非空；manifest/lock/实装三方一致；lock IN-GIT/TRACKED；两 tree hash 与 HEAD 一致。

**G17（W10-表）**
```bash
# 表：41 数字行，ID 1..41 连续，出处列 41/41 非空
$ grep -rnE '<附录 B 16 模式>' project_tcs/App project_tcs/Core/Src esp32/components esp32/main --include=*.c | wc -l
41                                        # 册注"40"；增量=ft5446.c:369（W1a）
```

**G18（终审行）**：矩阵 A=42 行 < 自枚举 DoD（见 §3，29 条漏列）；§1c 非全绿（G2/G4/G12）；跨模型不可用（见 §6）→ ✗。

**附加常驻门禁**
```bash
[rtt_count] PASS (16)
report: … (135 files, 0 uncovered, 0 unclassified)
[card_paths] 70 checked, 0 bad
[font_audit] PASS — 207 个字符串字面量全部字形在库（码表 265 字）
[ws_json] PASS
[vendor_sha] FAIL（干净树，见 G12）→ run_all: HAS FAILURES
```

---

## 3 矩阵漏列清单（29 条）

> 枚举依据=任务书原文：各 W 节 DoD + §8 F 矩阵 + §9 排程 + §11 C1-C14 + §1b/§1b.5/§0b/§1e/§1c。
> 性质：**缺失**=矩阵完全无对应行；**合并**=被一行笼统覆盖、未逐条给证据；**类别**=整类未设行。

| # | 册内出处 | DoD 条目 | 矩阵现状 | 性质 |
|---|---|---|---|---|
| M-01 | §2.4 表行 2 | esp_lcd_touch_ft5x06 源码入 `docs/ref/`（只作文本提及，未取源） | 无行 | 缺失 |
| M-02 | §2.4 表行 3 | FT5446 数据手册 400k 合法性页码引用 | 无行 | 缺失 |
| M-03 | §2.4 表行 4 | H743 datasheet DS12110 PD12/13 AF4 截图 | 无行 | 缺失 |
| M-04 | §2.1 表 | 7 个 ct_iic_* 全删 + 上层零改动 | 仅 nm 门禁间接覆盖 | 类别 |
| M-05 | §2.2 条1 | PD12/13 AF4_OD 上拉/高速配置 | 无行（且 §1c 文本门禁反证） | 缺失 |
| M-06 | §2.2 条4 | `FT5446_BITBANG`/`FT5446_I2C_FAST` 双宏保留一版 | 无行 | 缺失 |
| M-07 | §2.3 | 时序账（平均/最坏）+ WCET/预算表更新 | 无行 | 缺失 |
| M-08 | §2.5 W1c | E-01 触摸全链复跑 | 无行、C 节也无 | 缺失 |
| M-09 | §2.5 W1c | P-4 实拍双轨 | 无行 | 缺失 |
| M-10 | §2.5 W1c | touch_session 358 断言 + K4b + 双浸泡 30min 幽灵=0 | 被"回归全量"合并 | 合并 |
| M-11 | §2.5 W1b | 空闲态 TD_STATUS=0x0F 行为一致 | 被合并 | 合并 |
| M-12 | §2.5 W1d | 400k NAK>0.5%/P-7 决策与回退 | 被合并 | 合并 |
| M-13 | §2.5 W1f | 传输模式（轮询/中断/DMA）官方例程评估（**非板依赖**） | 被并入"板窗挂账" | 合并/理由过宽 |
| M-14 | §2.5 W1g | 上电时序量化（板依赖） | 被合并 | 合并 |
| M-15 | §3.3 | CDP 24+ 改前后 JSON 语义等价 diff | 无行 | 缺失 |
| M-16 | §5.2 步2-4 | 历史 RTT 证据链重跑 / `_write` 路径核对 / vendor 整体回退条款 | 无行 | 缺失 |
| M-17 | §6 接点 | nightly 全程实跑（FAIL 聚合层） | 无行（W5 §6 挂账） | 缺失 |
| M-18 | §7/§8 | W6：F8 字库移交 G 线挂账 | 无行、台账无登记 | 缺失 |
| M-19 | §8 | F9-F13 → W7 固化 | 无行、W7 报告零记录 | 缺失 |
| M-20 | §7 尾 | 🟦 集合边界（14+ 文件不在 VERIFY 表）挂账 | 无行（台账 W7 行有） | 缺失 |
| M-21 | §7d #3 | W1 新调用面（ft5446 Mem_Read/Mem_Write/IsDeviceReady） | 无行 | 缺失 |
| M-22 | §8 | F 项逐项覆盖（F2/F4/F6/F7/F8/F9-13/F16 无行；仅 F1/F3/F5/F13/F14/F15 有） | 无 F 矩阵节 | 类别 |
| M-23 | §1b | 每 W 双签 + 发现≥3 + 证伪≥3（11 组，仅 W4.2 有行） | 仅 W4.2-5 | 类别 |
| M-24 | §1b.5 | 最差执行者测试 + DoD 改写建议（每 W） | 无行 | 类别 |
| M-25 | §0b-10 | 纪律核查 1-9（每 W；报告内有，矩阵无行） | 无行 | 类别 |
| M-26 | §11 | C1-C14 自审逐条 | 无行 | 类别 |
| M-27 | §1e | 实物走查执行（A 节无行，仅 D 节待办） | 无行 | 缺失 |
| M-28 | §1c 表头 | "17 条"与实际 18 行（v13.4 加终审行未改数） | 口径矛盾 | 类别 |
| M-29 | §1f 腿一 | 各 W 引用的 V/E/P/Q 项（E-01/E-09/E-10/P-3/P-4/P-7/T-12/CDP24+/Q6/Q11 等；C 节仅 6 项） | 部分 | 缺失 |

**行数对账**（按册内小节/bullet 粒度手工枚举，可复核）：

| 来源 | 可枚举条目 |
|---|---|
| 各 W 节（W0/W1/W2/W3/W4/W5/W7/W8/W9/W10，含子步骤与材料表） | 80 |
| §1c 机器门禁（含终审行） | 18 |
| §8 F 项（F1-F16） | 16 |
| §9 每 W 独立 tag+双签 | 11 |
| §11 自审 C1-C14 | 14 |
| §1e 实物走查 | 1 |
| 各 W 引用的 V/E/P/Q 项（去重） | ≥12 |
| **合计** | **≥152** |

矩阵 A=42 行（B=18、C=6 为门禁/引用项）。即使只按"W 节 80 + §1c 18"最保守口径（98），42 也远小于可枚举数 → §1c 终审行"矩阵行数 ≥ 册内 DoD 枚举数"**不成立**；本次仅列出其中 29 条完全缺失/被合并的条目（不主张穷尽到每条子 bullet）。

---

## 4 跨 W 集成缝（单 W 绿、组合态问题）

### S-1 W4.2 vendor 官方 printf × W4.1 F14/F16 × W1a 新日志（用户点名项①）

- **全库 16 个 `SEGGER_RTT_printf` 调用点格式符逐点核对**（脚本 `%TEMP%\printf_audit.py`，排除 `App/rtt/`）：`%d×8、%u×2、%x×17、%lu×2、%08lx×1`；全部落在官方支持面内（c/d/u/x/X/s/p），**无 %f、无 %i**。
- 官方实现 `vendor/SEGGER_RTT_printf.c` L349-360 **显式过滤 `l`/`h`**，故 `%lu/%08lx` 按 `%u/%x` 工作 → **W1a 的 i2c4 日志功能正常**（我最初据 F16 结论怀疑其损坏，读官方源码后排除，特此记录推理修正）。
- 但：① W4.1 扩展门禁 `%l=0` 在 HEAD=**1**；② W4.2 差异表写"无 i/l"不准确（l 被接受并忽略，i 才是不支持）；③ facade 注释称"门禁：rtt_count + W4 grep 三条"，实际 run_all **只有 rtt_count**，三条 grep 均未入门禁。
- 影响：无功能/安全影响；证据与门禁口径失实（MF-3）。

### S-2 W2 queue_work × W10 #18/#19（用户点名项②）

- 直调 `httpd_ws_send_frame_async` 仅剩两处：`websocket.c:81`（`ws_tx_work_fn`，经 `httpd_queue_work` 投递=**httpd 线程**，合法）与 `websocket.c:295`（`ws_on_open`=handler 上下文，合法）。
- 推送任务 `ws_push_task` 走 `fds_send`→`queue_work`；队列深度 6=`CONFIG_LWIP_UDP_RECVMBOX_SIZE`（sdkconfig:1793，真名 RECVM**BOX**，W2 报告误写 RECVMDB；红队已记录"不另行立案"）。
- 结论：**一致，无缺陷**。W10 表 #18 仍写"偏差→分流 W2"属历史记录，矩阵 W2-3 记闭环，可接受。

### S-3 W1a 计算器聚合包含 × W4.2 RTT 聚合包含（用户点名项③）

- `ft5446.c:94 #include "i2c_timing_utility.c"` 与 `segger_rtt.c:26-27 #include "vendor/SEGGER_RTT.c|_printf.c"` 分属两个 TU，互不相干。
- CMake 源列表只含 `segger_rtt.c`，**不含** `i2c_timing_utility.c`（F11 约束）；ELF 符号唯一：`I2C_GetTiming T`、`_SEGGER_RTT B`、`SEGGER_RTT_printf T` 各一份。
- 结论：**无冲突**。

### S-4 W8 卡行数/行号 × W1a 代码偏移（用户点名项④）

- **不是**已挂账的"G 线五卡"，而是三张 HAL 卡的新失效：
  - `研学卡_HAL_I2C`：③ 自称"→5 命中，下表 5 行"；HEAD 实 10（ft5446 增 5）。提交 `38d0692`"补 W1a 调用面 2 行"只改了 ④ 节文字，**③ 表未增一行**。
  - `研学卡_HAL_GPIO_EXTI`：53 vs 实 54（W1a 新增 `HAL_GPIO_Init`）；且 ft5446.c 行号 60/62/69/158/163/344/351 共 **7 处**指向错误代码行（W1a 插入导致位移）。
  - `研学卡_HAL_PWR_RCC`：40 vs 实 42（`__HAL_RCC_I2C4_CLK_ENABLE`/`__HAL_RCC_GPIOD_CLK_ENABLE`）。
  - 另：I2C 卡 `main.c:440/466` 仅在 **G 线工作区**成立（HEAD 对应行为 `__HAL_RCC_I2C1_CLK_ENABLE`/`}`），说明该卡也隐式依赖工作区快照，却未被列入"G 线五卡"。
- 根因：§7b"③ 与 grep 行数一致（机器查）"**没有常驻机器门禁**；run_all 的 `card_paths` 只查路径存在，不查计数，因此 W1a 后无人发现。
- 结论：矩阵 W8-2 "22/22 零失配" 被证伪（MF-1）。

### S-5 W3-A 证据行号 × 代码状态

- `W3_txq_评估.md` §1 的 H743 六个行号全部是 G 线工作区快照（main.c:156/187/282/311/599/608），HEAD 实为 153/184/279/308/592/601；`comm.c:98` 等 ESP32 行号在 HEAD 一致。
- 影响：调用点覆盖结论不变（仍是 6 个），但"文件:行"证据在审计基线（HEAD）上点不开；且 §6 的复跑命令本身不可执行（BRE 未配对括号）。矩阵 W3A-1 标 ✓ 过宽（△）。

### S-6 W10 复审表 × W1a 新调用面

- 见 0.3-2。基线命中 40→41 的增量正是 `ft5446.c:369 HAL_I2C_Mem_Read`；表 41 行但无该行 → "行数≥命中数"碰巧相等，"逐行对应"不成立。§7d 第 3 条明确点名 W1 新调用面，属 DoD 缺口（MF-2）。

### S-7 W1a 计算器单次调用约束（新发现，潜在）

- 官方 `I2C_GetTiming` 使用静态累加器 `I2c_valid_timing_nbr`；我实测**同进程第二次调用** 400k@120MHz 得 `0x60F00C16`（首调 `0x20E21F33`）。当前代码只在 `ft5446_i2c4_init` 单点调用、`ft5446_init` 仅 main.c:527 一处调用，约束成立；但该约束只写在注释，**无机器门禁**。若未来加入重试/控制台重init，TIMINGR 会静默变形（仍可能是合法 400k，但非官方现算目标值）。建议加"init 只允许一次"的计数器断言或改官方计算器为无状态封装。

---

## 5 其他发现（分级）

| # | 级别 | 发现 | 证据 | 处置 |
|---|---|---|---|---|
| F-1 | 一般 | W9 完成报告内部数字不一致（§1"12 组件行"、§8"13 行"、实际 14 行） | 脚本清点 14×6 全非空 | 文档勘正（wont-fix 可接受，矩阵数正确） |
| F-2 | 一般 | W1a 报告"存量警告 3 条"实际 4 类/6 行（qrcodegen.c:211/299 漏计，W4.2 红队已勘正过一次） | 基线/HEAD 构建日志各 6 条 warning | 文档勘正 |
| F-3 | 一般 | W1a map 体积 +920B vs 干净树复测 +992B（bss 完全一致） | 两次全量构建日志 | 记录即可；建议报告注明构建环境 |
| F-4 | 一般 | 矩阵 B 表头"17 条"实际 18 行；§1f/§11 也写 17 | 矩阵 B 逐行计数 | 册修订（M-28） |
| F-5 | 一般 | W10 完成报告 §6 交叉引用 "#16/#9" 与 v2 表编号不符（#16=iwdg KR、#9=main 初挂） | 表 vs 报告 | 文档勘正 |
| F-6 | 微瑕 | W2 报告把 `CONFIG_LWIP_UDP_RECVMBOX_SIZE` 写成 `RECVMDB`（红队已记录不立案） | sdkconfig:1793 + IDF httpd_main.c | 已在案 |
| F-7 | 微瑕 | 终裁卡 W1a 状态仍为"待红队终裁"（实际已关单） | `W_终审_用户终裁卡.md:19` | 腿三更新 |
| F-8 | 微瑕 | `ft5446.c` 文件头仍写"整体重写为软件 bit-bang，抄 ctiic.c"（W1a 后实现已 HAL 化） | ft5446.c:1-16 | §1d-2/8，随 MF-8 修 |
| F-9 | 微瑕 | W2 完成报告 §3 调用点行号（ws_status_json:166）已随 W2 自身改动漂移到 269 | 代码实读 | 文档勘正 |
| F-10 | 微瑕 | 用户终裁卡抽验 2 要求"仓库根跑 run_all"，在 fresh clone 会因 G12 红 | 干净树实测 | 随 MF-7 修后即成立 |

---

## 6 Q12 独立性声明（§1f 腿二"环境允许换模型"）

- 本会话=**同模型独立上下文**（`opencode-go-chat-2/deepseek-v4.1-flash`），非跨模型。
- 环境**无跨模型通道**：调用模型目录工具返回
  `model_catalog_unavailable: this session cannot list models — the host did not provide a model catalog. This is a capability gap, not an empty configuration.`（原文其余部分为对 workflow `subagent_model` 的提示）
  即宿主未提供可选模型目录，本会话无法切换到不同模型执行本腿，也无法派发异模型子代理（工具集无 Agent/子会话能力）。
- 独立性强度（按 §1f 排序）：机器门禁重跑（本报告 §2，无倾向）＞**本会话（同模型、全新上下文、未读制方过程结论只信代码/证据）**＞制方自检。未达到"跨模型=不同系统性盲区"的最强近似。
- 缓解：本会话所有关键结论均给出可复跑命令/文件行号；矩阵 42 行 100% 开文件核对（超过册要求 20%）；§1c 全量重跑（非抽查）。**同模型同盲区风险如实保留**，请腿三（用户）对 §2 的 G2/G4/G12 三条"字面非绿"做最终确认。

---

## 7 三腿状态与客观待办（不构成不通过理由）

| 腿 | 状态 | 说明 |
|---|---|---|
| 腿一 矩阵 | **未达标**（本次不通过的理由之一） | 42 行 < 可枚举 DoD；29 条漏列；2 行 ✗、3 行 △ |
| 腿二 本会话 | 完成（本报告） | 同模型局限见 §6 |
| 腿三 用户终裁卡 | **待执行**（客观待办） | 卡已成文 5 项抽验；其中第 5 项（probe-rs RTT 真机）需板；第 2 项在 fresh clone 需先修 MF-7 |
| §1e 实物走查 | **待执行**（客观待办） | 脚本七步成文；需板+用户同场 |
| 板窗批 | **待执行**（客观待办） | W1b-g、E-02/E-17×5/E-19、RTT L3 真机对照、W4.2-R3 cache 观察、W2-5、W4.2-4 |

---

## 8 结论与 must-fix（清零前不放行）

**终审结论：不通过。** 未发现功能/安全缺陷；核心十包交付（F14/F16 修复、cJSON 统一、txq NO-GO、RTT vendor、审计门禁、查证注释、API 复审、版本锁定、研学卡主体、ft5446 HAL 化）主体经独立复跑成立。不通过源于**证据链与门禁的若干断言在 HEAD 上不成立**，以及册自身"缺一行=终审失败"的硬规则。

| MF | 级别 | 必修项 | 证据 | 修复动作（建议） | 验收 |
|---|---|---|---|---|---|
| MF-1 | 一般（DoD 假绿） | W8-2 "22/22 零失配" 不成立：HAL_I2C 5→10、GPIO 53→54、PWR/RCC 40→42；GPIO 7 处行号陈旧；I2C 卡 main.c 行号依赖 G 线工作区 | §4 S-4、§2 G15 | 重跑三卡 ③ 并更新行号/表；把"③ 行数==grep 命中"做成 run_all 常驻门禁（脚本化每卡命令）；矩阵 W8-2 改 ✓ 并附新计数 | `bash tests/run_all.sh` 出现 `[card_surface] PASS (22/22)`；三卡自报数=实跑数 |
| MF-2 | 一般（DoD 缺口） | W10 复审表漏 ft5446 三个新调用点（§7d#3） | §4 S-6、§2 G17 | 表增 #42-44（Mem_Read/Mem_Write/IsDeviceReady，含 20ms 超时/DevAddress 8bit/AF 计数语义）；基线注从 40 改 41；补行后 44≥41 门禁成立；矩阵 W10-1/3 改 ✓ 并注明"逐行对应=41 旧行+3 新行" | 表行数≥基线命中且逐行可点开 |
| MF-3 | 一般（门禁/证据失实） | F16 `%l` 门禁 HEAD=1；`%f/%l` 门禁未入 run_all；W4.2 表"无 i/l"不准；facade 注释夸大 | §4 S-1、§2 G11 | 二选一：①`ft5446.c:134` 改 snprintf 预格式化恢复 `%l=0`；②留 `%lu` 但退役 F16 门禁、在 W4.1/W4.2 报告与矩阵写明"官方 printf 过滤 l/h（printf.c:349-360），F16 门禁废止"；并把 `%f` grep 入 run_all | run_all 含 `[fmt_gate]`；%f=0；%l 口径成文 |
| MF-4 | 一般（腿一硬规则） | 矩阵漏列 29 条（§1f"缺一行=终审失败"） | §3 | 按 §3 清单补 A 节行（M-01..M-29）；C 节补 V/E/P/Q；F 矩阵单列 | 行数≥可枚举 DoD；抽查 10 行证据可点开 |
| MF-5 | 一般（DoD 缺口） | W1 §2.4 三件非板材料未做；W1f 评估未做（均不需板）；§1c W1 两门禁定义缺陷（GPIO 文本 3≠0、TIMINGR 无 json） | §2 G2/G4、§4 S-4 | 取 ft5x06 源码入 docs/ref + 手册页码/DS 截图入 evidence；W1f 成文（三模式对照+数字估算）；册 §1c W1 行改写（预处理器后 HAL 域 0 命中；TIMINGR 证据=计算器 SHA+运行时打点/独立复算） | 材料路径存在；W1f 文档在；§1c 命令字面可绿 |
| MF-6 | 一般（DoD 缺口） | W6/F8 移交无载体；W7 F9-F13 固化无记录；W7 🟦 边界挂账未入矩阵 | §3 M-18..20 | 台账补 W6/F8 行（指向 G 线 lv_font_conv 计划）；W7 报告或清单补 F9-F13 逐项"固化=清单行号"；矩阵补行 | 台账/矩阵可检索到 |
| MF-7 | 一般（门禁健壮性） | `vendor_sha` 门禁在 fresh clone/autocrlf=true 必红（文件名带 CR） | §2 G12 | `while read` 内 `name=${name%$'\r'}`（或 `tr -d '\r'` 整行）；重跑干净树 run_all | 干净树 run_all ALL GREEN |
| MF-8 | 微瑕级 must（§1d-3/2） | `comm_txq_push_front` 双端 0 调用死代码；`ft5446.c` 文件头仍称 bit-bang | W3A-2、ft5446.c:1-16 | 删除 push_front（及 comm.h:17 承诺注释）或落正式 wont-fix 红队裁定；改 ft5446.c 头注为"HAL I2C4 传输层+位bang 保留域" | §1d-3 无死代码/有裁定；注释与实现一致 |
| MF-9 | 微瑕级 must（证据时效） | W3-A 行号为工作区快照、命令不可执行；W1a map 数字 +920 vs +992；W9 内部 12/13/14 不一致 | §4 S-5、F-1..F-4 | 重锚 HEAD 行号/改 `grep -rnE`；文档勘正数字 | 命令可直接粘贴复跑；行号可点开 |

**must-fix 清零后的复验路径（建议）**：MF-1/2/3/7 涉及 W8/W10/W4.1/W4.2，按 §1f"缺口回填→对应 W 重走红队→重终审"执行轻量红队（仅对补证部分证伪）；MF-4/5/6/8/9 为文档/册修订，可由同一会话完成并留 diff；全部清零后重跑本报告 §2 全部门禁，再记 `v13 线终审 PASS[三腿证据]`。

**明确不因以下事项判不通过**（客观待办，须在关账时明列）：W1b-g（R2 前置+板窗）、W2-5/W4.2-4（板窗 L3）、E-02/E-17×5/E-19、§1e 实物走查、腿三用户终裁卡、W4.2-R3 cache 观察。

---

## 附录 A 审计复跑环境与命令留档

```text
仓库：/d/Espressif/ws/tcs（feature/lvgl-port，HEAD ec93db0=tag w1a）
干净树：git archive ec93db0 | tar -x -C /tmp/tcs_audit
基线树：git archive ad654e7 | tar -x -C /tmp/tcs_base
工具链：arm-none-eabi-gcc 13.2.1 + Ninja（全量 56 步，0 error）
主机 C：/c/Program Files/mingw64/bin/gcc.exe 8.1.0（仅用于官方计算器独立复算与 run_all）
grep：/usr/bin/grep（GNU 3.0）；注意 rtk 钩子会把裸 grep 改写为 ugrep 7.8.4
构建体积：ad654e7 text=163592 bss=68512；ec93db0 text=164584 bss=71160
```

### A.1 官方计算器独立复算（W1a TIMINGR）

```c
/* /tmp/timing_test/main_single.c：单进程单次调用 */
I2C_GetTiming(120000000, 400000) → 0x20E21F33
I2C_GetTiming(120000000, 100000) → 0x70D04648
/* 同进程二次调用（反例，验证静态累加器）→ 0x60F00C16 */
```

### A.2 路径存在性 100% 机器检查

```python
# 提取矩阵全部 docs/…、project_tcs/…、esp32/… 路径 → os.path.exists
A rows=59（含 B 节 W 开头行）  missing paths=0
```

### A.3 研学卡 ③ 失配明细

```text
HAL_I2C     卡内"→5 命中/5 行"   实 10 命中（+ft5446 Mem_Write/Mem_Read/IsDeviceReady/Init/ConfigAnalogFilter）
HAL_GPIO    卡内"→53 命中/53 行" 实 54 命中（+HAL_GPIO_Init @ft5446 i2c4_init）
HAL_PWR_RCC 卡内"→40 命中/40 行" 实 42 命中（+__HAL_RCC_I2C4/GPIOD_CLK_ENABLE）
GPIO 卡行号失配：ft5446.c 60/62/69/158/163/344/351（7 处）
```

### A.4 官方 printf 格式面（S-1 依据）

```text
vendor/SEGGER_RTT_printf.c L349-360：
  /* Filter out length modifier */
  if ((c == 'l') || (c == 'h')) { sFormat++; ... }
switch: c/d/u/x/X/s/p/%；default: break（不消费 va_arg）
```

---

## 附录 B 本报告未改动生产代码/未触碰 G 线声明

- 全程只读仓库；写入仅本文档（`docs/results/W_终审_报告.md`）与 `/tmp` 临时树（构建、计算器 harness）。
- `git status` 中 G 线既有 modified/untracked 文件（control.c/ltdc.*/main.c/CMakeLists/ld/lvgl_port 等）**零触碰**。
- 未使用 cmd/bat/vcvars；MSVC 路径的 run_all 分支未触发（干净树用 MinGW gcc）。

---

## 8 MF 回填记录（制方，2026-10-27 晚；对应本报告 §5 九项）

| MF | 回填动作（证据路径） | 状态 |
|---|---|---|
| MF-1 | 三卡 ③ 实更：HAL_I2C 5→10（增 W1a 五调用点行）/GPIO 53→54（+GPIOD Init 行，7 处 ft5446 行号重锚 61/62/63/71/233/238/449/456，级联事故已用占位符纠正并复核语义配对）/PWR_RCC 40→42；新增常驻门禁 `[card_surface]`（docs/ref/研学卡_行数基线.txt 7 卡，run_all PASS） | ✅ |
| MF-2 | API复审表增 #42-44（Mem_Write/Mem_Read/IsDeviceReady，含 AF 计数语义与 F1 勘正注记）；基线注 40→41；表 44 行≥41 命中 | ✅ |
| MF-3 | ft5446 i2c4 日志改 %u/%08x 恢复 %l=0；`[fmt_gate]` 入 run_all（%f/%l 双零常驻）；W4.2 表补"l/h 显式过滤（printf.c:349-360）"；facade 注释改指 fmt_gate | ✅ |
| MF-4 | 矩阵 v3：A+ 节 M-01..M-29 逐条（21 项✅/8 项⏸客观）+C+ 节 V/E/P/Q 14 项+F 项 F1-F16 逐项矩阵+册修订建议 6 条 | ✅ |
| MF-5 | docs/ref/esp_lcd_touch_ft5x06/（官方管理器 ==1.1.1，Apache-2.0，SHA256 留档）；W1f 评估成文（W1a 报告 §10，三模式+数字估算）；手册页码/DS 截图=用户/外部件挂账明列 | ✅（两项挂账明列） |
| MF-6 | 台账补 W6/F8 移交行+W7 F9-F13 固化行；矩阵 M-18/19/20 行在案 | ✅ |
| MF-7 | vendor_sha 解析行内去 CR（`${name%$'\r'}` 等三重）；Windows 侧 run_all PASS | ✅（干净树复验请垂验） |
| MF-8 | `comm_txq_push_front` 双端删除（函数+声明+引用注释；双端 diff 仅剩 W7 注释+MAX_TXQ 定差异）；ft5446.c 文件头改"HAL I2C4 传输层+位bang 保留域" | ✅ |
| MF-9 | W3-A 行号重锚 HEAD（153/184/279/308/592/601）+复跑命令改 -E 可粘贴；W1a map 注 +912~+992 环境差；W9 12→14 行；终裁卡 W1a 状态改关单；W2 报告 ws_status_json 行号 269 | ✅ |

**回填后门禁全量**：`run_all ALL GREEN`（rtt_count 16/vendor_sha 5/fmt_gate %f=0 %l=0/card_surface 7 卡/audit 0 uncovered/card_paths 70-0/ws_json 7 向量）；`nm ct_iic=0`、`HAL_I2C_Mem_=2`；H743 双构建 0 error 0 新警告；ESP32 build PASS；双端 comm 同源维持。
