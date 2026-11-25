# S-G h1a 结果 — H743 驱动与内核（2026-11-25）

分支 `fix/s-g-h1a`，基座 `fix/s-u-int2` @ bf854ce（未 rebase，合并由协调人做）。
范围：任务清单 20 条 + 协调人中途追加的 P1-06 残余、X05-F-08、X05-F-05（H743 部分），共 23 条。
不碰设备：所有真机部分都在"遗留"里，步骤见 §3.3、§4.4、§5。

## 0 提交

| 提交 | 内容 | 条目 |
|---|---|---|
| 1b1a331 | fix(touch)：触摸链路掉线恢复、读失败不出样本、在线判定改配置回读 | M02-F-01 / 02 / 04 |
| 41e6831 | fix(main)：临界区保存恢复 PRIMASK、删 I2C1_EV 的 NVIC 使能、DBGMCU 用 HAL 宏、两处注释勘正 | X03-F-10 / M01-F-10 / 11 / 06 / 12 |
| 9b18ba7 | fix(prod)：交付档不初始化 v1 本地外设、不驱动任何执行器（两层门控 + 交付档主机用例） | P1-06 |
| b848cc4 | refactor(registry)：删死函数 param_load_eeprom | M05-F-16 / 18 |
| 2e52941 | fix(post)：POST 的 TMP117 项宽限窗内重试 | M06-F-12 |
| ea52ecd | fix(heap)：_sbrk 加上界 | M01-F-08 |
| 26dce62 | fix(ltdc)：SDRAM 自检、注释、裸写集中、死函数、返回值 | M02-F-05 / 08 / 09 / 10 / 11、X05-F-08 |
| ca0cea7 | fix(post)：post_sim_fail_item 进 TCS_SIM | X05-F-05 |
| 365e351 | test(run_all)：[prodlink] 成功时打印 PASS | — |
| （本提交） | docs/results/S_G_h1a_结果.md | — |
| （最后一个） | 审计清单与机器报告 | — |

## 1 逐条判定

判定口径（五种里前两种的界线，本批次按这个分）：
**已修并验证** = 判别性测试 / 构建证据覆盖了这条缺陷的触发路径本身，真机上只剩"不回归"的抽查；
**已实施待真机** = 修复效果或改动本身的安全性只有上真机才看得到（触摸恢复、SDRAM 自检、启动探测、引脚状态）。

