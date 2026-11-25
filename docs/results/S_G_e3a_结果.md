# S-G e3a 结果 — ESP32 控制内核 / 参数 / 故障证据

- 分支 `fix/s-g-e3a`（工作树 `D:\Espressif\ws\tcs-g-e3a`），基座 `fix/s-u-int2` @ `177d3c1`。
- 全程未碰设备（无 probe-rs、无串口、无烧录、无任何驱动 TEC / 加热的操作）。安全判据的验证全部靠主机用例 + 变异。
- 目标侧接口一律按 `177d3c1` 写。e2 / l1 / e3b / feat/mode-page 合入后的对接见第六、七节。

## 一、提交列表（按先后）

| 提交 | 条目 |
|---|---|
| `aa68781` | X04-F-06 + M09-F-09 读数卡死活性判据 |
| `be1ef32` | P1-09 SAFE 关断走安全关断通道 + `tec_known` |
| `5ba1283` | X04-F-03 + R5-13 现场故障证据（ESP32 侧） |
| `b288389` | P1-12 服务启动结果汇总 + `ALM_SERVICE`（含 e2 转来的 M12-F-16 `main.c` 半、M13-F-09） |
| `649056f` | X02-F-05 + M12-F-08 的 `param_set` 半 |
| `6f0a1ac` | X06-F-03 五条 P0 安全路径判别性用例 + 负向验证 |
| `905e9d6` | X04-F-07 参数存储故障置 `ALM_EEPROM` |
| `f45d8a1` | M09-F-12 + X05-F-05（ESP32 部分） |
| `8120faa` | M10-F-04 反序列化校验 |
| `2a736ff` | M10-F-07 注释与死函数 |
| `1983225` | M10-F-03 参数写路径加锁 |
| `e0710c5` | M09-F-07 落盘移出控制任务 |
| `741e6bc` | X03-F-08 命令意图 / 模块回显取值接口 |
| `b18cbe7` | M09-F-06 自动布防的前置条件 + 正典 |
| `6385c8a` | X06-F-09 版本单一源（机制部分） |

## 二、判定表

判定只用五种。关单条件里的"真机"部分一律没有做（本批次不碰设备）。

