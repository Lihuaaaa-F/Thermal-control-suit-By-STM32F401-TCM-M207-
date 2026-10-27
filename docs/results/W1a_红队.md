# W1a 红队评审（ft5446 → ST HAL I2C4：配置编译步）

- 评审人：红队独立会话（只信代码与证据，制方叙述不采信）
- 对象：W1a=ft5446.c 传输层 HAL 化 + i2c_timing_utility.{c,h} + docs/ref/CubeH7_I2C/ + cubemx/ + docs/evidence/W1/
- 非对象：G 线未提交改动（main.c/CMakeLists/ltdc*/control.c/flash.ld/third_party/build-*/LVGL_G4_进度）
- 日期：2026-10-27；分支 feature/lvgl-port
- 状态：**评审完成——整改后增量复验：放行（F1/F2/F4-F12 通过；F3 矩阵注记/F13 commit 为关单动作；残留微瑕 2 条；见 §7）**

## 1 逐 DoD 证伪记录

### 1a 门禁三跑

| 破坏尝试（亲手复跑） | 结果 | 结论 |
|---|---|---|
| `arm-none-eabi-nm build/tcs_main.elf \| grep -c ct_iic` | **0** | 未被证伪。另核 `build/CMakeFiles/tcs_main.elf.dir/Core/Src/ft5446.c.obj` 与 build-sim 同名 obj，均 0；位bang 符号未进默认构建 |
| `grep -c "HAL_I2C_Mem_" Core/Src/ft5446.c` | **2**（L340 Mem_Write、L364 Mem_Read） | 未被证伪 |
| `cd project_tcs && cmake --build build && cmake --build build-sim` | 双 exit 0；日志仅 1 条 `LOAD segment with RWX permissions`（链接器警告，双构建同） | 增量绿；新警告 0（见下注） |
| 强制单 TU 复编 `arm-none-eabi-gcc <build.ninja 同 flags> -c Core/Src/ft5446.c` | exit 0，**stderr 0 行**（HAL 态与 `-DFT5446_BITBANG` 态各一次） | 当前源码零警告，不依赖增量缓存 |
| `git diff --stat` 核 W1a 集 | 仅 `project_tcs/Core/Src/ft5446.c`（108+/4-）+ 未跟踪新文件；`qrcodegen.h`/`tcs_ui.c` 不在 W1a diff 内 | 存量 qrcodegen.h:359/tcs_ui.c:172 警告非 W1a 新增；本次增量未重编这两个 TU，故日志未复现，但来源已隔离 |
| §1c W1 附加门禁：`grep -c "GPIO_PIN_1[23]" Core/Src/ft5446.c` | **3**（L57/L59 位bang 宏、L105 HAL 初始化），期望 **0** | **红**。制方 §4 自述“引脚在 ft5446_i2c4_init、MspInit 待 G 线合入后迁移”，但未说明该机器门禁实际为 3；见 F3 |
| `arm-none-eabi-nm build/tcs_main.elf \| grep -c I2C4` | **2**（`I2C4_ER_IRQHandler`/`I2C4_EV_IRQHandler` 弱符号；handle 为 static） | 旁证：hi2c4 文件局部，无全局符号污染 |
| map 体积 | text **164520**、data 4404、bss **71160**；基线 163600（W4_2_红队.md L33 干净态）/68512（制方报告） | +920B/+2648B 与制方数字一致；当前 map 实测复核通过 |

结论：三跑主体全绿；附加的 §1c 引脚文本门禁为红（F3）；零警告结论成立（F1 的地址 bug 不是编译器能发现的警告）。

### 1b 官方计算器零改动验证（sha256 + curl 逐字节）

| 破坏尝试 | 结果 | 结论 |
|---|---|---|
| `sha256sum Core/Src/i2c_timing_utility.c` vs `docs/ref/CubeH7_I2C/SHA256.txt` | `31fdefa45cc0a9fe7fdc96c34f6f679e5d49841a72b5278a9f23e61e733a1032` 完全一致；头文件 `04b58517705faeb4a85abff3ce4902c761c09d33adcf38cd2304aabb3b7a71e0` 一致 | 留档自洽 |
| `curl raw.githubusercontent.com/.../8ec3113d.../Projects/NUCLEO-H723ZG/Examples/I2C/I2C_TwoBoards_ComPolling/Src/i2c_timing_utility.c` | 远端 sha256 同为 `31fdef...`；头文件同为 `04b585...` | 与 GitHub 锚定 commit 一致 |
| `tr -d '\r'` 后 `cmp` | C 与 H 均 **IDENTICAL_AFTER_CR_STRIP** | 官方锚定成立（去 CR 后逐字节相同） |
| 出处路径交叉核对 | 本地三份 `I2C_TwoBoards_Com*_main.c` 的 sha256 分别为 `0d5eea...`/`c7af8a...`/`601baa...`，逐一等于 **NUCLEO-H743ZI** 远端同型；NUCLEO-H723ZG 的 ComPolling main.c 为 `3ccdac...`，且 **不含** L39-42 的 `I2C_TIMING 0x00901954` 注释 | 制方 §2.3“NUCLEO-H723ZG main.c L39-42 交叉锚定”**路径与数值出处均不成立**：main.c 来自 H743ZI，utility 来自 H723ZG；见 F4 |
| 官方静态值复现 | 零改动计算器 `I2C_GetTiming(100000000,400000)` = **0x20B11829**（新鲜进程），不是 0x00901954；0x00901954 字段为 PRESC=0/SCLDEL=9/SDADEL=0/SCLH=25/SCLL=84，按 utility 的 fast 参数（tr=250/tf=100）在 100MHz 下约 625kHz，非 400k 合法解；在 50MHz+tr100/tf10 下反推才约 398kHz（该参数化下零改动计算器实际给 0x0090303C，tie-break 差异） | “100MHz→0x00901954”旁证在数值上不成立；该值未被代码采用，故不构成功能缺陷，但证据链必须更正（F4） |

