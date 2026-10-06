# 墨水屏驱动移植工作流 v1.4（已审核锁定）

> 状态：**执行中，S0 起硬件阻塞**（2026-10-07）。v1.4 = 规格书核验轮：
> ①撤回"屏已接好"误判（S1 首次 PASS 系悬空引脚噪声，判别实验证实 BUSY 无任何驱动源）；
> ②实证 BUSY 需上拉（busy=低/空闲=高 开漏语义，代码已启用内部上拉）；
> ③S2-S6 代码全部完成且逐模式编译通过，唯刷新忙相验证被 S0 阻塞；
> ④**规格书核验（Rev3.1, UC8151D，PDF 已存 porting-ref/）：裸屏 FPC 为 24 脚，接口脚 9-14、
>   电源 15-17、升压脚(2/3/5/20-24)外置——旧"8 脚 FPC"接线认知作废**；BUSY_N 低有效、
>   BS=L 选 4 线 SPI、写 SPI 上限 20MHz，全部与现有代码吻合。
> **解除阻塞路径：docs/wiring-check.md 第⓪步先判明手上是模块还是裸屏；裸屏无驱动板则需补购 Module (B)/Driver HAT**。
> 本文件是 S0-S6 全程的执行依据；改流程先改这里。

## 1. 对象与路线

- **屏**：微雪 WFT0290CZ10 = 2.9" (B) **红/黑/白三色**裸屏，296×128，SPI，**UC8151D**（规格书 Rev3.1 已核，PDF 存 porting-ref/），[官方页](https://www.waveshare.com/2.9inch-e-paper-b.htm)
- **裸屏物理事实**（v1.4 规格书核验）：FPC **24 脚**；MCU 接口脚 = 8(BS→GND)/9(BUSY_N)/10(RST_N)/11(DC)/12(CSB)/13(SCL)/14(SDA)，电源 = 15(VDDIO)/16(VCI)/17(GND)；升压引脚(2 GDR/3 RESE/5 VDHR/20-24 高压轨)外置在驱动板上 → **裸屏必须经含升压电路的 Module (B)/Driver HAT 接入；8 线接线表指驱动板排针，不是 FPC 脚序**
- **蓝本**（已拉取核实，存 `esp32/components/epaper/porting-ref/`）：
  - `EPD_2in9bc.c/h` —— 经典 B 屏（API：`Init/Clear/Display(blackimage, ryimage)/Sleep`，**双平面签名已确认**）
  - `EPD_2in9b_V4.c/h` —— B 屏 V4 版（额外有 `Init_Fast/Clear_Fast/Display_Fast`）
  - 子型号（bc vs V4）在 S1 接线后按微雪 wiki + S3 三色条终审确定
- **帧缓冲**：双平面（黑平面 + 红/黄平面，各 4736B，内部 RAM，DMA 可达）
- **三色屏物理**：全刷 ≈15s；黑白局部 ≈1.8s（仅黑白内容有效）；无黑白屏式 0.3s 快刷

## 2. 架构分层（扩展性承诺：换屏只换 chip 层）

```
app 层    display_task：唯一屏消费者，从队列收"显示什么"（温控/WiFi/BLE/STM32 都是生产者）
paint 层  epaper_paint：双平面帧缓冲 + 字符串/数字绘制
chip 层   uc8151_bc：命令序列（S2 从 porting-ref 移植）← 唯一随屏更换的文件
io 层     epaper.c：SPI 传输 / 手动 CS / DC / RST / BUSY 等待(带超时)
公共 API  epaper.h —— 对上稳定
```

## 3. 阶段门禁（未过不前进；每门 = 本地 commit + 审查 + push）

| 门 | 动作 | 验收标准（可观测） | 失败动作 |
|---|---|---|---|
| **S0 硬件静态** | 接线（8 线表见 epaper.h） | 接线逐项核对 + 供电 3V3 确认 | 停，重接 |
| **S1 总线活化** | `TEST_MODE=1`：复位脉冲 → 发 **PON(0x04)** → 观察 BUSY → 发 **POF(0x02)** | **BUSY 拉高后回落** = 面板活且命令通路通（三态：OK / NO_ACTIVITY=芯片对命令零响应=未接或坏 / STUCK_BUSY）。⚠️ 不能只复位不发命令——UC8151 BUSY 可能需 PON 才拉起，v1.1 逻辑会误判 | 查接线供电；2 次失败出报告 |
| **S2 命令层** | 移植 Init/Sleep 命令序列；**sleep→wake→重刷循环** | busy 时序正常、无超时、睡眠唤醒后能再刷（change-driven 依赖此路径） | 疑版本子型号错配，换蓝本重核 |
| **S3 首屏** | `TEST_MODE=2` 三色条（黑/白/红） | 肉眼三色正确、无花屏——**兼任屏版本终审**（刷不出红 = 版本错配） | 停，核对蓝本 |
| **S4 性能** | SPI 升 4.5M + 计时；老化用 TEST_MODE 扩展(连续刷+计数日志，可分会话执行) | §5 表全过，重点 **4.5M×100 次全刷零失败**、无 WDT 复位 | 降回 2M 锁定，性能项转已知限制 |
| **S5 快速更新** | v1.2 重定义：**主路径 = V4-Fast**（若屏是 V4）；实验项 = 自写 BW-partial LUT（官方两版驱动均无 Display_Partial，高风险）；兜底 = change-driven 全刷 | 主路径可用且稳定；实验项成功则加验残影 | 实验项失败不阻塞（它本来就是 bonus），落兜底 |
| **S6 集成** | display_task + 队列，首帧温度样例 | 断电重启 10 次显示路径全正常 | 逐层回退定位 |

