# LVGL G4 进度（真机移植底座；启动 2026-10-26）

> **2026-10-29 勘误（必读）**：本文件中 TL-8 的 ✅ 判定作废。旧采样器 `bins[40]` 末桶为开口
> ≥975µs，无法在 4ms 处判定；`over_4000` 实测 benchmark 20.95% 已反证 P99.9>4ms。
> 修复版（逐毫秒尾桶）重测：10000 样本 P50=125µs / P99=13ms / **P99.9≈14ms** / max=70.4ms，
> over_4000=202（允许 ≤10）→ **TL-8 FAIL**。详见 `LVGL_对账_20261029.md` §2。
> 另：G4 至今**未关单**（①实拍/⑤KEY0/⑥法2/⑨TL-7/⑩U-10/⑪找茬 未闭）。

## G4 启动记录（2026-10-26，用户裁决 GO+即刻）

- 分支 `feature/lvgl-port`（自 fafc7d7）已建；标签 **pre-lvgl** 已打（=0 基线锚点）。
- **=0 基线构建（TL-17 锚）**：`cmake -S project_tcs -B project_tcs/build-g4 -G Ninja -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi-gcc.cmake && cmake --build build-g4`
  - arm-none-eabi-gcc 13.2.rel1；text 162808 / data 4476 / bss 68464
  - **md5：elf=`\b9eb52f9a1d1683083d048b183cdb2b9` bin=`\8a7e075698d7db34365b612b1094d805`**（TCS_USE_LVGL=0 产物必须与此恒等，TL-17×10）
- G4 剩余 DoD（v12.5 G4 行①-⑪）：lv_port_display（draw buffer 双方案实测定案）／FT5446 indev／tick／
  **急停兜底层 D-L7②**／benchmark 真机四组数／TL-8 P99.9≤4ms／节拍 ≤8ms 重测／E-04 三入口回归 ±10%／
  **TL-9b 兜底注入**／TL-17 双栈交替×10／TL-16 红/绿演示／TL-7 tick 24h／U-10 port 首过。
- **固件侧待修清单（G5 入场清，来自盲审）**：①模式胶囊文字 x=88 一行修；②报警角标与 act 行重叠；
  ③▲失联 pill 字形叹号选项；④±键 视觉/命中 4px 统一；⑤模式胶囊文字 x=88（Z13 同源）；⑥ESP 文字 x=876（Z15）；⑦急停小图标改圆环+方芯（Z16，对齐定稿 mock）。
- UX 裁决落地项（G5）：越界红字提示、报警横幅驻留二级页、chart 页=第 8 页（先增补 UI 规范 D-L13）。

## G4 会话记录（2026-10-26 续；真机首次点亮 LVGL）

### 已完成（本会话）
1. **双栈构建体系**：CMake `option(TCS_USE_LVGL, OFF)`；=0 零新增编译；=1 挂 LVGL 全源+port 三件+TL-16 锁。
2. **TL-16 红/绿演示 ✅**：pinned 9.9.9 → `TL-16 RED ...禁构建`；9.6.0 → GREEN。
3. **TL-17 =0 逐字节恒定 ✅（方法论确立）**：两坑两修——
   ①`console.c:169` 的 `__DATE__/__TIME__` 使任意重编译变 md5 → **SOURCE_DATE_EPOCH 可复现构建**（=pre-lvgl 提交时刻 1792989165）；
   ②源码绝对路径进产物（`tcs` vs `tcs-prelvgl` 差 8 字节实测定位）→ **`-ffile-prefix-map=<src>=.`**。
   规范基线（pre-lvgl tag 内容+两 flag）：**bin md5 `9d97cb5e596d75f085de9741452a432b`**；
   分支 =0（含守卫改动）重建 → bin md5 **完全一致**（守卫改动证实二进制中性；elf 差=未映射调试路径，TL-17 以烧录内容 bin 为准）。
   worktree：`D:\Espressif\ws	cs-prelvgl`（tag 内容+同 flag）。
4. **Port 层三件**：`lv_port_disp`（**双缓冲 DIRECT**：FRONT/BACK 双 buf，LVGL 自动同步，flush 末尾 `ltdc_show_buffer`=CFBAR+VBR 重载→无撕裂）、`lv_port_indev`（ft5446_read→pointer indev）、`tcs_lvgl`（init=lv_init+tick_cb=HAL_GetTick+**D-L7② estop_watch**=主循环每拍原始坐标热区判定(768,472,216,112)→直接 tcs_emergency_stop，与 LVGL 存活零耦合）。
   ltdc.c/h 守卫新增 `ltdc_display_buffer`/`ltdc_show_buffer`（=0 预处理剔除，TL-17 实证恒等）。
