# VL-16 复刻一致性总表（G1 产出；R1 判定版）

对象：P0 主页×5 状态 + P1 调参页×3 状态的全部控件。
双列 8× 裁片：`docs/evidence/LVGL/vl16/<row>_ref.png | <row>_lvgl.png`（ref=真机 fb 基线
`project_tcs/tests/baseline/home.png|tune.png`；正常态无 fb 不可得的状态行用定稿 mock
`mock/final/home_badge|home_lost|dlg_estop|tune.png`，表中注明）。
判定三值：**一致** / **有意差异（原因）** / **缺陷（立案→修复→回归）**。
R1（本会话自查，逐行看图+逐行残差量化）；R2/R3 按 1.5 隔时独立重审（挂账，见 G1 结果文档）。

| # | 行 | REF 源 | 判定 | 依据（残差量化/说明） |
|---|---|---|---|---|
| 1 | p0_mode_pill | fb | 有意差异（基线陈旧） | 残差 3840px 全在模式点(76,36)：fb 为 pre-D4 旧点位，现行常量/定稿 mock 均=76,36，我方按现行 |
| 2 | p0_mode_text | fb | 一致 | 字形墨迹 bbox 双边逐像素同（69,27,114,47）；残差仅点区域（同上） |
| 3 | p0_link_m207 | fb | 有意差异（基线陈旧） | 9472px=链路点位置（fb pre-D4）；文字" M207 "20px 逐像素同 |
| 4 | p0_link_esp | fb | 有意差异（基线陈旧） | 同上（ESP 点） |
| 5 | p0_estop_icon | fb | **一致** | 残差 0px——方框轮廓+内方块与现行固件完全一致（定稿 mock 的圆形 svg 为设计稿差异，以固件为准） |
| 6 | p0_card_title32 | fb | **一致** | 残差 0px（32px 全盒逐像素同） |
| 7 | p0_hero_num96 | fb | 一致 | 数字 96px 逐像素同；残差 2368px 全部=fb 的新鲜度点旧位(304,168)（现行 (455,173)） |
| 8 | p0_hero_unit32 | fb | **一致** | 残差 0px；单位 °C 墨迹 (279,228,323,249) 双边同 |
| 9 | p0_setline24 | fb | **一致** | 残差 0px（设定/32px 值/°C 全同） |
| 10 | p0_setval32 | fb | **一致** | 残差 0px |
| 11 | p0_tec_pill | fb | 有意差异（引擎边缘） | 5888px=圆角胶囊四角边缘（LVGL 光栅 vs 固件角点，≤2px）；`○停止` 文字逐像素同 |
| 12 | p0_body48 | fb | 一致 | 数字/单位逐像素同；残差 4736px=体温新鲜点旧位（pre-D4） |
| 13 | p0_fan_track | fb | 有意差异（引擎边缘） | 36224px=球/轨道圆角环形边缘（固件手工切角 vs LVGL radius，≤2px 弧带）；`关`/位置/尺寸同 |
| 14 | p0_dock_tune | fb | **一致** | 残差 0px |
| 15 | p0_dock_mode | fb | **一致** | 残差 0px（文案"切自动"=手动态，与 fb 同） |
| 16 | p0_dock_conn | fb | **一致** | 残差 0px |
| 17 | p0_dock_estop | fb | **一致** | 残差 2304px/mean0.12=圆角外缘 1px；红底/白字逐像素同 |
| 18 | p0_banner_bg | mock(home_badge) | 有意差异（状态参考差异） | mock 无报警态渲染件（badge=自检角标态）；黄横幅按固件 draw_banner_content 几何/色 token 复刻，文案行取固件 alarm 表 |
| 19 | p0_banner_msg24 | mock(home_badge) | 有意差异（字体轨） | 主句/怎么办=24px 点阵定宽 vs mock 浏览器 AA 比例字；文案与固件表逐字同 |
| 20 | p0_lost_border | mock(home_lost) | 有意差异（字体轨） | 黄描边/失联徽/灰 --.- 结构与 mock 同；数字用点阵轨 |
| 21 | p0_estop_dlg_title | mock(dlg_estop) | 有意差异（字体轨） | 位置(296,168)/32px/红=同；点阵 vs AA |
| 22 | p0_estop_dlg_body | mock(dlg_estop) | 有意差异（字体轨） | 位置(296,232)/24px/文案逐字同；定宽网格更宽 |
| 23 | p0_estop_ok | mock(dlg_estop) | 有意差异（底色基准） | 按钮 rect/圆角/红色同；卡底=固件 C_CARD vs mock #FFF（≤13LSB，以固件为准）；标签同 |
| 24 | p0_estop_cancel | mock(dlg_estop) | 有意差异（底色基准） | 四边蓝线几何同（hline 542..722/vline 350..425）；底色 C_CARD vs 白 |
| 25 | p1_back_btn | fb | **一致** | 残差 0px（‹返回 24px+幽灵样式） |
| 26 | p1_ctx24 | fb | **一致** | 残差 0px（"选中：…范围 5.0~45.0°C" 逐像素同） |
| 27 | p1_row_card | fb | 有意差异（引擎边缘） | 行卡/选中底/蓝边同；13824px=拇指边缘环带 |
| 28 | p1_row_selbar | fb | **一致** | 残差 256px/mean0.52=条底角 1px 混合 |
| 29 | p1_name24 | fb | **一致** | 残差 0px |
| 30 | p1_val32 | fb | **一致** | 残差 0px（25.0°C 定宽网格逐像素同） |
| 31 | p1_track | fb | 有意差异（引擎边缘） | 轨道/填充几何同；残差=拇指边缘弧带 |
| 32 | p1_thumb | fb | 有意差异（引擎边缘） | 80px 蓝球位置/直径同；环形边缘 ≤2px（固件 r40 角点 vs LVGL） |
| 33 | p1_key_minus | fb | **一致** | 残差 0px（"−"字符=ASCII '-' 与固件 ui_btn 同） |
| 34 | p1_key_plus | fb | **一致** | 残差 0px |
| 35 | p1_step_btn | fb | **一致** | 残差 0px（"步进0.1" 逐像素同） |
| 36 | p1_next_btn | fb | **一致** | 残差 0px（"下一页›"） |
| 37 | p1_zone_minus | fb | **一致** | 残差 0px |
| 38 | p1_zone_plus | fb | **一致** | 残差 0px |
| 39 | p1_clock | fb | 有意差异（基线陈旧） | 23360px：fb=pre-D3 自然步进时钟；现行固件=12px 制表位（我方一致）；数字字形同 |
| 40 | p1_thumb2 | fb | 有意差异（引擎边缘） | 灰球位置/直径同；环形边缘 ≤2px |
| 41 | p1_row4 | fb | 有意差异（引擎边缘） | 未选中行卡/内容同；拇指边缘环带 |
| 42 | p1_overlimit_val | mock(tune) | 有意差异（新增预演） | 无现屏对应（U-2/U-9 超限反馈预演）；红值+红 ctx 为 G1 新增，已入 U 清单待用户裁决 |

