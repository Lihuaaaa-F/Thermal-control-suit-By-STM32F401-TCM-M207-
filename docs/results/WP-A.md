# WP-A 结果文档 — 屏幕缺陷修复（代码层）

日期：2026-10-18 ｜ 状态：**完成（关单）** ｜ 基线：任务书 v5.1 §3 WP-A

## 1. 完成内容

| 项 | 内容 | 结果 |
|---|---|---|
| A.1 取证 | 帧缓冲直读替代相机拍照（probe-rs 读 0xC0000000 前缓冲 → PNG，工具 `tools/fb_dump.py`） | 3 页面×状态全采集，另存缺陷存档 2 张 |
| A.2a 字库缺字 | 新建 `tools/font_audit.py`（UI 源码字符串×码表审计，缺字退出非零）；`gen_font_cn.py` 字符集改自动收集（扫 UI 源码）；重生成 188 字 24px + 97 字 32px；**font_audit 永久进 run_all.sh** | ✓ 三页零 '?' |
| A.2b 调试页脚 | `draw_debug_footer` 包 `#ifdef TCS_DEBUG_UI`（默认关），ASCII 混排页脚不再上屏 | ✓ |
| A.2c 报警横幅误报 | **A.5 commissioned 治本**（见下） | ✓ 未布防零横幅 |
| A.2d 线条/花屑 | **根因=DMA2D 多行 R2M 每行实际步进 +32px**（详见 §3）；修复=多行填充拆逐行 h=1 + flip 前作废 back 的 D-Cache 行 | ✓ 红条纹/噪点清零，90s 稳定 |
| A.5 commissioned | 参数 0x08（u8 默认 0，RW/SCREEN/WEB/PERSIST）；未布防时体温/M207 失联只记状态沿与日志，不上横幅不进 SAFE；ALM_OVERTEMP 与急停不受布防影响；MAX_TXQ 8→16（注册表 8 条目全量推送不丢） | ✓ PC 单测 + 上板验证 |

## 2. 运行时证据（docs/evidence/）

- `WP-A_home_alarm_fb.png` — 主页×报警态（EEPROM 提示性横幅，字库/布局全对）
- `WP-A_home_clean_fb.png` — 主页×未布防+体温失联：卡内红 `--.-`+红框（设计态），**顶部无横幅**（A.5 生效）
- `WP-A_tune_fb.png` — 调参页：8 参数名零 '?'（含新增"布防"行 0.0）
- `WP-A_home_stability_90s_fb.png` — 90 秒后复查，零劣化（时间 00:05:18 在走）
- `dbg_after_clear.png` / `dbg_junk_crop.png` — 修复前缺陷存档（满屏 32px 斜带漏色）
- 运行日志（RTT 环直读）：`boot reset-src=PIN`、`ltdc init rv=0`、`eeprom cfg invalid -> defaults`
- 活性：`g_stat_loop_passes` 88 圈/s（10ms 节拍）；`stat` 命令 `arm=0 alarm=0x0000 m207=0 sensor=0`
- 修复中发现并复测的一过性问题：逐行填充首版 `NLR` 漏置 NLH=1 → 全部填充 TC 超时（RTT 刷屏 `[WARN] dma2d TC timeout`，RTT 环直读取证）→ 一行修复后全绿

## 3. 红线条根因链（WP-A.2d，本次最大成果）

1. 帧缓冲直读证明"红线"**在显存里**（SWD 双读差分 18/65536 字 → 非读取伪影）。
2. junk 值全部是调色板色（0xE249 红/0x5B2E 灰）→ 是"UI 内容的错位残影"（考古层）。
3. 分音压测（scrtest stress 0-4）：**纯 flip 也出 junk** → 排除具体绘制算子；
   SDRAM 直写保持测试（30s 零衰减）→ 排除硬件保持；front/back 同脏 → 非搬运层独有。
4. 行内 run 坐标分析：漏色带**每行位移恰好 32px** = DMA2D 多行 R2M 的行间步进比
   `NLW+OOR` 多走 32px（64B，一个 AXI 突发）→ 每行漏一条 32px 斜带，旧行内容透出。
5. 修复：`dma2d_fill_rect` h>1 拆逐行 h=1（无行跳转零误差）；`gfx_flip` 改
   memcpy+`SCB_InvalidateDCache_by_Addr(back)`（顺带根除 CPU 读 DMA2D 产区的
   cache 陈旧隐患）。M2M 搬运已实测同样 +32px/行，弃用留档。
6. 期间踩坑并修复：逐行版 `NLR` 漏置 NLH=1 → 传输不启动 TC 恒超时 → 主循环假死
   （RTT 取证）→ `NLR = w | (1<<16)`。

## 4. 本 WP 自主决策与理由

- **取证方式**：无相机，用 probe-rs 帧缓冲直读生成 PNG——像素级、可差分、可复现，
  对"代码层缺陷"的取证强度高于拍照。真实产品照片记入待决清单请用户顺手补。
- **审计范围**：首版只扫 tcs_ui.c，调参页实测仍现 '?' → 参数名来自 param_registry.c，
  已并入扫描范围（gen 与 audit 同步），这是"板上实测抓审计盲区"的闭环样本。
- **16px 字库策略**：16px 档维持纯 ASCII（8x16），中文一律 24px（调参页顶提示已改走
  24px）。64px 大数值由 32px 字形 2× 放大（WP-B 启用）。
- **MAX_TXQ 8→16**：加第 8 个参数后 GET_REGISTRY 全量（8 条目+ACK）会挤爆 8 深队列，
  提前扩容（RAM +2KB，预算内）。
- **ALM_EEPROM 时序顺带修复**：原代码在 control_init 清 alarm_bits 之前置位导致
  EEPROM 报警丢失（stat 与日志矛盾实测发现），改为 control_init 之后置位。

## 5. 遗留与依赖

- 相机实拍照片（任务书 A.1 原口径）：等用户顺手补拍即可，帧缓冲 PNG 已覆盖验收语义。
- EEPROM "配置无效"横幅属提示性报警（真实状态：旧镜像 CRC 与新 8 参数布局不符），
  WP-D 做完 `p save` 固化后此横幅自然消失。
- 调参页选中行/±长按等交互态取证并入 WP-G 交互录屏。

## 6. 待决清单（需用户事后拍板）

| # | 事项 | 已采用默认方案 |
|---|---|---|
| A-1 | 是否需要真实相机照片归档（现有帧缓冲 PNG 已可验收） | 默认 PNG 即验收证据 |
| A-2 | 出厂/交付前是否默认布防=1（当前默认 0，台上安静；真穿戴必须布防） | 默认 0，布防走屏/网页/CLI |