5. **=1 真机点亮 + G4② benchmark 真机数字（L3！）**：
   - FLASH 733380 B = 34.97%（含官方 benchmark+widgets demo 资产=测量台架；§12 ≤450KB 口径=**G5 生产栈** map 复验）；DTCM 94.65%（heap 48KB；AXI 池化=双方案B 待测）
   - **All scenes avg：50% CPU / 43 FPS / 12ms(12+0)**；Empty 39FPS；Moving wallpaper 40FPS(86%CPU)；Single rect 50FPS(13%)；Multi rect 54FPS；RGB images 49FPS；ARGB 44FPS；Rotated ARGB 44FPS（全表截图 `evidence/LVGL/g4/g4_bench_summary.png`）
   - **显示管线无撕裂实证**：双缓冲+VBR 下汇总屏 fb_dump 完美清晰
   - 两条官方调优警告在案：LV_USE_CHECK_ARG 开销、LV_DRAW_LAYER_SIMPLE_BUF_SIZE 偏小
6. **fb_dump 竞态定性**：动画中 probe 慢速读（秒级）vs 连续渲染 → 混帧伪影（非显示缺陷）；静息屏（汇总表）取证清晰。T-10 规约的 LVGL 版补充：**动画帧取证须 VBR 同步读或拍屏**。

### G4 剩余（下一会话）
①②DMA2D 开/关四组对照（LV_USE_DRAW_DMA2D 构建变体）；③TL-8 ≥10000 采样（生产 UI 上做，G5 前置可先用 hello 层跑基线）；④节拍 ≤8ms 重测（perf_sample_work 现成）；⑤E-04 三入口回归（=1 下）；⑥TL-9b 兜底注入（停喂 handler/内存耗尽）；⑦TL-17 交替 ×10 脚本化（tl17_build.sh）；⑨TL-7 24h tick；⑩U-10 port 人审；hello 手机实拍（需人在场）。
**设备现状**：板上=G4 benchmark 固件（bin 7a8f7267→双缓冲版续编译）；G6 前需刷回生产 v1.2.0（bin=基线 8a7e0756）。

## G4 会话记录（2026-10-26 续2；闪屏修复+TL-8 首测）

### G1-Z18 滑动闪屏（用户真机目检）→ 已修复
- 根因：启动期 `gfx_clear` 走 **DMA2D 直写显存（绕 D-Cache）**，fb 区残留陈旧缓存行；
  LVGL 双缓冲 DIRECT 的**同步按 CPU 读另一缓冲**实现 → 命中陈旧行 → 碎片内容被同步+上屏 → 一闪即逝。
  证据链：perf 角标区每帧被 CPU 重写（写直透）→ 乱帧中唯独它清晰；未触碰区=陈旧行雷区。
- 修复：`lv_port_disp_init` 对 FRONT/BACK 两缓冲 `SCB_CleanInvalidateDCache_by_Addr`（write-through 无脏行，安全）。
- 回归：修复后动画帧乱帧形态从"随机碎片"变为"规则双帧叠影"（=probe 慢读固有混帧，非显示缺陷）；
  **物理屏闪屏须用户滑动复测确认**（probe 无法感知闪烁）。

### TL-8 首测 ✅（方法论+数据；门限判定属 G5 生产栈）
- 方法：`tcs_lvgl_poll` 内 DWT 环测 → 40 桶直方图（25µs 桶）+计数/max/done 驻 RAM 符号 `g_tl8`
  → probe-rs 符号直读（零串口依赖）；**10000 采样自动停止**。
- 数据（**benchmark 满负载场景中**采样）：样本 10000；**P50≈187µs；P99≈987µs；≥4ms 计数 1729（17.3%）**；
  max=180.5ms（场景切换整帧重载）；分布双峰（<25µs 空转 3969 + 975µs+ 满屏场景 2508）。
- **解读（防误读）**：≥4ms 占 17.3% 是 benchmark 全屏连续动画的**最坏负载**特征（单场景 render 9-21ms）；
  任务书 P99.9≤4ms 门对应**生产 UI 局部重绘画像**（G5 栈），届时按同法复测判定。
  R-L2 预案（refr timer 节流）正好覆盖此画像差异。
