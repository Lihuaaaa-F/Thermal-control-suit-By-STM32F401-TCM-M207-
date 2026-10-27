# 研学卡：SEGGER RTT（第三方官方件 · W8 §7b）

- 制卡日期：2026-10-27。仓库：`D:\Espressif\ws\tcs`。
- 版本事实源：`project_tcs/App/rtt/vendor/UPSTREAM.md`（W4.2 留档）+ `docs/ref/依赖版本清单.md` SEGGER RTT 行。
- 调用面数据：本卡制卡时实跑 grep（命令见 ③）。

## ① 官方文档与版本

| 项 | 内容 |
|---|---|
| 组件 | SEGGER RTT（Real-Time Transfer，J-Link 日志通道，H743 主控端） |
| 精确版本 | **官方仓 @4d8feab，基于 Release V8.56a**（=V8.56a tag ad6970d 后 3 个清理提交；浅克隆 2026-10-27 取得）——出处 `project_tcs/App/rtt/vendor/UPSTREAM.md` 实测 |
| 来源 | github.com/SEGGERMicro/RTT（**SEGGER 官方仓**；segger.com RTT 产品页 https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/ 直接背书的官方分发渠道，比官网 zip 直链出处更强：commit hash 可锁） |
| 锁定方式 | vendor/ 五文件**零改动逐字拷贝** + `VENDOR_SHA256.txt`（五文件 SHA256，51f8969b…等，复核命令在文件内）+ UPSTREAM.md 出处 |
| 许可 | SEGGER 2026 BSD 单条件（`vendor/LICENSE.md`） |
| 官方文档 | 上游仓 README + SEGGER 官网 RTT 产品页（UM08025 J-Link 手册 RTT 章）；本机 `vendor/SEGGER_RTT_ConfDefaults.h` 内含逐项配置注释（官方文档级） |
| 升级风险注记 | 升级=重 clone → SHA256 重录 + 行为差异表复核（见 ④） |

## ② 官方示例文件路径（逐文件）

本机 vendor/ 快照仅含五文件（官方仓 RTT 核心子集），Config/Examples 结构指向上游：

| # | 路径 | 说明 |
|---|---|---|
| 1 | `project_tcs/App/rtt/vendor/SEGGER_RTT.c`（73840B） | 官方核心实现（零改动） |
| 2 | `project_tcs/App/rtt/vendor/SEGGER_RTT.h` | 官方公共头（零改动） |
| 3 | `project_tcs/App/rtt/vendor/SEGGER_RTT_printf.c` | 官方 printf 实现（零改动） |
| 4 | `project_tcs/App/rtt/vendor/SEGGER_RTT_ConfDefaults.h` | 官方配置默认（零改动） |
| 5 | `project_tcs/App/rtt/vendor/LICENSE.md` + `UPSTREAM.md` + `VENDOR_SHA256.txt` | 许可/出处/完整性三件 |
| 6 | 上游仓 `Config/` 目录：github.com/SEGGERMicro/RTT → `Config/SEGGER_RTT_Conf.h` | 官方设计为**空壳用户配置文件**（UPSTREAM.md L10-11 实证）；本工程实例化于 `project_tcs/App/rtt/SEGGER_RTT_Conf.h`（非 vendor 目录=允许用户编辑层：UP=1024/DOWN=0/缓冲数 1+1/模式不覆盖=官方默认 NO_BLOCK_SKIP） |
| 7 | 上游仓 `Examples/` 目录：github.com/SEGGERMicro/RTT → `Examples/` | 官方示例目录（**本机未随五文件拷入**，按任务书口径记上游 URL） |
| 8 | 工程薄壳 `project_tcs/App/rtt/segger_rtt.h` / `segger_rtt.c` | 门面层：保现封装签名（官方 API 带 BufferIndex 首参，宏壳兼容）；`segger_rtt.c` 聚合包含 vendor 两 .c（G 线未提交期路径约束的正解） |

## ③ 我们全部调用面清单（文件:行）

grep 命令：`grep -rn "SEGGER_RTT_" project_tcs/App project_tcs/Core --include=*.c`
总命中 **366 行**；拆分口径（13 调用点+App/rtt 薄壳域分开，任务书指定）：

