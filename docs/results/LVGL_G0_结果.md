# LVGL G0 关单结果——模拟器底座（调温服任务书 v12.5 §7 G0）

日期：2026-10-26｜执行：ZCode 会话（无人值守条款）｜产物树：`D:\Espressif\ws\tcs_lvgl`（PC 侧独立，D-L1）
构建：MSVC 19.42 + Ninja + SDL2 2.32.10｜sim.exe sha256[:16]=`00f28c3018f05c1b`｜LVGL v9.6.0（commit 80ca777e…，三重锁 PASS）
试验日志：`docs/evidence/debug/G0_debug_20261026.md`（E1-E7）

## 一、判定表（G0 六条 DoD，行行挂 artifact）

| # | 验收标准 | 判定 | 证据（artifact） |
|---|---|---|---|
| ① | 两 demo 各 ≥10 张连续帧 + benchmark 帧率数字（标"模拟器参考值，非预算依据"） | **PASS** | widgets 12 帧 `docs/evidence/LVGL/g0/frames/widgets_0000..0011.png`（帧间 mean diff 13-27，非全同）；benchmark 12 帧 `benchmark_0000..0011.png`（首末 diff 60，跨场景）；CSV 16 场景 + 结果帧 `benchmark_result.png` + `benchmark_report.log`——**All scenes avg 38 FPS / 0% CPU（模拟器参考值，非预算依据；G4 起只认真机 L3）** |
| ② | 双轨字体各渲染同一 20 汉字+ASCII+°℃ 混排串；font_audit 零缺字；基线对齐截图 | **PASS** | 混排串=「贴身水箱体温风扇急停确认调参连接模式失联 Alarm 25.6°C ±℃ ▲」（20 汉字全在现屏 24px 表）；帧 `frames/fonts_dual_track.png`；`font_audit_lvgl.py` PASS（33 字面量×2 轨全覆盖，轨 A 含 24→32 fallback 链）；基线量化：**双轨字底相对各自基线标记均 -2px，相对偏差 0px**；8× 裁片 `crops_g0/fonts_dual_{hanzi,degC,baseline}_track{A,B}_T1.png` |
| ③ | crop8x.py 对 demo 帧产出 ≥8 片样例裁片 + 规范文档 + 盲号重排演示 | **PASS** | 10 片（≥8）`crops_g0/*.png`（覆盖字形/AA 边缘/圆角头像/1px 下划线/渐变/基线六类部位）；规范 `tcs_lvgl/tools/crop8x_spec.md`（随工具归档）；盲号 `crops_g0/blind/B01..B10.png` + 映射表 `blind_map_20261026_134317.json` 物理分离归档；清单可复现 `crops_g0/manifest_g0.csv` |
| ④ | 一键重建脚本在新目录执行到出帧 ≤30min | **PASS** | `tools/rebuild_check.sh` 实测新目录：复制 3s + 全量构建 17s + 出帧 1s = **21s ≪ 1800s**（干净树=无 build/无产物；日志 rebuild_build.log 已存产物树） |
| ⑤ | 附录 A 五要素齐全（含 issue URL 集） | **PASS** | `tcs_lvgl/README.md` 附录 A：版本+commit（80ca777e…）/vendoring 定案+三重锁/MIT+SDL2 许可证路径/钉定 v9.6.0 URL 集（2026-10-26 实存核验，含站改版勘误：`LV_COLOR_DEPTH`→`LV_COLOR_FORMAT_DEFAULT` 映射）/宿主定案+脚本路径 |
| ⑥ | 找茬 ≥5 且三类齐全 | **PASS** | 见下节 Z1-Z8：视觉放大 4 + 极端输入 1 + 竞态打断 1 + 固定动作 2；坐实缺陷 2 项已修复+回归 |

**主动找茬记录（1.4；坐实=修复+回归，排除=写清证据）**

