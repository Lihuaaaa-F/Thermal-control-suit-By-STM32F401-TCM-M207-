# 研学卡：nayuki QR-Code-generator（第三方官方件 · W8 §7b）

- 制卡日期：2026-10-27。仓库：`D:\Espressif\ws\tcs`。
- 版本事实源：`docs/ref/依赖版本清单.md` nayuki 行 + `project_tcs/App/tcs/qr/裁剪记录.md`。
- 调用面数据：本卡制卡时实跑 grep（命令见 ③）。

## ① 官方文档与版本

| 项 | 内容 |
|---|---|
| 组件 | nayuki QR-Code-generator **C 版**（qrcodegen） |
| 精确版本 | **vendor 快照 2026-10-19**（上游无版本号发布，按 commit 演进无 tag）；实测本地 `.c`=**999 行**（=上游 1027−28 裁剪行精确相符）、`.h`=**401 行**（=上游 385+16 行 **H13 期未记录手改**：尾部 getSize/getModule 声明块重复 + #endif 粘连注释——红队 W9 坐实，依赖清单 nayuki 行即补录） |
| 来源 | github.com/nayuki/QR-Code-generator 官方仓（C 版子目录） |
| 锁定方式 | git tree hash 锁 `43a4576dc4fd3874c1f0b5ee5813312d8e5693c9`（=`git rev-parse HEAD:project_tcs/App/tcs/qr`，任一文件改动即失效）+ 裁剪记录.md 全文留档 |
| 许可 | MIT（LICENSE 随库入 git，位于 `project_tcs/App/tcs/qr/LICENSE`） |
| 官方文档 | 上游仓 README（含算法说明与示例节）；本地裁剪档=`project_tcs/App/tcs/qr/裁剪记录.md` |
| 升级风险注记 | 再同步=手工 diff+裁剪重放（上游无 CHANGELOG，README 演进）；快照锁定后不再自动同步；**GAP 注 2：未录上游 commit hash**，二期清单补（对照快照日 2026-10-19 附近 master） |

## ② 官方示例文件路径（逐文件）

本机**无上游仓 clone**（vendor 快照仅取 C 版两源文件），官方示例指向上游仓：

| # | 路径 | 说明 |
|---|---|---|
| 1 | https://github.com/nayuki/QR-Code-generator （README「Examples」节） | 官方示例代码所在（本机无副本，按任务书口径记 URL） |
| 2 | 本地库体 `project_tcs/App/tcs/qr/qrcodegen.c`（999 行）+ `qrcodegen.h`（401 行） | vendor 快照本体（.c 裁剪 28 行、.h 含 H13 手改块 16 行） |
| 3 | 本地许可 `project_tcs/App/tcs/qr/LICENSE` | MIT 随库入 git |
| 4 | 本地裁剪档 `project_tcs/App/tcs/qr/裁剪记录.md` | 4 处 ECI 裁剪全文留档（见 ④） |
| 5 | 我方验证 harness `project_tcs/tests/test_qr.c` | 最小渲染验证：编码→PBM→`tools/qr_verify.py`(zxing-cpp) 回读比对 |

## ③ 我们全部调用面清单（文件:行）

grep 命令：`grep -rn "qrcodegen_" project_tcs/App project_tcs/Core --include=*.c`
总命中 **90 行**；拆分口径（qr/ 库目录自身与 tests 分开计，任务书指定）：

| 域 | 行数 | 位置 |
|---|---|---|
| **业务调用面（qr/ 目录之外）** | **6** | 全部在 `project_tcs/App/tcs/tcs_ui.c`（逐行见下表，表行数=6） |
| qr/ 库内部（vendor 本体定义/内部引用，不计入调用面） | 84 | `project_tcs/App/tcs/qr/qrcodegen.c` |
| tests（分开计，范围外补跑：`grep -rn "qrcodegen_" project_tcs/tests --include=*.c`） | 9 | `project_tcs/tests/test_qr.c` |