| 域 | 行数 | 位置 |
|---|---|---|
| vendor/ 域（官方源内部，不计调用面） | 320 | `project_tcs/App/rtt/vendor/`（SEGGER_RTT.c 等） |
| **App/rtt 薄壳域**（门面 #undef/包装，见下表 A） | 7 | `project_tcs/App/rtt/segger_rtt.c:13-27` |
| **业务调用面**（App/rtt 之外，见下表 B，表行数=39） | **39** | 9 文件（=13 printf + 24 WriteString + 1 Write + 1 Init） |

另注：`SEGGER_RTT_Conf.h` 为宏配置头（不含 "SEGGER_RTT_" 调用形式之外的定义命中，未入上表；--include=*.c 亦不含 .h）。

表 A：App/rtt 薄壳域 7 行（segger_rtt.c）：

| # | 行 | 内容 |
|---|---|---|
| 1 | 13 | `#undef SEGGER_RTT_WriteString`（宏壳自噬防护） |
| 2 | 14 | `#undef SEGGER_RTT_Write` |
| 3 | 15 | `unsigned SEGGER_RTT_WriteString0(const char*)` 包装定义 |
| 4 | 17 | `return SEGGER_RTT_WriteString(0u, s);`（转官方带首参签名） |
| 5 | 20 | `unsigned SEGGER_RTT_Write0(const void*, unsigned)` 包装定义 |
| 6 | 22 | `return SEGGER_RTT_Write(0u, buf, len);` |
| 7 | 27 | `#include "vendor/SEGGER_RTT_printf.c"`（聚合包含） |

表 B：业务调用面 39 行（表行数=39）：

| # | 文件:行 | API | 内容 |
|---|---|---|---|
| 1 | App/eeprom/eeprom.c:203 | WriteString | "eeprom OFFLINE (NAK on probe)" |
| 2 | App/eeprom/eeprom.c:210 | **printf** | "eeprom online magic=0x%02x" |
| 3 | App/eeprom/eeprom.c:259 | WriteString | "eeprom selftest OK" |
| 4 | App/tcs/console.c:74 | WriteString | `_write` 控制台镜像（buf） |
| 5 | App/tcs/console.c:83 | **Write** | printf 镜像 RTT（无 COM 线观测） |
| 6 | App/tcs/m207.c:433 | WriteString | 模块报文透传（b） |
| 7 | App/tcs/m207.c:445 | WriteString | "[NET] m207 online" |
| 8 | App/tcs/m207.c:449 | WriteString | "[NET] m207 recovered" |
| 9 | App/tcs/m207.c:470 | WriteString | 模块报文透传（b） |
| 10 | App/tcs/m207.c:513 | WriteString | "[ERROR] m207 OFFLINE (5 fails)" |
| 11 | App/tcs/m207.c:538 | WriteString | "[WARN] m207 UART ORE cleared" |
| 12 | App/tcs/post.c:175 | **printf** | "[SIM] post item %d forced FAIL" |
| 13 | App/tcs/post.c:188 | WriteString | POST 结果（b） |
| 14 | App/tcs/post.c:261 | **printf** | "[POST] done pass=0x%02x (fail=0x%02x)" |
| 15 | App/tcs/post.c:288 | WriteString | "[POST] arm LOST->ARMED" |
| 16 | App/tcs/post.c:290 | WriteString | "[POST] arm ARMED->LOST" |
| 17 | App/tcs/post.c:292 | WriteString | "[POST] arm UNARMED->ARMED" |
| 18 | App/tcs/tmp117.c:63 | WriteString | 温度采样日志（b） |
| 19 | App/tcs/tmp117.c:68 | WriteString | "[SIM] body temp sim OFF" |
| 20 | App/tcs/tmp117.c:86 | WriteString | "[WARN] TMP117 LOST (unarmed)" |
| 21 | App/tcs/tmp117.c:90 | WriteString | "[ERROR] TMP117 LOST -> ALM_BODY_SENSOR" |
| 22 | App/tcs/tmp117.c:167 | WriteString | 温度日志（b） |
| 23 | Core/Src/ltdc.c:250 | WriteString | "mpu+d-cache write-through ok" |
| 24 | Core/Src/ltdc.c:253 | WriteString | "pll3 ltdc pclk=50M ok" |
| 25 | Core/Src/ltdc.c:256 | WriteString | "sdram_init FAIL" |
| 26 | Core/Src/ltdc.c:260 | **printf** | "sdram selftest=%d" |
| 27 | Core/Src/ltdc.c:264 | WriteString | "ltdc_hw_init FAIL" |
| 28 | Core/Src/ltdc.c:283 | **printf** | "bl_diag AHB4ENR=%08x GPIOB_MODER=%08x ODR=%08x" |
| 29 | Core/Src/ltdc.c:285 | **printf** | "bl_diag PB5: MODER[11:10]=%u ODR_bit5=%u" |
| 30 | Core/Src/ltdc.c:294 | **printf** | "ltdc init rv=0 g_lcd_ok=%d double_buf=1" |
| 31 | Core/Src/ft5446.c:308 | **printf** | "tp_diag sda_idle=%d" |
| 32 | Core/Src/ft5446.c:315 | **printf** | "tp_diag sda_drive low=%d high=%d" |
| 33 | Core/Src/ft5446.c:322 | **printf** | "tp_diag ACK at 0x%02x (wr 0x%02x)" |
| 34 | Core/Src/ft5446.c:326 | **printf** | "tp_diag scan done, %d device(s)" |
| 35 | Core/Src/ft5446.c:391 | **printf** | "ft5446 ver=%02x%02x fingers=%02x ver_ok=%d" |
| 36 | Core/Src/ft5446.c:431 | WriteString | 触摸报文（db） |
| 37 | Core/Src/main.c:474 | WriteString | "[ERROR] ERROR_HANDLER entered (WP-C)" |
| 38 | Core/Src/main.c:507 | **Init** | SEGGER_RTT_Init()（启动初始化，官方同签名直通） |
| 39 | Core/Src/stm32h7xx_it.c:51 | **printf** | FAULT CFSR/HFSR/BFAR/PC/LR/SP 现场（HardFault 黑匣） |