| ID | 判定 | 改了什么 | 怎么验证的（命令与结果） | 遗留与原因 |
|---|---|---|---|---|
| M02-F-01 | **已实施待真机** | 判定在 `touch_session.c:225-312`（纯函数 `ts_tp_link_poll` / `ts_tp_link_step` / `ts_tp_link_force_offline`，阈值在 `touch_session.h:103-106`：连续 3 次轮询失败判离线，退避 100ms，IC 复位 20ms + ROM 加载 300ms）。动作在 `ft5446.c`：`ft5446_service()`（:682）每拍最多走一步：清总线（:505）→ I2C4 复位重配（:547）→ 复位 IC → 写配置 → 回读核对；离线期间 `ft5446_read()`（:901）不碰总线。读/写事务前先看两线电平（:299），被拉死不进 HAL。只读遥测 `ft5446_link_status()`（:671）+ `g_ft5446_buserr_cnt`。`main.c:1032` 主循环加一行。台架钩子 `g_tp_force_recover`（:456，仅 TCS_PROFILE ≥ 1） | 主机：`test_touch_session.c` 新增 7 组用例（掉线判定、恢复时间线、卡死总线 / 器件不在 / 回读不一致、掉线再恢复、毫秒回绕、强制恢复），PASS。变异 T01~T12 全部被杀。构建：PROD 符号表有 `ft5446_service` / `ts_tp_link_step`、无 `g_tp_force_recover`。节拍影响见 §3 | 关单要真机：拔插触摸排线 20 次、恢复率 100%（步骤 §5）。界面横幅归界面层，控制台显示归 h1b；本批次只给 `ft5446_link_status()` |
| M02-F-02 | **已修并验证** | `ft5446_read_ops()`（`ft5446.c:118`）：触点状态读失败（:141）或 6 字节读失败（:180）都返回 -1，样本字段全 0，不再解析缓冲；`buf[6]` 清零 | 未修代码：把 `ft5446_read()` 原文抽出来配桩——6 字节读失败时返回 0、valid=1、touched=1，坐标是栈残值 (1023,599)。修后：`test_read_ops_fail` / `test_read_ops_property`（1024 种组合，任一读失败不出样本）PASS；变异 F01/F02（把两条判失败删掉 = 原缺陷）被杀 | 真机抽查：`tp_diag` 期间无幽灵触点 |
| M02-F-04 | **已实施待真机** | 在线判定 = 纯函数 `ft5446_probe_online()`（`ft5446.c:222`）：4 个配置写都成功 + THGROUP / PERIODACTIVE 回读一致；版本号只记录。`ft5446_init()`（:597）返回码 -2 不应答 / 回读不一致、-3 I2C4 起不来、-4 配置写失败。不再读触点数寄存器当在线凭据（本屏空闲态读回 0xFF，从来没有证明力） | 主机：`test_probe_online`——全 0 读回 → 不在线；版本 0x0000 但回读一致 → 在线；任一写失败 / 回读失败 / 不一致 → 不在线，PASS；变异 F03~F06 被杀 | **必须真机确认**：在线凭据换了，依据是 WP-M 的 `touch diag` 回读四项全 OK 和版本寄存器实测 0x0001。首次上电 RTT 必须是 `ft5446 ver=0001 cfgwr=1 ver_ok=1`；万一不是，触摸起不来（会自动重试但永远不过） |
| M02-F-05 | **已实施待真机** | `ltdc.c` 分两段，上段纯图样（:44-58）：范围 = 3 块帧缓冲（front / back / LVGL 渲染块），每 4KB 一个探测点，值 = k 的 16 位散列（单射）。`sdram_selftest()`（:226）读回前做 `SCB_CleanInvalidateDCache_by_Addr` 整段（:240） | 未修代码：旧图样在故障注入下，地址线 A16~A21 卡死 6 根查不出、数据线 D0~D11 卡 0 十二根查不出。修后：`test_drv_pure`（范围、偏移、值两两不同、A12~A21 与 16 根数据线卡 0/卡 1 全查得出）PASS=69；变异 L01~L03 被杀 | 真机：自检耗时增加（估算 < 30ms，整段 cache 维护 + 900 点写读），上电 RTT 仍须 `sdram selftest=1`；原关单条件"写坏第 1.5MB 处 1 字节自检必须报错"要真机 |
| M01-F-10 | **已修并验证** | 删 `MX_I2C1_Init()` 里 NVIC 使能 I2C1_EV 的两行（说明在 `main.c:838`）。新增常驻门禁：`run_all.sh:304` 起 `[irq_handler]`——出现过的每个 `<名>_IRQn` 都要有 `<名>_IRQHandler` 定义 | 门禁在基座 bf854ce 上 FAIL（`I2C1_EV_IRQn 被引用，但没有 I2C1_EV_IRQHandler`），修后 PASS（3 个 IRQ 被引用，0 个没有处理函数） | — |
| M01-F-08 | **已修并验证** | `syscalls_stub.c:53` 纯函数 `sbrk_fits`；`_sbrk`（:61）越过 `_Heap_Stack_Limit` 返回 -1 / ENOMEM；只读遥测 `g_heap_used_max`（`main.h` 声明） | 未修代码：`_sbrk` 原文配 1KB 堆区，连续申请 1,000,000 字节全部成功、从不返回 -1。修后：`test_drv_pure` 的堆上界一组 PASS；变异 S01~S04 被杀。PROD map：`_end`=0x20014670、`_Heap_Stack_Limit`=0x2001DC00 → 堆实际可用 38,288 字节（"堆只有 1KB"是链接时的最小保留） | 栈溢出本身仍无运行时检测（MSPLIM 可做，本条不含）；真机读一次 `g_heap_used_max` 量水位 |
| M02-F-08 | **已修并验证** | `ltdc.h:57/72`、`ltdc.c` 文件头改为"CPU 写穿透，不用 DMA2D"并指向文件末尾的实测结论 | 逐字核对；`git grep -nE "DMA2D_" -- Core App` 仍只有 `__HAL_RCC_DMA2D_CLK_ENABLE()` 一处（研学卡 HAL_DMA2D = 1 不变） | — |
| X03-F-10 | **已修并验证** | `main.c:285-304`：`crit_enter` 记下进入前的 PRIMASK 并计深度，`crit_exit` 只在最外层恢复；没配对的 exit 不动中断状态 | 反汇编：修前 `crit_exit = cpsie i; bx lr`；修后两个函数里 `cpsie` 出现 0 次、`msr PRIMASK` 1 次。调用点走查：`s_ops`/comm 的临界区只在 `comm_txq_push`、`comm_poll_tx` 两处，区内不调回调，全部在主循环，ISR 不调 `comm_*`——当前无嵌套，这是把契约做实 | `comm.h:89-90`"MCU 必须给 __disable_irq/__enable_irq"的注释归 h1b（App/comm/），未动 |
| M06-F-12 | **已修并验证** | `post.c:128-153` 纯函数 `post_retry_init/due/judge`：首读没过不裁决，每 20ms 再试，窗内成功即 PASS，2500ms 到点仍失败才 FAIL。`post_init`（台架/仿真档）只读一次；`post_poll`（:293）按间隔重试 | 未修代码：`test_post` 新用例编译失败（新符号不存在）。修后 `test_post` PASS=81；变异 R01~R04 被杀。暂存区版本 post.c 用真实编译命令 -Werror 检查 PROD / BENCH / SIMWG 均 clean | 真机抽查：台架档冷启动 20 次，`[POST] done pass=` 里 TMP117 位为 1 |
| M01-F-06 | **已修并验证** | `iwdg.h:6-11` 改为 RLR=249 → 2.0s，并注明 32.7s 是 WP6 期间的临时发布配置、现为理论上限 | 逐字核对 `iwdg.h` 与 `iwdg.c:37`（`RLR = 249U`）；`main.c:66-67` 的"32.7s 复位循环"是历史事实（当时发布配置就是 4095，见 WP6 报告），保留 | — |
| M01-F-11 | **已修并验证** | `main.c:904` 裸写换成 `__HAL_DBGMCU_FREEZE_IWDG1()`（`stm32h7xx_hal.h:905`，同一寄存器同一位） | 源码核对；研学卡 IWDG 调用面行数不变（32） | — |
| M01-F-12 | **已修并验证** | `main.c:1-21` 文件头改成 v2 分工 + 档位说明，节拍清单标出〔台架〕步骤（只改注释） | 逐字核对：与同文件 `snap_apply` 处注释、`tcs_profile.h` 档位定义一致 | — |
| M02-F-09 | **已修并验证** | `ltdc.c` 文件头、函数头、两处行内注释改为 622 / 634，并写明 50MHz ÷ (1344×635) = 58.59Hz（-2.3%） | 算术复算：LCD_HEIGHT=600 → 3+20+600-1=622、3+20+600+12-1=634 | — |
| M02-F-10 | **已修并验证** | 两条寄存器写收进唯一的 `ltdc_set_fb()`（`ltdc.c:93`），写明为什么不用 HAL：`HAL_LTDC_SetAddress` 会经 `LTDC_SetConfig` 重写整组图层寄存器并触发立即重载，`HAL_LTDC_Reload` 还会开重载中断而本工程没有 LTDC 中断处理。`gfx_flip` 现在只有 `ltdc_init` 上电清屏调两次（两块都是黑屏） | `git grep -nE "LTDC(_Layer1)?->" -- Core App` → 只剩 `ltdc_set_fb()` 的两行。寄存器写与原来逐条相同 | 任务说明里"`ltdc_show_buffer` 在 LVGL 刷新路径上"的前提不成立：它是零调用点（LVGL 的 `flush_cb` 直接 memcpy 进两块帧缓冲），所以这条没有运行期风险；上电显示正常即可 |
| M02-F-11 | **已实施待真机** | `ltdc_clk_set` 返回 int（`ltdc.c:147`），`ltdc_init` 判它 → -4（:314）；`HAL_SDRAM_ProgramRefreshRate != HAL_OK` → `sdram_init` 返回 -1（:219） | 源码核对 + 构建。没法在主机上给 HAL 打桩（HAL 返回错只在真硬件上出现） | **必须真机确认**：以前这两个返回值没人看，如果真机上 HAL 本来就返回错，现在会不点屏。首次上电 RTT 必须仍有 `pll3 ltdc pclk=50M ok` |
| M05-F-13 | **未做** | — | — | EEPROM 位敲物理层迁到 HAL_I2C 需要台架真机验证波形（逻辑分析仪 / 400kHz），本批次不碰设备。交付档已不再碰它（P1-06：`eeprom_init` 不执行，PH4/PH5 保持复位态）；台架/仿真档里它是 GS 工程验证过的实现，没有硬件就重写风险大于收益。建议：随台架档 EEPROM 专项一起做，同批补 SCL 超时 |
| M05-F-15 | **未做** | — | — | 15 个 key 与正典 §2.5 不一致仍然成立（H743 `param_registry.c` 与 ESP32 `tcs_core/param_registry.c` 两端逐字相同）。改哪一边都不是本批次能定的：改正典 = 改文档；改代码 = 两端 + 网页 / CLI / 文档一起改，且 ESP32 侧不在 h1a 范围。与本地 EEPROM 无关。需要裁决（§6） |
| M05-F-16 | **已修并验证** | 删 `param_load_eeprom()`（`param_registry.c:392` 留说明）、头文件声明、三个测试里只为它备的两个桩 | `git grep -n param_load_eeprom` → 只剩两处"已删"说明；`nm` 无该符号；受影响的三个测试 PASS（post 81 / registry 74 / control 73） | — |
| M05-F-18 | **已修并验证** | 随 M05-F-16 消失（块作用域 extern 就在那个函数体里） | 修前：`cl /W4` 编 `test_post` 报 `param_registry.c(396)/(397): warning C4210` ×2；修后同一命令 warnings=0 | — |
| M07-F-11 | **已修未记** | — | `console.c:256-259` 注释 + `:285-294`、`:369-374` 已是 `__HAL_RCC_GPIOx_CLK_ENABLE()`；提交 ae1a5e7（2026-11-15，"fix(r1): P0 安全闭环"）。`git grep -nE "RCC->"` → 只剩 `console.c:770` 读 RSR 和 `ltdc.c` 两处诊断读，没有写 | — |
| P1-06 | **已实施待真机** | 见 §4：交付档 `control_init` 三个执行器钩子传空且 `control_init` 拒收（两层）；`fan_init` / `m207_init` / `eeprom_init` / USART3 引脚 / `MX_I2C1_Init` 进 `#if !TCS_H743_LINK_ONLY`；交付档不再探测本地 TMP117 | 主机：`test_prod_link_only.c`（按交付档编译）PASS=32；回滚验证 FAIL=9；既有 `test_control`（台架语义）PASS=73 不变。四档构建通过；PROD 符号表无 `m207_init` / `eeprom_init` / `MX_I2C1_Init`；`main()` 反汇编里 PB14 掩码立即数 PROD 0 次、台架档 1 次。变异 X01~X05 被杀 | 真机读 GPIO 寄存器核对引脚状态（期望值 §4.4，PB14 必查）；PB14 实际接没接风扇光耦待用户确认 |
| X05-F-08 | **已修并验证** | 删 `ltdc_show_buffer()`：定义、`ltdc.h` 声明、`lv_port_disp.c` 那行 extern | `git grep -n ltdc_show_buffer` → 只剩三处"已删"说明；四档 `nm` 均无该符号 | `g_shown_fb` 保留：删完后仍有读者（§4.5） |
| X05-F-05 | **已修并验证** | `post_sim_fail_item` 的声明（`post.h`）和实现（`post.c:201-210`）进 `#ifdef TCS_SIM`，与唯一的调用者（`console.c` 的 `sim post fail`，本就在 TCS_SIM 块里）同条件。console.c 未动 | `arm-none-eabi-nm build-matrix/<档>/tcs_main.elf`：PROD / BENCH / SIM 查不到 `post_sim_fail_item`，SIMWG 有。三档 -Werror 语法检查 post.c 均 clean | 修前交付档符号表里也没有它（零调用被链接器丢掉），所以这条没有"符号由在变不在"的红；红在源码：基座 `post.c:171` / `post.h:94` 不带开关 |

