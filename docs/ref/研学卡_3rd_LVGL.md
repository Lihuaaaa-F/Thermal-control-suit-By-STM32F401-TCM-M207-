# 研学卡：LVGL（第三方官方件 · W8 §7b）

- 制卡日期：2026-10-27。仓库：`D:\Espressif\ws\tcs`。
- 版本事实源：`docs/ref/依赖版本清单.md` LVGL 行。
- 调用面数据：本卡制卡时实跑 grep（命令见 ③）。
- **G 线未提交区特别标注**：`project_tcs/App/lvgl_port/` 与 `project_tcs/third_party/` 均在 **git 未跟踪工作区**（`git status` 显示 `?? project_tcs/App/lvgl_port/`、`?? project_tcs/third_party/`，2026-10-27 实测）——本卡全部行号/文件均为该暂态区快照，G 线合入 commit 前不可 git 审计，`git clean` 即抹除。

## ① 官方文档与版本

| 项 | 内容 |
|---|---|
| 组件 | LVGL（Light and Versatile Graphics Library，H743 屏 UI，G 线引入） |
| 精确版本 | **9.6.0**（实测 `project_tcs/App/lvgl_port/LVGL_PINNED_VERSION` 文件内容=`9.6.0`） |
| 来源 | GitHub lvgl/lvgl 官方（本机 `project_tcs/third_party/lvgl/` 快照树） |
| 锁定方式 | `lvgl_version_check.cmake` 强校验 pin（**TL-16**：pinned 改 9.9.9 → `TL-16 RED …禁构建`；9.6.0 → GREEN，红绿演示双证，见 `docs/results/LVGL_G4_进度.md` §构建体系）+ third_party 树 |
| 许可 | MIT（`project_tcs/third_party/lvgl/LICENCE.txt`） |
| 官方文档 | docs.lvgl.io（9.x 线）+ 本机 `third_party/lvgl/src` 内官方源注与 `lv_conf.h` 模板注释 |
| 升级风险注记 | G 线管辖；升 9.7+ 走 G 线重评估（W9 CHANGELOG 扫记）。**暂态锁注记（红队 W9）**：锁两端（PINNED_VERSION/third_party）现处 G 线未跟踪工作区，合入前不可 git 审计，`git clean` 即抹除——G 线合入 commit 后本锁生效可审计（依赖清单行原文） |

## ② 官方示例文件路径（逐文件，G 线未提交区，标注）

| # | 路径 | 说明 |
|---|---|---|
| 1 | `project_tcs/third_party/lvgl/demos/`（G 线未提交区） | 官方演示集：`benchmark/`、`gltf/`、`keypad_encoder/`、`music/`、`render/`、`stress/`、`lv_demos.c/h`、`Kconfig`、`README.md`——**benchmark 即 G4② 四组数字的真机来源**（tcs_lvgl.c:34 调用 `lv_demo_benchmark()`） |
| 2 | `project_tcs/third_party/lvgl/include/`（+顶层转发头 `lvgl.h`/`lvgl_private.h`/`lv_version.h`） | 官方公共 API 头树（`lv_version.h` 为官方 deprecated 转发件→`include/lvgl/lv_version.h`，实测头部 #warning） |
| 3 | 上游仓 `examples/` 目录：github.com/lvgl/lvgl → `examples/`（get_started/porting 等） | **本机快照未含顶层 examples/ 目录**（third_party/lvgl/ 仅 demos+include+src），官方入门/porting 示例按上游 URL 记 |
| 4 | 工程 port 三件（G 线未提交区）：`project_tcs/App/lvgl_port/lv_port_disp.c/.h`、`lv_port_indev.c/.h` | 我方 port 层（官方 porting 模式实例：PARTIAL 渲染+双 fb 写+FT5446 indev） |
| 5 | 工程入口 `project_tcs/App/lvgl_port/tcs_lvgl.c/.h` + `lv_conf.h` + `LVGL_PINNED_VERSION` + `lvgl_version_check.cmake` | 初始化/hello/benchmark 入口、配置、TL-16 锁三件 |