另：`UPSTREAM.md` L10-13 仍写“本工程 I2C4 内核配 **PLL3R=100MHz**……400k Timing=0x00901954 原样采用”“100k 路径 #error 防误用”，与当前实现（D3PCLK1=120MHz + 运行时计算器，无 #error）**直接矛盾**；这是制方 §2 已明确否掉的旧方案，文档未随实现更新（F4）。

### 1c 计算器正确性抽查（PC 独立复算）

独立复算方式（不使用制方脚本）：WSL Ubuntu 20.04 + gcc 9.4，直接把零改动 `i2c_timing_utility.c` 与自写小 main 编译为 x86-64 程序；官方头文件只依赖 `uint32_t`，用 `shim/stm32h7xx_hal.h`（仅 `#include <stdint.h>`）隔离 HAL。每次取值均在**新鲜进程**中完成（该计算器有静态累加器，见下）。

| 输入 | 返回值（新鲜进程） | 解码/合规性 |
|---|---|---|
| `I2C_GetTiming(120000000, 400000)` | **0x20E21F33** | PRESC=2/SCLDEL=14/SDADEL=2/SCLH=31/SCLL=51；tI2CCLK=8ns；SCLL=1314ns、SCLH=834ns、总周期 2498ns→**400.3kHz**；SCLDEL=360ns≥250+100；SDADEL=48ns∈[26,358]；SCLH≥600、SCLL≥1300。合规 |
| `I2C_GetTiming(120000000, 100000)` | **0x70D04648** | PRESC=7/SCLDEL=13/SDADEL=0/SCLH=70/SCLL=72；总周期 9698ns→**103.1kHz**（80-120k 内）；SCLDEL=896ns≥640+250；SCLH=4610≥4000；SCLL=4738≥4700。合规 |

`I2C_Charac_t` 抽查（官方文件 L78-122，hash 与官方一致故即官方值）：
- 400k 行：freq=400000、min=320000、max=480000、hddat_min=0、vddat_max=900、sudat_min=100、lscl_min=1300、hscl_min=600、trise=250、tfall=100、dnf=0；
- 100k 行：freq=100000、min=80000、max=120000、hddat_min=0、vddat_max=3450、sudat_min=250、lscl_min=4700、hscl_min=4000、trise=640、tfall=20、dnf=0；
- 与 I2C 标准模式/快速模式参数一致，且文件零改动，抽查通过。

**静态状态破坏实验（重要）**：`I2c_valid_timing_nbr` 是文件级静态累加器，`I2C_GetTiming` 从不重置。同一进程先算 400k 再算 100k：
- 400k→100k 顺序：第二次得 **0x20E2BDC1**（PRESC=2/SCLDEL=14/SDADEL=2，SCLDEL=450ns < 100k 要求的 890ns），**非合规**；
- 400k→400k 顺序：仍得 0x20E21F33（本例如同值，但不可依赖）；
- 当前固件只在 `ft5446_init` 中调用一次（`main.c:530` 是唯一调用点），单次启动无影响；任何“运行时 400k→100k 回退”或重复 init 都会取到污染值。见 F7。

W1b 上板对照基准：RTT 行应为 `i2c4 up pclk4=120000000Hz target=400000Hz timing=0x20E21F33`（前提 PCLK4=120MHz、单次调用）；若不符，先查时钟树/调用顺序，不要先怀疑计算器。

### 1d 内核时钟链核对（D3PCLK1 语义）

亲手推演（不采信注释）：
1. `main.c:323-345`：HSE=25MHz，PLL1 M=5 → 5MHz；N=192 → 960MHz VCO；P=2 → **SYSCLK=480MHz**；AHB DIV2 → **HCLK=240MHz**；APB4 DIV2 → **PCLK4=120MHz**。
2. `Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal_rcc_ex.c:2921-2929`：`HAL_RCCEx_GetD3PCLK1Freq()` = `HAL_RCC_GetHCLKFreq() >> (D1CorePrescTable[(RCC->D3CFGR & RCC_D3CFGR_D3PPRE) >> 4] & 0x1F)`。
3. `Drivers/CMSIS/.../system_stm32h7xx.c:153`：`D1CorePrescTable[16] = {0,0,0,0,1,2,3,4,1,2,3,4,6,7,8,9}`；`RCC_APB4_DIV2`=`RCC_D3CFGR_D3PPRE_DIV2`=0x4<<4（`stm32h7xx_hal_rcc.h:469`），索引 4 → 表值 1 → 240MHz>>1=**120MHz**。
4. `stm32h7xx_hal_rcc_ex.h:886`：`RCC_I2C4CLKSOURCE_D3PCLK1 = 0`；`stm32h7xx_hal_rcc_ex.c:1309-1322` 对 `RCC_PERIPHCLK_I2C4` 写 `I2C4SEL`，选 D3PCLK1。`HAL_RCCEx_GetD3PCLK1Freq()` 返回的正是 I2C4 内核频率 120MHz，与计算器输入一致。
5. PLL3 冲突核对：`ltdc_init` 内配 PLL3 给 LTDC 50MHz（`main.c:360` 注释/`ltdc.c`），I2C4 走 D3PCLK1 不碰 PLL3；制方否掉 PLL3R=100MHz 方案的理由成立。

偏差：`ft5446.c:94` 注释“D3PCLK1=120MHz@本树 **480/2**”算式不完整——480/2=240 是 HCLK，PCLK4 还需再 /2 才是 120；数值对、推导错（F9）。官方 H743ZI 例程的 APB1=100MHz 与本工程 APB4=120MHz 不是同一时钟域，静态 0x00901954 不可直接搬用（支持运行时计算器路线）。

结论：内核链语义与 120MHz 输入**未被证伪**；注释微错 F9。

### 1e 位bang 保留期（-DFT5446_BITBANG 编译 + 守卫配对扫描）