计数：已修并验证 15、已实施待真机 5（M02-F-01 / 04 / 05 / 11、P1-06）、已修未记 1（M07-F-11）、不适用 0、未做 2（M05-F-13 / 15）。

关于"不适用"：M05 的四条里没有判不适用的——对象（EEPROM 位敲层、15 个 key）都还在，只是交付档不再用 EEPROM。

## 2 本批次验证总结果

**构建矩阵**（`export CMAKE_BUILD_PARALLEL_LEVEL=2 && bash tools/build_matrix.sh --twice`，两次，H743 四档从零）：

```
第 1 次（ca0cea7）                                第 2 次（365e351 + 未提交的审计产物，-dirty）
[PROD]   exit=0 error=0 warning=4  FLASH: 451012 2; DTCMRAM: 92784 128; AXIRAM: 205952 320   （同左）
[BENCH]  exit=0 error=0 warning=4  FLASH: 468324 → 第 2 次 468332（版本串长度变了 8 字节）
[SIM]    exit=0 error=0 warning=4  FLASH: 468324 → 第 2 次 468332
[SIMWG]  exit=0 error=0 warning=4  FLASH: 507468 2; DTCMRAM: 96472 128; AXIRAM: 205952 320   （同左）
可复现构建：剔除 .comment 与调试信息后 md5 dfc22127…213d / dfc22127…213d → 相同；原始 bin md5 相同
交付档干净核对：PASS
build_matrix: PASS
```