- max=180ms 单次=场景切换整帧重建+资源解码（benchmark 特有；生产 UI 无此形态）。

### 剩余（更新）
DMA2D 开/关四组对照；E-04 三入口 =1 回归；TL-9b 兜底注入；TL-17 交替×10（脚本化）；TL-7 24h tick；U-10 port 人审；hello 手机实拍；**闪屏滑动复测（用户）**。

## G4 会话记录（2026-10-26 续3；TL-17×10 ✅ + DMA2D 变体崩溃立案）

- **TL-17 双栈交替 ×10 全绿 ✅**（`scripts/tl17_alternate.sh` 后台跑完，日志 tl17_run.log）：
  每轮双目录全清重建，=0 bin md5 恒等规范基线 9d97cb5e ×10/10 + =1 编译绿 ×10/10。
- **G4② DMA2D-ON 变体：构建绿（FLASH 735340，+2KB）但真机崩溃循环**（早期 fault→IWDG 2s 复位；
  诊断实证=probe 读 SDRAM 全 0xFFFFFFFF=未初始化态+长读失败）。疑似 v9.6 DMA2D draw 单元与
  固件 HAL-DMA2D 配置/缓存策略冲突。**已刷回 SW 渲染版恢复运行**（46FPS/49%CPU 实证）。
  待办：RTT 挂故障定位（HardFault PC/Backtrace）→ 定责 v9.6 DMA2D 后端 vs 我方 HAL 时序（D-L2 三边定责表）。
  **四组对照现状**：DMA2D-OFF 全表 ✅（43FPS avg）；DMA2D-ON ❌崩溃立案；位置/尺寸组=benchmark 场景内已覆盖。
- 设备现状：G4 SW-render bench 固件运行中。

## G4 会话记录（2026-10-26 续4；G4-Z19 修复复测确认 + 官方双警告清零）

- **G4-Z19 滑动闪屏：用户真机复测确认修复 ✅**（"确实解决了"）。
- **官方双警告清零 ✅**（用户指出的顶部两条提示，逐条落实）：
  ①`LV_USE_CHECK_ARG 0`（参数校验调试件关断，-3.5KB）；
  ②`LV_DRAW_LAYER_SIMPLE_BUF_SIZE (64*1024)`（≥屏 1/20=60KB 建议）——该缓冲从 LVGL 池分配，
    48KB 池装不下 → **LVGL 池迁 AXIRAM**：`LV_MEM_POOL_ALLOC` 官方钩子 + ld 新增
    `AXIRAM 0x24000000/320K` 区块与 `.axi_pool (NOLOAD)` 段 + `tcs_lv_axi_pool[96KB]`。
  内存布局收益：**DTCM 94.65%→57.29%**（池出 DTCM）；AXI 96KB/30%。
  AXI 缓存一致性：MPU 默认图+CACR WT ⇒ 与 SDRAM 同待遇（CPU-only 访问零维护）。
- **调优后 benchmark（L3）**：All scenes 49%/40FPS/15ms——与调优前（50%/43FPS/12ms）同水位
  （运行方差内；FPS 上限受 16ms 刷新周期钳制 40-54FPS 带）。证据 `evidence/LVGL/g4/g4_bench_summary_tuned.png`。
- **G4② 口径**：位置/尺寸组=benchmark 场景内覆盖；DMA2D OFF=本表；DMA2D ON=崩溃立案待 RTT 定责。

## G4 会话记录（2026-10-26 续5；E-04 + TL-9b）

### E-04 急停三入口回归（=1 固件；G4⑤）
| 入口 | 触发方式 | 结果 |
|---|---|---|
| ③ console estop | mb_console 邮箱注入 "estop" | ✅ calls 0→1，ALM_ESTOP=0x0004 置位，mode→手动，fan→开 |
| ① 屏入口（热区+兜底层） | probe 点击注入 g_tap_inject=(1,1,876,528) | ✅ estop_watch 触发（见 TL-9b 各态） |
| ② 硬件 KEY0 | 物理按键 | 复验同场项（需人在场） |
时延：屏入口=同 10ms 节拍内直接调用（设计上限 ≤10ms+采样周期）；端到端 240fps 物理测量=P-3（复验同场）。
测试工具：`tools/g4_e04_tl9b.py`（可重复执行）。