| 破坏尝试 | 结果 | 结论 |
|---|---|---|
| `arm-none-eabi-gcc <同 flags> -DFT5446_BITBANG -c Core/Src/ft5446.c -o /tmp/ft_bitbang.o` | exit 0，stderr **0 行** | 位bang 域可编，0 error/0 warning |
| 同 flags 默认（HAL）`-c` | exit 0，stderr **0 行** | HAL 域可编，0 warning |
| Python 预处理栈扫描（`#if/#ifdef/#ifndef/#elif/#else/#endif`） | max depth=3，leftover=0，mismatches=0 | 守卫配对平衡 |
| 守卫覆盖抽查 | 引脚宏（L56-60）、7 个 ct_iic 声明/定义、delay_us、diagnose 的 SDA 直驱段均已 `#ifdef FT5446_BITBANG` 包裹；`FT5446_BITBANG` 以注释形式保留（L81），宏名入册 | 主体保留期成立 |
| 死代码扫描 | `delay_us_init()`（L188-191）与 `g_fac_us`（L187）**不在** `#ifdef` 内，且 `ft5446_init` L433 无条件调用；HAL 态下 `delay_us` 被编掉，`g_fac_us` 只写不读 | **部分守卫**：HAL 默认构建存在死代码（F8，§1d 第 3 条 ✗） |
| 新 RTT 日志格式 | `%lu/%08lx`；W4.2 官方 SEGGER printf 会过滤 'l' 并按 32-bit int 读参，ARM 上 `unsigned long`=32-bit，ABI 兼容 | 不会重演 F16；但 3 个新调用点触发 rtt_count 门禁（F2） |

结论：保留期编译与配对扫描通过；`delay_us_init` 守卫缺口 F8。

## 2 主动破坏点

### 2a NAK 计数语义漂移

- 位bang 版（`git show HEAD:...ft5446.c`）：`g_ft5446_nak_cnt++` 出现在**三个检查点**（写地址、写寄存器号、读地址），但每次都紧跟 `return`，单次事务最多 +1；只统计 `ct_iic_wait_ack()` 的 SDA 高（真 NAK）。
- HAL 版（L364-369）：`HAL_I2C_Mem_Read(...) != HAL_OK` 即 +1。HAL 的返回把 **NAK（HAL_ERROR，ErrorCode=AF）**、**超时（20ms）**、**总线错误/仲裁丢失/参数错**、**HAL_BUSY（句柄忙）** 全部折叠为“非 HAL_OK”。
- 对 W1d 决策的定性：分母 `g_ft5446_read_cnt` 语义不变（每次 rd_reg 前 +1）；分子从“NAK 事务数”变成“全部失败读事务数”。400k 下若出现总线被拉低导致 20ms 超时，会被记成 NAK，0.5% 阈值会**提前触发降档**（保守方向，不会漏掉真 NAK，但会误判故障类型）。建议只对 `hi2c4.ErrorCode & HAL_I2C_ERROR_AF` 累加，或把计数器改名并在 W1d 报告中同时给 AF/超时/忙三列。见 F5（一般，W1d 前置）。

### 2b Mem_Read 20ms 超时 WCET 推演

- 正常路径（400k，6B 读）：地址写 9bit + 寄存器号 9bit + 重复起始/读地址 9bit + 6×9bit = 81bit ≈ **202.5µs**；加 HAL 轮询/函数开销约 0.2-0.3ms。1B 读：36bit ≈ **90µs**。100k 档分别约 0.81ms/0.36ms。与册 §2.3 的“触摸 ≈253µs（101bit）”同量级。
- 故障路径：`FT5446_I2C_TIMEOUT_MS=20`；`HAL_I2C_Mem_Read` 在 L2680 取一次 `tickstart`，后续 `I2C_WaitOnFlagUntilTimeout` 共用同一个起点（L2682/2697/2722），故超时是**整次调用 20ms**，不是每字节 20ms。一次触摸 tick 若先读 0x02（1B）再读 0x03（6B）且两次都超时，最坏 **40ms**；空闲 tick 最坏 20ms。20ms 约为 6B 正常传输的 **99 倍**。
- 上下文：`ft5446_read` 调用点为 `tcs_ui.c:1408`（主循环 `tcs_ui_poll`）与 `lvgl_port` indev（主循环/任务），`stm32h7xx_it.c` 无 ft5446 调用；**未在中断上下文调用**，“禁中断”铁律成立。主循环 `control_poll` 为 500ms 节拍（`main.c:618`），40ms 故障阻塞不会破控制死线，但会吞掉 1-4 个 10ms 触摸节拍，并推迟 comm/tcs_ui。
- 结论：册 §2.3 的“≤250µs”只覆盖正常路径；故障路径 WCET 是 20-40ms，制方报告未写。建议 W1b 记录 `g_stat_touch_us_max` 并考虑把超时降到 2-5ms，或明确保留 20ms 的理由（与 tmp117 同口径）。见 F10（微瑕/一般）。

### 2c probe 语义等价性（8bit wr_cmd vs 7bit addr）

**坐实缺陷（重大）**：
- `FT5446_CMD_WR = 0x70`（`ft5446.h:27`，即 7-bit 0x38 左移一位的 8-bit 写地址）。
- 位bang 分支 `ct_iic_send_byte(wr_cmd)` 直接把 0x70 发上总线，正确。
- HAL 分支 L382 传 `(uint16_t)(wr_cmd >> 1)` = **0x38**。HAL 的 `HAL_I2C_IsDeviceReady` 文档明确写：`DevAddress ... The device 7 bits address value in datasheet must be shifted to the left before calling the interface`（`stm32h7xx_hal_i2c.c:3256-3259`）；`I2C_GENERATE_START` 把 `address & I2C_CR2_SADD` 原样写入 CR2（`stm32h7xx_hal_i2c.h:801-807`），硬件以 SADD[7:1] 为 7-bit 地址。于是传入 0x38 → 总线实际寻址 **0x1C**（0x38>>1），不是 0x38。
- 影响面：`ft5446_probe` 只被 `ft5446_diagnose()` 的全地址扫描调用（L417），主读/初始化路径不走 probe；所以是**诊断路径功能错误**，不是触摸主链崩溃。但诊断会把 0x1C 的 ACK 标成 0x38（扫描 `wr=addr<<1`，probe 再右移，等于每个地址都减半），完全失去“找 0x38/找真实地址”的判定能力。
- 正确写法：`HAL_I2C_IsDeviceReady(&hi2c4, wr_cmd, 1, FT5446_I2C_TIMEOUT_MS)`（8-bit 写地址原样传入），或 `((wr_cmd >> 1) << 1)`（对偶地址等价于 wr_cmd）。`Trials=1` 语义本身正确（一次尝试），返回 HAL_OK=ACK、其余=NAK 的映射也正确；修掉地址后语义等价。
- 旁证：同文件 `HAL_I2C_Mem_Read/Write` 都直接传 `FT5446_CMD_WR`（0x70），说明作者知道 Mem_* 要 8-bit；probe 的 `>>1` 是唯一的反向转换，属 HAL API 误用。见 **F1（重大 must-fix）**。

