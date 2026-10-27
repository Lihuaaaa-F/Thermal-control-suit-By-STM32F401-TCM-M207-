# W_终审 复验报告（§1f 缺口回填 → 重终审；独立上下文）

**结论：重终审不通过（残余 must-fix 4 项：R-1..R-4；均为门禁/证据级，未发现功能或安全缺陷）。**
上一轮 MF-2/MF-3/MF-5/MF-6 已清零；MF-4 主体在案；**MF-1/MF-8/MF-9 部分未清、MF-7 未清**。按册 §1f"must-fix 清零才放行"，本册当前仍不得记 `v13 线终审 PASS`。

| 项 | 内容 |
|---|---|
| 审计对象 | `docs/调温服任务书_v13.4_官方库对齐修复专册.md` + `docs/results/W_终审_符合性矩阵.md`（v3 内容）+ 上轮 `W_终审_报告.md` §8 回填记录 + 全部 W 报告/证据 |
| 代码基线 | 分支 `feature/lvgl-port`，HEAD **`3e695b0`**（2026-10-27 17:01，回填 commit）；G 线未提交改动不属对象 |
| 干净树 | `git -c core.autocrlf=true clone --depth 1 --branch feature/lvgl-port file:///d/Espressif/ws/tcs /tmp/tcs_crlf_check` → HEAD `3e695b0` |
| 审计环境 | Windows + Git Bash；MinGW gcc **8.1.0**（`/c/Program Files/mingw64/bin`）；GNU grep 3.0；Python 3.12；arm-none-eabi 13.2.1；**最终口径未使用 cmd/bat/vcvars** |
| 审计方法 | MF-1..MF-9 逐项亲手复跑；7 行研学卡基线逐条重数；三卡 ③ 抽验 + GPIO 9 处行号语义配对；§1c 18 条整批重跑；干净克隆 CRLF 实测 |
| 声明 | 全程只读生产代码；G 线 modified/untracked 文件零触碰；本会话唯一新增文件=本报告 |

---

## 0 MF 垂验总表（9 项）

| MF | 判定 | 一句话证据 |
|---|---|---|
| MF-1 | **△ 部分清零** | `[card_surface] PASS (7 cards)`、7 基线全中、GPIO 9 处全对；但 PWR 卡 #6/#7 行号 151/336 只在 43e5f4b 成立（HEAD 实为 226/439），I2C/GPIO 卡 main.c 行号为 G 线工作区快照（非 HEAD），三卡 ③ 文字/页脚仍写 5/53/40（实 10/54/42）→ **R-3** |
| MF-2 | **✅ 清零** | 附录 B 命令亲数 **41**；表数据行 **44**；#42-44（ft5446.c:345/369/389）逐行可点开；44≥41 成立。尾注仍写 "40/41"（→ R-4 一致性） |
| MF-3 | **✅ 清零** | 实跑 `[fmt_gate] PASS (%f=0 %l=0)`；业务面 `SEGGER_RTT_printf` 的 %f/%l/%i 均 0；W4.2 表 "l/h 显式过滤（printf.c:349-360）" 在案。注：ft5446.c 的 `%lu` 仅存在于 snprintf 预格式化/注释（合规） |
| MF-4 | **✅ 主体 / △ 数字** | 矩阵 A+ 节 M-01..M-29 全 29 行、C+ 节 17 行、F 矩阵 F1-F16 在；抽查 6 行证据可点开。但矩阵内部多处数字未随回填更新（→ R-4） |
| MF-5 | **✅ 清零** | `docs/ref/esp_lcd_touch_ft5x06/` 实存（官方 1.1.1，Apache-2.0 license.txt）；W1a 报告 §10（W1f 三模式+数字）/§11（材料补录）在；手册页码/DS 截图=用户/外部件明列挂账 |
| MF-6 | **✅ 清零** | 台账 `docs/台账.md` L212（W6/F8 移交）、L213（W7 F9-F13 固化）、L196（W9 关单）均在 |
| MF-7 | **❌ 未清** | `run_all.sh` 三变量剥离的是 `$'\n'` 而非 `$'\r'`；干净克隆（autocrlf=true）实测 **5 FAIL / run_all HAS FAILURES** → **R-1** |
| MF-8 | **△ 部分清零** | `comm_txq_push_front` 0 真定义/调用（仅注释提及），双端 comm.c 仅 W7 注释差、comm.h 仅 MAX_TXQ 16/8；但 `ft5446.c` 头注仍称"软件 I2C bit-bang"（L2/L4），回填 commit 只改了 L131-139 格式串 → **R-2** |
| MF-9 | **△ 部分清零** | W3-A §1 行号 153/184/279/308/592/601 与 HEAD main.c 逐一相符；§1 复跑命令可粘贴（43 行/15 调用点）；终裁卡 W1a=关单✓；但 W9 报告仍 12/13/14 三口径、§6 仍为散文/占位符、矩阵/复审表多处数字陈旧 → **R-4** |

---

## 1 MF-1 垂验：run_all + 7 行基线 + 三卡

### 1.1 run_all 实跑（MinGW gcc 口径）