告警四档均为 4（基线，全在第三方 qrcodegen）。批次的两次额度已用完；之后只动了 `tests/run_all.sh` 和文档，不影响固件。

**主机单测全量**（`bash tests/run_all.sh`，MSVC）：

```
[irq_handler] PASS (3 个 IRQ 被引用，0 个没有处理函数)
[prodlink] PASS（test_prod_link_only: PASS=32 FAIL=0）
[drvpure] PASS          （test_drv_pure: PASS=69 FAIL=0）
[audit] PASS (报告与代码一致: 210 files, 0 uncovered, 0 unclassified)
[card_surface] PASS (7 cards)
[rtt_count] PASS (16)
[mut-estop] PASS（mutation_estop_gate: 20 个变异体，0 个异常）
[touchsession] PASS     [post] PASS (81)   [registry] PASS (74)   [control] PASS (73)
esp32-tests: ALL GREEN
run_all: ALL GREEN
```

**变异核对**（一次性脚本，做法同 `tests/mutation_estop_gate.py`）：对本批次新增 / 改动的四组用例逐个改坏实现，34 个变异体全部被杀，0 个活下来、0 个编译失败。其中四个就是原缺陷本身：F01（状态读失败当无触摸）、F02（6 字节读失败照样解析）、S01（`_sbrk` 无上界）、X01（交付档照收执行器钩子）。

**红先于绿**（每条在未修代码上的那一次）：

| 条目 | 未修代码上的红 |
|---|---|
| M02-F-01/02/04 | `test_touch_session` 新用例编译失败；抽取桩：状态读失败 → rc=0 valid=1；6 字节读失败 → rc=0 valid=1 touched=1 (1023,599) |
| M02-F-05 / M01-F-08 | 抽取桩：旧 `_sbrk` 1,000,000 字节全成功；旧 SDRAM 图样漏 A16~A21、D0~D11 |
| M06-F-12 | `test_post` 新用例编译失败 |
| M01-F-10 | `[irq_handler]` 在 bf854ce 上 FAIL |
| P1-06 | 回滚（去掉 `control_init` 的门控）→ `test_prod_link_only` FAIL=9 |
| X03-F-10 | 反汇编 `cpsie i` |
| M05-F-18 | MSVC C4210 ×2 |

## 3 专题：触摸总线被拉死时的主循环节拍（M02-F-01）

**下面的数字都是按 HAL 源码和调用关系推算的，没有真机实测。** 量法见 §3.3。

### 3.1 修前（基座 bf854ce）

- `ft5446_read()` → `ft5446_rd_reg()` → `HAL_I2C_Mem_Read()`。总线卡死时它要么在数据相位等到超时（`FT5446_I2C_TIMEOUT_MS` = 20ms），要么在开头的 BUSY 等待里超时（`I2C_TIMEOUT_BUSY` = 25ms，`stm32h7xx_hal_i2c.c:344`，用在 `:2682`），然后返回 HAL_ERROR。
- 调用方不判：`sta` 保持 0 → 按"合法无触摸"返回 0。
- 一拍两次读：主循环每拍 `tcs_lvgl_estop_watch()` 一次；`lv_timer_handler()` 里的读回调在 LVGL 定时器到点时再一次——节拍一旦超过它的周期（约 33ms），就每拍都读。
- 所以：**每拍阻塞 40~50ms，节拍约 41~53ms，而且一直如此**（直到复位）。两次 `kill_key_poll()`（KEY0 急停）之间也就隔这么久：KEY0 去抖要连续两个采样，按下到触发的上界从约 20ms 变成约 100ms。链路收发、`comm_tick`、快照回灌同样被拖慢；RX 环 512 字节在 230400 波特下只够 22ms，会溢出丢帧。

### 3.2 修后（本分支）

