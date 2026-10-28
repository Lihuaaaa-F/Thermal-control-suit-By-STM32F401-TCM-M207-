# v13.4 官方库对齐修复专册 执行结果总文档

- 日期：2026-10-26 ~ 2026-10-27（W 线执行会话，含跨日）
- 仓库：D:\Espressif\ws\tcs（分支 feature/lvgl-port；终态 HEAD=5088459，tag `w-final-review`）
- 任务书：docs/调温服任务书_v13.4_官方库对齐修复专册.md（质量地板=v11.3 全机制+§0b 纪律十律+§1b 双签+§1c 机器门禁+§1d 工艺门槛+§1e 用户终验+§1f 终审三腿）
- **结论：v13 线终审 PASS[三腿证据]；12 包关单（每包红队双签+整改清零+独立 tag）；客观待办全部明列挂账，无隐藏欠账。**

---

## 一、执行概要

| 项 | 数 |
|---|---|
| 关单工作包 | **12**（W4.1/W0/W5/W7/W10/W9/W2/W3-A/W8/W4.2/W1a/终审收口，tag `w0`…`w-final-review`） |
| 红队评审轮次 | **15 轮**（12 包各≥1 轮 + W4.1/W2/W7/W9/W10/W4.2/W1a 整改复验 + 终审两轮）；首派因 token/限额中断 3 次均挂起重派，未降标准 |
| 红队坐实并修复的真缺陷 | **6 个**（F16 %lu 日志损坏、m207 错误恢复缺 Abort、ws 线程上下文违约、W2 reg 缓存代虚标、W1a probe 地址减半、vendor_sha CR 解析） |
| 红队坐实并勘正的文档失实 | 20+ 处（枚举虚构、行号漂移、计数口径、出处错位——逐处留整改记录） |
| 终审 | 腿二两轮"不通过→回填→PASS"（MF-1..9 + R-1..4 全清零）；腿三用户终裁卡成文待签字 |
| 新增常驻门禁 | **7 道**（入 run_all，任何人可复跑） |
| 双构建终态 | H743 build/build-sim 0 error 0 新警告（text +912~992B）；ESP32 build PASS；nm `ct_iic`=0 |

---

## 二、十二包关单明细

### W4.1 — F14 浮点日志修复 + 新立 F16（commit c296ea2，tag w4.1）
- 4 处 `%.1f/%.2f` + 2 处 `%lu` 调用改官方模式 `snprintf+SEGGER_RTT_WriteString`（官方 RTT printf 无浮点，文档明示）；工程 `-u _printf_float` 链接旗标在案保证 snprintf 浮点可用。
- **找茬新立 F16**：自研 printf 无 `'l'` 修饰符，`%lu` 吐字面量且参数失步（post.c/ft5446.c 共 3 转换点）——已回标清单并修复。
- 取证：主机直链目标机同源实现，BEFORE=字面量 / AFTER=真值（docs/evidence/W4/f14_f16_修复前后对照.txt，可复跑）。
- 红队战果：96B 缓冲理论截断（升级修复→160B）、harness 自打红门禁（迁 evidence+DoD 改写建议）、台账开单漏登（纪律课起点）。

### W0 — 状态核销包（commit 789d5d4，tag w0）
- F1（led_strip 3.1.1，b741ef2+E-26 复跑挂 R7）/F3（eeprom 随 R4 删，致 R4 两条门禁入台账）/F5（**检索留档本体补齐**：ft5x06 官方 1.1.1+TMP117 唯一 GPL 件实测，evidence/官方库检索/）/F13（TouchGFX 拒绝在案）四项核销。
- 红队战果：F5"转述≠留档"坐实→补证据本体；开单漏登再现→整改为"开单即时入台账"制度。

### W5 — 审计门禁接入（commit ff7cc20，tag w5）
- audit 脚本接入 nightly.sh ⑧ 步+run_all（EXIT 透传）；脚本 chdir 锚定仓库根、manifest 注释读写对称、原子写、build* 前缀匹配；清单 SOP 头 3 行。
- 探针双向验证留档（塞 tmp_probe.c→红点名/删→绿）；**门禁上线首跑即抓真违规**（G 线 g4_e04_tl9b.py 未登记）。
- 新增 rtt_count 调用数对账门禁（采纳 W4.1 红队建议，堵"删日志行使 %f 门禁归零"捷径）。

### W7 — 查证注释 sweep（commit f1e255d，tag w7）
- 7 文件官方查证注释三要素齐（检索日期/F 项引用对位/实现参考或不适用豁免；首版仅 3/7 齐被红队证伪后整改）。
- 审计黄灯机制上线并双向实证（0↔1↔0，黄不阻断 exit）；IDF 官方示例路径 5/5 实存。
- 挂账：🟦 集合边界机器化（册修订项）。