| ID | 判定 | 改了什么 | 怎么验证的（命令与结果） | 遗留与原因 |
|---|---|---|---|---|
| X04-F-06 | 已实施待真机 | 超温半边在基座已修（`control.c` 过温块先 `safety_trip(OVERTEMP)`，位于布防判断之前），未重复修；卡死半边见 M09-F-09 | 同 M09-F-09；另 X06-F-03 的 Z1 断言超温当拍 `tec1_on==0 && state==SAFE` | 真机：冻结 `tc1_act=30` 且 TEC 加热 30 s，系统进 SAFE |
| M09-F-09 | 已实施待真机 | `SAFETY_BIT_STUCK`：源健康 + 读数逐位不变 ≥30 s + TEC 命令已开 ≥30 s（取较晚者）→ 锁存并进 SAFE，不受布防门控。体温只在自动模式 HEAT/COOL 下判，水温 TC1 开着就判。报警位借用 `ALM_BODY_SENSOR` / `ALM_SENSOR_OPEN`，按通信类自愈（读数重新变化 2 s 后清）。STUCK 不落盘 | `mutation_control.py --old <177d3c1 的 control.c>`：修前红 20（K 系列）；修后绿。变异 M31–M42 全 CAUGHT | 真实 TMP117 的噪声无数据；时冻时动的传感器每次"动一下"最多换 30 s 输出；SIM / 现场档恒定注入 + 自动 + TEC 开 30 s 必进 SAFE；TC2 无卡死判据；建议专用报警位 + 人工确认语义（需两端界面文件） |
| P1-09 | 已实施待真机 | `control_ops_t` 末尾加 `tec_force`：SAFE 的关断、500 ms 重申、cmd 11 停机都走它（没提供则退化）；做出 `tec_known` 与 `control_tec_known()` | 修前（`aa68781` 的 `control.c`）红 8（P9-1/2×2/4/6×4）；修后绿。变异 M43–M46 全 CAUGHT | "enter_safe 不下发"基座已修；彻底修法在 e2 的 `m207_tec_force`（见第七节）。快照 / 屏 / 网页显示"未知"归 l1 与界面。屏端 `snap_reassert` 的 OR 保护在 H743。真机：拔 M207 后急停，重连后 TEC 必须为 0 |
| X04-F-03 | 已实施待真机 | coredump 到 flash（配置 + 256K 分区）；启动第一行打复位原因；启动日志解出 coredump 摘要；报警位随安全锁存落 NVS（记录升 v2，兼容 v1，只作证据不回灌） | `check_layout.py`：修前 FAIL (4)，现 PASS。N1–N4 + 变异 C20–C26 全 CAUGHT。ELF：`.rtc_noinit` 28 B，coredump 符号已链入 | 分区表只随整片烧录生效；panic 路径变长；coredump 真实落盘与解码待真机；没有清除 coredump 的台架命令。H743 一侧方案见第五节 |
| R5-13 | 已实施待真机 | 新模块 `boot_record`（28 B，魔数 + 版本 + 长度 + CRC32），落点 RTC NOINIT，1 Hz 刷新运行时长 / 报警位 / 锁存 / 状态；`status` 行尾加 `prst= pup= palm= plat= pseq= nvalm= cd=` | B1–B10：512 张冷上电随机映像全部识破；前 24 字节逐位翻转全部判无效；格式锁定值由 Python `zlib.crc32` 独立算出。变异 C01–C18 全 CAUGHT | RTC NOINIT 跨复位存活的实测待真机（软复位后 `status` 应显示 `prst=3 pup=<上次秒数>`） |
| P1-12 | 已实施待真机 | 新纯模块 `svc_status`：逐个登记启动结果，全起来才 all up；有失败 → E 级 + 名单 + `ALM_SERVICE`（`ALM_*` 段末尾 0x0040）。`control_service_start()` 改返回 `esp_err_t`。注册表加载拆三态日志 | S1–S7 + 变异 C30–C38 全 CAUGHT | link / web 是 void 接口，如实记"未核实"（第七节）。屏 / 网页对 `ALM_SERVICE` 无文案。真机：人为让 `m207_service_start` 失败 |
| X02-F-05 | 已实施待真机 | 模块没受理时 `param_set` 返回 2（值已入注册表并广播）；屏链路 0x12 回 BUSY；网页回执 `ok=0 err=2`、事件日志降 W 级（只动应答两处） | P1–P5 + 变异 C40–C44、D01 全 CAUGHT | 前端对 `ok=0` 的呈现、屏端 BUSY 的显示未核对（界面文件不在本批次）。真机：拔 M207 后网页 set 看回执 |
| X06-F-03 | 已修并验证 | Z1（超温）/ Z2（在途状态机）/ Z3（复位后仍 SAFE）直接断言执行器与状态；② 已由 P9-6 覆盖；③ H743 侧由既有门禁 `tools/site/verify_prod_clean.py` 查 | `mutation_control.py`：M47 抓 13、M48 抓 1、M49 抓 6、M50 抓 5，ALL AS EXPECTED；`test_control` 274/0 | — |
| M09-F-12 + X05-F-05（ESP32 部分） | 已修并验证 | 停喂狗的变量 / 使用点 / 函数 / 声明与 `r1e_probe.c` 整文件进 `#if CONFIG_TCS_R1E_PROBE`；"1 s 铁律"注释改成与 TWDT 的 3 s 一致 | `xtensa-esp32s3-elf-nm`：默认档 ELF 无 `hang_test / r1e_probe / s_hang_test` 符号 | 取证档本身没有编译验证（不在三档矩阵里，且上电会自驱真模块） |
| X04-F-07 | 已实施待真机 | 启动时 NVS 读失败 / 镜像校验不过 / 有记录被拒 → `ALM_EEPROM`（放在 `control_init` 之后）；首次上电不报 | E1–E3 + 变异 C45、C46 全 CAUGHT | 真机：擦除 NVS 命名空间后屏上出现 `ALM_EEPROM` |
| M10-F-04 | 已修并验证 | `param_deserialize_ex()`：类型 / 持久化标志 / NaN 的记录跳过并计数；`body_cold >= body_hot` → 整份不采信 | V0–V6 + 变异 C50–C55 全 CAUGHT | — |
| M10-F-07 | 已修并验证 | 注释照 IDF v5.5.5 源码改正（`nvs_api.cpp:411-414`：`nvs_commit` 是 no-op）；不再调 `nvs_commit`；删 `param_load_eeprom` 与 `eeprom_*` 别名 | `git grep` 三个符号在 `esp32/` 下无引用；主机套件去掉桩后 ALL GREEN | — |
| M10-F-03 | 已实施待真机 | `param_set` 的校验 / 写入 / 侧效应与落盘的序列化 / 清脏进同一把递归互斥锁；广播在锁外 | 重构后行为由既有用例守住（广播次数、落盘时序）；构建 0 告警 | 竞态在单线程主机上复现不了。真机：双任务压测，重启后值一致、无 cold ≥ hot |
| M09-F-07 | 已实施待真机 | 落盘移到 `persist_worker`（优先级 2，每 500 ms）；控制任务里不再有 NVS 调用 | 构建 0 告警；落盘时序用例不变 | 真机：改参数时控制周期不被拉长。`perf` 的 `pollmax` 不再含 flush，口径变了 |
| X03-F-08 | 已实施待真机 | `control_tec_cmd()`（命令意图，只有 `out_tec_sw` 写）与 `control_tec_act()`（回显）；卡死判据改读命令意图 | K18；变异 M51 CAUGHT | 复联判据改读 `cmd` 在 e2 的 `m207_service.c`；快照带两个字段在 `tcs_snap.c`（l1） |
| M09-F-06 | 已实施待真机 | 自动布防 = 双源健康 **且** 上电参数镜像完好（`param_boot_image_ok()`）；正典 0x08 一行同提交注明 ESP32 侧"POST 过"的含义 | G1–G6 + 变异 C56、C57 全 CAUGHT | `post.h` 里未实现的 `post_init / post_poll` 声明没删（与 H743 的 `post.h` 是受管同源件）。真机：冷启动未接传感器不能自动布防 |
| M12-F-08（`param_set` 半，e2 转来） | 已实施待真机 | 并入 `649056f`：模块没受理 → 可映射成 BUSY 的返回码；PF_M207 同值重写补发模块 | 同 X02-F-05 | 真机：灌满队列，网页收到 BUSY |
| M12-F-16（`main.c` 半，e2 转来） | 已修并验证 | 并入 `b288389`：`m207_service_start()` 的返回值进汇总，失败另打一条说明性 E 级日志 | 同 P1-12 的 S2 | 服务侧一半由 e2 完成 |
| M13-F-09（e2 转来） | 已修并验证 | 并入 `b288389`：风扇脚注释 GPIO48 → GPIO42 | 逐字核对（对照 `fan_service.h` 与 e2 结果文档第三节） | — |
| X06-F-09 | 未做 | 机制已做（`6385c8a`）：`protocol/tcs_version.h` 唯一源，应用版本与 `fw_ver` 从它推导，`tools/version_bump.py --check / --set` | `--check`：修前 FAIL (4)，现 PASS | 文档漂移没有逐条修：README 的 v1.1.0、正典的 `fw_ver(0x010100)`、交付清单、指向不存在文件的链接、旧 results 的"历史快照"头。原因：最终版本号待用户定；额度用尽。定版后 `--set` 改一处再统一文档 |
| X04-F-05（接线半，e2 转来） | 未做 | 没有写代码：取值接口 `m207_module_status()` 在我的基座里不存在 | — | 合并后约 10 行接线（只日志、不进 SAFE、不报警），位置见第八节；待定清单同节 |