```bash
$ cd /d/Espressif/ws/tcs/project_tcs
$ PATH="/c/Program Files/mingw64/bin:$PATH" bash tests/run_all.sh; echo $?
0
```

关键行（全文见附录 A.1）：

```text
[m207] PASS  [m207golden] PASS  [registry] PASS  [control] PASS  [post] PASS
[proto_comm] PASS  [touchsession] PASS  [qr] PASS  [tmp117] PASS
[font_audit] PASS — 207 个字符串字面量全部字形在库（码表 265 字）
[rtt_count] PASS (16)
report: …官方库审计_机器报告.md (143 files, 0 uncovered, 0 unclassified)
[card_paths] 70 checked, 0 bad
[vendor_sha] PASS (5 files)
[fmt_gate] PASS (%f=0 %l=0)
[card_surface] PASS (7 cards)
===================
run_all: ALL GREEN
```

**满足**"须见 `[card_surface] PASS (7 cards)` 且 ALL GREEN"。

### 1.2 7 行基线逐条亲数（`docs/ref/研学卡_行数基线.txt`）

cwd=`project_tcs`，命令=基线文件口径 `grep -rnE <模式> App Core/Src --include=*.c | wc -l`：

| 卡 | 模式 | 基线 | 实测 | 判定 |
|---|---|---|---|---|
| HAL_I2C | `HAL_I2C_` | 10 | **10** | OK |
| HAL_UART | `HAL_UART_` | 37 | **37** | OK |
| HAL_GPIO_EXTI | `HAL_GPIO_\|HAL_EXTI_` | 54 | **54** | OK |
| HAL_LTDC | `HAL_LTDC_` | 6 | **6** | OK |
| HAL_DMA2D | `DMA2D_` | 1 | **1** | OK |
| HAL_PWR_RCC | `HAL_RCC_\|HAL_PWR_` | 42 | **42** | OK |
| HAL_IWDG | `IWDG` | 28 | **28** | OK |

7/7 与基线一致，`[card_surface]` 常驻门禁成立。

### 1.3 三卡 ③ 表行数 == 卡内自报 == 基线

| 卡 | ③ 表数据行 | 卡头自报 | 基线 | 判定 | 卡内 ③ 正文/页脚 |
|---|---|---|---|---|---|
| HAL_I2C | **10** | 10 行 == 10 命中 | 10 | ✅ | 仍写 "→5 命中/下表 5 行"、"5/5+2 补录行"（陈旧） |
| HAL_GPIO_EXTI | **54** | 54 行 == 54 命中 | 54 | ✅ | 仍写 "→53 命中/下表 53 行"、"53/53"（陈旧） |
| HAL_PWR_RCC | **42** | 42 行 == 42 命中 | 42 | ✅ | 仍写 "→40 命中/下表 40 行"、"40/40"（陈旧） |

### 1.4 GPIO 卡 9 处 ft5446 行号语义配对（逐一开源码核）

| # | 卡内行 | 源码实际内容 | 语义配对 |
|---|---|---|---|
| 6 | `ft5446.c:61` | `#define CT_IIC_SCL(x) HAL_GPIO_WritePin(...)` | ✅ SCL 宏封装 |
| 7 | `ft5446.c:62` | `#define CT_IIC_SDA(x) HAL_GPIO_WritePin(...)` | ✅ SDA 宏封装 |
| 8 | `ft5446.c:63` | `#define CT_READ_SDA HAL_GPIO_ReadPin(...)` | ✅ SDA 回读宏 |
| 9 | `ft5446.c:71` | `#define FT_RST(x) HAL_GPIO_WritePin(...)` | ✅ 复位宏 |
| 10 | `ft5446.c:233` | `HAL_GPIO_Init(CT_IIC_SCL_PORT, &gi)` | ✅ SCL 推挽初始化 |
| 11 | `ft5446.c:238` | `HAL_GPIO_Init(CT_IIC_SDA_PORT, &gi)` | ✅ SDA 开漏初始化 |
| 12 | `ft5446.c:449` | `HAL_GPIO_Init(FT_RST_PORT, &gi)` | ✅ RST 推挽初始化 |
| 13 | `ft5446.c:456` | `HAL_GPIO_Init(FT_INT_PORT, &gi)` | ✅ INT 输入初始化 |
| 54 | `ft5446.c:115` | `HAL_GPIO_Init(GPIOD, &gi)`（PD12/13 AF4_OD） | ✅ W1a I2C4 引脚 |

**GPIO 卡 9/9 配对正确**（此前级联事故已纠正）。

### 1.5 但（残留）