## 4. 测试固件机制（决策 #2）

**Kconfig 单工程多模式**：`EPAPER_TEST_MODE`（0=正常 app / 1=S1 总线 / 2=S3 三色条 / 3=纯白），menuconfig 切换重编译即换测试。业务代码零污染，所有等待带超时（局部 3s / 全刷 20s），**驱动内不存在无界 while**。

## 5. 性能验收表（决策 #3：锁定修正版）

| 项 | 标准 | 属性 |
|---|---|---|
| 三色全刷时间 | ≤16s（规格 15s+10%） | 物理锁死 |
| 黑白局部刷时间 | ≤2s（规格 1.8s） | 物理锁死 |
| SPI 验收强度 | **4.5M（规格上限）× 100 次全刷零失败** | 工程上调 |
| 耐久老化 | **1000 次全刷老化 + 快刷 200 次无残影累积**（TEST_MODE 连续刷+计数日志，分会话执行） | 新增 |
| 上电到首帧命令提交 | **≤5s**（软件路径；刷新完成前屏面保持旧图 = e-paper 断电保持特性，属预期行为） | 新增（v1.2 修正：原"首帧≤5s"与全刷 15s 物理矛盾） |
| 帧渲染管线 | **<50ms**（数字→双平面帧缓冲，纯软件） | 新增 |
| CPU（刷屏等待期） | **<1%**，vTaskDelay 轮询零死等 | 上调 |
| 低温策略 | **<10°C 自动停用局部刷**，转全刷+拉长间隔 | 新增 |

## 6. change-driven 核心策略（感知性能 > 物理刷新）

三色屏 15s/次的现实下，**少刷就是最快的刷**：
- 温度变化 < 0.5°C → 不刷屏（生产者照常上报，display_task 丢弃无意义帧）
- 页面内容 diff 为零 → 跳过
- <10°C → 停用局部刷（低温波形失真），全刷间隔拉长
- 深睡前主动全刷一次（保屏面一致）

## 7. 决策记录（2026-10-07，你拍板）

1. **推送策略**：新开 `feature/epaper-driver` 分支；每个单元完成**且通过审查**后即 push（不再等逐次点头）
2. **测试固件**：Kconfig 单工程多模式
3. **验收标准**：锁定 §5 修正版（物理项 16s/2s，工程项全上调）
4. **开工时点**：立即脚手架（屏未到手先铺代码）

## 8. 执行进度

- [x] `feature/epaper-driver` 分支 + 骨架 commit（db81537）
- [x] porting-ref 双蓝本拉取核实（bc / b_V4，双平面签名确认）
- [x] 工作流 v1.2（三缺陷修正）→ v1.3（诊断轮修正）→ v1.4（规格书核验轮）
- [x] chip 层 uc8151_bc.c：Init/Clear/Display/Sleep 全序列移植（busy 全带超时）
- [x] paint 层 epaper_paint.c：双平面绘制 + 七段数字渲染（免字库）
- [x] S6 代码：display_task + 队列 + change-driven(<0.5°C 跳过) + 温度样例
- [x] 测试框架：TEST_MODE 0-6 全模式逐个编译通过（0/2/3/4/6 五连 PASS）
- [x] BUSY 硬件事实定论：开漏需上拉（判别实验：下拉 0/100、上拉 100/100 钉死）→ 代码已启用内部上拉
- [x] 诊断记录：复位/PON/PSR/POF 后 BUSY 零跳变、刷新 30s 无忙相 → 命令未达芯片
- [x] 接线发现模式（TEST_MODE=5）：RST 六候选×复位探测全无应答 → **坐实电源级问题**（VCC/GND 反接/未接实/接触不良），非信号线错位；该模式留作核线后的复测工具
- [x] 规格书核验（v1.4）：2.9(B) 裸屏 = **24 脚 FPC + UC8151D**，升压电路外置；旧"8 脚 FPC 圆点=1 脚"认知作废（1 脚实为 NC）；规格书 PDF 已存 porting-ref/
- [ ] **S0 硬件静态——未过（阻塞项）**：BUSY 无驱动源。按 docs/wiring-check.md 第⓪步判明单元：模块/驱动板形态 → ①②③核线后复测；纯裸屏 → 补购 Module (B) 或 Driver HAT 后再核线
- [ ] S1-S6 门禁验证：等 S0 解除后按 §3 顺序执行（命令已全部就绪）

## 9. 解除阻塞后的一条龙命令

**前提（断电操作!）：按 docs/wiring-check.md 第⓪步确认是"模块/裸屏+驱动板"形态（纯裸屏无法杜邦线直连，先补驱动板）；排针 VCC→3V3、GND、DIN→11、CLK→12、CS→13、DC→14、RST→21、BUSY→39。然后：**

```bash
source ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/activate_idf.sh
cd /d/Espressif/ws/tcs/esp32

# 第一步: 复测接线(出现 [DISCOVER] 命中 = 接线找回;仍无命中 = 还有线没通)
echo "CONFIG_EPAPER_TEST_MODE=5" >> sdkconfig
bash ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh all
sed -i '/CONFIG_EPAPER_TEST_MODE=5/d' sdkconfig

# 第二步: 一条命令连跑全部门禁(S1→S2→S3→S4→S6,失败即停)
bash run_gates.sh
```
发现模式命中后若引脚映射与默认不同，把命中日志里的 6 个 GPIO 改进 components/epaper/Kconfig 默认值再跑 run_gates.sh。
