# 研学卡：Chart.js（第三方官方件 · W8 §7b）

- 制卡日期：2026-10-27。仓库：`D:\Espressif\ws\tcs`。
- 版本事实源：`docs/ref/依赖版本清单.md` Chart.js 行。
- 调用面数据：本卡制卡时实跑 grep（命令见 ③）。

## ① 官方文档与版本

| 项 | 内容 |
|---|---|
| 组件 | Chart.js（前端图表库，嵌入 ESP32 web 页） |
| 精确版本 | **v4.4.1**（实测 `esp32/components/tcs_web/web_files/chart.umd.min.js` 文件头注：`Chart.js v4.4.1 / https://www.chartjs.org / (c) 2023 Chart.js Contributors / MIT License`） |
| 来源 | 官方发行版 `chart.umd.min.js`（200.3K，gzip 67.7K 嵌入 web_files，含 `.gz` 预压缩对） |
| 锁定方式 | 文件头版本串 + git blob（web_files 树随库）；无 manifest（前端静态资产，不走 npm/组件注册表） |
| 许可 | MIT |
| 官方文档 | 官方文档站 **chartjs.org**（docs：`https://www.chartjs.org/docs/`，v4 线 API） |
| 升级风险注记 | v4 线内 minor 升级低风险；**升 v5=UMD 包可能消失（ESM-only 趋势），嵌入链路需重验**（W9 清单行注记）；4.4.1 已知问题（tooltip 溢出等）不涉我们的 line 图基础用法 |

## ② 官方示例文件路径（逐文件）

| # | 路径 | 说明 |
|---|---|---|
| 1 | https://www.chartjs.org/docs/ （官方文档站：Getting Started / Line Chart 示例页） | 官方用法示例（本机无独立示例目录，文档站即官方示例源） |
| 2 | `esp32/components/tcs_web/web_files/chart.umd.min.js` | 本机官方发行版本体（v4.4.1 umd.min，+ 同名 `.gz` 预压缩对） |
| 3 | `esp32/components/tcs_web/web_files/index.html:79` | 挂载点：`<script src="/chart.umd.min.js"></script>`（实测） |
| 4 | `esp32/components/tcs_web/web_files/app.js:258-284` | 我方唯一用例 `ensureCurve()/pushCurve()`（见 ③） |

## ③ 我们全部调用面清单（文件:行）

grep 命令：`grep -n "Chart" esp32/components/tcs_web/web_files/app.js`
命中数：**2 行**（表行数=2）。

| # | 文件:行 | 调用 | 用途 |
|---|---|---|---|
| 1 | esp32/components/tcs_web/web_files/app.js:260 | `if (curve \|\| !window.Chart) return;` | 特性检测：UMD 包未加载则静默降级（曲线区不建） |
| 2 | esp32/components/tcs_web/web_files/app.js:261 | `curve = new Chart($("curve"), { type:"line", … })` | 唯一实例化：line 图（体温 °C，x=linear 时间轴，y=35..39，animation:false，tension:0.25，30min 窗口 900 点） |

补充口径（非 "Chart" 字面命中，避免漏计）：实例生命周期方法在同文件 `curve.update("none")`（app.js:284，pushCurve 内无动画刷新）与 `curvePts` 数组维护（:279-283）——它们是**实例方法调用**（grep "Chart" 不命中），如实另记 1 行：`app.js:284 curve.update("none")`。故 Chart.js 相关调用合计 3 处（2 处构造/检测 + 1 处实例刷新）。

## ④ 对照结论（✓/偏差+理由/待整改）

| 锚点 | 结论 |
|---|---|
| 版本锁定 | ✓ v4.4.1 文件头串实测相符；`index.html:79` UMD 全局挂载，`window.Chart` 检测在 :260 与之闭环 |
| v4 线内升级低风险 | ✓ 我们仅用 line 图基础面（type/data/options.scales/plugins 四层，无自定义 plugin/controller）；4.4.1 已知问题不涉（W9 CHANGELOG 扫记） |
| v5 ESM-only 风险 | 偏差预告（非当前缺陷）：v5 若取消 UMD 包，`<script src>` 全局挂载与 `window.Chart` 检测链路整体失效——**待整改入 W9/二期**：升级窗口钉在 v4 线内 minor；若必须升 v5，嵌入链路（serve 路径+gzip 对+特性检测）需整体重验 |
| 与后端契约 | ✓ st/pv 帧数值 `37` vs `37.0` 语义等价（Chart.js Number 解析无差，W2 §5.1 诚实化注记——前端零改动依据）；`animation:false + update("none")` = 500ms 周期推送不闪不卡 |
| 降级路径 | ✓ `!window.Chart` 时曲线区静默缺席，页面其余（仪表盘/参数/Toast）不受影响（app.js:260 守卫） |
| 待整改归属 | 无 W8 新增项；唯一前瞻项=v5 升级链路重验（随 W9 清单注记滚动） |