口径交叉验证：run_all.sh:119 门禁 `grep -rn "SEGGER_RTT_printf" App Core --include=*.c | grep -cv "App/rtt/"` = **13**（PASS 恒值）；上表 printf 标粗 13 行（#2/12/14/26/28/29/30/31/32/33/34/35/39）+ WriteString 24 + Write 1（#5）+ Init 1（#38）= 39 ✓。

## ④ 对照结论（✓/偏差+理由/待整改）

| 锚点 | 结论 |
|---|---|
| W4.2 vendor 定案 | ✓ 五文件零改动+SHA256/许可/出处三留档齐备（UPSTREAM.md + VENDOR_SHA256.txt）；官网 zip 直链机器人闸（curl 202）→ 官方 GitHub 仓等价更强渠道，理由在档（`docs/results/W4_2_完成.md` §2） |
| 行为差异表 | ✓ 引 `docs/results/W4_2_完成.md` §3 全表：**写满行为** 自研覆盖最老 → 官方 **NO_BLOCK_SKIP 丢新**（1024B+16ms 消费下满极少见，接受并留档）；**并发锁** 官方 SEGGER_RTT_LOCK（PRIMASK）=无害加固（我们铁律只主循环写）；**printf 格式面 官方 c/d/u/x/s/p**（无 i/X/l/f）——现用 13 printf 调用点 histogram（%d/%u/%x/%02x/%08x/%s）**全在交集**；未来需 i/X/l 一律 snprintf 预格式化 |
| %f/%l 门禁 | ✓ 长期归零（W4.2 DoD grep=0/0；F14/F16 教训"vendor 前后都必需"命中） |
| API 面兼容 | ✓ 宏壳保现封装签名（官方带 BufferIndex 首参），业务 39 调用点零改动；守卫名 SEGGER_RTT_FACADE_H 隔离防官方头跳空 |
| 控制块/探针 | ✓ probe-rs 符号查找命中官方 `_SEGGER_RTT`（nm 实证 @0x2000f554 .bss/DTCM） |
| rtt_count 口径演进 | ✓ 14→13 语义更强（纯调用点 vs 含定义行），run_all 注释+完成报告+台账三处留痕 |
| 偏差（挂账，非缺陷） | ① probe-rs attach 真机+全启动日志对照（L3）=**待整改入 R 线复验批**（G 线采样期板不动）；② CMake 正常化（聚合包含→独立源条目）=**二期**（G 线 CMakeLists 合入后）；③ 历史 RTT 证据链（W4/rtt_float_probe.c 绑自研实现）复现=checkout tag w4.1（红队裁决）；④ tcs_ui.c:72 未用参警告=二期（pre-existing，W4.2 外） |