- 在线时每次读 / 写事务前先看 PD12 / PD13 电平（`ft5446_lines_idle()`，两次 GPIO 读）：不是都高就直接报失败，不进 HAL。
- 连续 3 次轮询失败判离线（按每拍一次轮询算 ≤ 30ms），之后 `ft5446_read()` 第一行就返回，不再碰总线。
- 恢复流程每拍最多一步。各步耗时估算（480MHz）：
  - 清总线：最多 9 个脉冲，半周期约 5~8µs，加 STOP 和两次引脚模式切换，≤ 0.3ms；
  - I2C4 复位重配：HAL 调用无等待，几十 µs；
  - 写配置：4 次事务，从机应答时约 0.4ms，不应答时更快且第一个失败就停；
  - 回读核对：3 次事务，约 0.3ms；
  - 复位 IC 的两步各一条 GPIO 写；中间的 20ms / 300ms 靠"等时间到"，不占节拍。
- 两线一直被拉死时，每一轮只有"清总线"一步会执行（约 0.3ms，每 100ms 一次），后面的步骤不碰 I2C 外设。
- **最坏情况**：总线恰好在电平预检之后、事务中途死掉，那一次事务等到超时，单拍最多多 20ms；这是一次性的（最多 3 拍就判离线，之后不再进 HAL），不是每拍。
- 结论：稳态下主循环节拍与触摸正常时相同；卡死总线上唯一的变化是每 100ms 一拍多约 0.3ms。

### 3.3 真机量法（上机卡用；短接是物理操作，只能由用户做）

符号地址取自最终 PROD 产物 `build-matrix/PROD/tcs_main.elf`；换固件后用第一条命令重新取。

```bash
cd project_tcs
arm-none-eabi-nm build-matrix/PROD/tcs_main.elf | grep -E " (g_stat_work_us_max|g_stat_work_us_avg|g_stat_tick_ovf|g_stat_loop_passes|g_dbg_snap_gap_max|g_ft5446_buserr_cnt)$"
# 读一个：probe-rs read --chip STM32H743II b32 0x<地址> 1      （接了多个探针时加 --probe <序列号>）
```

| 符号 | 含义 | 地址（本批次 PROD） |
|---|---|---|
| `g_stat_work_us_max` | 开机以来单拍工作时间的最大值（µs） | 0x20002D24 |
| `g_stat_work_us_avg` | 单拍工作时间的滑动平均（µs） | 0x20002D20 |
| `g_stat_tick_ovf` | 单拍工作超过 10ms 的累计拍数 | 0x20002D18 |
| `g_stat_loop_passes` | 主循环累计圈数 | 0x20002D28 |
| `g_dbg_snap_gap_max` | 相邻两帧 0x20 快照的最大间隔（ms） | 0x20001018 |
| `g_ft5446_buserr_cnt` | 触摸读事务里 NAK 以外的失败（新增；基座没有） | 0x20003AD8 |

步骤：

1. 上电，停在首页，手不碰屏，等 30s。读上表六个值，记为"短接前"。`g_stat_work_us_max` 此时就是日常上限（局部刷新应在几 ms；整页重建可以到 20ms 以上，所以它是开机以来的最大值，不要求小）。
2. **短接触摸 SDA（PD13）到 GND**（开漏线，直接短接安全）。等 10s，期间不要碰屏。
3. 再读六个值，记为"短接后"。两个"增量"= 短接后 − 短接前（都是无符号 32 位，直接相减）。
4. 断开短接，等 2s，读 `s_tp_link`（地址 0x20003054，32 字节）第一个字节 = 0 表示已回到在线。
5. 想量"修前"作对照：烧基座固件（bf854ce）重复 1~3（基座没有 `g_ft5446_buserr_cnt` 和 `s_tp_link`，跳过）。

| 读数 | 修前（推算） | 修后（应得） | 通过判据（修后） | 不通过 |
|---|---|---|---|---|
| `g_stat_work_us_max` 增量 | 升到 ≥ 40,000（预期 41,000~53,000）并保持 | < 1,000 | 短接没有抬高单拍最大工作时间 | 增量 ≥ 20,000 = 预检没拦住，仍进了 HAL 超时 |
| `g_stat_work_us_avg` 增量 | 升到 ≥ 40,000 | < 1,000 | 同上 | 同上 |
| `g_stat_tick_ovf` 10s 增量 | ≈ +190 ~ +240（每拍 +1） | 与同样 10s 的空闲期相同（首页空闲应为 0） | 增量 = 0 | ≥ 10 |
| `g_stat_loop_passes` 10s 增量 | ≈ 190 ~ 240 | ≈ 1,000 | ≥ 950 | ≤ 500 |
| `g_dbg_snap_gap_max` | 变大（RX 环溢出丢帧后可达数百 ms） | 不变 | ≤ 短接前 + 10 | 明显变大 |
| `g_ft5446_buserr_cnt` 增量 | （无此符号） | +2 ~ +6：判离线前那几次轮询各记一次，之后清总线不走 HAL、不再增长 | 落在 +2 ~ +6 | = 0 且触摸离线 = 预检没生效；> 50 = 离线后还在进 HAL |

读探针会让内核停一下，前后两次读的间隔里 `g_stat_loop_passes` 会少几圈，按 10s 的 ±5% 看，不影响判据。

## 4 专题：交付档上电初始化（P1-06 残余）

### 4.1 基座的交付档上电做了什么