业务调用面 6 行（表行数=6）：

| # | 文件:行 | API | 用途 |
|---|---|---|---|
| 1 | project_tcs/App/tcs/tcs_ui.c:900 | qrcodegen_BUFFER_LEN_FOR_VERSION(6) | qrc 输出缓冲定容 |
| 2 | project_tcs/App/tcs/tcs_ui.c:901 | qrcodegen_BUFFER_LEN_FOR_VERSION(6) | tmp 临时缓冲定容 |
| 3 | project_tcs/App/tcs/tcs_ui.c:902 | qrcodegen_encodeText("http://192.168.4.1", …) | 唯一编码入口（BYTE 模式，调用跨 :902-903 两物理行） |
| 4 | project_tcs/App/tcs/tcs_ui.c:903 | qrcodegen_Ecc_MEDIUM（encodeText 续行实参） | ECC M；版本区间传参 2..6，掩码 -1 自动 |
| 5 | project_tcs/App/tcs/tcs_ui.c:905 | qrcodegen_getSize | 模块边长（渲染循环界） |
| 6 | project_tcs/App/tcs/tcs_ui.c:912 | qrcodegen_getModule | 逐模块取色（x,y） |

调用面 API 计 6 个：BUFFER_LEN_FOR_VERSION、encodeText、Ecc_MEDIUM、getSize、getModule（BUFFER_LEN_FOR_VERSION 出现 2 行）。

## ④ 对照结论（✓/偏差+理由/待整改）

| 锚点 | 结论 |
|---|---|
| ECI 裁剪 4 处 | ✓ 与 `裁剪记录.md` 逐条相符：①`getTotalBits()` 的 ECI 分支；②`qrcodegen_makeEci()` 整函数（−1114B）；③`numCharCountBits()` 的 ECI case；④头文件 `qrcodegen_Mode_ECI=0x7` 枚举与 makeEci 声明（枚举显式赋值 0x1/0x2/0x4/0x8，移除不移义）。保留 NUMERIC/ALPHANUMERIC/BYTE/KANJI 全部算法表与渲染路径（算法核心未动，降低裁剪风险） |
| .h 手改块补录（W9） | ✓ 已入档：依赖清单 nayuki 行改诚实口径（".h 本地 401 行=上游 385+16 行 H13 期未记录手改——尾部 getSize/getModule 声明块重复+#endif 粘连注释，红队 W9 坐实，本行即补录"）；`docs/results/W9_完成.md` §整改表 G2 条（nayuki .h 记录失真+未记录手改）已修 |
| 手改块物理实证 | ✓ 本卡制卡时复核 `project_tcs/App/tcs/qr/qrcodegen.h:355-360`：残缺 `/*` 注释+重复声明块仍在（W4.2 双构建 3 警告之一 `qrcodegen.h:359` 同源） |
| 用途面 | ✓ 与裁剪记录一致：仅 `encodeText` BYTE 模式编码 `"http://192.168.4.1"`（ECC M）。微偏差（措辞级）：裁剪记录写"版本自动 2~3"为期望区间注记，代码实参 `minVer=2, maxVer=6`（tcs_ui.c:903）——URL 长度下实际选定版本落在 3 以内，非行为偏差 |
| GAP（清单注 2） | 偏差+理由：上游 commit hash 未录（上游无 tag 无版本号，仅快照日期 2026-10-19）——**待整改入二期清单**（依赖版本清单"注 2"已挂，二期补录对照快照期 master commit） |
| qrcodegen.h:359 注释警告 | 待整改：二期清单（W4.2 §挂账 3，H13 手改块残余，随二期 .h 手改块规整一并处置） |
| 验证链 | ✓ 最小渲染验证双轨：`tests/test_qr.c` 编码→PBM→zxing-cpp 回读比对；屏端 fb_dump PNG 同一解码器扫码实测（docs/evidence/H13_*.png） |