## ③ 我们全部调用面清单（文件:行）——概览口径（G 线域，标注量级）

grep 命令：`grep -rn "lv_" project_tcs/App/lvgl_port project_tcs/Core/Src/ltdc.c --include=*.c`
总命中 **32 行**（任务书指定概览口径：标注量级即可，不逐行穷举到 src 树）。按文件拆分（表行数=32 概览行）：

| 文件 | 命中行 | 量级与内容概览 |
|---|---|---|
| project_tcs/App/lvgl_port/tcs_lvgl.c（G 线未提交区） | 15 | 入口域：`lv_init/lv_tick_set_cb(HAL_GetTick)`（:18-19）→ port 两 init（:24-25）→ hello（`lv_screen_active/lv_obj_set_style_bg_color/lv_label_create/lv_label_set_text/lv_obj_center` :28-32）→ `lv_demo_benchmark()`（:34，官方 demos 面）→ `lv_timer_handler`（:41，主循环泵）；含 `lv_demos.h/lv_port_*.h` include（:7-9）与零耦合注记（:4） |
| project_tcs/App/lvgl_port/lv_port_disp.c（G 线未提交区） | 9 | 显示 port：`flush_cb` 内 `lv_draw_buf_width_to_stride`（:28）+ `lv_display_flush_ready`（:39）；init `lv_display_create/set_flush_cb/set_buffers`（:51-53）；含头注 PARTIAL+双写策略说明（:1） |
| project_tcs/App/lvgl_port/lv_port_indev.c（G 线未提交区） | 8 | 输入 port：`lv_indev_create/set_type(LV_INDEV_TYPE_POINTER)/set_read_cb/set_display`（:21-24）+ `touch_read_cb`（:5，FT5446 原始采样→指针） |
| project_tcs/Core/Src/ltdc.c | **0** | **零直接调用**——LTDC 与 LVGL 解耦：LVGL 经 flush_cb 写 fb，ltdc.c 只做时基/SDRAM/LTDC 硬件（域内 grep 零命中实测） |

（32=15+9+8；行级明细即上表"命中行"列所列，已覆盖全部物理行号。）

## ④ 对照结论（✓/偏差+理由/待整改）

| 锚点 | 结论 |
|---|---|
| G 线管辖 | ✓ 版本与调用面均 G 线域：lvgl_port/ 与 third_party/ 未跟踪（git status 实测），本卡仅作台账快照，不作 W8 整改依据 |
| TL-16 版本锁 | ✓ 红绿演示双证（台账 2026-10-26 L161：TL-16 红绿演示✅；G4_进度.md §构建体系：9.9.9→RED 禁构建、9.6.0→GREEN）；锁机制=lvgl_version_check.cmake 强校验 PINNED_VERSION |
| 官方双警告清零 | ✓ 台账 2026-10-26 L171（G4 调优轮关单）：①`LV_USE_CHECK_ARG 0`；②`LV_DRAW_LAYER_SIMPLE_BUF_SIZE 64KB`（≥屏 1/20 官方建议）→ LVGL 池迁 AXIRAM（LV_MEM_POOL_ALLOC 官方钩子+ld .axi_pool 段 96KB），DTCM 94.65%→57.29%。**注记**：G4 排期第 1 项=警告修复合入候选终版+TL-7 基线重录（G4_进度.md §排期，2026-10-26 用户裁决"排入后续"后已由续4 会话清零）——合入动作仍待 G 线候选终版落地 |
| 调用面规模 | ✓ 量级与 G 线一致：port 三件+入口 32 行 lv_ 面（概览），生产 UI 栈尚未展开（G2 chart 页规格草案在册） |
| 暂态锁风险 | 偏差+理由（红队 W9 已立案）：锁两端在未跟踪区，`git clean` 即抹除、合入前不可审计——**待整改入 G 线**（合入 commit 后依赖清单锁行生效；无需 W8 动作） |
| 升级路径 | ✓ 9.7+ 走 G 线重评估（W9 CHANGELOG 扫记）；当前 9.6.0 与 lv_conf/池布局/AXIRAM 段强耦合，升版本=池与警告清零重验 |