| 初始化 | 基座交付档 | 碰的引脚 / 外设 | 现在 |
|---|---|---|---|
| `MX_UART_Init()` 的 USART3 段 | USART3 时钟、引脚 AF7（RX 上拉）、`HAL_UART_Init`、FIFO、NVIC 使能 `USART3_IRQn` | PB10、PB11、USART3 | 不执行 |
| `MX_I2C1_Init()` | I2C1 时钟、引脚 AF4 开漏上拉、`HAL_I2C_Init` | PB8、PB9、I2C1 | 不执行 |
| `eeprom_init()` | PH4 推挽 / PH5 开漏输出，位探测，整片读约 120ms（结果随即丢弃） | PH4、PH5 | 不执行 |
| `fan_init()` | PB14 推挽输出，写高（关） | PB14 | 不执行 |
| `m207_init()` | 挂 USART3 单字节 RX 中断 | USART3 中断 | 不执行 |
| `control_init()` 的执行器钩子 | 接入 `fan_set` / `tec_sw` / `tec_temp` | — | 交付档传空，且 `control_init` 拒收 |
| **运行期**：`tcs_emergency_stop()`（屏幕热区 / KEY0 / 控制台 `estop` 同一入口）和 `control_observe_temp()` | 都调 `out_fan(1)` → PB14 **写低**（= 风扇开）。交付档不跑 `control_poll`，这个低电平保持到复位 | **PB14** | 钩子为空，不写任何引脚；急停照旧置本地自持和报警位、经 `s_cmd_hook` 发 cmd 8 |
| `tmp117_init()` | 只存句柄、清软件状态、置 `g_tcs.body_sensor_ok = 1` | 无 | **保留**（见 4.2） |
| `post_init()` 的 TMP117 探测 | I2C1 上 3 次读 | I2C1（PB8/PB9） | 交付档不探测，该项固定记 FAIL |

### 4.2 门控了什么、没门控什么（依赖检索）

检索命令（`git grep -nE`，范围 `project_tcs/Core project_tcs/App`；完整输出留在实施记录 `h1a_work/p106_grep.txt`）：

| 被门控 | 检索式 | 交付档里剩下的引用 | 结论 |
|---|---|---|---|
| `fan_init` | `fan_init\|fan_set\|fan_get` | 只有 `fan.h` 的定义、`control.c` 判空调用、`main.c` 的门控块 | 门控 |
| `m207_init` / USART3 | `m207_init\|huart_m207\|USART3` | `stm32h7xx_it.c` 的 `USART3_IRQHandler` 和按实例分发的两行 | 门控。中断入口在，但 `USART3_IRQn` 不再使能，进不来；`m207_set_temp` 等只往发送队列压，不碰 UART |
| `eeprom_init` | `eeprom_init\|eeprom_ready\|eeprom_read_image\|eeprom_commit_image\|eeprom_selftest` | `console.c:409-410` 的 `ee` 命令（先问 `eeprom_ready()`）、`main.c:468` 的提交钩子 | 门控。没初始化时 `eeprom_ready()` = 0，`eeprom_commit_image()` 直接返回 -1，都不碰硬件 |
| `MX_I2C1_Init` / I2C1 | `MX_I2C1_Init\|hi2c_tmp117\|I2C1\|tmp117_init\|tmp117_read_devid` | `tmp117_init(&hi2c_tmp117)`、`post.c` 的台架分支 | 门控。交付档没有任何 `HAL_I2C_*` 调用经过 `hi2c_tmp117` |
| `tmp117_init` | 同上 | `main.c:992` | **不门控**：它不碰引脚和外设，但 `status_tx()` 的 0x10 帧在首帧快照到达之前用 `g_tcs.body_sensor_ok`，初值就是它置的（M01-F-10 的教训：本机的 0 会传到网页）。引脚在 `MX_I2C1_Init`，那边已门控 |

### 4.3 `s_ops.*` 调用点走查（交付档）

| 位置 | 调用 | 判空 | 交付档下 |
|---|---|---|---|
| `control.c:86` | `s_ops.fan_set(on)` | `if (s_ops.fan_set)` | 空，跳过；`g_tcs.fan_on` 照写 |
| `control.c:93` | `s_ops.tec_sw(ch, on)` | 判空 | 空，跳过 |
| `control.c:98` | `s_ops.tec_temp(ch, v)` | 判空 | 空，跳过 |
| `control.c:105` | `s_ops.on_state(st)` | 判空 | `main.c` 传的就是 NULL，跳过 |

`control_init()`（`control.c:108`）先把四个钩子清零，交付档只从调用方接 `on_state`。`tcs_emergency_stop()` 的路径：置本地自持和待投递标志 → 置 `ALM_ESTOP` → 模式回手动 → 两个 `out_tec_sw(0)`、`out_fan(1)`（只改 `g_tcs` 显示态）→ `goto_state(SAFE)` → 首次调用 `s_cmd_hook(8)`。`s_cmd_hook` 是 `link_send_cmd_upstream`，`main.c` 无条件装配。没有任何空指针解引用。`test_prod_link_only` 把这条路径走了一遍并断言 cmd 8 恰好发一次、再按不重发、确认后新一轮重发、`control_init(NULL)` 也不倒。

### 4.4 交付档引脚寄存器期望值（合入后用 probe-rs 核对；PB14 必查）

基址：GPIOB = 0x58020400，GPIOH = 0x58021C00。寄存器偏移：MODER 0x00、OTYPER 0x04、OSPEEDR 0x08、PUPDR 0x0C、IDR 0x10、ODR 0x14、AFRL 0x20、AFRH 0x24。
读法：`probe-rs read --chip STM32H743II b32 0x58020400 7`（MODER~BSRR 七个字）、`probe-rs read --chip STM32H743II b32 0x58020420 2`（AFRL、AFRH）。

单个引脚（MODER：00 输入、01 输出、10 复用、11 模拟=复位值=高阻）：