1. **PWR 卡 #6/#7 行号陈旧**：卡内 `ft5446.c:151`/`:336` 在基线 `43e5f4b` 确为 `__HAL_RCC_GPIOD_CLK_ENABLE()`/`__HAL_RCC_GPIOH_CLK_ENABLE()`；W1a（ec93db0）插入 i2c4 块后，当前 HEAD 实为 **`:226`/`:439`**。卡头仍写"基线=HEAD 43e5f4b"，与终审基线 HEAD 3e695b0 不一致。
2. **I2C 卡 #4/#5 行号依赖 G 线工作区**：`main.c:440/466` 只在当前工作区成立（G 线 main.c 为 M 态）；HEAD 3e695b0 实为 **`:437`/`:463`**。
3. **GPIO 卡 main.c 8 行为工作区快照**：卡内 374/375/380/392/451/476/488/634；HEAD 实为 **371/372/377/389/448/473/485/622**（未声明为 G 线快照）。
4. **三卡 ③ 正文与页脚未更新**：3 处仍写 5/53/40，与卡头 10/54/42 矛盾；`card_surface` 只数命中行数，不校验"文件:行是否点开且命中 API token"，故未拦住上述行号漂移。

→ 计入 **R-3**。

---

## 2 MF-2 垂验：API 复审表 #42-44

```bash
$ grep -rnE 'HAL_UART_Receive_IT[ ]*\(|HAL_UART_Transmit[ ]*\(|HAL_I2C_Mem_Read[ ]*\(|httpd_ws_send_frame_async[ ]*\(|nvs_get_blob[ ]*\(|nvs_set_blob[ ]*\(|nvs_commit[ ]*\(|xTaskCreatePinnedToCore[ ]*\(|esp_task_wdt_add[ ]*\(|esp_task_wdt_reset[ ]*\(|ledc_channel_config[ ]*\(|ledc_set_duty[ ]*\(|ledc_update_duty[ ]*\(|esp_wifi_set_config[ ]*\(|uart_driver_install[ ]*\(|IWDG1->KR[ ]*=' project_tcs/App project_tcs/Core/Src esp32/components esp32/main --include=*.c | wc -l
41
$ awk '/^\| [0-9]+ \|/{n++} END{print n}' docs/ref/API复审表.md
44
```

| # | 文件:行 | 实际源码 | 判定 |
|---|---|---|---|
| 42 | `ft5446.c:345` | `return (HAL_I2C_Mem_Write(&hi2c4, FT5446_CMD_WR, reg, …` | ✅ |
| 43 | `ft5446.c:369` | `if (HAL_I2C_Mem_Read(&hi2c4, FT5446_CMD_WR, reg, …` | ✅ |
| 44 | `ft5446.c:389` | `return (HAL_I2C_IsDeviceReady(&hi2c4, (uint16_t)wr_cmd, 1, …` | ✅ |

**核心验收成立**：基线命中 41（不是 44；增量=W1a 的 ft5446.c:369），表 44≥41，三新行逐行可点开。
**残留**：附录 B 尾注仍写 "→40（2026-10-26 实测）；本表数据行 41≥40"，与表头 "41/44" 不一致；表头"实测命中 41（W1a 后含 ft5446 三新调用点）"措辞不严谨（附录 B 模式只含 Mem_Read 一个新点，另两新点由表行 #42/#44 覆盖）。→ R-4。

---

## 3 MF-3 垂验：fmt_gate 与 %lu

```bash
$ grep -rn "SEGGER_RTT_printf" project_tcs/App project_tcs/Core --include=*.c | grep -v "App/rtt/" | grep -cE "%[0-9.]*[efg]"
0
$ … | grep -cE "%[0-9.]*l[a-z]"
0
$ … | grep -cE "%[0-9.]*i"
0
# run_all 实跑：
[fmt_gate] PASS (%f=0 %l=0)
```

- `ft5446.c` 中 `%lu` 仅 2 处：L536 注释、L539 `snprintf` 格式串（F16 预格式化路径，snprintf 合法支持 `%lu`）；**SEGGER_RTT_printf 面 = 0**。
- W4.2 行为差异表（`docs/results/W4_2_完成.md:29`）在案：**"l/h 长度修饰符被显式过滤（printf.c:349-360 `sFormat++`）——%lu 按 %u 工作"**。

**MF-3 清零**。若按"整文件 grep `%lu`"字面口径会命中 2 处（注释+snprintf），属口径误解，非门禁破损。

---

## 4 MF-7 垂验：vendor_sha 的 CR 剥离（未清）

### 4.1 源码事实

`project_tcs/tests/run_all.sh` L186-191（HEAD 3e695b0 提交字节）：

```bash
while read -r h lf name; do
    h=${h%$'
'}; lf=${lf%$'
'}; name=${name%$'
'}
```

`od -c` 确认引号内是 **0x0A（LF）**：三变量剥离的是 `\n`，而 `read` 已消费行尾 `\n`，`\r` 原样留在 `name` 尾部。
（回填 §8 写的是 `${name%$'\r'}`；推测作者写了字面 CR，被 core.autocrlf 在提交时归一成 LF，修复因此失效。）

### 4.2 模拟 + 干净克隆实测

