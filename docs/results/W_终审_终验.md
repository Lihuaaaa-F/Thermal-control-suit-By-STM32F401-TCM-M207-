# W_终审 终验报告（§1f 第三轮；独立上下文；只信代码与证据）

**结论：终审通过（v13 线 PASS[腿一矩阵 v3+腿二两轮+腿三待签字]）。R-1..R-4 四项清零；门禁七项 ALL GREEN；客观待办=用户终裁卡/板窗批/W1b-g，均非不通过理由。**

- 审计对象：`docs/调温服任务书_v13.4_官方库对齐修复专册.md` + `docs/results/W_终审_符合性矩阵.md`（v3）+ 上轮 `docs/results/W_终审_复验.md`（含 §9 制方回填）+ 全部 W 报告/证据。
- 代码基线：分支 `feature/lvgl-port`，HEAD **`4b2b843`**（2026-10-27 17:23，R 项清零 commit）；G 线未提交改动不属对象，行号一律以 `git show HEAD:<path>` 取。
- 工具链：Git Bash + mingw64 gcc（`/c/Program Files/mingw64/bin` 前插 PATH）；`run_all.sh` 在无 gcc 时默认回退 MSVC cl 的路径另跑一轮，两轮同绿（§5）。
- 方法：每项先读代码/文档，再实跑可复现命令；凡"回填声称已改"处均以 diff/实测复核，不采信自述。

---

## 1 R-1：vendor_sha 的 CR 剥离（原 MF-7）

### 1.1 代码面（HEAD）

`project_tcs/tests/run_all.sh` L183-202 现为：

```bash
line=$(printf '%s' "$line" | tr -d '\r')      # L189 整行剥离
h=$(printf '%s' "$line" | cut -d' ' -f1)      # L191
name=$(printf '%s' "$line" | awk '{print $NF}')  # L192
```

- `grep -n '\$'"'"'\\r'"'"'' run_all.sh` → 无命中（rc=1）：`$'\r'` 旧写法已彻底移除。
- `grep -c $'\r' run_all.sh` → **0**（脚本内无字面 CR 残迹；L189 为反斜杠转义文本）。
- `bash -n project_tcs/tests/run_all.sh` → **SYNTAX OK**。

### 1.2 CR 注入模拟（亲手执行，双档）

基线 `project_tcs/App/rtt/vendor/VENDOR_SHA256.txt`：md5 `ca92618e950cf0a51f40e0eb7f315fc0`（= `git show HEAD:` 同 blob），11 行 LF、0 CR。

| 步骤 | 命令/动作 | 结果 |
|---|---|---|
| ① 备份 | `cp VENDOR_SHA256.txt /tmp/vsha_orig.bak` | md5 同上 |
| ② 注 CR | `sed -i 's/$/\r/' VENDOR_SHA256.txt` | 11 行全部 CR，`tr -cd '\r'`=**11** |
| ③ 跑该段 | 提取 run_all L183-202 原文为 `/tmp/vseg.sh` 实跑 | `[vendor_sha] PASS (5 files)` |
| ④ 对照组 | 用旧解析（`name=${line##* }`，无 CR 剥离）跑同一 CRLF 文件 | **6 行 FAIL**（5 文件+1 空行），证明注入有效、新代码确为修复源 |
| ⑤ 全量 | CRLF 基线态跑整份 `bash tests/run_all.sh` | EXIT=0，`[vendor_sha] PASS (5 files)`，`run_all: ALL GREEN` |
| ⑥ 恢复 | `cp /tmp/vsha_orig.bak` 回写 | md5 `ca92618e950cf0a51f40e0eb7f315fc0`（未变）、CR=**0**、`git status` 该文件**干净** |

### 1.3 判定

**R-1 清零。** `$'\r'` 反斜杠写法改为整行 `tr -d '\r'`+`awk $NF` 取名；CRLF 注入下该段与全量门禁均 PASS；恢复后文件与 HEAD 逐字节一致（md5 复核）。干净克隆/core.autocrlf=true 的同型输入已被覆盖。

---

## 2 R-2：ft5446.c 头注 / 官方库对照清单（原 MF-8）

### 2.1 ft5446.c 头注（HEAD，L1-6）