计数：已修并验证 7 / 已实施待真机 13 / 已修未记 0 / 不适用 0 / 未做 2（共 22 行）。

## 三、本批次验证总结果

主机套件（`cd esp32 && bash tests/run_m207_tests.sh`）：`test_control: PASS=274`、`test_tcs_core: PASS=102`、`test_web_status: PASS=230`、`check_layout: PASS`、`version_check: PASS`、`esp32-tests: ALL GREEN`（基座为 control 187，其余相同）。
变异：`mutation_control.py` ALL AS EXPECTED（M01–M51，旧 U 线实现红 55 条，W2 16 条）；`mutation_core.py` ALL AS EXPECTED（C01–C57 + D01）。
目标增量构建（默认档）：最后一次 errors=0 warnings=0，app 0xfc4d0（3 MB 分区余 67%）。

- H743 主机全量 `bash tests/run_all.sh`：`run_all: ALL GREEN`（rc=0；含 ESP32 套件 control 274 / tcs_core 102、审计门禁 `218 files, 0 uncovered, 0 unclassified`）。第一次跑时审计门禁判红——新增文件后机器报告过期（38 行差异），按说明更新清单并重新生成报告后转绿。
- ESP32 三档从零构建 `bash tools/build_matrix.sh --esp32-only --diag --site`（IDF_BUILD_JOBS=2，本批次只跑了这一次）：
  - 默认档 exit=0 / 0 告警，诊断档 exit=0 / 0 告警。
  - 现场档在 IDF 自带组件 `esp_lcd/rgb/esp_lcd_panel_rgb.c:722` 遇到编译器自身崩溃（internal compiler error: Segmentation fault；与本批次改动无关，同一份文件在另两档编译正常），矩阵脚本在该步中途退出、总体判 FAIL。随后用 `idf.py -B build-matrix-site -D SDKCONFIG=sdkconfig.matrix.site build` 从断点续编：rc=0、0 告警。
  - 三档分区表逐字节相同（sha256 `137dde96…`）：nvs 0x9000/24K、otadata 0xF000/8K、ota_0 0x20000/3M、ota_1 0x320000/3M、coredump 0x620000/256K。应用 0xfc4d0 / 0x11ae20 / 0x117e50，3 MB 的 OTA 分区分别余 67% / 63% / 64%。
  - 交付档干净核对（矩阵脚本没走到，手工按同样参数执行 `verify_prod_clean.py`）：默认档 PASS（无台架命令）、诊断档 PASS（全部台架命令）、现场档 PASS（linkloop / linkmute / webtest）。
  - 没有做的："可复现比对"那一步（脚本在现场档那一步就退出了）和 H743 四档从零构建——留给合并后的全量矩阵。