### 2d PC 单测回归

执行方式：`bash tests/run_all.sh` 在 WSL Ubuntu 20.04 下跑（gcc 9.4 存在，脚本走 gcc 分支；**未触发 cmd/bat/vcvars**，符合禁则）。结果：

- C 单测 **8/8 PASS**：m207、m207golden、registry、control、post、proto_comm、touchsession、qr；附加 tmp117、m207_adv、font_audit 亦 PASS。
- **`[rtt_count] FAIL: SEGGER_RTT_printf 调用点 16 ≠ 13`**。
  - 归因：HEAD 的 ft5446.c 有 5 处 RTT 调用，当前 8 处（+3：`i2c4 up`、`i2c4 init FAIL`、HAL 态 `tp_diag`）；其余文件 HEAD/当前均为 ltdc 4、it 1、eeprom 1、post 2，合计 13。**16 的增量 3 全部由 W1a 引入**，G 线未改 RTT 调用数。
  - 门禁原文：“调用点恒 13……增删调用点须同步本数并说明”。W1a 未同步，故这是 W1a 造成的**代码性门禁红**（F2，一般 must-fix）。
- 其余 3 项红为执行环境产物，不计 W1a：
  - `qr_verify`：WSL Python 3.8 无 `zxingcpp`；Windows Python 3.12 `import zxingcpp` 成功。
  - `ws_json`：WSL 下 `/d/Espressif/...` 不可解析；Windows 侧 `/d/Espressif/frameworks/esp-idf-v5.5.5/components/json/cJSON/cJSON.c` 实存。
  - `card_paths`：WSL 下 `D:/...` 不可解析；Windows 侧 W8 关单记录为 70 checked/0 bad。
- 备注：touchsession 测试用 `TCS_UNIT_TEST` 编译 ft5446.c，整个 HAL 传输块被排除；它证明上层零改动，但**不覆盖**新 HAL 路径。HAL 路径的真覆盖仍在 W1b A/B（挂账在案）。

结论：W1a 的 C 行为未回归；但 run_all 整脚本非全绿，且唯一代码性红是 W1a 的 rtt_count（F2）。

### 2e i2c_timing_utility 符号污染

- 全仓 `grep -rn "I2C_GetTiming\|I2C_Charac_t"`（排除 docs/ref）只命中：`Core/Src/i2c_timing_utility.c`（定义/typedef）、`.h`（声明）、`ft5446.c`（调用）。`I2C_Charac_t` 是 .c 内部 typedef，无外部链接；`I2C_GetTiming` 在最终 ELF 中为单个 `T` 符号（`0x080064dc`）。
- `CMakeLists.txt` 显式列源文件（L60-83），没有 `file(GLOB Core/Src/*.c)`；`i2c_timing_utility.c` 未被单独编译，因此 `#include "i2c_timing_utility.c"` 的聚合包含不会产生重复定义。build 与 build-sim 均链接通过。
- 潜在陷阱：该文件是 `.c` 却不在 CMake 源列表；未来若有人把 Core/Src 改成 glob 或把该文件加进源列表，`I2C_GetTiming` 会重复定义。建议改名为 `.h/.inc` 或在 CMakeLists 加注释“此文件只允许被 ft5446.c 聚合包含”。见 F11（微瑕/一般，防未来）。

### 2f 发现清单汇总