```
/**
 * ft5446.c — FT5446/FT5206 触摸驱动实现（M3；**W1a 起传输层=ST HAL I2C4 缺省**，
 * 软件 bit-bang 旧实现按册 §2.2 条4 以 FT5446_BITBANG 宏保留一个版本期）。
 * 寄存器序列抄官方 LVGL 例程 ft5206.c 原码；位bang 域抄 ctiic.c：
 *   - SCL=PD12 推挽、SDA=PD13 开漏+上拉
 *   - ct_iic_start/stop/wait_ack/ack/nack/send_byte/read_byte（抄原码）
```

与实现一致：L81 `/* #define FT5446_BITBANG */`（默认关闭）、L84 `#ifndef FT5446_BITBANG` 起为 HAL I2C4 实现域（L345 Mem_Write / L369 Mem_Read / L389 IsDeviceReady），位bang 段全部包在 `#ifdef FT5446_BITBANG` 内（L147/L216/L332/L354/L381/L401/L440/L458）。头注"缺省 HAL + 位bang 保留域"表述成立。

### 2.2 官方库对照清单

- `grep -c push_front docs/官方库对照清单.md` → **1**；唯一命中 L23："急停=常规尾插（**push_front 已删=W3-A/MF-8**）"——仅为"已删"注。
- `grep -n "队头插队" docs/官方库对照清单.md` → **0 命中**（无功能描述）。
- 交叉核对 `comm.c`（HEAD）L8："急停=常规尾插（队头插队函数已按 W3-A/终审 MF-8 删除：双端 0 调用死码）"；W3-A §6 第 4 命令 `grep comm_txq_push_front ... | wc -l` → **0**。

### 2.3 判定

**R-2 清零。** 头注与 HAL 缺省实现一致；清单仅留"已删"注，无"队头插队"功能描述。

---

## 3 R-3：三张研学卡的行号与计数（原 MF-1 残余）

### 3.1 10 处 `文件:行` 逐行对 HEAD（`git show HEAD:project_tcs/Core/Src/main.c | sed -n '<N>p'`）

| 卡 | 引用 | HEAD 该行实文 | 命中 |
|---|---|---|---|
| I2C #4 | main.c:437 | `void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)` | ✓ |
| I2C #5 | main.c:463 | `if (HAL_I2C_Init(&hi2c_tmp117) != HAL_OK) Error_Handler();` | ✓ |
| GPIO #46 | main.c:371 | `HAL_GPIO_Init(GPIOB, &gi);` | ✓ |
| GPIO #47 | main.c:372 | `HAL_GPIO_WritePin(GPIOB, LED1_Pin \| LED2_Pin, GPIO_PIN_SET);` | ✓ |
| GPIO #48 | main.c:377 | `HAL_GPIO_Init(GPIOA, &gi);` | ✓ |
| GPIO #49 | main.c:389 | `HAL_GPIO_Init(port, &gi);` | ✓ |
| GPIO #50 | main.c:448 | `HAL_GPIO_Init(GPIOB, &gi);` | ✓ |
| GPIO #51 | main.c:473 | `HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);` | ✓ |
| GPIO #52 | main.c:485 | `uint8_t now = (HAL_GPIO_ReadPin(KILL_KEY_GPIO_Port, KILL_KEY_Pin) == GPIO_PIN_SET) ? 1 : 0;` | ✓ |
| GPIO #53 | main.c:622 | `HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, (led ^= 1) ? GPIO_PIN_RESET : GPIO_PIN_SET);` | ✓ |
| PWR #6 | ft5446.c:226 | `__HAL_RCC_GPIOD_CLK_ENABLE();` | ✓ |
| PWR #7 | ft5446.c:439 | `__HAL_RCC_GPIOH_CLK_ENABLE();` | ✓ |

锚标签"HEAD ec93db0 锚定"：`ec93db0` 为真实 commit，其 `project_tcs/Core/Src/main.c` 与 HEAD `4b2b843` 的 blob md5 同为 `d75fc5c90b01c9ab9c75cb6ed2fe8850`，行号在当前 HEAD 仍逐行成立。

### 3.2 正文计数行与表行数

| 卡 | ③ 计数行（L4 门禁行 / 跑命令注释行） | 表数据行 | `研学卡_行数基线.txt` |
|---|---|---|---|
| I2C | `10 行 == 10 命中` / `# → 10 命中` | **10** | 10 |
| GPIO | `54 行 == 54 命中` / `# → 54 命中` | **54** | 54 |
| PWR | `42 行 == 42 命中` / `# → 42 命中` | **42** | 42 |