### W10 — 官方 API 复审（commit fb2e3e1，tag w10）
- API复审表 **44 数据行**（基线 grep 41 命中逐行对应+终审 MF-2 增 3 行），出处列全非空（红队 6 处亲核官方源原文）。
- **m207 错误恢复补 HAL_UART_AbortReceive**（三路 UART 自愈配对门禁 1/1/1 入附录 C）；**F15 定论=豁免留档**（iwdg 寄存器序列与 HAL 逐键等价，红队独立裁决成立，iwdg.c 头查证注释）。
- 分流：ws async 上下文违约→W2（已闭环）；链路超时裕度 1.8×→W3-A 注记。

### W9 — 依赖版本锁定（commit c796ea8，tag w9）
- 依赖版本清单 **14 行六要素**（组件/精确版本/来源/锁定方式/许可/升级风险）：led_strip 3.1.1（dependencies.lock **入 git**=官方可复现机制，破双重 .gitignore 陷阱）、cJSON 1.7.19、Chart.js 4.4.1、LVGL 9.6.0（暂态锁注记）、HAL/CMSIS V1.10.7+nayuki 快照（**git tree hash 锁**+重录协议）、Pillow 12.2.0/zxing-cpp。
- CHANGELOG 扫记 4 项；GAP 诚实挂账（HAL 上游 tag、nayuki commit hash→二期）。
- 红队战果：**主交付物 Write 盘符笔误落错镜像树**（四路搜空抓出，教训入档）；nayuki .h 401=385+16 行 H13 期未记录手改补录。

### W2 — websocket 上行 JSON 统一 cJSON（commit ebf9d23，tag w2）
- 6 手拼点全 cJSON 化（PrintBuffered 256/128/8192 堆策略+reg **代缓存**）；**W10#18 修复**：发送改 `httpd_queue_work` 官方线程路径（队列深度实为 6，红队勘正）。
- 红队抓出真缺陷 **R2：reg 缓存 gen 虚标**（换入应盖数据快照代 g0）+R3 root Add 失败路径——均已修。
- 转义 7 向量单测（真实 IDF cJSON 源编译）入 run_all PASS；门禁 `cJSON_PrintUnformatted=0`/`snprintf=0`。
- **移交 R 线**：LINK_REGISTRY_MAX=32 vs H743 注册表 50 参上限不一致（0x100 段静默丢风险）。

### W3-A — comm txq 官方化评估（commit 43e5f4b，tag w3a；结论 **NO-GO**）
- v1 被 3 重大证伪（2 条虚构生产链/漏真源 tcs_snap/push_front 幻影行）→v2 换腿：主判据=H743/PC 无 FreeRTOS→等价金测与行为同源不可达（GO 结构自锁）；次判据=安全链路零收益承担全量重认证。红队两轮独立复核成立。
- F7 定论=评估拒绝（清单同步）；W3-B 不执行（前置未满足，历轮已覆盖不欠账）。
- 新发现移交：**push_front 双端 0 调用死代码**（终审 MF-8 已删）；comm.h MAX_TXQ 16/8 双端定差异口径成文（二期）；册 §0"生产者收敛"前提勘误。

### W8 — 官方研学卡制度（commit 4e07406，tag w8）
- **22 张卡**（册要求 ≥15）：ST HAL 7+ESP-IDF 10+第三方 5，四节结构（官方文档版本/官方示例路径零编造/调用面 grep 行数对账/对照结论锚定 W1-W10 定论）。
- 常驻门禁 `[card_surface]`（docs/ref/研学卡_行数基线.txt 7 卡行数对账）+`[card_paths]`（70 条本地路径存在性，0 bad）。
- 红队战果：三要素缺失 4/7、路径 `_main` 失配、引文不逐字、行号错位——全部整改清零。

### W4.2 — SEGGER RTT vendor 官方化（技术体 24caf77，关单 7b6f138，tag w4.2）
- **官方仓 github.com/SEGGERMicro/RTT @4d8feab（Release V8.56a）零改动 vendor**（出处=segger.com 产品页背书渠道，5 文件双哈希 CRLF+LF 留档）；官方 Config 实例化（UP=1024/DOWN=0/1+1 缓冲，模式=官方默认 NO_BLOCK_SKIP）。
- 门面薄壳保现封装签名（WriteString/Write 宏壳+守卫隔离）；**行为差异表**（满丢行为/官方锁/格式面 c d u x s p+l h 显式过滤）。
- 红队三轮（前两任 token 故障挂起）：官方性 raw 去 CR 逐字节一致；R1 四处数字勘正、R2 双哈希、R7 vendor_sha 门禁入 run_all。
- 挂账：probe-rs 真机对照（板窗）；cache 感知 R 线观察；CMake 正常化（G 线合入后）。