| 编号 | 级别 | 发现 | 证据 | 处置 |
|---|---|---|---|---|
| F1 | **重大** | HAL probe 把 8-bit 写地址 `0x70` 右移成 `0x38`，`HAL_I2C_IsDeviceReady` 按 8-bit 约定再左移，总线实际寻址 0x1C；诊断扫描地址全部减半 | `ft5446.c:382`；`ft5446.h:27`；`stm32h7xx_hal_i2c.c:3256-3259`；`stm32h7xx_hal_i2c.h:801-807`；`stm32h743xx.h:12849` | **must-fix**：传 `wr_cmd`（或 `(wr_cmd>>1)<<1`）；W1b 诊断扫描复验 0x38 ACK |
| F2 | 一般 | W1a 新增 3 处 `SEGGER_RTT_printf`，`run_all.sh` 的 rtt_count 仍为 13，实测 16 → run_all 红 | `tests/run_all.sh:116-125`；HEAD/current ft5446.c 5→8；总 13→16 | **must-fix**：同步门禁数到 16 并在台账说明，或减少调用点；复跑 run_all |
| F3 | 一般 | §1c 引脚文本门禁 `grep -c GPIO_PIN_1[23]` 期望 0，实测 3（2 位bang 宏 + 1 HAL init）；且该门禁与“位bang 保留一版”本身冲突（保留域必然含 PD12/13 文本） | `ft5446.c:57,59,105`；册 §1c W1 行；册 §2.2 条 4 | **must-fix（DoD 改写/审批）**：把 GPIO/RCC 迁入 `stm32h7xx_hal_msp.c` 的 I2C4 分支（非 G 线文件），并把门禁改为“预处理后 HAL 构建 0 命中”或“排除 `#ifdef FT5446_BITBANG` 域”；红队书面同意后方可关单 |
| F4 | 一般 | 官方出处文档与实现不一致：UPSTREAM.md 仍写 PLL3R=100MHz/0x00901954/100k #error（实现是 D3PCLK1+计算器，无 #error）；main.c 实为 H743ZI（H723ZG main.c 无 L39-42 注释）；“100MHz→0x00901954”旁证不成立（零改动计算器给 0x20B11829）；`cubemx/tcs_i2c4.ioc` 是 HSI 64MHz、Timing=0x00B03FDB 的装载测试件，非本工程 120MHz 来源 | `docs/ref/CubeH7_I2C/UPSTREAM.md:6-13`；`W1a_完成.md:18-19`；curl 两版 main.c hash；计算器 100MHz 复算；`tcs_i2c4.ioc:12,51,71,76,101` | **must-fix（文档）**：改写 UPSTREAM.md 与完成报告 §2.3，删除/标注 .ioc 为非权威；保留“官方零改动+运行时日志+独立复算”为正式出处链 |
| F5 | 一般 | NAK 计数从“仅 NAK”漂移为“全部 HAL 非 OK（NAK/超时/总线错/BUSY）”；W1d 0.5% 阈值会被非 NAK 故障污染 | `ft5446.c:364-369`；HAL 返回语义（`stm32h7xx_hal_i2c.c` Mem_Read 各返回点） | **must-fix（W1d 前置）**：按 `ErrorCode & HAL_I2C_ERROR_AF` 计 NAK，或改名并分列 AF/超时/忙；W1d 报告注明口径 |
| F6 | 一般 | `FT5446_I2C_FAST` 在源内无条件 `#define`，`-DFT5446_I2C_FAST=100000` 触发重定义警告且仍走 400k；100k 档只能靠改源码注释切换，不是真正的编译宏开关 | `ft5446.c:85,112-117`；实测 `-D` 预处理仍为 `400000u` + `warning: "FT5446_I2C_FAST" redefined` | **must-fix（W1d 前置）**：改 `#ifndef FT5446_I2C_FAST`/值宏或 `FT5446_I2C_100K`，要求两种构建都能编并打出 distinct target |
| F7 | 一般 | `I2C_GetTiming` 静态累加器不重置；同进程 400k→100k 得 0x20E2BDC1（SCLDEL=450ns<890ns，非合规）。当前单次调用安全，但运行时降档/重复 init 会中招 | 复算实验（§1c）；`i2c_timing_utility.c:125-126,139-169` | **wont-fix（W1a）**：以注释/台账约束“每启动只调一次”；若 W1d 要运行时回退，须另立方案（不能私改官方文件） |
| F8 | 微瑕 | `delay_us_init()`/`g_fac_us` 未随位bang域守卫；HAL 默认构建里只写不读，属死代码 | `ft5446.c:187-191,433`；`grep g_fac_us` 仅 L187/190/195 | **must-fix（微瑕级）**：把 delay_us_init/变量纳入 `#ifdef FT5446_BITBANG`，或给出 HAL 态仍需的理由 |
| F9 | 微瑕 | 注释错误：L94 “480/2” 漏了 PCLK4 的二次 /2；L381 “7 位地址语义”与 HAL 实际 8-bit 约定相反（F1 的认知根因） | `ft5446.c:94,381`；`main.c:341-345`；HAL 文档 | **must-fix（随 F1/F4 一并改）** |
| F10 | 微瑕/一般 | 20ms 超时是故障路径 WCET（单次 20ms、触摸 tick 最坏 40ms），报告只写“同 tmp117 口径”，未给 WCET 推演；册 §2.3 的 ≤250µs 只适用于正常路径 | `ft5446.c:82`；`stm32h7xx_hal_i2c.c:2680-2722`；`main.c:593-618` | **wont-fix（W1a）+ W1b 观测**：报告补故障路径上界；考虑 2-5ms 或说明保留 20ms 的理由 |
| F11 | 微瑕 | 官方 `.c` 以 `#include` 聚合进 ft5446.c，但文件放在 Core/Src 且不在 CMake 源列表；未来 glob/误加源列表会重复定义 `I2C_GetTiming` | `ft5446.c:90`；`CMakeLists.txt:60-83`；`nm` 单符号 | **wont-fix（W1a）+ 注释/改名**：在 CMakeLists 或文件头标注“只许聚合包含” |
| F12 | 微瑕 | 官方三铁律的 LICENSE 核对无留档：ST 文件头引用 LICENSE，但 `docs/ref/CubeH7_I2C/` 未附 LICENSE/许可结论；仓根也无 LICENSE 文件 | `ls docs/ref/CubeH7_I2C/`；文件头 L15-17；UPSTREAM.md 无许可行 | **must-fix（微瑕级）**：在 UPSTREAM.md 补“ST CubeH7 BSD-3-Clause/文件头版权行已保留”的核对结论 |
| F13 | 流程 | §0b 律1“末=台账回写+commit”未完成：W1a 开单行在册（台账 L210），但代码/完成报告均未提交，无 W1a 会话末行；W8 I2C 研学卡“W1 后补面”仍挂账 | `git status`（W1a 文件均未提交）；`docs/台账.md:210`；W8 关单行 | **关单前补**：commit+tag、台账关单行、W8 I2C 卡闭合 |

## 3 最差执行者测试 + DoD 改写建议（"官方计算器现算"出处裁定）

### 3.1 逐 DoD 的最差执行者捷径