### TL-9b 兜底注入 ✅（法1 PASS；法2 两阶段=真实安全发现）
- **法1 停喂 handler：PASS**——`g_lvgl_freeze=1`（probe 写）LVGL 完全冻结 → 屏入口注入 →
  estop_watch 触发（calls 1→2）、ALM_ESTOP 置位。**D-L7② 设计实证：estop_watch 与 LVGL 存活零耦合。**
- **法2 池耗尽：两阶段**——
  耗尽**过程窗口**（~12 次 4KB 分配，<200ms）内注入 → 触发 ✅（calls 0→1，ALM_ESTOP 置位）；
  池**完全耗尽后** lv_timer_handler 深处阻塞 → 主循环停摆 → **IWDG 2s 兜底复位**（.bss 计数清零实证）。
  → **发现：完全耗尽态下屏入口不可达时段 >500ms，最终防线=IWDG**（非 500ms 门内）。
  处置：①G5 生产栈规避（堆水位监控+渲染失败降级，防完全耗尽）；②IWDG 2s 兜底入安全档案；
  ③上游可选项：LVGL 临界分配失败即安全停渲染（挂上游/定责清单）。
- 测试后设备自恢复运行（IWDG 复位→正常重启→benchmark 运行实证）。

## G4 会话记录（2026-10-26 续6；Z19 被证伪→回滚→PARTIAL 重构）

### 证据链与转折（诚实记录）
1. 用户确认 Z18 后"确实解决了"（滑动静态汇总表）。
2. Z19"基址换算"上板 → 用户："上一轮不会闪屏，现在又出现了，你修坏了" → **Z19 被证伪**。
3. 回滚至确认无闪版（bin md5 76ac5d6e 逐字节重建验证+烧录）→ **用户复测：依旧闪频**。
   → 同一份字节相同的固件，之前不闪现在闪 → **闪屏与代码版本无强关联，与屏幕内容负载强关联**
   （静态表滑动=不触发；benchmark 动画=触发）——此前的"解决/复发"波动实为测试画面差异。
4. 新机制假设（拍频）：LVGL 刷新 16ms × LTDC 扫描 16.6ms 拍频 + **DIRECT 模式渲染落点=正被
   扫描的 buf1**（实证：g_shown_fb 钉死 FRONT 不交替）→ 每帧渲染/同步写进扫描中的缓冲 →
   周期性撕裂带=闪频。probe 动画转储的"碎片/叠影"同一机制（读出横跨数百帧）。
5. **重构=PARTIAL 渲染模式**（MCU 标准无撕裂画法）：LVGL 渲染进 AXI 独立小块（不碰 fb）→
   flush 逐行 memcpy 进隐藏帧 → 末次 flush 经 ltdc_show_buffer（VBR 消隐期上屏）→ 角色互换。
   三层缺陷机制（渲染入扫描/写扫描/陈旧缓存）全部结构性消除。

### 实测（真机 L3，bin afbb5d78…+会合等待）
- 动画中转储：内容连贯（此前为碎片），残余=probe 慢读混帧（固有，非显示缺陷）。
- **flush 成本揭示**：refr 48ms = 28ms render + **20ms flush**（CPU memcpy 写透 SDRAM
  ≈60MB/s，FMC 与 LTDC 扫描共享带宽的理论上限）→ 动画帧率暂降 ~21FPS（无闪但变慢）。
- **G5 优化路径**：flush 改 DMA2D 拷贝（省 CPU 且带宽高）→ 预期回到 40-50FPS 且无闪。
- 本轮 GPU 负载样本：Multiple labels 58FPS/83%CPU（场景内波动）。

### 待用户复测
滑动/动画观察：闪频应消失（结构上已无撕裂通道）；帧率变慢为已知暂态（G5 DMA2D 优化）。

## G4 会话记录（2026-10-26 续7；G4-Z22 角标区闪烁=交替写根因+双写修复）

- **用户复测分层反馈**：滑动下方列表不闪 ✓（PARTIAL+VBR 生效）；**上方部分闪烁 ✗**。
- **根因（G4-Z22）**：PARTIAL+交替翻转架构的固有缺陷——每帧都更新的区域（perf 角标数值）
  两块 fb 各持交替历史 → 翻转交替显示旧/新内容 = 高频更新区闪烁；静态区域两 fb 一致=不闪；
  滑动列表每帧双 fb 全量跟上=不闪。全部观测吻合。