```bash
# 构造 CRLF 基线副本
$ sed 's/$/\r/' App/rtt/vendor/VENDOR_SHA256.txt > /tmp/vsha_crlf.txt
$ bash /tmp/vsha_exact.sh      # 原样抽取 run_all L186-199
sha256sum: 'App/rtt/vendor/SEGGER_RTT.c'$'\r': No such file or directory
… 5 个文件全 FAIL

# 真实干净克隆（Git for Windows 全局 core.autocrlf=true）
$ git -c core.autocrlf=true clone --depth 1 --branch feature/lvgl-port file:///d/Espressif/ws/tcs /tmp/tcs_crlf_check
$ file project_tcs/App/rtt/vendor/VENDOR_SHA256.txt
… with CRLF line terminators
$ PATH="/c/Program Files/mingw64/bin:$PATH" bash tests/run_all.sh; echo $?
1
[vendor_sha] FAIL: SEGGER_RTT.c  != 51f8969b…
[vendor_sha] FAIL: SEGGER_RTT.h  != fe21e961…
[vendor_sha] FAIL: SEGGER_RTT_printf.c  != 3a4f1f39…
[vendor_sha] FAIL: SEGGER_RTT_ConfDefaults.h  != f2f1e3a6…
[vendor_sha] FAIL: LICENSE.md  != ec948bcb…
[fmt_gate] PASS (%f=0 %l=0)
[card_surface] PASS (7 cards)
run_all: HAS FAILURES
```

### 4.3 反证：CR 剥离后即可绿

```bash
$ bash /tmp/vsha_tr2.sh   # 仅加 tr -d '\r'
# CRLF 克隆： [vendor_sha] PASS (5 files)
# LF 工作区： [vendor_sha] PASS (5 files)
```

**判定：MF-7 未清。** 影响：用户终裁卡抽验 2"仓库根跑 run_all"在 fresh clone/默认 autocrlf 环境必红；本轮全绿仅因本工作区的 `VENDOR_SHA256.txt` 恰为 LF（vendor .c 为 CRLF，首列哈希按 CRLF 记录，故哈希值本身无误，唯独文件名尾部 CR 致 sha256sum 打不开）。→ **R-1**。

---

## 5 MF-8 垂验：push_front 与 ft5446 头注

```bash
$ grep -rn "comm_txq_push_front" project_tcs esp32 --include=*.c --include=*.h
# （0 行，exit=1）
$ grep -rn "push_front" project_tcs esp32 --include=*.c --include=*.h
project_tcs/App/comm/comm.c:104:  * （push_front 已按 W3-A/终审 MF-8 删除）；故此处 head 无竞态。*/
project_tcs/App/comm/comm.h:67:    volatile uint8_t tx_head;   /* 消费索引（主循环 comm_poll_tx / push_front）*/
esp32/components/tcs_link/comm.h:67: 同
esp32/components/tcs_link/comm.c:105: 同
```

- **0 真定义/调用**，仅 4 处历史注释（允许）。
- 双端 `comm.c` diff：仅 esp32 侧多 1 行 W7 官方查证注释；`comm.h` diff：仅 `MAX_TXQ 16(H743)/8(ESP32)`。同源维持。

**但**：`project_tcs/Core/Src/ft5446.c` L1-7 头注仍为：

```text
 * ft5446.c — FT5446/FT5206 触摸驱动实现（M3，软件 I2C bit-bang）
 * 整体重写为软件 bit-bang，抄官方 LVGL 例程 ctiic.c + ft5206.c 原码：
```

与 L73-139 的默认 HAL I2C4 实现矛盾；回填 commit `3e695b0` 对该文件只改了 L131-139 的 RTT 格式串（`%lu/%08lx`→`%u/%08x`），**头注未动**，与回填记录"文件头改 HAL I2C4 传输层+位bang 保留域"不符。
另：`docs/官方库对照清单.md:23`（F7 行）仍写"急停帧 txq_push_front 队头插队"，与删除后的实现不符。

**判定：push_front 死码清零；头注/清单未清零。** → **R-2**。

---

## 6 MF-9 垂验：W3-A / W9 / 终裁卡

### 6.1 W3-A §1 行号与 HEAD main.c 对照（3 处抽查 + 全 6 处）

| §1 表 | 声明 | HEAD main.c 实际 | 判定 |
|---|---|---|---|
| H1a comm_txq_push | main.c:153 | `153: if (fn) comm_txq_push(&g_link, frame, fn);` | ✅ |
| H1b/H1c/H1d | 184/279/308 | 184/279/308 同型 `comm_txq_push` | ✅ |
| H2 comm_poll_tx | main.c:592 | `592: comm_poll_tx(&g_link);` | ✅ |
| H3 comm_tick | main.c:601 | `601: comm_tick(&g_link);` | ✅ |

（工作区因 G 线 main.c 修改整体 +3 行：156/187/282/311/599/608；报告已明示锚定 HEAD。）

### 6.2 §1 复跑命令可粘贴

```bash
$ grep -rnE 'comm_send\(|comm_txq_push|comm_poll_tx\(|comm_tick|link_send_frame_mt|link_set_param|link_send_cmd|link_request_registry' project_tcs esp32 --include=*.c | grep -v build | wc -l
43
```

调用点级= **15**（H743 6 + ESP32 6 + LPBK 3），与 §1 表一致；43 行中其余为定义/注释/调用包装。

### 6.3 §6 命令

`W3_txq_评估.md` §6 四行仍是叙述/占位符：