| DoD | 只想关单者会怎么做 | 现有门禁是否堵住 | 改写建议 |
|---|---|---|---|
| nm ct_iic=0 | 把位bang 整段删掉/挪到不参与链接的文件，默认构建自然 0 | 只查默认 ELF；不查“保留一版” | 同时要求 `-DFT5446_BITBANG` 构建 0 error + 符号存在（本次红队已补做） |
| HAL_I2C_Mem_≥2 | 加两处永不执行的调用/注释里的调用 | 只数文本，不查调用图 | 改为“两处调用点 + 目标 ELF 有 I2C4 初始化路径 + W1b A/B 读数”；本次 nm I2C4=2 可作旁证 |
| TIMINGR 一致性（代码常量 vs cubemx_timing.json） | 同时伪造常量和 JSON，互相“一致” | 不能防自证；且与运行时计算器路线不兼容 | 改为“官方源 SHA256/curl + 目标启动日志 timing=0x… + PC 独立复算两档逐一相等”；本次 0x20E21F33/0x70D04648 即基准 |
| 引脚只在 MspInit（grep GPIO_PIN_1[23]=0） | 删掉位bang 保留域（违反 §2.2 条 4）或把引脚藏进宏/字符串 | 文本 grep 与保留期条款自相矛盾；本次实测 3 | 改为“预处理后的 HAL 构建 grep=0”；或将 GPIO/RCC 迁入 `stm32h7xx_hal_msp.c` 的 I2C4 分支（不碰 G 线 main.c） |
| FT5446_I2C_FAST 双档 | 源码里写死 400k，注释一句“改宏即 100k” | 没有双构建/双 target 证据 | 要求 `-DFT5446_I2C_FAST`（或缺省）两种构建都编，且 RTT target 分别为 400000/100000；100k 值须复算 0x70D04648 |
| 零警告 | 增量构建不重编，旧对象“零警告” | 增量日志可能无该 TU | 要求 `-c` 单 TU 强制复编（本次已做，0 warning） |
| A/B 20 行 CSV | 手填 20 行/只测 3 点 | 脚本数行可挡一部分，挡不住伪造 | 要求原始 CSV + 脚本判等 + 逐行原始日志时间戳（W1b 执行） |

### 3.2 “官方计算器现算”对册“CubeMX 唯一合法来源”的独立裁定

**裁定：偏差可豁免（附条件；不是等价满足，也不是不满足）。**

理由：
1. 册 §2.2 条 2 的字面要求是“CubeMX（官方工具）生成 + 400k/100k 两档 + 计算页截图入 evidence”，且 §1c-3 要求“代码常量 vs `evidence/cubemx_timing.json` 逐字段相等”。W1a **没有** CubeMX 截图、没有静态代码常量、没有 cubemx_timing.json；字面不满足。
2. 但条款的目的是“禁止手算照抄网络值，保证官方数值来源”。W1a 采用的是 **ST 官方仓、锚定 commit、零改动、SHA256/curl 双重锚定** 的 `I2C_GetTiming()`，在目标工程内以真实 PCLK4 运行时计算；这比“从 CubeMX 抄一个静态常数”更难伪造，且红队已用零改动源码独立复算得到 0x20E21F33/0x70D04648。目的满足，且证据强度不低于截图。
3. CubeMX 路径在本机的实际阻塞有日志：本机 Repository 仅 F1/F4（`~/STM32Cube/Repository`），无 H7 包；`cubemx/tcs_i2c4.ioc` 只是 HSI 64MHz 的装载测试件（Timing=0x00B03FDB，非本工程 120MHz 配置），不能作为 120MHz 的官方截图；G 线又占着 `main.c` 的 MspInit 迁移口。强行要求 CubeMX 截图在当前窗口不可达。
4. 豁免条件（缺一即转为“不满足”）：
   - W1b 必须抓到目标启动日志 `pclk4=120000000Hz target=400000Hz timing=0x20E21F33`（或解释差异）；
   - W1d 降 100k 前必须用修好的宏开关构建 100k 档，并复算/实测 `0x70D04648`；
   - `cubemx/tcs_i2c4.ioc` 必须标注“装载测试件，非 TIMINGR 出处”，或从 evidence 链移除；
   - 册 §2.2 条 2/§1c-3 必须在台账/符合性矩阵中正式改写为“官方计算器零改动 + SHA256 + 运行时日志 + 独立复算”路线，不能留“CubeMX 唯一合法来源”的字面红条。
5. 若上述条件不满足，本裁定的备选结论是“不满足”，W1a 不得以“计算器”名义关单。

## 4 工艺门槛打分（§1d 八条）

| # | 条款 | 判定 | 证据 |
|---|---|---|---|
| 1 | 命名与官方示例同风格 | ✓ | `hi2c4`/`I2C_HandleTypeDef`/`I2C_InitTypeDef`、`rp`/`gi` 与工程既有 `gi` 风格一致；`I2C_GetTiming` 保留官方名 |
| 2 | 注释只讲约束与为什么 | ✓（有 1 处事实错误并入 F1/F9） | 注释以出处/约束为主（L74-80、L94、L103、L381）；L381“7 位地址语义”是错误认知，随 F1 改 |
| 3 | 零死代码（旧实现挂显式宏） | **✗** | 位bang 域已挂 `FT5446_BITBANG`，但 `delay_us_init()`/`g_fac_us` 未守卫，HAL 默认构建只写不读（F8）；其余无注释掉的旧代码/永假分支 |
| 4 | 新魔法数=0 | ✓ | 超时命名 `FT5446_I2C_TIMEOUT_MS=20u`；速率 400000/100000 在宏分支内；`Trials=1`、返回码 -3/-2 与既有错误码风格一致；无未解释的 TIMINGR 常数（运行时计算） |
| 5 | 函数 ≤50 行、圈复杂度 ≤10 | ✓ | 新增 `ft5446_i2c4_init` 42 行、3 个 if，CC≈4；既有长函数未增结构复杂度 |
| 6 | 每个 diff hunk 能答“为什么需要” | ✓（F1 hunk 的“为什么”答错） | 各 hunk 均有出处/约束注释；probe 的“HAL 7 位语义”hunk 是错误理由，见 F1 |
| 7 | 错误路径全覆盖（HAL 返回值逐条列） | ✓（报告有微瑕） | init 三处 HAL 调用逐条判错（L98/127/128）；rd/wr 的 0/1 映射保留既有调用方忽略 wr_reg 返回的行为（非 W1a 引入）；完成报告未列“HAL 返回逐条表”，记文档微瑕（随 F4 整改一并补） |
| 8 | 与周边风格一致 | ✓ | 中文注释、4 空格、HAL 类型、`SEGGER_RTT_printf`/`tcs_log` 用法与工程一致 |