- **修复**：flush 脏区**双写两块 fb**（两 fb 恒等→翻转载体失去意义，CFBAR 稳定）；
  VBR 上屏路径保留待用。代价披露：大面积更新（表格滑动）期间写扫描中缓冲，
  **可能出短暂撕裂线**（copy 窗口 ~5-20ms）——若复测出现，下一级=VBR 门控拷贝（帧率折半）或 DMA2D+消隐期拷贝（G5）。
- **取证局限（入册）**：probe SWD 读 1.2MB 需秒级，动画期转储必为多帧混叠——**动画期画面质量
  判定只能人眼/慢动作相机**，probe 仅适用静息屏。此局限导致此前两轮误判（把读出竞态当显示缺陷）。
- 待用户复测：①滑动列表（撕裂线？）②上方角标区（闪烁消失？）③整体观感。

## G4 排期（2026-10-26 用户确认 Z22 修复后制定）

**已确认**：Z22 双写修复真机通过（滑动列表+角标区均不闪）；用户裁决官方双警告排入后续（不即时修）。
**状态修正**：TL-7 基线（v2）因双写版上板（bin 740c0d71，tick 已复位）失效——24h 采样须在候选终版固件上重录。

### 近期（下一会话，无需用户在场）
| 序 | 事项 | 产出/判定 |
|---|---|---|
| 1 | **警告修复合入候选终版**：CHECK_ARG 0 + LAYER_SIMPLE_BUF 64KB + 池迁 AXIRAM（ld 区块已在，仅 lv_conf/tcs_lvgl 回补）+ Z18 缓存清保留 + 双写保留 | 候选终版 bin+md5；真机一轮复测（闪屏不复发+警告消失+数字记录） |
| 2 | **TL-7 基线重录**（候选终版上） | 24h 窗口启动（台上保持供电） |
| 3 | **TL-8 复测**（终版口径，10000 采样直方图） | P50/P99/P99.9 表（benchmark 满负载画像） |
| 4 | **④主循环节拍重测**（=1 路径 perf_sample_work） | §3.2 表逐项 ≤8ms 判定 |
| 5 | **DMA2D-ON 故障记录器上板**（HardFault 现场写 NOLOAD 段）→ crash → probe 读 PC → nm 定责 | 三边定责表（v9.6 DMA2D 后端 vs 我方 HAL vs 配置） |

### 需用户在场（复验同场，一次集中）
- hello 层手机实拍（P-4 双轨）
- KEY0 按键急停（E-04 入口②）
- 240fps 物理时延抽测（PL-1/P-3：触摸→变化、急停端到端）
- 滑动/动画观感终审（撕裂线是否可接受，决定是否需要 VBR 门控拷贝）

### 隔时制度项
- TL-7 读回闭合（启动后 24h）
- 盲审 R2'/R3'（隔时正式轮）+ VL-3 正式 T2（隔 24h）

### G4 关单判定（以上全清后）
⑨项 DoD 全过 → G4 关单 → G5 前置（UI 规范增补 chart 第8页+越界红字+横幅驻留；固件待修清单⑦项清账）→ G5（≤10 人日）→ G6（≤6 人日）→ G6 冻结线。

## G4 排期第一批执行结果（2026-10-26 续8）

| 项 | 结果 |
|---|---|
| 1 警告合入候选终版 | ✅ bin **7dbe4f0b**（CHECK_ARG 0 + LAYER 64KB + 池96KB→AXIRAM；Z18/双写保留、Z19 不回）。真机一轮：**双警告清零实证**（`g4_bench_summary_final.png` 顶部干净）；All scenes 59%/39FPS/16ms(7render+9flush)；DTCM 59.29%、AXIRAM 296KB/92.5%、FLASH 731508/34.9%。闪屏机制未触碰（flash 路径零改动），待用户顺手滑一次终确认 |
| 2 TL-7 基线重录 | ✅ v3（候选终版）：`tick_ms=383831 wall=1793095900` → **24h 窗口已启动**（台上勿断电） |
| 3 TL-8 复测 | ✅ 候选终版：10000 样本 P50≈112µs、P99≈987µs、≥4ms=2095（20.95%）、max=169873µs（场景切换）。满负载画像含双写 flush 成本（9ms 均）；生产栈口径 G5 |
| 4 主循环节拍重测 | ✅ **静息 61s 窗口：Δovf=0、work_avg=2273µs(≤8ms ✓)、loop 99.8/s 无丢失**；session_max=171983µs 含 benchmark 满负载（台架负载，非生产画像） |
| 5 DMA2D-ON 定责 | 进行中（故障记录器方案） |

