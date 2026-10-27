# W1a 完成报告（ft5446 → ST HAL I2C4：配置编译步）

依据：任务书 v13.4 §2.2/§2.5 + §1c(W1 门禁 5 条中的可执行 4 条) + §1b/§1d/§0b。开单：2026-10-27 用户"没做完请做完"指令（W1b-g 仍挂账 R2 前置+板窗，见 §6）。

## 1 改了什么（逐文件）

| 文件 | 性质 |
|---|---|
| project_tcs/Core/Src/i2c_timing_utility.c/.h | 新增：**ST 官方时序计算器零改动 vendor**（CubeH7 官方仓 @8ec3113d，NUCLEO-H723ZG I2C 例程自带 339 行；出处/SHA256=docs/ref/CubeH7_I2C/UPSTREAM.md） |
| project_tcs/Core/Src/ft5446.c | 改：**传输层双路径**——默认 HAL I2C4（hi2c4+ft5446_i2c4_init：内核 PCLK4 选择+PD12/13 AF4_OD 上拉+官方计算器现算 Timing+HAL_I2C_Init+模拟滤波）；rd_reg/wr_reg/probe HAL 分支（Mem_Read/Mem_Write/IsDeviceReady，册 §2.1 逐函数映射表兑现）；**FT5446_BITBANG 宏保留旧位bang 实现整域**（引脚宏/7 函数/delay_us/诊断 SDA 直驱段全部守卫，一个版本期）；FT5446_I2C_FAST 双档（400k 缺省/100k 注释切换，两档均由官方计算器现算——比静态常数更强的出处） |
| docs/ref/CubeH7_I2C/ | 新增：3 份官方例程 main.c+时序计算器 c/h+UPSTREAM.md（锚 commit 8ec3113d）+SHA256.txt |
| project_tcs/cubemx/ | 新增：tcs_i2c4.ioc+CLI 脚本+两轮 CLI 日志（**CubeMX 路径取证：CLI 可载 .ioc 但代码生成需 FW_H7 包，本机无（仅 F1/F4），无头下载不可行——官方数值改经计算器源码路线，见 §2**） |
| docs/evidence/W1/AN4235_official.pdf | 新增：ST 官方应用须知原件（SHA256 7e25b9e5…；实测为计算器教程无预计算表——排除一条候选路径的证据） |

## 2 官方数值出处裁定（册 §2.2 条 2"CubeMX 唯一合法来源"的执行实况）

1. **CubeMX GUI/CLI**：本机 CubeMX 6.x 装机；CLI 可 `config load` 本 .ioc（两轮日志在 cubemx/）但 `project generate` 需 FW_H7 固件包（本机 Repository 仅有 F1/F4；~GB 级包无头下载不可行）——**本会话不可达，留档日志为证**。
2. **采用路线（更强出处）**：**ST 官方计算代码 `I2C_GetTiming()` 零改动链入**，init 期以实际 PCLK4（HAL_RCCEx_GetD3PCLK1Freq() 运行时取）×目标速率现算 TIMINGR——这是 ST 自己维护的时序计算器（AN4235 所述机制的可执行形态），在目标工程内运行并打点（`i2c4 up pclk4=…Hz target=…Hz timing=0x…` 日志行）。
3. 三份官方例程（**NUCLEO-H743ZI** 板 ComPolling/ComIT/ComDMA main.c L39-42）锚定"100MHz 内核→400kHz=0x00901954"**仅作官方计算惯例旁证**（红队 F4 勘正 v1"交叉锚定"措辞——本工程内核 PCLK4=120MHz，取计算器现算值：400k=**0x20E21F33**/100k=**0x70D04648**，红队独立复算一致）。
4. **挂账加强项**：W1b 上板前可用 CubeMX GUI（装 FW_H7 后）对 120MHz×400k 复核计算器输出并留截图（可选双保险，红队裁量）。

被否方案：❌ PLL3R=100MHz 复现例程静态值内核点——PLL3 已被 LTDC 占 50MHz（main.c:360 注释实证），改 PLL3=破坏屏时钟；❌ HSI 内核+例程 HSI 值——官方例程该值注释自相矛盾（"0x0020098E"vs define 0x00200817 且"HSI=16MHz"系 F4 残留注释），出处不洁弃用；❌ 手算 TIMINGR——册明令禁止。

## 3 怎么验证的（DoD 表）