## 四、风险与需要人看的点

1. **分区表变了**（X04-F-03）：末尾加 `coredump` 256K @0x620000，现有分区偏移 / 大小不动。只经网页 OTA 升级的设备仍是旧分区表，没有 coredump 分区，该功能不生效（启动日志报 `No core dump partition found`，照常运行）；要整片烧录一次才有。现场档的本地 `sdkconfig.site` 我用新默认档重新生成过（原件 = 默认档 + `CONFIG_TCS_SIM=y`）。
2. **panic 路径变长**：写 core dump（擦 + 写实际导出，分区只擦用到的扇区）期间输出保持复位前的状态，典型多零点几秒，最坏可达数秒。
3. **卡死判据的误报面**：真实 TMP117 的噪声特性仓库里没有数据（所有 `body` 恒值的日志都是 `sim=1`）。若真实探头在 TEC 开着时 30 s 内读数逐位不变，会误进 SAFE（自愈型，读数一动就退出）。窗口是 `control.c` 里的一个常量。真机上线前建议先录一段真实体温读数序列。依赖恒定注入的台架流程（`r1e_usercheck.py` 等）会受影响，本批次没有改它们。
4. **卡死锁存是自愈型**：两端界面只对已知报警位出文案，卡死借用的两位在两端都归"自动恢复类"。时冻时动的传感器每次"动一下"最多换来 30 s 输出。要改成人工确认语义，需要新报警位 + 两端文案（`lvgl_ui/`、`tcs_web/`）。
5. **写路径持锁期间做 flash 写**（M10-F-03 + M09-F-07）：别的任务的 `param_set` 最多等一次写（数十毫秒量级）。
6. **`ALM_SERVICE` 没有界面文案**：屏端与网页目前只在 alarm 原值里看得到。
7. 取证档（`CONFIG_TCS_R1E_PROBE=y`）本批次没有编译验证。
8. 我在本批次里犯过并已修复的错（都留在了提交前的验证里，没有进提交）：`stuck_is()` 把"没有样本"当成"不活"；`main.c` 多了一个花括号；补丁脚本提前闭合列表；裁剪脚本把三份文件写成了 `\r\r\n`（已全部修复并全树扫描确认无残留）。