`bash tests/run_all.sh` → **`[card_surface] PASS (7 cards)`**（§5 同批日志）。

### 3.3 判定

**R-3 清零（三项判据全中）。** 附非阻塞观察：三卡页脚与 PWR L35 的旧数字未同步（见 §6-①），不影响行号命中/正文计数/门禁。

---

## 4 R-4：文档与矩阵数字口径（原 MF-9 残余 + MF-2/4 一致性）

### 4.1 W9 报告

- `grep -n "12 行\|12/12\|13 行" docs/results/W9_完成.md` → **0 命中**（无残留，更严于"仅留历史勘正注"）。
- 主口径：L26 `docs/ref/依赖版本清单.md 14 行逐行 | 14/14`；L73 复验"清单文件存在+14 行六要素"。
- 清单/矩阵同口径：矩阵 W9-1 "14 行六要素"。

### 4.2 W3-A §6 四命令（原样粘贴，仓库根执行）

| # | 命令 | 实测输出 |
|---|---|---|
| 1 | `grep -rnE 'comm_send\(|comm_txq_push|comm_poll_tx\(|comm_tick|link_send_frame_mt|link_set_param|link_send_cmd|link_request_registry' project_tcs esp32 --include=*.c \| grep -v build \| wc -l` | **43**（与 §1"43 行/15 调用点"一致） |
| 2 | `diff <(sed 's/\s*$//' project_tcs/App/comm/comm.c) <(sed 's/\s*$//' esp32/components/tcs_link/comm.c) \| head -4` | 仅 ESP32 侧增 W7 官方查证注释（9a10），无代码差 |
| 3 | `grep -n 'define MAX_TXQ' project_tcs/App/comm/comm.h esp32/components/tcs_link/comm.h` | H743=16（WP-A.5）/**ESP32=8**（既有设计差） |
| 4 | `grep -rn 'comm_txq_push_front' project_tcs esp32 --include=*.c --include=*.h \| wc -l` | **0** |

四命令均可原样粘贴执行，无占位词。

### 4.3 API 复审表附录 B

- 表头 L5："实测命中 **41**（W1a 后含 ft5446 三新调用点）；本表数据行 **44**（41 旧+3 新）=逐行对应"。
- #42-44（L59-61）逐行可点开：ft5446.c:345 Mem_Write / :369 Mem_Read / :389 IsDeviceReady（`git show HEAD` 三行原文命中）。
- 尾注 L75："→ **41**…；本表数据行 **44 ≥ 41**"（旧"40/41"已消失）。
- 表数据行实计数 = **44**。

### 4.4 矩阵抽查（指定 5 项 + 扩展）

| 抽查点 | 实文 | 判定 |
|---|---|---|
| W10-1 | "v2=**44 数据行**/基线 **41**（含 W1a 三新行 #42-44）" | ✓ |
| W10-3 | "表 #1-44 覆盖" | ✓ |
| W10-表 | "44≥41+44 非空" | ✓ |
| W1a-2 | "0 error 新增警告 0；text **+912~+992B**"（旧 +920B 已改） | ✓ |
| B 节 | 表行 **18**（W1-nm…终审） | ✓ |
| C+ 节 | 标题"**17 项**"、表行 **17** | ✓ |
| A+ 节 | **29** 行（M-01..M-29）= **17 ✅ / 12 ⏸** | ✓ |

证据路径抽查 7/7 存在：API复审表、W5 探针双向验证、官方库审计机器报告（黄灯数 0）、esp_lcd_touch_ft5x06、依赖版本清单、build/build-sim `tcs_main.elf`。

### 4.5 判定

**R-4 清零（四项判据全中）。** 附非阻塞观察：矩阵 L3 与 M-29 仍写"14 项"（C+ 节已 17），见 §6-②。

---

## 5 门禁全批（终态实跑）

工具链：mingw64 gcc 前插 PATH，`cd project_tcs && bash tests/run_all.sh`；另以脚本默认回退（MSVC cl）跑一轮，两轮均 `ALL GREEN`。