| 引脚 | v1 用途 | 基座交付档 | **交付档期望（修后）** |
|---|---|---|---|
| **PB14** | 风扇光耦，低=开 | MODER[29:28]=01 推挽输出；上电 ODR=1，**急停或超温后 ODR=0 并保持** | **MODER[29:28]=11**、OTYPER14=0、PUPDR[29:28]=00、ODR14=0（从不写）、AFRH[15:12]=0 |
| PB10 | USART3_TX → M207 | MODER=10、AF7 | MODER[21:20]=11、AFRH[7:4]=0 |
| PB11 | USART3_RX | MODER=10、AF7、上拉 | MODER[23:22]=11、AFRH[11:8]=0、PUPDR=00 |
| PB8 | I2C1_SCL → TMP117 | MODER=10、AF4、上拉、开漏 | MODER[17:16]=11、AFRL[19:16]=0、PUPDR=00、OTYPER8=0 |
| PB9 | I2C1_SDA | 同上 | MODER[19:18]=11、AFRL[19:16]=0、PUPDR=00、OTYPER9=0 |
| PH4 | EEPROM SCL | MODER=01 推挽输出，ODR=1 | MODER[9:8]=11、OTYPER4=0、ODR4=0 |
| PH5 | EEPROM SDA | MODER=01 开漏输出，ODR=1 | MODER[11:10]=11、OTYPER5=0、ODR5=0 |

整寄存器（上电后停在首页；"基座"一列是按基座源码里的引脚配置推的，没有实测）：

| 寄存器 | 基座交付档（推算） | **交付档期望（修后）** | 说明 |
|---|---|---|---|
| GPIOB_MODER | 0xDFAAF7F5 | **0xFFFFF7F5** | 只有 PB0 / PB1（LED）、PB5（背光）是输出，其余全高阻 |
| GPIOB_OTYPER | 0x00000300 | **0x00000000** | I2C1 的开漏位没了 |
| GPIOB_OSPEEDR | 0x00F00800 | **0x00000800** | |
| GPIOB_PUPDR | 0x00440400 | **0x00000400** | 只剩背光的上拉 |
| GPIOB_ODR | 0x00004023（上电，PB14=1）/ 0x00000022 或 0x00000023（急停后 bit14 变 0） | **0x00000022 或 0x00000023** | bit0 = LED1 每 500ms 翻转；bit14、bit11:8 恒 0 |
| GPIOB_AFRL / AFRH | 0x00000000 / 0x00007744 | **0 / 0** | |
| GPIOH_MODER | 0xAAAA15AF | **0xAAAA1FAF** | PH4 / PH5 高阻；PH6（触摸复位）输出、PH7（触摸 INT）输入不变 |
| GPIOH_OTYPER | 0x00000020 | **0x00000000** | |
| GPIOH_ODR | 0x00000070 | **0x00000040** | 只有 PH6 = 1（触摸 IC 复位已释放） |
| RCC_APB1LENR | bit18（USART3EN）= 1、bit21（I2C1EN）= 1 | **两位都是 0** | 时钟也不再开 |

IDR 的 PB14 / PB8~11 / PH4 / PH5 读数取决于外部接线（H743 不驱动），不设期望值。

### 4.5 `g_shown_fb` 为什么保留

`git grep -n g_shown_fb`（全仓）：
读者——`ltdc.c:603`（`gfx_use_shown_buffer`）、`:608`（`gfx_use_hidden_buffer`）、`:637`（`gfx_sync_rect`）；`tools/fb_dump.py:46/51/64`（`nm` 找符号 + 探针读）；`tools/rehearsal.py:111/115`（探针读，等第一次翻页）。`lv_port_disp.c:14` 有一行 extern，该文件里没用。
写者——删掉 `ltdc_show_buffer()` 后只剩 `ltdc_set_fb()`（上电清屏时经 `gfx_flip` 调两次）。
上电两次之后 `g_shown_fb = LCD_FRONT_BUF`，与实际 CFBAR 一致，`fb_dump.py` 读到的还是正在扫描的那块。**注意**：`gfx_use_shown_buffer` / `gfx_use_hidden_buffer` / `gfx_sync_rect` 三个函数本身在固件里零调用（LVGL 直接写两块帧缓冲），`rehearsal.py` 等的"第一次翻页"也只发生在上电。

## 5 真机验证步骤（M02-F-01 关单）

前提：BENCH 档（要用台架钩子）；步骤 2、3 在交付档上也成立。触摸 FFC 在屏组件背面，拔插前断电。

1. **基线**：上电，RTT 应有 `ft5446 ver=0001 cfgwr=1 ver_ok=1`。手指点屏各页正常；`s_tp_link` 第一字节 = 0。
2. **强制恢复（不用拔线）**：探针把 `g_tp_force_recover`（BENCH 档，地址用 `nm` 取）写 1。
   期望：RTT 先后出现 `[BENCH] touch link forced offline -> exercising recovery` 和 `[SYSTEM] touch link back online: tries=1 offline=<430~500>ms recoveries=1`（退避 100ms + 复位 20ms + 等 ROM 300ms，其余是每拍一步的零头）；屏上正在进行的按压被当成抬起；0.5s 内触摸恢复；每做一次 `recoveries` 加 1。建议脚本连做 100 次。
3. **拔线**：断电拔掉触摸 FFC，上电。RTT `ver_ok=0`，约 100ms 后开始每约 420ms 一轮恢复；屏不响应触摸是预期的；**KEY0 急停照常**（串口 `KEY0 estop`）。按 §3.3 量节拍（这一步是 NACK 型失效，不卡总线，节拍本来就不受影响，量出来应与基线相同）。
4. **插回（带电插拔）**：断电插回 FFC 再上电的，上电直接在线。要验证"运行中恢复"，在 BENCH 档带电插回（排线 3.3V / GND / SCL / SDA / INT / RST，先对准再压下）：≤ 0.7s 内 RTT 出现 `touch link back online`，触摸恢复，`recoveries` 加 1。
5. **重复 20 次**（步骤 2 或 4）：恢复率 100%；每次 `offline` 时长在 0.45~0.8s。
6. **卡死总线**（可选，§3.3）：短接 SDA 到地 → RTT `[WARN] touch link offline: 3 polls failed in a row -> recovering`；松开后恢复。
7. **急停入口回归**：首页底栏急停键长按 600ms 仍触发（`[SAFE]` 日志 / `g_estop_calls` 加 1，BENCH 档有这个钩子）；触摸离线时 KEY0 触发。

