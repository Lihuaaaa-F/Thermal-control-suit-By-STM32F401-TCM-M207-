# WP-K 结果文档 — UI 重构专册（任务书 v9.5，2026-10-19）

## 一、做了什么

按 v9.5 穷举补齐 K0~K13 全部工作包，屏端+网页端同设计系统重构（token=C 仪表定稿）：

- **K0/K0b**：frontend-design 三版 token 候选（A 素灰/B 白瓷/C 仪表，`mock/candidates/`）
  → 全页面 mock（`mock/final/` 7 页+状态机）→ 三眼评审（自视负锚锚定+audit skill 四题+对比表）
  → 定稿 C。规范矛盾 9 条全部留档裁决（规范 §8）。
- **K1**：`Core/Inc/ui_layout.h` 设计唯一来源——全部 rect/语义色/字号档/交互时限 + 编译期
  静态断言（8px 网格/触摸高度/边界/底栏间距/弹窗键距/行区不越界）。
- **K2**：`tools/gen_font_ui.py` 96/48/20 原生字模（基线 yoff 定位，禁放大）+ gen_font_cn 扩
  32px 中文集与 cmap 缺字硬失败 + font_audit 注释剥离；预算表 63KB const Flash。
- **K3**：渲染重构——小更新 patch 直绘显示缓冲+`gfx_sync_rect` 矩形同步，**flip 仅切页/弹窗**
  （根治 500ms×乒乓条带闪屏）；预算实测 hero 2.51ms≤3 / 整页 24.9ms≤25 / 滑条 20Hz；
  perf 命令扩 hero/page/flips/patches/touch_hz 回填口。
- **K4/K4b**：tcs_ui.c 全量重写——组件库+主页五状态+调参两页（滑条行）+连接页+四弹窗；
  滑条细则全落地（映射 step 对齐/20Hz 回显/±32px 取消回弹+tap 抑制/1s 防抖/并发后到优先）。
- **K6**：run_all 修复（font_audit 注释剥离）后全绿；wp_check PASS；rehearsal 坐标迁移后
  **附录 A 连续两遍 20/20 ALL GREEN**；nightly 短档 ALL GREEN；基准帧三页 diff=0。
- **K8**：网页端同 token 重构（375px，style.css/index.html 重写+app.js 仅色板与 K10 文案）；
  `web_audit.py` CDP 断言 12/12 PASS；gz 重生成（真机重烧留 I 侧动作）。
- **K9**：`tools/mock_diff.py` mock↔fb_dump 容差 diff（色距 24/文本膨胀/动态区掩码），
  三页硬差异 0.000/0.003/0.000%。
- **K10**：报警文案表（屏/网同源五条"主句+怎么办"）+术语表+`colorblind_check.py`
  （灰度/Deuteranopia/Protanopia 三变换 PASS）。
- **K11**：`tools/vision_audit.py`（对比度/触摸/网格/报警突显，JSON+锚定表）+
  `novice_tasks.py` 新手矩阵×5 全过（净时 ≤9.9s，错点 ≤1）。
- **K12**：报点率计数器入 perf——**真 IC 实报 100Hz**；滤波参数表（30ms 去抖/200px 幽灵帧
  丢弃/20Hz 回显限幅/100ms 按住反馈）；真人滑感与四点校准留 I5（不阻塞）。
- **K13**：`docs/现行UI规范_v1.0.md` 正典（含矛盾裁决 9 条）+README 指向。
- **K7**：视觉包 v3 全端包 `docs/results/视觉包v3_20261019.md`（三候选+真屏五状态+网页+新旧
  对比+自评表+预算表），**异步送用户，通过前 UI 项不关单**。

## 二、怎么验证的（证据索引）

真屏帧：K9_home/tune/connect、K4_home_lost、K9_home_safe、K9_home_badge、K4_dlg_estop2/3、
K2_fontpage、K4b_drag2/applied/cancel（滑条三态）、K3_home_final（去 sync 后无残影）；
旧貌对照：K0_before_home/tune.png。机器门禁：ui_layout.h 静态断言（编译期）、mock_diff 套件、
vision_audit.json、colorblind_check.json、K11_novice.json、K8_web_audit.json+K8_web_375.png、
H3_scorecard_pass1/2.json（20/20×2）、nightly_k6.log（ALL GREEN）、基准帧 tests/baseline/。

## 三、遇到的问题与根因（全部闭环）

1. **嵌套 patch 切换缓冲**（主页残留标定页）→ s_render_full 状态机降级。
2. **等高字格整格擦底**压设定行 → 字模墨迹裁剪+yoff 基线定位。
3. **° 单字节判续字节**→ utf-8 双字节回填；**▸◂ .notdef 空框**→ cmap 硬失败+换 ‹›。
4. **第五章线框自身多处非 8px 网格**（460/532/180/356/580/弹窗 140）→ 触摸件落格、容器豁免，裁决 #7/#8。
5. **± 键被行选中拦截**（附录 A-05）→ 选中行键命中放行。
6. **切页 sync_buffers 40ms 冗余**→ 不变量证明后移除（预算 52.6→24.9ms）。
7. **fb_dump 慢读拍到重绘中间帧**（伪缺陷）→ 静止帧 diff=0 证伪；probe 读 SRAM 受 D-Cache
   陈旧性限制 → 计数器经 console perf 读，屏幕真值走 fb_dump。
8. **font_audit 把注释引号短语当字符串** → c_strip.py 注释剥离（两工具共用）。
9. **web_audit 三连坑**：WS 源（/__/audit 同源页）、Content-Length 字符数 vs 字节数、
   headless virtual-time 与 WS 竞速（改 CDP 真实浏览器）；g3 画布遗漏恢复、summary 30px→44px。

## 四、没做完的 / 移交

- **用户动作**：①视觉包 v3 人审（三候选翻案窗+五状态+网页）；②I5 真人项：触摸四点校准、
  真人滑感、新手任务真人版、2m 视距拍照、长曝光闪屏复核；③ESP32 真机重烧（gz 已更新）。
- 非目标内遗留：K9 mock_diff 接入 nightly 完整档（短档已具 build 门禁；套件命令就绪，
  真屏帧捕获依赖探针独占，建议夜间无探针冲突时并入）；曲线 x 轴时区为本地时分。

## 五、红线自查（分离原则）

改动清单：tcs_ui.c（重写）、ltdc.c/.h（+gfx_sync_rect/use_*声明）、ui_layout.h（新）、
字库 inc×4（生成物）、console.c/.h（perf 字段）、gen_font*.py/font_audit.py/c_strip.py/
mock_diff.py/vision_audit.py/novice_tasks.py/colorblind_check.py（工具新增/修复）、
rehearsal.py（坐标迁移）、nightly_frames.py（掩码/导航）、web 三件套+gz、mock/（设计源）、
README/规范文档。**param/control/m207/协议/post/eeprom 零改动**（git diff 可核）。