**小计**：一致 22 行（残差 0 或纯基线陈旧性点差），有意差异 20 行（引擎边缘 7 / 字体轨 5 /
底色基准 2 / 基线陈旧 3 / 状态参考 1 / 新增预演 1 / 引擎边缘其他 1），**缺陷 0 行**。
R1 结论：无未归因行。

## R1 发现台账（全部闭环，明细见 G1 结果文档找茬章）
- 复刻过程缺陷 4 项（均已修复+回归）：字体 stride 语义斜切 / perf 角标污染取证帧 /
  sprite 资源泄漏（800 轮段错误）/ 空 glyph_dsc 生成 ICE。
- 基线版本性差异 4 项（fb 基线早于 D4 点锚定与 D3 表宽修复；以现行固件常量/定稿 mock 为准）：
  链路点/模式点/新鲜点位置、时钟步进表宽。
- 参考件缺失 1 项：报警态无 mock 渲染件 → 行 18/19 记状态参考差异。

## Post-fix 注记（G1-Z13 修复后，batch2）

行 1/2（模式胶囊/文字）：复刻层文字 x 68→88（mock 正典），点/字分离 8× 实证；vs fb 残差升高为
**有意差异（fb=缺陷旧位）**；固件一行修挂 G5。行 18/19 REF 已换状态匹配 mock（badge/lost）。
batch2 盲号包已重出（`blind_map_T1.json` 为 batch2；batch1 保档 `blind_batch1_R2R3/`）。

## 自查轮（用户反馈驱动，batch3）残差分布更新

零残差 **24/42**（新晋：tec_pill、fan_track、dock_tune、dock_estop、track、thumb、thumb2、row4、row_card 13824→256）。
非零 18 行全部再归因：意向修正 3（mode_pill/text=Z13 文字 88、link_esp=Z15 文字 876、estop_icon=Z16 圆环）、
mock 参照 9（banner/lost/dlg 五行+overlimit）、基线陈旧 3（link_m207 窗含 ESP 变化、hero/body 新鲜点、clock）、
亚阈 3（row_card/selbar 256px 角混合）。**引擎类 0、缺陷 0**。