| # | 类 | 现象 | 处置 |
|---|---|---|---|
| Z1 | 视觉放大 | 轨 A ℃ 渲染为占位框——点阵 24px 表原生无 U+2103（现屏同 limitation，gen_font_cn.py 只把 ℃ 放 32px 固定集） | **坐实缺陷→修复**：轨 A 增加 tcs_font_lv32 fallback 链；回归=复验帧 ℃ 正常 + font_audit PASS（E7） |
| Z2 | 视觉放大（日志证据） | widgets demo 启动告警 MONTSERRAT_16 缺失回退默认字体 | **坐实→修复**：lv_conf.h 启用 LV_FONT_MONTSERRAT_16；回归=最终构建日志告警计数 0 |
| Z3 | 视觉放大 | 折线图渐变填充 8× 下可见水平色带（量化台阶） | **归因非缺陷**：RGB565 16bpp 垂直渐变的固有量化；留作 **VL-2 16/32 位对比定案的佐证素材** |
| Z4 | 视觉放大 | fallback 32px ℃ 墨底浮离基线 ~2px（本轨其余字 -2px） | **挂 G1**：跨字号 fallback 度量差；复刻页若 24px 行需 ℃，做基线校正或专生 24px ℃ 字形（真实需求驱动，禁预造） |
| Z5 | 极端输入 | 混排串在轨 A 定宽网格下宽超屏宽，label 边界截断——无崩溃、无换行错乱 | 归因=LVGL label 默认裁切语义，行为正确；G1 页面文案按 UI 规范长度，VL-12 极端内容真机收口 |
| Z6 | 竞态打断 | slideshow 页切换动画中抽帧 12 张，行差孤立尖峰(row313) | 目视+分析：横滚中间帧两页并排连贯，**零撕裂零黑闪**；尖峰=两页接缝自然差异。真机版 TL-10/11 收口（此为 L2 预演） |
| Z7 | 固定动作（严于门禁） | font_audit 只查缺字 | 追加断言：转换器强制码表升序/无重复/字节数核对；双轨基线相对偏差量化=0px |
| Z8 | 固定动作（放大再看） | 10 片裁片全量过目 | 另发现 tab 下划线 1px 干净、头像圆边无锯齿圈、AA 无子像素彩边（灰度 AA 预期） |

## 二、G0 期间的结构性认知（回写台账同步）

1. **v9.6 三处 API/构建事实**（G1/G4 直接复用）：配置走 `LV_BUILD_CONF_PATH`/lv_conf.h + `CONFIG_LV_USE_SDL=ON` 触发依赖解析；`LV_COLOR_DEPTH` 废弃→`LV_COLOR_FORMAT_DEFAULT`；benchmark 测量依赖 `LV_USE_PERF_MONITOR` 且 end_cb 接管 summary 责任。
2. **lvgl.h 公共头在 LV_USE_SDL=1 时拖入 SDL 私有头**：下游库（lvgl_demos/lvgl_examples）需补 SDL include（CMakeLists 已处理并有注释）。
3. **影子帧驱动（driver_shadow）**：DIRECT 模式单全屏缓冲 + flush 计数 + 整帧落盘，与 H743 "SDRAM fb 直扫" 取证语义同构，G1 容差 diff/G5 真机 fb_dump 对账直接复用。
4. **版本锁三重法**：vendoring 无 .git 时 = 版本宏 + 源码树 sha256 + tarball sha256（verify_locks.sh 红=禁构建）。

## 三、未尽事项（不阻塞 G0 关单）

- 盲审 R2/R3：1.5 三轮独立盲审的对象是 **G1 的 VL-3/VL-16/U-1**，G0 裁片为工具演示品，R1 自查已入上表；G1 产出正式包后按 1.5 走满三轮（R2 隔 ≥4h/新会话，R3 隔日/换人）。
- Z4 挂 G1；Z3 素材留 VL-2。
- v11.3 主线优先（D-L14①）：G1 在等待窗口续跑。

## 四、G0 关单结论

六条 DoD 全 PASS，证据齐挂，找茬三类齐全且坐实项已回归。**G0 关单，准出 G1**（token 映射 + P0/P1 复刻）。