| # | 门禁 | 实测行 | 结果 |
|---|---|---|---|
| 1 | rtt_count | `[rtt_count] PASS (16)` | ✓ |
| 2 | vendor_sha | `[vendor_sha] PASS (5 files)`（含 CRLF 注入轮，§1.2⑤） | ✓ |
| 3 | fmt_gate | `[fmt_gate] PASS (%f=0 %l=0)` | ✓ |
| 4 | card_surface | `[card_surface] PASS (7 cards)` | ✓ |
| 5 | audit | `(143 files, 0 uncovered, 0 unclassified)` | ✓ |
| 6 | card_paths | `[card_paths] 70 checked, 0 bad` | ✓ |
| 7 | ws_json | `test_ws_json_escape: PASS`（6 向量） | ✓ |

`run_all: ALL GREEN`，EXIT=0。

**符号门禁**：`arm-none-eabi-nm build/tcs_main.elf | grep -c ct_iic` → **0**；build-sim 同 **0**；ELF 内 `HAL_I2C_Mem_*/IsDeviceReady` 符号 **3**（HAL 传输层在位）。

**H743 双构建（clean 全量重编，非增量侥幸）**：`cmake --build build --target clean` + `cmake --build build-sim --target clean` 后重建：

- build：EXIT=0，`error:` **0**；`text 164520 / data 4404 / bss 71160`，hex/bin 已生成。
- build-sim：EXIT=0，`error:` **0**；`text 203800 / data 4404 / bss 71216`。
- 警告 6 条（两构建同）：qrcodegen（vendor，-Wcomment×2、-Wtype-limits×2）、tcs_ui.c:172 未用参数×1、链接器 `LOAD segment with RWX permissions`×1——均为既有第三方/链接器提示，非本次改动引入，不构成门禁失败。

**ESP32**：按指令不跑增量构建。只读声明：`git status --short -- esp32` 为空（本会话未触）；前轮全链路（build→flash→log）已验在案。

---

## 6 观察项（非阻塞，不改变本轮判定）

① **三卡页脚旧数字未同步**：I2C 尾注仍"（5/5+2 补录行）"（实 10 行）、GPIO"（53/53）"（实 54）、PWR"（40/40）"（实 42），PWR L35 仍写"故不在 40 行内"（实 42）。正文计数行/门禁行/基线文件均已 10/54/42，页脚仅文本残留；制方 §9"页脚已改"的表述与 diff 不符（diff 只改正文计数行）。建议随 G 线合入或册 v13.5 顺手清理。

② **矩阵两处"14 项"残留**：L3 状态行"V/E/P/Q 14 项"、M-29 行"见下方 C+ 节扩展（14 项）"，而 C+ 节标题/表行已为 **17**。不影响 7 项抽查结论。

③ **侧写影响（非代码）**：门禁运行会重生成 `docs/evidence/官方库审计_机器报告.md`（会话前即为 M 态，本轮计数行 227→228）；`build/`、`build-sim/` 产物被 clean 重建（均 ignore）。**生产源码与 G 线零改动**；`VENDOR_SHA256.txt` 逐字节恢复（md5 复核）。

---

## 7 最终结论

**终审通过（v13 线 PASS[腿一矩阵 v3+腿二两轮+腿三待签字]）；客观待办=用户终裁卡/板窗批/W1b-g，均非不通过理由。**

| 项 | 判定 | 关键证据 |
|---|---|---|
| R-1 vendor_sha CR | **清零** | 整行 `tr -d '\r'`+`awk $NF`；CRLF 注入（段 PASS+全量 ALL GREEN）；对照组旧解析 6 FAIL；恢复 md5 不变；`bash -n` OK |
| R-2 头注/清单 | **清零** | 头注"HAL I2C4 缺省+位bang 保留域"与 L81/L84 实现一致；清单 push_front 仅"已删"注、"队头插队"0 命中 |
| R-3 三卡行号/计数 | **清零** | 10 处 HEAD 逐行命中；表行/正文计数/基线=10/54/42；`card_surface PASS (7 cards)` |
| R-4 口径一致性 | **清零** | W9 无 12/13 残留（14/14）；W3-A §6 四命令实跑；附录 B 41/44；矩阵 5+ 抽查全中 |
| 门禁 | **全绿** | 七项见 §5；nm ct_iic=0；H743 clean 双构建 0 error；ESP32 未触（前轮已验） |

非阻塞文档微瑕 2 组见 §6，建议清理，不构成本轮否决项。终裁卡 5 项抽验、板窗批（W1b-g 等）按既有计划由用户/实物窗口关闭。

——终验完。