## 五、X04-F-03 的 H743 一侧（只读核实 + 方案，未改任何 H743 文件）

现状（已核实）：
- 故障记录器在**所有档位包括交付档**里：`project_tcs/Core/Src/stm32h7xx_it.c:24-63`，`g_fault_rec`（magic / PC / LR / CFSR / HFSR / BFAR）放在 `.noinit`（`gcc/stm32h743xx_flash.ld:143`），`HardFault_Handler` 写完后死等 IWDG 复位。C01 只把屏链波形采集与 RX 环划进了台架档，没有动它。
- `project_tcs/App/tcs/console.c:758` 的 `console_print_fault_record()` 在启动时（`Core/Src/main.c:850`）打印，只进 RTT，不主动清，事后没有命令可以重打。
- RTC 备份寄存器在这块板上实测不可写（`docs/evidence/WP6_iwdg_defect.md:100`）——审查方案里"写 RTC 备份寄存器"的前提不成立，除非重新验证。
- 没有"最近 8 条状态迁移环"；仓库 `tests/`、`tools/` 对 `g_fault_rec`、`reset_source` 零命中。

需要做的事（精确到文件与函数）：
1. `project_tcs/App/tcs/control.c`：新增 `.noinit` 的迁移环 `struct { uint32_t ms; uint8_t from, to; uint16_t alarm; } ring[8]` + 序号 + 魔数，在 `goto_state()` 与 `tcs_emergency_stop_src()` 里各写一笔。环逻辑写成纯函数，主机用例进 `project_tcs/tests/test_control.c`（照 ESP32 侧 `boot_record.c` 的做法：魔数 + 序号 + CRC，随机内容一律判无效）。
2. `project_tcs/Core/Src/stm32h7xx_it.c`：`fault_rec_t` 增一个 8 位累加序号（同一次上电内第二次 HardFault 能与第一次区分；全 0 视为未发生）。不要改用备份寄存器。
3. `project_tcs/App/tcs/console.c`：`console_print_fault_record()` 之后一并打印迁移环；新增命令 `fault` 按需重打（启动那一行只有 RTT 开着才看得到）。
4. `project_tcs/tools/build_matrix.sh`（或 `verify_prod_clean.py`）加一条**正向**核对：交付档 ELF 里必须有 `g_fault_rec` 与迁移环，且在 `.noinit`（`arm-none-eabi-objdump -h` 显示 NOLOAD）。现在的门禁只查"不该有的符号"，记录器被哪次清理误删不会被发现。
5. 掉电仍丢：H743 本身没有不掉电的存储。要真正落盘，只能把记录经链路交给 ESP32（NVS / coredump 分区），需要一条新消息——归 l1 的协议段。