```text
$ 调用点级对账（权威口径=红队机器复跑，勿手写计数）：…
$ diff 双端 comm.c → 仅 W7 注释 1 行
$ grep MAX_TXQ 双端 comm.h → 16 / 8（§3 定差异在案）
$ grep -rn comm_txq_push_front 全库 → 0 真调用…
```

"双端/全库"为占位词，不能原样粘贴执行；可执行的是 §1 第 7 行的 -E 命令（已实测 43 行）。**MF-9 的"§6 命令原样粘贴可跑"未满足**（→ R-4）。

### 6.4 W9 报告 14 行口径

| 位置 | 原文 | 实际 |
|---|---|---|
| `W9_完成.md:9`（§1） | "14 组件行（原写 12，实为 14）" | 14 ✅ |
| `W9_完成.md:26`（§3 DoD） | "docs/ref/依赖版本清单.md **12 行逐行 / 12/12**" | 14 ❌ |
| `W9_完成.md:73`（§8） | "清单文件存在+**13 行**六要素" | 14 ❌ |

依赖清单实测 14 数据行（清单 L7-20）、manifest `^3.0.0`、lock `3.1.1`、`git check-ignore` 无输出（IN-GIT）、双 tree hash 与 HEAD 一致。**正文数字仍 12/13/14 三口径** → R-4。

### 6.5 终裁卡 W1a 状态

`W_终审_用户终裁卡.md:19`：`W1a … 关单✓（红队整改后放行）`。✅ 已改。

### 6.6 W1a map 体积注记

`W1a_完成.md:34`：`text →164520（增量 +912~+992B 视构建环境；干净树全量复测 +992B=终审 F-3）`。✅ 已补环境差；
但矩阵 `W1a-2`（L46）仍写 `text +920B`（→ R-4）。

**MF-9 判定：行号/§1 命令/终裁卡已清；§6 与 W9 数字未清 → 部分。**

---

## 7 MF-4/5/6 垂验

### 7.1 矩阵节存在性与行数

| 节 | 位置 | 行数/内容 | 判定 |
|---|---|---|---|
| A+（M-01..M-29） | `W_终审_符合性矩阵.md:87` | 29 行，M-01..M-29 连续；实际 **17 ✅ / 12 ⏸**（回填 §8 写"21✅/8⏸"，不符） | ✅ 节在 / △ 数字 |
| C+（V/E/P/Q） | 同文件 `:121` | 17 数据行（标题写"14 项"，不符） | ✅ 节在 / △ 数字 |
| F 项矩阵 | 同文件 `:143` | F1-F16 逐项在 | ✅ |

### 7.2 抽查 6 行证据可点开

| 行 | 证据 | 核验 |
|---|---|---|
| M-01 | `docs/ref/esp_lcd_touch_ft5x06/` | ✅ 目录实存：`esp_lcd_touch_ft5x06.c`、`include/`、`license.txt`（Apache-2.0）、`SHA256.txt`、`idf_component.yml`（1.1.1） |
| M-05 | `ft5446.c:108-115` | ✅ L108 注释 AF4_OD/上拉/高速；L109-115 `gi.Pin=PD12|PD13; Mode=AF_OD; Pull=UP; Speed=HIGH; Alternate=AF4_I2C4; HAL_GPIO_Init` |
| M-13 | `W1a_完成.md §10` | ✅ 三模式（轮询/IT/DMA）+数字估算（~0.2ms 事务） |
| M-18 | `docs/台账.md:212` | ✅ "W6（F8 字库移交 G 线）载体登记" |
| M-21 | `API复审表.md #42-44` | ✅ 三行均在 |
| M-23 | 11 组 `*_红队.md` | ✅ `ls docs/results/*_红队.md`=11 |

### 7.3 MF-5 材料

- `docs/ref/esp_lcd_touch_ft5x06/` 实存（上表）；`license.txt` 首部 "Apache License"；`SHA256.txt` 记录 `esp_lcd_touch_ft5x06.c` 哈希与获取方式。
- `W1a_完成.md §10`（W1f 评估）、`§11`（§2.4 材料补录）均在；FT5446 手册页码/DS12110 截图明确标 ⏸ 用户/外部件。

### 7.4 MF-6 台账

`docs/台账.md`：L212 W6/F8 移交、L213 W7 F9-F13 固化、L196 W9 关单。✅

### 7.5 矩阵内部陈旧数字（→ R-4）

| 位置 | 现文 | 应为 |
|---|---|---|
| 矩阵 L24 W10-1 | "v2=41 数据行/基线 40/41" | 44 行/基线 41 |
| 矩阵 L26 W10-3 | "表 #1-41 覆盖" | #1-44 |
| 矩阵 L73 B·W10-表 | "41≥40+41 非空" | 44≥41+44 非空 |
| 矩阵 L46 W1a-2 | "text +920B" | +912~+992B（同 W1a 报告） |
| 矩阵 L3 状态 | "底稿 v2" | v3 |
| 矩阵 L53 B 节头 | "17 条" | 18 行 |
| 矩阵 L121 C+ 头 | "14 项" | 17 行 |
| 报告 §8 MF-4 | "21 项✅/8 项⏸" | 17✅/12⏸ |