任一 ✗=must-fix：第 3 条对应 F8（微瑕级 must-fix）。其余无新增 must-fix。

## 5 纪律核查（§0b 十律）

| 律 | 判定 | 证据/说明 |
|---|---|---|
| 1 会话首末动作 | △ | 首：完成报告引册 §2.2/§2.5/§1c，开单行在 `docs/台账.md:210`；末：W1a 代码/完成报告均未 commit，无“会话末”行。关单前必须补 commit+tag+台账（F13） |
| 2 单线推进 | ✓ | 本会话只 W1a；W1b-g 挂账在完成报告 §6；G 线为并行线且台账声明零触碰 |
| 3 禁跳步 | ✓ | W1a 内按“配置/编译→门禁”执行；W1b-g 未抢跑，明确挂 R2/板窗 |
| 4 禁范围蔓延 | ✓ | W1a 跟踪 diff 仅 ft5446.c + 新增文件 + docs/ref + cubemx + evidence/W1；G 线文件未进 W1a 集；`git diff --stat` 可核 |
| 5 官方三铁律 | △ | 逐字+来源+SHA256 ✓（§1b）；LICENSE 核对无留档（F12）；W8 I2C 研学卡“W1 后补面”仍挂账（台账 W8 关单行），W1 全关前须闭合 |
| 6 文档先行 | △ | 设计决策在完成报告 §2 有记录，但 `UPSTREAM.md` 仍是旧 PLL3R 方案，文档与实现相反（F4）；决策文字晚于实现 |
| 7 失败处置 | ✓ | 完成报告 §5 记录了 JSON 转义两连击与 PLL3 冲突排查；未触 D3b 三边界，无硬件/采购/感官风险 |
| 8 台账即账本 | ✓ | W1a 开单行 L210 在册（时间/触发/出处/状态齐）；关单行待红队双签后补，符合流程 |
| 9 红队独立 | ✓ | 本报告为独立红队会话；未采信制方叙述，全部结论有命令/代码证据；制方未参与本报告 |
| 10 纪律入审 | ✓ | 本节即载体；发现 F13/F12/F4 均回写台账要求 |

未达“违反两次=关单驳回”：律 1 的 commit 缺失是明确的末动作缺口，但可在关单会话补；律 5/6 为留档/文档 △。若 F13 在关单前仍不补，则升级为第二次违反。

## 6 结论

（首轮结论；整改后的增量复验终裁见 §7：**放行**。）

**整改后放行。** W1a 的传输层 HAL 化主链（Mem_Read/Mem_Write、D3PCLK1=120MHz、官方计算器零改动、双构建、位bang 保留域）经独立复跑**未被证伪**；但存在 1 个重大功能缺陷、3 个一般 must-fix 和若干微瑕/挂账，清零前不得关单。

### must-fix（5 项，清零前不得关单）

1. **F1（重大）**：`ft5446_probe` HAL 分支地址错误——`wr_cmd >> 1` 使 `HAL_I2C_IsDeviceReady` 实际寻址 0x1C。改传 `wr_cmd`（0x70）或 `(wr_cmd>>1)<<1`；W1b 用 `tp_diag` 全地址扫描复验 0x38 ACK。
2. **F2（一般）**：`run_all.sh` rtt_count 16≠13（W1a +3 处 RTT 调用）。同步门禁数并在台账说明，或合并/删除新增日志；复跑 run_all 至全绿（排除已归因的环境项）。
3. **F3（一般）**：§1c 引脚门禁红（3≠0）且与“位bang 保留一版”条款冲突。把 I2C4 GPIO/RCC 迁入 `stm32h7xx_hal_msp.c` 的 MspInit 分支（非 G 线文件），并把门禁改为预处理后 HAL 构建 0 命中；或由红队书面同意 DoD 改写并留档。
4. **F4（一般）**：官方出处文档与实现相反/数值旁证不成立/`.ioc` 非权威。改写 `UPSTREAM.md` 与完成报告 §2.3；删除或标注 `tcs_i2c4.ioc` 为装载测试件；把“官方零改动 + SHA256 + 运行时日志 + 独立复算”定为正式出处。
5. **F8（微瑕级 must-fix，§1d-3 ✗）**：`delay_us_init()`/`g_fac_us` 未随位bang域守卫；HAL 默认构建只写不读。纳入 `#ifdef FT5446_BITBANG`，或给出 HAL 态仍需要的理由。

### W1d 前置（2 项，可在 W1a 关单时挂账，但 W1d 决策前必须关闭）

6. **F5**：NAK 计数区分 `HAL_I2C_ERROR_AF` 与超时/总线错/BUSY，或改名分列；否则 0.5% 阈值口径失真。
7. **F6**：`FT5446_I2C_FAST` 改为真正可外部选择（`#ifndef`/值宏/CMake option），并完成 100k 档构建与 `0x70D04648` 复算/实测。

### 微瑕/流程（随整改或关单补）

8. F7：在注释/台账写明“`I2C_GetTiming` 每启动只调一次，静态累加器不可重入”；禁止运行时直接二次调用。
9. F9：修正 L94 的 480/2 推导与 L381 的“7 位地址语义”注释。
10. F10：完成报告补 20ms/40ms 故障路径 WCET，并说明与册 §2.3 ≤250µs 的口径差异。
11. F11：在 CMakeLists/文件头标注 `i2c_timing_utility.c` 只允许聚合包含，防未来 glob 重复定义。
12. F12：在 `UPSTREAM.md` 补 ST CubeH7 许可核对结论（BSD-3-Clause/版权行保留）。
13. F13：关单前补 W1a commit+tag、台账关单行、W8 I2C 研学卡“W1 后补面”。