| DoD 项 | 等级 | 证据 | 数字 |
|---|---|---|---|
| §1c-1 `nm \| grep -c ct_iic`→0（BITBANG 关闭构建） | L1 | 实跑 | **0** |
| §1c-2 ft5446.c `HAL_I2C_Mem_`≥2 | L1 | 实跑 | **2**（Mem_Read+Mem_Write） |
| §1c-3 TIMINGR 一致性 | L1（变体） | 计算器=官方源码零改动（SHA256 在案）+运行时打点行设计；cubemx_timing.json 式静态比对不适用（无静态值） | 出处链在档 |
| §1c-4 perf 平均+最坏双组 | **L3 ⏸** | 板上（W1c/T-12 批） | 挂账 |
| §1c-5 A/B 对拍 20 行 CSV | **L3 ⏸** | 板上（W1b） | 挂账 |
| 零警告编译 | L1 | build+build-sim 0 error，**新警告 0**（存量 6 行/4 类=RWX/qrcodegen.h:359/qrcodegen.c:211,299/tcs_ui.c:172；终审 F-2 勘正原"3 条"） | ✓ |
| map 体积对比 | L1 | text →**164520（增量 +912~+992B 视构建环境；干净树全量复测 +992B=终审 F-3）**；bss 68512→71160（+2.6KB=hi2c4+计算器静态表） | 在案 |
| 位bang 保留期可编 | L1 | `gcc -DFT5446_BITBANG -c ft5446.c` 单 TU 编译 **0 error/0 warning**（/tmp/ft_bitbang.o 6.1K） | ✓ |
| 上层零改动（册 §2.1 表末行） | L1 | touch_session/坐标映射/T-13 断言未触碰（diff 仅 ft5446.c 传输层+新增文件） | ✓ |

## 4 机器门禁自查

```
$ arm-none-eabi-nm build/tcs_main.elf | grep -c ct_iic        → 0
$ grep -c "HAL_I2C_Mem_" Core/Src/ft5446.c                    → 2
$ arm-none-eabi-nm build/tcs_main.elf | grep -cE "I2C4"       → 2（ER/EV 弱中断符号；handle 静态）
$ cmake --build build && cmake --build build-sim              → 0 error，新增警告 0
$ arm-none-eabi-gcc -DFT5446_BITBANG … -c ft5446.c            → 0 error/0 warning
```
（§1c W1 行的"引脚只在 MspInit"文本门禁：本实现引脚配置在 ft5446_i2c4_init（模块自有底层，eeprom.c 同款惯例）——**偏差留档**：册 §2.2.3 指定 MspInit 加 I2C4 分支，但 HAL_I2C_MspInit 现在 main.c:440（G 线未提交区，不可触碰）；G 线合入后把 i2c4_init 的 GPIO/RCC 段迁入 MspInit I2C4 分支=二期正常化项，行为等价。）

## 5 找茬记录（≥3）

1. **JSON 转义层吃反斜杠两连击（坐实修复）**：python heredoc 补丁中 `\n` 经 JSON 解码变真换行→C 字符串断裂→守卫配对雪崩（两处：init FAIL 行/diagnose #else 块 MISS）——逐一用 Edit 工具修复；与既有记忆"heredoc 吃反斜杠"同根因，**教训升级：含 `\n` 的 C 源补丁必须走 Edit 工具不走 bash heredoc**。
2. **PLL3 冲突（排除）**：100MHz 内核复现官方静态值方案在通读时钟树时发现 PLL3=LTDC 50MHz 已占用——若未查直接改 PLL3 会毁屏时钟；方案②（计算器现算+PCLK4）天然无冲突。
3. **官方例程注释自相矛盾（弃用依据）**：WakeUpFromStop 例程"TIMING to 0x0020098E"注释与 `#define I2C_TIMING 0x00200817` 不一致且"HSI=16MHz"是 F4 残留——HSI 静态值路线出处不洁，不采用；此发现反证计算器路线的必要性。
4. **FT5446_I2C_FAST 切换机制**：档位切换靠注释宏（CubeMX 路线原本可两 .ioc 各生成）；现由计算器参数化，W1d 降档决策时改一行宏即得官方计算值——比静态双值更防呆。

## 6 挂账

1. **W1b-g 全部**：A/B 对拍（20 击 CSV 机器判等）/回归全量（touch_session 358 断言+K4b+双浸泡+T-12 perf 双组+E-01+P-4 实拍）/W1d 决策（400k 10min NAK 率）/W1f 传输模式评估对照/——前置=R2-H743+触摸基线绿+板可动（G 线采样窗口）。
2. CubeMX GUI 复核计算器输出（可选加强，需装 FW_H7 包）。
3. MspInit I2C4 分支迁移（G 线合入后二期正常化，§4 注记）。

## 7 自检勾（§1d 八条）

1-8 ✓（计算器=官方文件零改动+文件头出处注释；注释全为约束与出处；零死代码——位bang 域为册令保留非死码；魔法数 0=Timing 由计算器/超时 20ms 同 tmp117 口径/W1g 序列不动注释；错误路径 init 三返点+读 NAK 计数保留；两 JSON 转义失误如实入 §5 与台账）。


## 9 整改记录（W1a 红队"整改后放行 [must-fix 5]"→ 清零）