---

## 8 §1c 整批重跑（18 条，命令原文 + 输出）

> 环境：HEAD 3e695b0 工作区（G 线不计）；MinGW gcc 8.1.0；GNU grep 3.0；cwd=`project_tcs`（除注明仓库根）。

| # | 门禁 | 命令（原文） | 输出 | 判定 |
|---|---|---|---|---|
| 1 | nm | `arm-none-eabi-nm build/tcs_main.elf \| grep -c ct_iic` | `0` | ✅ |
| 2 | PrintUnformatted | `grep -c cJSON_PrintUnformatted esp32/components/tcs_web/websocket.c`（仓库根） | `0` | ✅ |
| 3 | snprintf | `grep -c snprintf esp32/components/tcs_web/websocket.c`（仓库根） | `0` | ✅ |
| 4 | 转义 | run_all 内 `test_ws_json_escape` | 7 向量全 OK / `[ws_json] PASS` | ✅ |
| 5 | 枚举 | `grep -rnE 'comm_send\(\|comm_txq_push\|comm_poll_tx\(\|comm_tick\|…' project_tcs esp32 --include=*.c \| grep -v build \| wc -l`（仓库根） | `43` 行；调用点级 15（6+6+3） | ✅ |
| 6 | %f | `grep -rn "SEGGER_RTT_printf.*%[0-9.]*f" App Core --include=*.c \| grep -v App/rtt/ \| wc -l` | `0` | ✅ |
| 7 | vendor SHA | run_all `[vendor_sha]`（LF 工作区）；另见 §4 干净克隆 | `PASS (5 files)` / CRLF 克隆 5 FAIL | ⚠️ 工作区绿、干净克隆红（R-1） |
| 8 | 探针 | `echo test > tmp_probe_final.c && python ../esp32/tools/official_lib_audit.py; echo $?` → 删文件再跑 | 有探针 `exit=1`（1 unclassified）；删除后 `exit=0`（0 unclassified） | ✅ |
| 9 | 黄灯 | `grep "黄灯数" docs/evidence/官方库审计_机器报告.md`（仓库根） | `- **黄灯数: 0**（7 文件全带官方查证注释）` | ✅ |
| 10 | 卡数 | `ls docs/ref/研学卡_*.md \| wc -l`（仓库根） | `22` | ✅ |
| 11 | 清单 | 依赖行/ manifest / lock / hash / check-ignore | 14 行；`^3.0.0`；`3.1.1`；`7dca0af2…`/`43a4576d…`；无输出（IN-GIT） | ✅ |
| 12 | 复审表 | 附录 B 命令 `\| wc -l`；表数据行数 | `41`；`44` | ✅ |
| 13 | rtt_count | run_all `[rtt_count]` | `PASS (16)` | ✅ |
| 14 | audit | run_all `official_lib_audit.py` | `143 files, 0 uncovered, 0 unclassified` | ✅ |
| 15 | card_paths | run_all `[card_paths]` | `70 checked, 0 bad` | ✅ |
| 16 | card_surface | run_all `[card_surface]` | `PASS (7 cards)` | ✅ |
| 17 | fmt_gate | run_all `[fmt_gate]` | `PASS (%f=0 %l=0)` | ✅ |
| 18 | run_all | `PATH="…/mingw64/bin:$PATH" bash tests/run_all.sh` | `run_all: ALL GREEN`，exit 0 | ✅（本工作区） |

补充亲数：`HAL_I2C_Mem_=2`、`GPIO_PIN_1[23]=3`（W1-mem 文本门禁=3，按 W1a 红队裁定 nm 符号为真值）；`comm_txq_push_front=0`。

---

## 9 残余 must-fix（清零前不放行）

