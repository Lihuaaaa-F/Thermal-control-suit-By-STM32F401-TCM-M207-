# v13.4 专册 用户终裁卡（§1f 腿三；一页纸，点 5 下完成）

**用法**：本卡只让你做三件事——①看总表任意行点开证据；②亲验 5 个指定抽验项（每项 1-3 分钟）；③签字。举证责任全在工程侧，你只当裁官。

## 一、总表（每行可点开证据路径；全量矩阵见 W_终审_符合性矩阵.md）

| W | 一句话交付 | 证据锚点 | 状态 |
|---|---|---|---|
| W4.1 | 4+2 处坏日志修复（浮点/%lu 字面量→真值） | docs/evidence/W4/f14_f16_修复前后对照.txt | 关单✓ |
| W0 | F1/F3/F5/F13 核销（F5 检索留档本体补齐） | docs/results/W0_核销表.md | 关单✓ |
| W5 | 审计门禁接入（探针双向验证+首跑即抓真违规） | docs/evidence/W5/探针双向验证.txt | 关单✓ |
| W7 | 7 文件官方查证注释+黄灯机制 | docs/ref/研学卡_HAL_I2C.md 等；audit 报告黄灯数=0 | 关单✓ |
| W10 | API 复审 41 行+F15 豁免定论+m207 Abort 修复 | docs/ref/API复审表.md | 关单✓ |
| W9 | 依赖版本锁定（lock 入 git+HAL/nayuki tree hash） | docs/ref/依赖版本清单.md | 关单✓ |
| W2 | websocket 全 cJSON+线程违约修复+转义 7 向量 | docs/results/W2_完成.md；run_all [ws_json] PASS | 关单✓ |
| W3-A | txq 官方化评估=**NO-GO**（两腿判据+红队两轮复核） | docs/results/W3_txq_评估.md | 关单✓ |
| W8 | 研学卡 22 张（≥15）+路径门禁 | docs/ref/研学卡_*.md；run_all [card_paths] 70/0 | 关单✓ |
| W4.2 | RTT 官方 vendor（SEGGER 仓 4d8feab 零改动+双哈希） | project_tcs/App/rtt/vendor/UPSTREAM.md | 关单✓ |
| W1a | ft5446→HAL I2C4 配置编译（官方计算器现算 Timing） | docs/results/W1a_完成.md；ft5446.c diff | 关单✓（红队整改后放行） |
| W1b-g | A/B 对拍+回归+决策 | —— | 挂账（前置 R2+板窗） |
| §1e | 实物走查脚本 | docs/results/W_终验_实物走查.md | 脚本成文，待你执行 |

## 二、指定抽验 5 项（你亲手，各 1-3 分钟）

| # | 动作 | 通过标准 | 你的结论 |
|---|---|---|---|
| 1 | 打开 docs/evidence/W4/f14_f16_修复前后对照.txt | 见"BEFORE=字面量行 / AFTER=真值行"成对 | ☐通过 ☐不通过 |
| 2 | 仓库根跑 `cd project_tcs && bash tests/run_all.sh; echo exit=$?` | 末尾 ALL GREEN 且 exit=0（约 3-5 分钟） | ☐通过 ☐不通过 |
| 3 | `ls docs/ref/研学卡_*.md \| wc -l` 并随手打开一张 | 数量 ≥15（现 22）且四节齐全 | ☐通过 ☐不通过 |
| 4 | 打开 docs/results/W3_txq_评估.md §4 | NO-GO 两腿判据可读懂、有复跑命令 | ☐通过 ☐不通过 |
| 5 | 板能上电时：probe-rs attach 看 RTT 日志 | m207/tmp117 行**数值正常**（非 %.1f 字面量）——F14 修复的用户可见证据；若板仍占用 G 线采样则勾"顺延" | ☐通过 ☐顺延 |

## 三、已知缺口（工程侧主动申报，非隐瞒）

1. W1b-g（A/B 对拍/回归/400k 决策）：前置=R2-H743 阶段+板窗，客观未做。
2. 板上批（E-02/E-17×5/E-19 水位/RTT L3 真机对照）：板在 G 线 24h 采样，客观未做。
3. §1e 实物走查：需你同场（脚本已备好七步）。
4. W4.2 红队 R3（cache 感知）等 3 条 wont-fix：真机观察项。

## 四、签字栏

- 抽验 5 项：____/5 通过（第 5 项可顺延）
- 终裁：☐ 通过并放行后续（W1b 解冻） ☐ 有条件通过（条件：＿＿） ☐ 不通过（缺口：＿＿）
- 签字：＿＿＿＿ 日期：＿＿＿＿