| 发现 | 处置 | 证据 |
|---|---|---|
| F1[重大] probe 传 wr_cmd>>1 致总线寻址减半（真 bug） | 已修：改传 8bit 形式 wr_cmd（HAL DevAddress 语义），注释记红队坐实 | ft5446.c probe #else 分支 |
| F2[一般] rtt_count 13→实测 16（W1a +3 日志点） | 已修：门禁口径 16（含 ltdc.c 4 处 HEAD 既有说明）；run_all ALL GREEN | run_all [rtt_count] PASS (16) |
| F3[一般] 引脚文本门禁与位bang保留条款矛盾 | 裁定：保护真值=nm 符号门禁（ct_iic=0 在案）；文本门禁与册 §2.2 条4 自相矛盾→**册修订建议**（文本门禁应改排除注释/守卫域或删除）；矩阵注记 |
| F4[一般] UPSTREAM 残留废弃方案文案 | 已修：UPSTREAM v2 重写（采用件/计算器两值/三路废弃方案留档/.ioc 非权威声明/BSD-3 许可结论） | docs/ref/CubeH7_I2C/UPSTREAM.md |
| F5[一般] NAK 计数漂移（全非 OK→会污染 W1d） | 已修：仅 ErrorCode&HAL_I2C_ERROR_AF 计 nak_cnt | ft5446.c rd_reg #else |
| F6[一般] FT5446_I2C_FAST 非真开关 | 已修：-DFT5446_I2C_100K 真编译开关（缺省 400k） | ft5446.c target 段 |
| F7[一般] 计算器静态累加器 | 已约束：注释"每启动单次调用"+UPSTREAM 约束节；本工程仅单点调用 | ft5446.c include 注释 |
| F8[微瑕级 must] delay_us_init/g_fac_us 未守卫 | 已修：连同调用点入 BITBANG 域（漏改一次导致 ld 失败，二次修复实证） | ft5446.c L439-441 |
| F9[微瑕] 注释 480/2 漏二级分频+7位语义措辞 | 已修 | ft5446.c 两处 |
| F10[微瑕] 20ms 故障路径 WCET 定性 | 记录：正常路径 ~0.2ms/6B@400k，20ms 仅故障帽；主循环调用未破铁律（红队推演在案） | 本报告§3 |
| F11[微瑕] 聚合包含重复定义风险 | 已注：ft5446.c include 处"严禁再入 CMake 源列表" | 同 F7 注释 |
| F12[微瑕] LICENSE 无留档 | 已补：UPSTREAM 许可结论 BSD-3-Clause | UPSTREAM v2 |
| F13[流程] 末动作 | 本关单 commit 落（代码+报告+红队+证据+台账） | git log |


## 10 W1f 传输模式评估（册 §2.5；终审 MF-5 补做，非板依赖）

| 模式 | 官方例程对照（CubeH7 仓 @8ec3113d） | 单次事务成本估算（7B 级短读@400k） | 结论 |
|---|---|---|---|
| **轮询（阻塞）** | I2C_TwoBoards_ComPolling（本工程采用，HAL 同步 API） | 总线时间 ≈ (1+8+1+9+8+1)×... 简化=addr+reg+重复起始+6B≈0.2ms；HAL 轮询开销≈数十 µs | **采用**：触摸轮询 10ms 节拍、单事务 ≤0.25ms 级，上下文切换开销（中断/DMA 路径 ≈5-15µs×2+ISR 调度）相对传输时长占比高，且引入并发状态机 |
| 中断（IT） | I2C_TwoBoards_ComIT | 传输同 ≈0.2ms + ISR 进出+回调调度 | 不采用：为短事务引入 ISR 复杂度；主循环阻塞窗口已可接受（册 §2.3 WCET 约束 ≤250µs 正常路径） |
| DMA | I2C_TwoBoards_ComDMA | 传输同 + DMA 通道配置/TC 中断 | 不采用：7B 级数据搬运 DMA 无收益（配置开销>搬运），且与 G 线 LTDC/DMA2D 资源占用并存 |

**评估结论（册要求"含数字估算留档"）**：单事务总线时间 ~0.2ms ≫ 中断/DMA 固定开销占比的收益阈值；7 字节级短读场景**轮询=最优**（默认预期命中）。若未来改 50+ 字节块读（如整帧触控缓冲），按本表重估 IT/DMA。若实测中断模式更优才切换——本评估为 W1b 上板后保留复评窗口（结论留档于本表）。

## 11 §2.4 材料补录（终审 MF-5）

| 材料 | 状态 |
|---|---|
| CubeH7 I2C 例程 + 官方时序计算器 | ✅ docs/ref/CubeH7_I2C/（UPSTREAM@8ec3113d+SHA256） |
| esp_lcd_touch_ft5x06 v1.1.1 源码 | ✅ docs/ref/esp_lcd_touch_ft5x06/（官方组件管理器 ==1.1.1 拉取；license.txt=Apache-2.0；仅对照不引入） |
| FT5446 数据手册 400k 页码 | ⏸ 用户既有件（不在仓）——上板前由用户提供页码截图入 evidence |
| H743 datasheet DS12110 PD12/13 AF4 截图 | ⏸ 外部 ST 文档（st.com 机器人墙）——替代证据已在案（gpio_ex.h AF 宏+RM0433 复用表经 HAL 头），截图随 W1b 补 |