### W1a — ft5446 → ST HAL I2C4 配置编译步（commit ec93db0，tag w1a）
- 传输层双路径：**HAL I2C4 缺省**（PD12/13 AF4_OD 上拉，内核=PCLK4 120MHz）+位bang `FT5446_BITBANG` 宏保留一版（TU 0 警告）；`-DFT5446_I2C_100K` 真编译开关。
- **TIMINGR=ST 官方时序计算器零改动 vendor 现算**（400k=0x20E21F33/100k=0x70D04648，红队独立复算一致+反例 0x60F00C16 证单次调用约束）；CubeMX GUI 路径缺 FW_H7 包不可达（CLI 两轮日志+.ioc 装载测试件留档；红队裁定"偏差可豁免"附条件已落）。
- 红队战果：**F1 重大=probe 传 wr_cmd>>1 致总线寻址减半**（真 bug，修复+ELF 反汇编复验）；NAK 计数改仅计 AF（W1d 阈值口径纯净）；聚合包含单次调用约束。
- 门禁：nm ct_iic=0/HAL_I2C_Mem_≥2/双构建 0 新警告/text +912~992B。
- **W1b-g 挂账**（前置=R2-H743+触摸基线绿+板窗）。

### 终审收口（5088459，tag w-final-review）
见下节。

---

## 三、§1f 终审三腿（两轮"不通过"是制度在工作）

### 腿一：符合性全量矩阵 v3（docs/results/W_终审_符合性矩阵.md）
- A 节 42 原始行+**A+ 补充 29 行（M-01..M-29，终审判漏列后逐条补）**：现态 17✅/12⏸（⏸ 全为板窗/用户/外部件客观待办）；
- B 节 §1c 门禁 **18 条**全量重跑记录；C/C+ 节 V/E/P/Q **17 项**；F 节 F1-F16 逐项覆盖矩阵；册修订建议 6 条。

### 腿二：独立终审会话两轮（Q12 局限=同模型独立上下文，已声明）
- **第一轮（W_终审_报告.md，480 行）判不通过**：矩阵核验 42 行全查（33✓/3△/2✗）+§1c 18 条整批重跑+自枚举 DoD diff（**漏列 29 条**）+跨 W 集成缝 7 项（含官方 printf 对 %lu 显式过滤 l/h 的功能无损实证）+**MF-1..9**。
- 制方回填（W_终审_报告.md §8）：三卡 ③ 实更+card_surface 常驻门禁、W10 表 #42-44、fmt_gate 入 run_all、矩阵 v3、ft5x06 源码归档、W1f 评估成文、push_front 删除、台账 W6/F8 行、行号重锚。
- **第二轮（W_终审_复验.md，480 行）仍判不通过**：残余 **R-1..R-4**（vendor_sha CR 剥离写错且干净克隆必红/头注与清单措辞/三卡正文计数与 HEAD 行号/四处一致性漂移）——干净克隆实测 5 FAIL 为最硬证据。
- 制方再回填（复验报告 §9）：`tr -d '\r'`+awk 取名（**CR 注入模拟验证**：加 \r 后门禁仍 PASS）、头注/清单措辞、三卡 HEAD 重锚（git show 逐一提取 10 处行号）、全部一致性更新。
- **第三轮终验（W_终审_终验.md）判 PASS**：R 四项逐项清零实证+门禁七项终态全绿（gcc 与 MSVC 回退双轮同绿）+干净克隆可复现。

### 腿三：用户终裁卡（docs/results/W_终审_用户终裁卡.md，待签字）
- 总表（12 包一行一证据锚点）+指定抽验 5 项（对照取证文件/亲手跑 run_all/卡数清点/NO-GO 可读性/RTT 真机目视[板可时]）+已知缺口申报栏+签字栏。

---

## 四、新增常驻资产（本册交付的长期机制）

| 资产 | 位置 | 说明 |
|---|---|---|
| 门禁 7 道 | project_tcs/tests/run_all.sh | rtt_count(16)/audit 探针/黄灯 0/card_paths 70-0/vendor_sha(CR 鲁棒)/fmt_gate(%f=%l=0)/card_surface(7 卡基线) |
| 转义单测 | project_tcs/tests/test_ws_json_escape.c | 7 向量，真实 IDF cJSON 源编译 |
| 研学卡 22 张+基线 | docs/ref/研学卡_*.md + 研学卡_行数基线.txt | 四节结构；③ 行数机器对账 |
| API 复审表 | docs/ref/API复审表.md | 44 行禁分组，官方语义出处全可点开 |
| 依赖版本清单 | docs/ref/依赖版本清单.md | 14 行六要素+tree hash 锁+重录协议 |
| 官方物料归档 | docs/ref/CubeH7_I2C/、esp_lcd_touch_ft5x06/ | 计算器+例程+组件源码，UPSTREAM/SHA256/许可全套 |
| 取证证据 | docs/evidence/W4/W5/W1/官方库检索/ | 修复前后对照/探针双向/AN4235 PDF/检索记录 |

