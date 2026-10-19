# WP-P 结果文档 — M207 参数软件落地（v10.11 P0-P6）

> 证据先行；日期 2026-10-20。架构 v2 分期：本册=板无关部分+网页侧（数据源=注册表占位），固件传输侧=R5（ESP32，第七阶段）。

## P0 协议正典（《现行M207协议_v1.md》）

- **字典重大发现**：EasyHost `Lang/zh-CN/ModelFile/` 含厂商按产品维护的参数模型 XML——TCM-M207OEM V6.2.0.1（209 参）/NTS V6.2（166 参）/V6.0（156 参），**三变体交集 122 核心参数**，含中文标签/单位/默认值/分组页/枚举释义——比反编译更高优先级的机读字典。全量入库 `docs/evidence/easyhost_decompile/`（XML×3+CSV×3+modulename/errorcode/replycode/tc_hlp/tc_opt/ini.xml/脚本×2）。
- **50ms 铁律实证**：EasyHost 出厂 ini `SendInterval=0.05`——厂商上位机即按 50ms 发送。
- 语法层=总师抽文 PDF 直采；`^` CHANGING 禁用（publiccmd.c 有、PDF 无、脚本语料 165 行 0 次出现）；C1-C6 矛盾清单随 v4 包。
- 交付：`docs/现行M207协议_v1.md`（正典 v1）。

## P1 映射表+所有权矩阵（规格说明书 v1.2）

- **H10 全表**（`docs/现行规格说明书_v1.2.md` §2.5）：0x100-0x10F TC1 / 0x110-0x11F TC2 镜像 / 0x120-0x126 系统 PCB / 0x130+ 只读遥测，六列（cmd/类型/值域/默认/单位/保存语义）+危险列（22 项 ✓）；不映射项双主禁令表。
- **所有权矩阵**（§2.6）：M207 模块 EEPROM=唯一真值；注册表=缓存镜像+透传（写→50ms 读回校验），本地不持久（现有 0x100/0x101 的 PF_PERSIST 违矩阵 → R5 移除，id 不动）。

## P2 板无关帧纯函数（fw）

- 解析器扩展：`CMD:REPLY=N` → `M207_KIND_ERRCODE`（正典 §2 分路），`m207.h/.c`；PC 单测 22 断言 PASS。
- m207.c H10 映射表填实（38 条 cmd/ch/decimals）；R5 接传输。

## P3 网页高级参数区（CDP 12 → 24 全绿）

- 固件：H10 全表入注册表（50 参=8 业务+42 M207；PF_WEB·M207·DANGER，无 SCREEN 无 PERSIST）；`PARAM_CAPACITY 32→64`；注册表下发改**节拍化**（2 帧/拍，50 条≈250ms，防 txq 16 深溢出）；`TCS_CMD_M207_SAVE=10`（保存到模块，R5 接 `!`）。
- 前端（`app.js/index.html/style.css`）：**分组卡片** 9 组（PID/保护/输出温度选项×TC1·TC2+PCB+系统+遥测）；危险参数 ⚠+二次确认（拒绝/同意双路径断言）；只读遥测无控件；客户端范围钳制（固件 clamp 仍权威）；「保存到模块」+掉电语义提示；`isAdv=id≥0x100`（PF_M207 语义=需下发，tc1/tc2_set 不误入高级区——a22 修复记录）。
- **web_mock 注册表=从固件 param_registry.c 实时解析**（golden：页 42=源 42=H10 42）。
- **CDP 断言 24/24 ALL PASS**（a13-a22 新增 12 项）→ `docs/evidence/K8_web_audit.json` + `K8_web_375.png`。
- 屏不显红线：42 个 M207 参数 flags&PF_SCREEN==0（机器断言 a20）。

## P4 仿真与闭环

- `tools/m207_sim.py` v2：全字典 199 参+ERRORCODE 全表+50ms 违例丢弃模拟+`!`保存/`--state` 掉电语义+地址+XOR 校验（**PDF §8.2 官方示例 `CMD:REPLY=1@0#7D` 逐字节复现**）+应答格式镜像（PDF 4.9）。
- **五行注入矩阵全绿**（`tests/test_m207_adv.py`）：设→读回一致（20 参）/越界→拒且原值不变（含动态值域 MAX 提升）/保存→重启保持/未保存→重启回旧/坏帧（无冒号 6/坏校验 7/校验不配地址 6/^ 禁用 6/只读保存 3）→ERROR 不崩。
- golden 双实现一致性（工程质量三）：sim TX 向（m207_golden.csv 16 帧）+ 固件 RX 向（m207_golden_rx.csv 16 条，test_m207_golden.c）各守一方向，语法由正典+PDF 示例三方锚定。
- 板内闭环注入=R5（分期）。

## P5 文档

- 规格说明书 v1.1→**v1.2**（H10 全表+所有权矩阵+§11 M207 接入设计）；用户手册 v1.0→**v1.2**（高级参数章：分组/即时下发读回/保存语义/危险⚠/屏不显说明+FAQ 2 条）。

## P6 门禁

- run_all 新增 `m207_adv`（python 矩阵）+`m207golden`（C 语料）套件，**run_all ALL GREEN**；nightly 并入（nightly.sh 沿用 run_all）。
- 真机重烧双板过（H743 wp_check PASS + ESP32 全链 PASS）。

## 移交
- R5（第七阶段 ESP32）：读写保存三函数+50ms 队列+写后读回+所有权实现+COMPUSHLIST 自发现；I4 真机项=C3 变体/C1 别名/C4 可存性/C2 错误码 0 边界（随 v4 包请用户向厂家确认）。
