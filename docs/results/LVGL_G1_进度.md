# LVGL G1 进度与续接状态（未关单；本文件=下窗口续跑锚点）

更新：2026-10-26｜前置：G0 已关单（见 LVGL_G0_结果.md）

## 已完成
1. **G1-a token lv_style 出口 ✅**：`tcs_lvgl/tools/gen_tokens_lvgl.py` 吃单源
   `project_tcs/Core/Inc/ui_layout.h`（UI_C_*_HEX 21 token + UI_W/H/MARGIN/GRID）→
   `tcs_lvgl/app/pages/tcs_tokens_lvgl.h`（TKS_* 宏，逐 token 带源行号）。
   CSS 出口未动（D-L8）。VL-14 对拍素材表：`tcs_lvgl/out/token_map_g1.md`。

## 正典裁决（G1-b 前提，需随 G1 关单入台账）
**G1 像素正典 = 《现行UI规范_v1.1.md》§2.3 几何 + 微观工艺章**，不是 v2.4 §3.7。
理由：①任务书 1.7 明定 UI 规范=视觉权威；②VL-16 对象是"现屏 vs LVGL"，现屏=WP-K
token=C 亮色 v1.1 版（§3.7 暗色是 H743 早期设计，已被 v10.x/WP-K 演进取代）；
③§3.7 降级为历史对照清单。此裁决沿用 UI 规范第 8 章矛盾裁决惯例，挂账待用户终裁翻案权。

## G1 剩余（顺序即执行序）
1. **比例宽字库转换器扩展**（tcs_font_to_lv.py 加 96/48/20 轨）：源=tcs_font96/48/20.inc
   的 ui_glyph_t{cp,w,adv,h,yoff,bits} 表（先定位表实例在 .inc 内的位置）；LVGL 映射：
   `line_height=L(名义字号), base_line=0, adv_w=adv<<4, box_w=w, box_h=h, ofs_y=L-h-yoff`
   （该式保证墨顶=现屏 y+yoff 语义，推导见 G0 试验日志 E6）。
2. **P0 主页×5 状态**（G1 判定口径）：正常/报警黄横幅(M207失联)/ERRORCODE 红横幅/失联灰显
   (--.-+▲角标)/急停确认弹窗(遮罩+白卡(256,136,512,320)圆角12+双键(296,344,192,88)实底
   红+取消(536,344,192,88)白底蓝边,间距48)。UI 规范 SAFE 变体（急停图标红实心）并入报警态
   差异项记录。几何全按 UI 规范 §2.3：顶栏56/hero卡(32|532,88,460,240)/体温风扇卡(y352,460,96)/
   底栏 y472(调参40·模式272·连接504 各208×96;急停768,472,216,112)/横幅(208,8,552,56)/
   模式胶囊(24,16,160,40)/链路点+急停图标(940,20)。
3. **P1 调参页×3 状态**：正常/超限红/编辑中。几何：返回(24,8,120,40)命中扩整列/行×4 y56 步进
   104/行内 名称(24,12) 值(24,44) 滑条(320,36,432×16) 拇指80 ±键(792|872,+8,64×64)/
   底部 −(32,472,96,96) ＋(896,472,96,96) 步进(160,488,176,64) 下一页(360,488,240,64)
   恢复默认(584,488,240,64)。
4. **VL-16 总表 ≥40 行**（双列 8× 裁片三值判定）+ **VL-3 裁片包 ≥28 片×双时点** +
   **U-1 交互路径矩阵**：全按 1.5 三轮独立盲审（R1 自查→R2 隔≥4h/新会话→R3 隔日/换人，
   盲号用 crop8x --blind，映射表分离）。**R2/R3 必须跨时点，天然多会话**——本窗口只做 R1。
5. 容差 diff 预演：fb 基准帧源=V-1 基准帧资产（project_tcs mock/final 与 fb_dump 产物，
   G1 用模拟器帧 vs mock 基准，mock_diff 口径）。
6. 找茬 ≥5 三类 + G1 结果文档（复制法增量）+ 台账回写。

## 复用资产速查
- 影子驱动/帧管线：sim capture <demo> n step outdir；raw565_to_png.py；crop8x.py --blind
- 双轨字体已就绪：tcs_font_lv24(+32 fallback)/font_aa_24；font_audit_lvgl.py PASS
- 版本锁：bash tools/verify_locks.sh（红=禁构建）
- 构建：bash build.sh；新 app/*.c 记得进 CMakeLists add_executable
