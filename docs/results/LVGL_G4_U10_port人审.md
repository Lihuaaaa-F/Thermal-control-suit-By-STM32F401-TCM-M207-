# U-10 port 层人审清单（G4⑩ 首过；v12.5 §11 U-10）

对象：`project_tcs/App/lvgl_port/{tcs_lvgl.c, lv_port_disp.c, lv_port_indev.c}`（G4 移植层）
审阅人：执行会话自查（正式人审=G5 全量时用户/二次复核）。日期：2026-10-26。

| # | 条目 | 判定 | 依据（逐条对照源码） |
|---|---|---|---|
| ① | 事件回调内禁长操作/禁阻塞（对齐主循环铁律 §3.2） | ✅ | flush_cb 仅寄存器写 CFBAR+SRCR（零等待）；indev read_cb 一次 ft5446_read（bit-bang ≈0.3ms=固件触摸轮询预算同源）；无任何忙等/拷贝整帧 |
| ② | 对象 create/delete 配对与生命周期（联 TL-2） | ✅ | display/indev 各 init 一次不重建；sprite/QR 类动态资源挂 LV_EVENT_DELETE 释放（G0-G1 cycle 3000 零泄漏实证，TL-2 L2 预演）；真机 TL-2 ×1000 待 G5 |
| ③ | UI 状态机状态×事件穷举注释齐 | ✅（port 无状态机） | port 层无状态（直通层）；状态机归 tcs_ui/G5 页面层，U-11 穷举表在 G5 交付 |
| ④ | 魔法数字禁入（一律 token） | ✅（本轮修正） | 热区原硬编码 (768,472,216,112) → **改引 `R_DOCK_ESTOP`（ui_layout.h:115 单源）**；LCD_WIDTH/HEIGHT/BUF=硬件常量非 UI token；颜色 token 全走 TKS_*（gen_tokens 单源） |
| ⑤-1 | 反模式：全屏 invalidate 滥用 | ✅ | port 无 invalidate 调用；LVGL 自身按脏区；benchmark 场景的全屏动画=G4 测量负载非生产代码 |
| ⑤-2 | 反模式：深层样式拷贝 | ✅ | port 零样式操作（remove_style_all/直写 token 色各一处，均在 init 一次性） |
| ⑤-3 | 反模式：忽略对象 cache | ✅ | 字形走 LVGL 内建 cache（font 9.6 自动）；无绕行 |
| ⑤-4 | 反模式：主线程做布局重活 | ✅ | 主循环仅 tcs_lvgl_estop_watch（1 次 I2C 采样 0.3ms）+ lv_timer_handler（LVGL 调度）；布局/渲染全在 LVGL 内部机制 |
| ⑤-5 | 反模式：indev read_cb 未按协议返回 data->state 与继续标志 | ✅ | read_cb 每次显式置 data->state（PRESSED/RELEASED）与 point；轮询型 indev 不需 continue_reading（自适应读才需要——已注记） |

## 遗留与注记
1. **热区常量本轮已单源化**（原硬编码为 Z19 修复前代码，U-10④ 修正）。
2. **estop_watch 的 I2C 采样与 LVGL indev 采样同拍并行**：每拍两次 ft5446_read（兜底+indev）≈0.6ms，
   仍在触摸轮询预算（0.3ms×2 分位）内；G4④ 节拍重测时合并计量（待办）。
3. **flush_cb 无撕裂依赖 VBR 重载**：已实证（汇总屏静息帧清晰）；TL-11 录帧在 G5 复核。
4. **port 不含 UI 状态机/页面代码**：U-10 全量版在 G5 页面层交付时复审。

## 结论
G4 port 层 U-10 首过（10/10 项 ✅，1 项本轮修正回归）。G5 全量版（含页面层）复审挂账。