| # | 级别 | 对应 | 必修内容 | 证据 | 验收 |
|---|---|---|---|---|---|
| **R-1** | 一般（门禁健壮性） | MF-7 未清 | `run_all.sh` L186-191 三变量改剥 `\r`（源码写反斜杠转义 `$'\r'`，勿写字面 CR；或 `name=$(printf '%s' "$name" \| tr -d '\r')`）；建议加"CRLF 基线副本"回归 | §4：HEAD 干净克隆（core.autocrlf=true）`run_all: HAS FAILURES`，5 个 vendor FAIL；加 `tr -d '\r'` 后双环境 PASS | 干净克隆 `bash tests/run_all.sh` → `ALL GREEN`；工作区仍 PASS |
| **R-2** | 微瑕级 must（§1d-2/8） | MF-8 未清 | 改 `ft5446.c` 头注：默认 **HAL I2C4 传输层**，`FT5446_BITBANG` 为保留一版的位bang域；同步 `docs/官方库对照清单.md:23`（F7 行）删除"txq_push_front 队头插队"表述 | §5：L2/L4 仍"软件 I2C bit-bang/整体重写"；3e695b0 diff 仅 L131-139；清单 F7 仍写 push_front | 头注与实现一致；清单 F7 与 W3-A §5 v2-1 一致 |
| **R-3** | 一般（证据行号失配） | MF-1 残余 | 按 HEAD 3e695b0 重锚三卡全部 `文件:行`：PWR #6/#7→226/439；I2C #4/#5→437/463（或注明工作区快照）；GPIO main.c 8 行→371/372/377/389/448/473/485/622；三卡 ③ 正文/页脚改 10/54/42；`card_surface` 增"文件:行可点开且命中 API token"检查 | §1.5：PWR 151/336 实为 226/439；I2C 440/466=工作区；GPIO 374…634=工作区；三卡文字 5/53/40 | 三卡每行 `sed -n '<行>p'` 命中表内 API；③ 文字/页脚=实数；card_surface 仍 PASS |
| **R-4** | 一般（文档/门禁口径） | MF-9 残余 + MF-2/4 一致性 | ① W9 `§3` 12/12→14/14、`§8` 13 行→14 行；② W3-A §6 四行改可执行命令（或明确标"叙述性摘要"），去掉"双端/全库"占位；③ API复审表附录 B 尾注 40/41→41/44；④ 矩阵 W10-1/W10-3/B·W10-表/W1a-2 数字更新；⑤ 矩阵头 v2→v3、B 节头 17→18、C+ 14→17、§8 回填 21✅/8⏸→17✅/12⏸ | §6.3/§6.4/§7.5 | 上述位置数字与 HEAD 一致；§6 命令可原样粘贴；抽查无自相矛盾 |

**说明**：R-1 是唯一会让"新环境一键门禁"变红的项；R-2/R-3/R-4 为证据链/注释一致性，无功能影响。

---

## 10 客观待办（不构成本次不通过的理由）

| 项 | 状态 |
|---|---|
| 板窗批：W1b-g（R2-H743 前置）、W2-5（E-02/E-17×5/E-19）、W4.2-4（probe-rs L3）、W1c/T-12（平均+最坏 perf）、W1d（400k NAK>0.5%/P-7）、CDP24+ | ⏸ 需板/用户同场 |
| §1e 实物走查 | ⏸ 脚本成文，待用户执行 |
| 腿三用户终裁卡（5 项抽验） | ⏸ 待用户；其中抽验 2"仓库根跑 run_all"在 fresh clone 会因 R-1 红，修 R-1 后成立；抽验 5（probe-rs RTT 真机）需板 |
| FT5446 手册 400k 页码截图 | ⏸ 用户既有件（不在仓） |
| H743 DS12110 PD12/13 AF4 截图 | ⏸ ST 外部文档；替代证据（gpio_ex.h 宏+RM0433）在案 |

---

## 11 环境、独立性与未改动声明

- **独立性**：本会话为同模型（`opencode-go-chat-2/deepseek-v4.1-flash`）**全新上下文**，未读取制方过程结论作为依据，所有判定均回到源码/命令输出；本环境无跨模型通道，故为"同模型独立上下文"而非最强跨模型近似（与上轮 §6 同）。
- **未改动**：生产代码零改动；`git status` 仅余 G 线既有 modified/untracked（control.c/CMakeLists/ltdc.*/main.c/ld/lvgl_port 等），本会话零触碰；唯一新增=本报告。
- **工具**：最终 run_all 口径为 MinGW gcc 8.1.0（`PATH` 前置），未触发 MSVC/cl；首跑曾因 PATH 无 gcc 由脚本自动落到 MSVC 分支（vcvars），该次结果已弃用，不计入本报告。干净克隆实测同样使用 gcc 8.1.0。

---

## 附录 A 门禁输出摘录

### A.1 run_all（工作区，HEAD 3e695b0）

```text
[m207] PASS
[m207golden] PASS
[registry] PASS
[control] PASS
[post] PASS
[proto_comm] PASS
[touchsession] PASS
[qr] PASS
[tmp117] PASS
[font_audit] PASS — 207 个字符串字面量全部字形在库（码表 265 字）
[rtt_count] PASS (16)
report: …官方库审计_机器报告.md (143 files, 0 uncovered, 0 unclassified)
[card_paths] 70 checked, 0 bad
[vendor_sha] PASS (5 files)
[fmt_gate] PASS (%f=0 %l=0)
[card_surface] PASS (7 cards)
===================
run_all: ALL GREEN
```

### A.2 干净克隆（autocrlf=true）

```text
[vendor_sha] FAIL: SEGGER_RTT.c  != 51f8969b…
[vendor_sha] FAIL: SEGGER_RTT.h  != fe21e961…
[vendor_sha] FAIL: SEGGER_RTT_printf.c  != 3a4f1f39…
[vendor_sha] FAIL: SEGGER_RTT_ConfDefaults.h  != f2f1e3a6…
[vendor_sha] FAIL: LICENSE.md  != ec948bcb…
[fmt_gate] PASS (%f=0 %l=0)
[card_surface] PASS (7 cards)
run_all: HAS FAILURES        (exit 1)
```

### A.3 三卡 ③ 表行数