---

## 五、后续工作（全部明列挂账，无隐藏项）

### 5.1 板窗批（触发=板可用；当前 G 线 24h 采样期禁动）
1. **W1b**：bit-bang↔HAL A/B 对拍（同点位 20 击 CSV 机器判等；空闲态 TD_STATUS=0x0F 一致）。
2. **W1c**：回归全量（touch_session 358 断言+K4b+双浸泡 30min 幽灵=0+T-12 perf 平均/最坏双组+E-01+P-4 实拍）。
3. **W1d**：400k 10min NAK 率决策（>0.5% 降 100k——100k Timing=0x70D04648 已备）；W1g 上电时序 300ms 富余性实测。
4. **W2-5**：E-02/E-17×5/真机 WS 抓帧/E-19 浸泡 heap 水位改前后对照。
5. **W4.2-4**：probe-rs attach 真机+全启动日志对照（预期含 `i2c4 up pclk4=120000000Hz target=400000Hz timing=0x20E21F33`）。
6. **§1e 实物走查**：用户同场七步（脚本 docs/results/W_终验_实物走查.md）+终裁卡 5 抽验签字。
7. **W4.2-R3**：cache 感知 SKIP 模式真机观察。

### 5.2 前置依赖批（触发=R 线进度）
8. **W1b-g 前置=R2-H743 完成+触摸基线绿**（册 §9 依赖矩阵）。
9. **R 线移交 3 项**：①LINK_REGISTRY_MAX=32 vs H743 50 参（R2 线核 0x14 组包上限）；②册 §0"生产者收敛"勘误知会；③链路 Transmit 20ms/最大帧 11ms 裕度 1.8×（扩帧前重估）。
10. **G 线合入后**：研学卡五卡（LTDC/DMA2D/GPIO/PWR/LVGL）重跑 ③（触发行在台账）；W1a 的 MspInit I2C4 分支迁移+CMake 正常化（RTT/I2C4 聚合包含→独立源条目）；W8 I2C 卡再补面。

### 5.3 用户/外部材料件
11. FT5446 数据手册 400k 合法性页码截图；DS12110 PD12/13 AF4 截图（替代证据已在案：gpio_ex.h 宏+RM0433）。
12. 终裁卡签字（第 5 抽验 RTT 目视可与板窗批合并）。

### 5.4 二期清单（汇总各 W 红队裁定）
13. comm md5 同源门禁口径成文（comm.h 16/8 定差异）；W2 R4 帧宽三数收口+reg 8192 扩容（50 参形态）；web_mock JSON 等价 diff 工具化；run_all PYBIN 锚定（IDF PATH 污染）；tcs_ui.c:172 与 qrcodegen 警告清理；W7 🟦 边界机器化（册修订）；HAL 上游 tag/nayuki commit hash 录入；黄灯升级红时点；WP6 证据结案追认。

### 5.5 册修订建议（v13.5 采纳，见矩阵"册修订建议"节）
14. §1c W1 两门禁定义改写；§1c"17 条"→18；🟦 边界条款；vendor 双哈希条款；挂起件入库只允许空节；W1f/W1g 从板窗拆分。

---

## 六、证据索引（快速入口）

| 类 | 路径 |
|---|---|
| 各包完成/红队报告 | docs/results/W4_1_完成.md、W0_核销表.md、W5_完成.md、W7_完成.md、W10_完成.md、W9_完成.md、W2_完成.md、W3_txq_评估.md、W8_完成.md、W4_2_完成.md、W1a_完成.md（各配同名 _红队.md） |
| 终审三件 | docs/results/W_终审_报告.md（首轮）、W_终审_复验.md（二轮+R 清零记录）、W_终审_终验.md（终判 PASS） |
| 矩阵与卡 | docs/results/W_终审_符合性矩阵.md（v3）、W_终审_用户终裁卡.md、W_终验_实物走查.md |
| 参考资产 | docs/ref/（API复审表/依赖版本清单/研学卡×22/行数基线/CubeH7_I2C/esp_lcd_touch_ft5x06） |
| 证据 | docs/evidence/W4/、W5/、W1/、官方库检索/、官方库审计_机器报告.md |
| 台账 | docs/台账.md 第七节（W 线全程开单/关单/移交/终审事件，时间戳齐全） |

——本文档为 v13.4 专册执行总收口；下一会话入口=台账尾+本文件 §5。