### 计算器两值（W1b 上板基准）

- `I2C_GetTiming(120000000, 400000)` = **0x20E21F33**（400.3kHz，合规；单次新鲜调用）
- `I2C_GetTiming(120000000, 100000)` = **0x70D04648**（103.1kHz，合规；单次新鲜调用）
- 反例：同进程 400k→100k = 0x20E2BDC1（SCLDEL=450ns<890ns，非合规，见 F7）。

### 证据路径

- 官方零改动：`docs/ref/CubeH7_I2C/i2c_timing_utility.{c,h}`、`SHA256.txt`、`UPSTREAM.md`
- W1a 完成报告：`docs/results/W1a_完成.md`
- CubeMX 装载测试：`project_tcs/cubemx/tcs_i2c4.ioc`、`cubemx_cli*.log`
- 构建/单测日志：本次红队 `/tmp/w1a_build.log`、`/tmp/w1a_buildsim.log`、`/tmp/ft_bitbang.err`、`/tmp/ft_hal.err`（临时证据；正式留档由制方在整改时补入 docs/evidence/W1/）
- 本报告：`D:\Espressif\ws\tcs\docs\results\W1a_红队.md`

## 7 增量复验（整改后，2026-10-27）

| 发现 | 复验动作/证据 | 结果 |
|---|---|---|
| F1 | 源码 L389 已传 `(uint16_t)wr_cmd`；ELF 反汇编 `0x80069d0 lsls r6,r5,#1` → `0x80069d8 uxth r1,r6` → `0x80069da bl HAL_I2C_IsDeviceReady`，无右移/减半 | **通过**（诊断扫描实际传 8-bit `addr<<1`） |
| F2 | `run_all.sh` 口径 13→16；实跑：C 测试全 PASS；`[rtt_count] PASS (16)`；`ws_json` PASS（`IDF_CJSON` 指向真实 IDF cJSON 源）；`vendor_sha` PASS(5)；audit **143 files/0 uncovered/0 unclassified**；Windows Python 复核 `qr_verify PASS`、`card_paths 70 checked/0 bad`。纯 WSL 下仍会因 `/d/`/`D:/` 路径与 WSL Python 缺 zxingcpp 显示两项环境红，非 W1a 代码问题 | **通过** |
| F3 | `W1a_完成.md` §9 裁定入册：保护真值=`nm ct_iic=0`；文本门禁与位bang保留条款矛盾→册修订建议。核对矩阵：`W1-mem` 行仍为“同上/⏸”，矩阵文件 mtime 13:37（早于整改），**“矩阵已注记”未在矩阵文件复现** | **裁定通过；矩阵注记为关单/ W1 全包动作**（非代码阻断） |
| F4 | `UPSTREAM.md` v2 实读：采用件=计算器（120MHz 两值 0x20E21F33/0x70D04648）；H743ZI 三 main.c 降为“惯例旁证”；PLL3R/HSI/.ioc 三路废弃留档；`.ioc` 标注“仅装载测试件（HSI 64MHz/0x00B03FDB）”；BSD-3-Clause 许可结论；0x00901954 不再作为本工程来源 | **通过** |
| F5 | L369-374 仅在 `hi2c4.ErrorCode & HAL_I2C_ERROR_AF` 时累加 `g_ft5446_nak_cnt`；超时/总线错不进入 NAK 分子 | **通过**；注释“超时/总线错另计”无独立计数器实装（微瑕） |
| F6 | `-DFT5446_I2C_100K` 真开关：默认预处理 `400000u`，加 `-DFT5446_I2C_100K` 为 `100000u`；默认/BITBANG/100K 三模式单 TU 编译均 **0 warning** | **通过**；L79 顶部注释仍写旧宏名 `FT5446_I2C_FAST`（微瑕） |
| F7/F11 | include 处 L90-93 注释：①`I2C_GetTiming` 静态累加器，每启动只调一次；②本 `.c` 严禁再入 CMake 源列表；UPSTREAM v2 亦有约束节 | **通过** |
| F8 | `delay_us_init()`/`g_fac_us` 及 `ft5446_init` 调用点均入 `FT5446_BITBANG` 域；三模式 0 warning；build/build-sim 链接通过（“ninja: no work to do”=制方已构建，符号地址随整改变化） | **通过** |
| F9 | L98-99 时钟推导改为 `480/2(AHB)=240→APB4/2=120`；L387-388 probe 注释记录红队 F1 坐实 | **通过**（L79 旧宏名残留见 F6） |
| F10 | `W1a_完成.md` §9 记录：正常 ~0.2ms/6B@400k，20ms 仅故障帽；主循环调用未破“禁中断上下文”铁律 | **通过**（定性留档；W1b 仍应观测 `g_stat_touch_us_max`） |
| F12 | UPSTREAM v2 许可结论：ST CubeH7 仓根 LICENSE.md=BSD-3-Clause，vendor 文件头版权段保留 | **通过** |
| F13 | 当前 `git log` 最新仍为 38d0692（无 W1a commit）；台账只有开单行 L210，无 W1a 关单行；`W1a_完成.md` §9 称“本关单 commit 落”但仓库未落 | **未落**（按用户说明随本关单 commit/台账补齐） |

### 7.1 增量终裁

**整改复验通过，放行。** F1/F2/F4-F12 已逐项复验闭环；F3 的裁定已在 `W1a_完成.md` §9 入册（矩阵 `W1-mem` 行仍 ⏸、矩阵文件未更新，作为 W1 全包/关单动作补齐），F13 的 commit+台账随本关单执行。残留微瑕 2 条不阻断：① `ft5446.c:79` 顶部注释仍写旧宏名 `FT5446_I2C_FAST`（实际开关 `FT5446_I2C_100K`）；② `ft5446.c:372` 注释“超时/总线错另计”无独立计数器实装（NAK 口径本身已修）。