## 六、X06-F-09：改版本号要动哪里，以及 H743 那一行

**只有一个地方**：`protocol/tcs_version.h` 的四个宏（`TCS_VER_MAJOR / MINOR / PATCH / SUFFIX`），或 `python tools/version_bump.py --set X.Y.Z[-后缀]`。ESP32 应用版本、数值化的 `fw_ver`、`g_tcs.fw_ver` 都从它推导；`python tools/version_bump.py --check` 核对没有第二处写死的版本号（已在 ESP32 测试入口里）。BCD 只能表达主.次.修订，后缀只在应用版本字符串里。

H743 侧（都不在本批次范围，原文与新写法）：
- `project_tcs/App/tcs/param_registry.c:22` 原文 `.esp_link_ok = 0, .fw_ver = 0x010200u,   /* v1.2.0 BCD（第六阶段：触摸会话重写+UI 工艺）*/` → 新写法 `.esp_link_ok = 0, .fw_ver = TCS_FW_VER_BCD,`，并在该文件顶部加 `#include "../../../protocol/tcs_version.h"`（放在 `<stdint.h>` 之后）。
- `project_tcs/App/tcs/console.c:215-218` 原文 `printf("tcs v%lu.%lu.%lu build=%s git=%s params=v1(8+0x100resv) proto=v%u\n", (unsigned long)(g_tcs.fw_ver >> 16) & 0xFFu, (unsigned long)(g_tcs.fw_ver >> 8) & 0xFFu, (unsigned long)(g_tcs.fw_ver) & 0xFFu, ...)`：逻辑不用改——它读的是快照里的 `fw_ver`，ESP32 合入后链路一通就是 `tcs v1.3.0`；链路没通之前显示的是 H743 自己的初值，所以上一条必须一起改。
- `project_tcs/Core/Src/main.c:851` 原文 `tcs_log("[SYSTEM]", "boot project_tcs fw=v1.2.0 HCLK=%lu MHz", ...)` → 新写法 `tcs_log("[SYSTEM]", "boot project_tcs fw=v" TCS_VER_STR " HCLK=%lu MHz", ...)`。

## 七、给合并人的接口清单

**我用到的 M207 接口（按 `177d3c1`，e2 合入后若有出入以 e2 为准）**：`m207_tec()`、`m207_set_temp()`、`m207_map_set()`、`m207_save_all()`、`m207_service_start()`（都直接调用，返回值原样进汇总或钩子）；`g_tcs.m207_online`、`g_tcs.m207_sensor_open`、`g_tcs.tc1_act / tc2_act`（只读）；`g_tcs.tec1_on / tec2_on`（我在 `out_tec_sw()` 写命令意图，M207 服务写回显——两者共用的正是 X03-F-08 说的那个字段）；`control_safety_trip(SAFETY_BIT_OVERTEMP)`（M207 服务调我，未变）。不再使用 `m207_hook_set_temp / m207_hook_map_set`（void 包装丢返回值），这两个函数现在没有调用者。

**e2**：
- `int m207_tec_force(uint8_t ch, uint8_t on)`——不占普通队列；离线也记下意图，复联**第一帧**补发（不排在 4 条状态查询之后）；每 500 ms 重试到 `TCSW?` 读回一致；返回 0=已受理，-1=参数错。合入后 `esp32/main/main.c` 只需加一行 `.tec_force = op_tec_force`（转调它）。
- `m207_go_online()` 里 `if (!g_tcs.tecN_on) 发 TCSW=0` 应改读 `control_tec_cmd(ch)`；回显写进独立的字段，`control_tec_act()` 跟着改一行。
- 复联时对 PF_M207 参数做一次同值重写（现在会补发），解决"值已记录但没到模块"。
- 模块保护状态：见第八节。
- 新锁存位 `SAFETY_BIT_STUCK`（1<<4）只有控制核置位；`safety_state_load()` 读到它会丢弃。