```text
HAL_I2C table_rows=10   top=10   baseline=10
HAL_GPIO_EXTI table_rows=54 top=54 baseline=54
HAL_PWR_RCC table_rows=42  top=42  baseline=42
# 但 ③ 正文/页脚：5/53/40（陈旧）
```

### A.4 CR 剥离反证

```text
CRLF 克隆 + 原样 run_all 块 → 5 FAIL
CRLF 克隆 + tr -d '\r'      → [vendor_sha] PASS (5 files)
LF 工作区 + tr -d '\r'      → [vendor_sha] PASS (5 files)
```

---

## 附录 B 关键文件:行索引

| 文件 | 行 | 内容 |
|---|---|---|
| `project_tcs/Core/Src/ft5446.c` | 2/4 | 头注仍"软件 I2C bit-bang"（R-2） |
| `project_tcs/Core/Src/ft5446.c` | 105/106/226/439 | RCC 时钟实际行（PWR 卡 #41/#42/#6/#7 应对照） |
| `project_tcs/Core/Src/ft5446.c` | 115/233/238/449/456/61/62/63/71 | GPIO 卡 9 处 |
| `project_tcs/Core/Src/ft5446.c` | 345/369/389 | API #42/#43/#44 |
| `project_tcs/tests/run_all.sh` | 186-199 | vendor_sha 解析（`$'\n'` 非 `$'\r'`，R-1） |
| `docs/ref/研学卡_HAL_PWR_RCC.md` | 44/45 | #6/#7 行号 151/336（R-3） |
| `docs/ref/研学卡_HAL_I2C.md` | 30/40/41/59 | 5 命中文字、main.c 440/466、5/5+2（R-3） |
| `docs/ref/研学卡_HAL_GPIO_EXTI.md` | 30/82-91/106 | 53 命中文字、main.c 行号、53/53（R-3） |
| `docs/ref/API复审表.md` | 5/59-61/76 | 表头 41/44、#42-44、附录 B 尾注 40/41（R-4） |
| `docs/results/W9_完成.md` | 9/26/73 | 14 / 12/12 / 13（R-4） |
| `docs/results/W3_txq_评估.md` | 7/65-72 | §1 可执行命令 / §6 散文（R-4） |
| `docs/results/W_终审_符合性矩阵.md` | 3/24/26/46/53/73/121 | v2/41/#1-41/+920B/17 条/41≥40/14 项（R-4） |
| `docs/官方库对照清单.md` | 23 | F7 仍写 txq_push_front（R-2） |
| `docs/台账.md` | 196/212/213 | W9/W6-F8/W7 F9-F13（MF-6 ✅） |
| `docs/results/W1a_完成.md` | 34/86/96 | map +912~+992、§10、§11（MF-5 ✅） |
| `docs/results/W_终审_用户终裁卡.md` | 19 | W1a 关单✓（MF-9 ✅） |

---

**重终审结论：不通过。** 残余 must-fix = R-1（vendor_sha CR 剥离，干净克隆必红）＋ R-2（ft5446 头注/清单）＋ R-3（三卡行号与文字）＋ R-4（W9/W3-A/复审表/矩阵数字口径）。四项清零后按 §1f 重跑本报告 §8 全部门禁（含干净克隆），再记 `v13 线终审 PASS[三腿证据]`。

---

## 9 残余 R-1..R-4 清零记录（制方，2026-10-27 深夜）

| 项 | 动作 | 复跑证据 |
|---|---|---|
| R-1 vendor_sha CR | 重写解析：`tr -d '\r'` 整行剥离+`awk $NF` 取名（原 `$'\r'` 写法被工具层转义破坏，已弃）；**CR 注入模拟**：给基线每行加 \r 后门禁仍 PASS（含恢复） | `[vendor_sha] PASS (5 files)`；CR 模拟命中在案 |
| R-2 头注/清单 | ft5446.c 头注改"HAL I2C4 缺省+位bang 保留域"（L2-4 实核）；清单 F7 行删 push_front 措辞改"已删=MF-8" | head -4 实核；grep push_front 清单=0 实质提及（仅"已删"注） |
| R-3 卡行号/正文 | PWR 卡 ft5446 :151/:336→**:226/:439**（HEAD）；I2C main.c :440/:466→**:437/:463**；GPIO main.c 八处→**371/372/377/389/448/473/485/622**（全部 HEAD 锚定，git show HEAD:main.c 逐一提取）；三卡正文/页脚计数 5/53/40→**10/54/42** | `[card_surface] PASS (7 cards)`；行号逐一对照 HEAD 源 |
| R-4 一致性 | W9 三口径→14/14；W3-A §6 换为 4 条可粘贴命令（-E 版）；API 表附录 B 尾注→41/44；矩阵 W10-1/W10-3/B/W1a-2/v2 标签/B 18 条/C+ 17 项/A+ 17✅/12⏸ 全量更新 | run_all ALL GREEN；bash -n 语法过 |

**清零后全量**：H743 双构建 0 error 0 新警告；run_all ALL GREEN（rtt_count 16/vendor_sha/fmt_gate/card_surface/audit 0 uncovered/card_paths 70-0/ws_json）；ESP32 build PASS。