## 6 需要人看的点

1. **在线判定换了凭据**（M02-F-04）：配置回读一致才算在线。真机首次上电必须 `ver_ok=1`。依据是 WP-M 的回读实测，不是这次上机测的。
2. **交付档不再接 fan_set**（P1-06）：急停在交付档从此不写 PB14。样机上 PB14 若真的还接风扇光耦，这是行为变化（变化的方向是"不再和 ESP32 抢"）。
3. **PLL3 / 刷新率的 HAL 返回值现在要判**（M02-F-11）：如果真机上 HAL 本来就返回错，现在会不点屏。首上电看 RTT。
4. **SDRAM 自检范围扩到 3.6MB**（M02-F-05）：自检会在 LVGL 渲染块里留 900 个散点，LVGL 每次刷新都整块重画，不受影响。
5. **交付档 `ee` 控制台命令**从此恒回 `ee offline`；`eeprom_*`、`m207_*` 的一部分函数还编在交付镜像里（都不碰未初始化的硬件）。清不清归 h1b。
6. **交付档 POST 的 TMP117 项固定 FAIL**（P1-06）：交付档不跑布防，它只出现在 `post` 命令里。要不要给它一个"不适用"的语义，需要定。
7. 中间提交 2~7 没有逐个完整构建：每个提交暂存时对改动文件做了 -Werror 语法检查（相关档位），整体以最后的矩阵为准。做二分时请以 365e351 为已知 good。

## 7 过程记录

- **分块暂存插错位置（已更正）**：按条目拆提交时用 `git apply --unidiff-zero` 挑块暂存，纯插入块被按新文件行号定位，`post.c` 的 `#include` 和 `#elif` 块两次落错行——第二次 `-Werror` 语法检查是过的（编得过但语义错：交付档分支里多了一行 `post_set_result(TMP117, 1)`），靠人工看暂存区那段才发现。两对坏提交（5c6b980 / 143f33a、92ded93 / a89cb02）已回滚重做，分支上现在是 9b18ba7 / b848cc4；工具改成按旧行号重编号，并在每次暂存后用独立实现逐字节核对暂存结果。
- 交付档用例 `[prodlink]` 第一版成功时不打印，看不出跑没跑；已补 PASS 行（365e351）。

## 8 合入后的真机核对（集成分支 fix/s-u-int2 @ 7b06668，2026-11-25 20:25；交付档固件；探针只读）

§4.4 的期望值是按源码推的、当时没有实测。这里是设备上读到的（H743 交付档，烧录后停在首页，ESP32 交付档在线）：

| 寄存器 | 实测 | §4.4 期望 | 结论 |
|---|---|---|---|
| GPIOB_MODER | 0xFFFFF6B5 | 0xFFFFF7F5 | **本条范围内的引脚全部相符**：PB14 [29:28]=11、PB11 / PB10=11、PB9 / PB8=11，都是高阻。不同的是 PB3 与 PB4：实测 PB3=10（复用）、PB4=01（输出），期望写的是 01 / 11——§4.4 按源码推算时把这两个显示相关引脚推错了。本批次的改动没有碰它们（合并的 diff 里没有 GPIOB 引脚初始化的增删行）。期望值以实测为准 |
| GPIOB_OTYPER / ODR / AFRL / AFRH | 0 / 0x00000022 / 0 / 0 | 同 | 相符；ODR bit14 = 0（从不写） |
| GPIOB_PUPDR | 0x00000500 | 0x00000400 | PB14 / PB11 / PB10 / PB9 / PB8 的上下拉位都是 00；多出的两位在 PB4 / PB5 |
| GPIOB_OSPEEDR | 0x00000CC0 | 0x00000800 | 不在本条范围 |
| GPIOH_MODER / OTYPER / ODR | 0xAAAA1FAF / 0 / 0x00000040 | 同 | 完全相符：PH5 / PH4 高阻，PH6 = 1 |
| RCC_APB1LENR | 0x00020000 | bit18、bit21 = 0 | USART3EN / I2C1EN 都是 0，时钟没开 |
| s_tp_link.state | 0（TP_LINK_ONLINE） | — | **触摸链路在线**：新的"配置寄存器回读一致才算在线"在本机首次上电通过 |

读法：`probe-rs read --chip STM32H743II --probe 0d28:0204 b32 0x58020400 7`（GPIOB MODER 起七个字）、`… 0x58020420 2`（AFRL / AFRH）、
`… 0x58021C00 7`、`… 0x58021C20 2`、`… 0x580244E8 1`（RCC_APB1LENR）；`s_tp_link` 用 tools/mb_console.py 的符号读取。

启动时那一行 `RTT: ft5446 ver=… ver_ok=…` 没抓到：RTT 上行环只有 1024 字节，镜像泵每 800ms 打一行 `[NET] mirror`，约 30 秒就绕一圈。
在线结论取自 `s_tp_link.state`。

没做的：真手指触摸（交付档没有注入钩子）；§5 的强制恢复 20 次与拔插排线，要在台架档会话里做或由用户做。