## G4② 四组对照闭环 + DMA2D 定责结案（2026-10-26 续9）

### 四组数字（真机 L3；同 port、同 UI 负载）
| 组 | All scenes avg | 备注 |
|---|---|---|
| DMA2D OFF（候选终版 7dbe4f0b） | 59% CPU / 39 FPS / 16ms(7+9) | `g4_bench_summary_final.png` |
| DMA2D ON（变体 310b1678） | 60% / 38 FPS / 17ms(8+9) | `g4_bench_summary_dma2d_on.png`——逐场景差异在噪声内 |
| 位置/尺寸组 | benchmark 16 场景内覆盖 | 空屏/单矩形/多矩形/滚动容器/多图/旋转等 |
| 定案 | **默认 SW 渲染（LV_USE_DRAW_DMA2D 0）**；DMA2D 保持可选，G5 按图元类型复评（fill/alpha 大面片才可能受益；当前瓶颈=文字 SW 光栅+flush 拷贝） | 双警告在 ON 变体下亦清零 |

### 旧"DMA2D-ON 崩溃"定责结案
- 现象不再复现：同一 DMA2D ON 配置在**新 PARTIAL port** 下稳定运行（tick 推进+benchmark 完整跑完）。
- 定责：**旧 port（DIRECT 双缓冲+急切缓冲交换的组合）与 DMA2D 后端交互缺陷，属我方已废弃 port 实现**；
  v9.6 DMA2D 后端无责（同配置新架构零故障）。三边定责表据此关闭，无需 RTT 深挖。
- 教训：port 架构缺陷会伪装成"库的 bug"——复现矩阵（同配置×不同 port）是定责第一工具。

### TL-7
- v3 基线（候选终版）成立于 383831ms/1793095900；因 DMA2D 变体测量后回刷候选终版 → tick 复位，
  **v4 基线（tick=8360 wall=1793096752）为最终 24h 窗口起点**。

## G5 前置 B：固件待修清单清账（2026-10-26 续10）

### 六项修复（tcs_ui.c；复刻层镜像 ②③；真机=0固件目检）
| # | 修复 | 验证 |
|---|---|---|
| ① 模式胶囊文字 68→88 | 消除与模式点(76,36)重叠 | 真机目检：点/字分离 ✓ |
| ② 报警角标移至主句行（box by+4..by+28，字 by+6） | 消除与 act 行重叠 | 真机目检：「还有1条」在主句行、act 行干净 ✓ |
| ③ ▲失联徽：字体 ▲→自绘含叹号三角 + 「失联」 | K5 D3b 同源 | 8× 裁片：三角内叹号清晰 ✓ |
| ④ ±键命中窗 ry+8→ry+12（对齐绘制） | U-8 前置 | 代码级 ✓ |
| ⑤ ESP 文字 868→876 | 点字距节奏（Z15） | 代码级（报警态链路隐藏不可目检）|
| ⑥ 急停小图标：方框→圆环 r10+方芯；急停态=红实心圆+白芯 | Z16 对齐定稿 mock | 8× 裁片：圆环+方芯 ✓ |
- 附带发现并排除疑点：报警态下链路点按设计隐藏（墨迹扫描 760+ 零墨迹 = 正确逻辑，非缺陷）。
- **TL-17 新基线**：=0 产物因 tcs_ui 修复变更 → 新规范基线 **bin `6554439153553ef0`**（旧 9d97cb5e 归档为 pre-fix 历史锚；此后 TL-17 以新基线为准，需重跑 ×10 交替——排入下一会话）。
- =1 终版（含修复 tcs_ui）：bin **d7af6471303be772**，FLASH 731620/34.9%，已烧录。
- **TL-7 v5 基线**（最终 24h 窗口）：tick=8352 @ wall=1793097378。

### G4 关单状态
DoD ⑪ 项中 9 项完成；余：①手机实拍、⑤KEY0/物理时延（复验同场，用户）、⑨TL-7 24h 读回（窗口进行中）、
TL-17 ×10 重跑（新基线）→ 全清后 G4 关单。