**l1**：`link_start()` 请改返回 `esp_err_t` 并把 `xTaskCreate` 的结果带出来——`esp32/main/main.c` 里把 `svc_unverified(&svc, SVC_BIT_LINK)` 换成 `svc_note(&svc, SVC_BIT_LINK, link_start())` 即可。`tcs_snap.c`：快照请带 `control_tec_cmd(ch)`、`control_tec_act(ch)`、`g_tcs.tec_known`（P1-09 / X03-F-08）。

**e3b**：`web_start()` 同理请返回 `esp_err_t` 并带出 `ws_init()` 的结果（`svc_unverified(&svc, SVC_BIT_WEB)` 换成 `svc_note`）。`app.js`：`ALM_SERVICE`（0x0040）需要文案；`pv` 回执 `ok=0 err=2`（已记录、模块未受理）的呈现请确认。

**feat/mode-page**：我在 `control.c`（卡死判据块、`out_tec_sw_ex()` 与 `tec_known()` 两个小函数、三个取值接口）、`param_registry.c`（`side_effects` / `param_set` 拆成锁内外壳、反序列化、镜像完好标志）、`esp32/main/main.c`（启动汇总、注册表加载、X02-F-05 的三处）、`control_service.c`（布防判据一行、`persist_worker`、`#if CONFIG_TCS_R1E_PROBE` 四处）的改动都是成块的，没有重排既有代码；`protocol/tcs_proto.h` 只在 `ALM_*` 段末尾追加了一行，没有碰快照 / 增量载荷段。新增逻辑都在新文件里（`boot_record`、`svc_status`、`tcs_bootrec`、`tcs_version.h`、`version_bump.py`）。`param_desc_t` 没有加字段。

## 八、X04-F-05（模块保护状态）：待定清单与合并后的接线

事实（来自 e2 结果文档与其接口注释）：取值接口 `m207_module_status(ch, &st)` 给出 `errstat`、`otpstat`（极性未在真机核实）、`fault_mask = (~errstat) & 0x7F`（位 0 external / 1 over-voltage / 2 over-current / 3 over-temp / 4 sensor-open / 5 load-short / 6 load-open，位义取自厂商帮助文件，没有逐位上机核实）。真机上模块 TC1 **恒回 `TCERRORSTATUS=253`**——锁存位，要模块断电才清，是 10 月那次自检帧把开路保护上限写成 1°C 留下的。所以 `fault_mask != 0` 不等于"此刻有故障"，直接接 SAFE / 报警就是一上电永久停机。

需要用户定的：
1. 哪些位算"故障"、哪些只是"历史 / 锁存"？锁存位怎么对待（显示为历史、要求模块断电后清、还是出一条清除流程）？
2. `TCOTPSTATUS` 的极性（是否与 `TCERRORSTATUS` 一样"正常=1"）——要在真机上核实后再用。
3. 风扇一侧：`fan_get_act()` 只能发现 ESP32 这一侧的输出通路故障；`fan_mismatch_count()`；`fan_weak_check()` 是间接证据，恒温稳态下会误判，阈值与适用条件要先定。
4. 审查方案里的新报警位 `ALM_EXEC` 没有定义，两端也没有文案。

合并后的默认行为（只日志 + 取值，不进 SAFE、不置报警），接线位置与写法：在 `esp32/main/tcs_bootrec.c` 的 1 Hz 定时器回调里，对 TC1 / TC2 各调一次 `m207_module_status()`，`fault_mask` 相对上一次有变化时打一条 W 级日志，逐位用 `m207_fault_bit_name()` 写出名字并注明"可能含上电锁存位"；`status` 一行追加 `mf=0x%02X/0x%02X`。约 10 行，不在我的基座里编译。
