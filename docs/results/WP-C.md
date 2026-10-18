# WP-C 结果文档 — T1 清理 + 性能实证（C.1~C.5）

日期：2026-10-18 ｜ 状态：**完成（关单）** ｜ 基线：任务书 v5.1 §3 WP-C

## 1. 完成内容

| 项 | 内容 | 结果 |
|---|---|---|
| C.1 | iwdg.c RLR=249（2s）+ **D3 债清偿：裸地址宏→CMSIS `IWDG1->` 符号** | ✓ 上板 wdttest 复位实测 |
| C.2 | 删 blackbox.c/h（CMake/main/it/ui 页脚四处解挂；it.c 保留 HardFault RTT 打印，**移除 SysTick ISR 喂狗实验臂**——2s 看门狗必须能抓主循环假死） | ✓ |
| C.3 | README 重写：工具纪律表（编译/烧录/复位核对/RTT/邮箱控制台/帧缓冲取证/单测/字库门禁/活性体检/ESP32） | ✓ |
| C.4 | 复位源判定 **IWDG1RSTF 优先于 PIN**（IWDG 复位时 PINRSTF 同置，旧序误报）+ 打印 RSR 原始值；stat 增显 arm/loop | ✓ 实测 RSR=0x04460000 → 正确判 IWDG |
| C.5 | DWT 实测（每拍工作/UI 重绘/触摸轮询）回填任务书 §5；`tools/wp_check.sh` 活性体检；节拍语义定夺 | ✓ 见 §3 |

新增命令：`perf`（DWT 汇总）、`wdttest`（5s 忙等，2s 超时触发）；`tools/wp_check.sh`（reset→loop_passes 两次读判增长→PASS/FAIL，6.1 关单工具）。

## 2. 运行时证据

- `WP-CHECK PASS loop_passes 414 -> 630`（tools/wp_check.sh 输出）
- wdttest 后启动行：`[SYSTEM] boot reset-src=IWDG (RSR=0x04460000)`（C.1+C.4 双证据）
- `perf`：`work avg=194us max=26330us ovf=3 | ui_full=13.4ms | touch max=181us | loop rate=99/s`
- `WP-C_home_final.png` / `dbg_d1/d2.png`：性能改造后画面取证（浅色完整、无伪影）
- 单测 run_all ALL GREEN（含 font_audit 门禁）

## 3. 性能攻坚实录（WP-C.5 主线，四次迭代）

| 版本 | ui_full | 诊断 | 处置 |
|---|---|---|---|
| ① memcpy flip（write-through） | 115.6ms | 1.2MB 写穿透 ~45MB/s，每 500ms 顶栏翻转一次 | 换真双缓冲 |
| ② LTDC CFBAR 乒乓（零拷贝 flip） | 86.7ms | flip 已免费；瓶颈=DMA2D 小搬运 ~18µs/次 × ~7000 次/页 | 填充改逐行 h=1→h=2 合并，收益有限 |
| ③ 单次多行 R2M 实验 | （实验） | **挂死系统总线**（主循环假死→IWDG 复位救回）→ 证实 DMA2D 写路径在本片不可用 | 弃用 DMA2D |
| ④ **CPU 写穿透填充（32 位 store 展开）+ 只填空白区** | **13.4ms ✓** | — | 定版 |

节拍语义定夺：保留 `等待≥10ms` 语义（负载下自然拉伸、自校正）——均值 10.1ms（99 圈/s）；整页重绘拍瞬时 13~26ms 属交互级，不在 500ms 周期路径。

**乒乓双缓冲一致性规则**（实测翻车后确立）：局部重绘天然保持两缓冲同页；仅**切页/弹窗/boot** 需 `gfx_sync_buffers()`（+~27ms，交互级一次）；同页整帧重绘（模式切换等）免同步。

**取证工具链修复**：LTDC 寄存器经调试口读数不可靠（恒 0），且 CFBAR 真实偏移为 layer+0xAC（RESERVED0[2] 易踩坑）——fb_dump.py 改由固件 RAM 变量 `g_shown_fb`（gfx_flip 维护）经 nm 定位直读。先前"黑底主页"影像即此 dump 伪影，非固件缺陷。

## 4. 自主决策与理由

- **DMA2D 全弃用**：三类实测缺陷（+32px/行斜带、多行挂死总线、小搬运高开销），CPU 写穿透在 480MHz 下反而更快且无一致性/斜带问题；DMA2D 代码移除并留档注释，防后续误用。
- **移除 ISR 喂狗**：实验臂 B 会掩盖主循环假死（H2 门禁失效），2s 看门狗必须由主循环独占喂。
- **perf 计数放 RAM 全局**（volatile）：mailbox/probe-rs 直读，无串口也能取证。

## 5. 遗留与依赖

- 调参页全清重绘 26ms 瞬时（可再用空白区策略优化，收益小，暂缓）。
- 网页↔屏同步 ≤1s 待 WP-F 真链路（飞线 U1）。

## 6. 待决清单

| # | 事项 | 已采用默认方案 |
|---|---|---|
| C-1 | 节拍语义：负载拍拉伸（现方案）vs 严格 10ms 丢弃工期 | 现方案（已文档化） |
| C-2 | DMA2D 是否永久弃用（已留档） | 弃用；如需恢复先复测斜带/挂死问题 |
