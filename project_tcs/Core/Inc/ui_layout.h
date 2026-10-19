/**
 * ui_layout.h — UI 设计系统唯一来源（任务书 v9.5 K1/K13，token=C 仪表 定稿）
 *
 * 全部布局常量、语义色、字号档位集中于此；tcs_ui.c 组件库只认本头文件的常量。
 * 违反本头的几何/颜色/字号 = 缺陷（nightly 断言 + 编译期静态断言双兜底）。
 *
 * 断言覆盖（第七章第 5 条机器化）：
 *   A. 8px 网格：所有 rect 坐标/尺寸为 8 的倍数（例外：estop 小图标 x=940 规范原文）
 *   B. 触摸目标：可点 rect 高 ≥64 主操作 ≥88、宽 ≥64；间距 ≥16（底栏 24）
 *   C. 字号档位：仅 96/48/32/24/20（枚举强制）
 *   D. 对比度：WCAG 实值冻结（tools/contrast_check.py 由 16 进制重算复核）
 *
 * 规范矛盾裁决记录（已上报，K7 视觉包供用户翻案）：
 *   1. §0b"卡片浅灰阶 #F2F3F5" vs 1.2"白卡"  → 取 §0b（v9.4 视觉规范修订，更新）
 *   2. §0b"蓝仅触摸反馈/选中" vs 三章"主按钮白底蓝边" → 蓝仅出现于交互元素
 *      （按钮描边/选中/拖动），数据层零蓝
 *   3. 四章"行高 96 间距 24"与五章"底栏 y=472"在 600 高屏上放不下 4 行
 *      （56+4×96+3×24=512>472）→ 行高 88/间距 16（仍满足 ≥64 触摸、≥16 留白）
 *   4. 五章"体温卡 hero 48px" vs 1.2"固定四档" → 48px 为第五档（原生渲染，48px
 *      数字字模 2KB）；章节 Pixel 级线框优先
 *   5. 1.2"中文 24/32px" → 20px 档仅 ASCII；原 16px ASCII(8x16) 退役
 */
#ifndef UI_LAYOUT_H
#define UI_LAYOUT_H

#include <stdint.h>

/* ===== 画布 ===== */
#define UI_W 1024
#define UI_H 600
#define UI_MARGIN 32
#define UI_GRID 8

/* ===== 语义色（第三章固定，RGB888 供审计；RGB565 供渲染） ===== */
#define UI_C_BG_HEX     0xEEF1F5  /* 页面底 */
#define UI_C_CARD_HEX   0xF2F3F5  /* 卡片（灰阶，§0b）*/
#define UI_C_BORDER_HEX 0xD8DEE6  /* 卡描边 */
#define UI_C_CLINE_HEX  0xD8DEE6  /* 底饰线 2px */
#define UI_C_TX_HEX     0x1F2937  /* 主文字 */
#define UI_C_TX2_HEX    0x44506B  /* 次级文字（v1.1 加深：card 6.5:1 / bg 6.9:1，总师 3.3:1 微瑕闭环）*/
#define UI_C_HERO_HEX   0x111827  /* 大数值 */
#define UI_C_BLUE_HEX   0x2563EB  /* 交互强调：按钮边/选中/拖动（数据层零蓝）*/
#define UI_C_PRESS_HEX  0xDBEAFE  /* 按住反馈 */
#define UI_C_GREEN_HEX    0x15803D  /* v1.1 加深(green-700): card 4.0:1 非文本≥3 达标 */  /* 正常/开 */
#define UI_C_GRAY_HEX   0x9CA3AF  /* 停/禁用/失联 */
#define UI_C_YELLOW_HEX 0xD97706  /* 注意（失联类）*/
#define UI_C_RED_HEX    0xDC2626  /* 危险/急停/过温 */
#define UI_C_REDDK_HEX  0xB91C1C  /* 危险按钮按住 */
#define UI_C_ALMBG_HEX  0xFEE2E2  /* 横幅红底 */
#define UI_C_ALMTX_HEX  0x991B1B  /* 横幅红字 */
#define UI_C_WARNBG_HEX 0xFEF3C7  /* 横幅黄底 */
#define UI_C_WARNTX_HEX 0x92400E  /* 横幅黄字 */
#define UI_C_WHITE_HEX  0xFFFFFF
#define UI_C_TRACK_HEX  0xD8DEE6  /* 滑条轨道 */
#define UI_C_TRACKFILL_HEX 0xBFDBFE /* 滑条已选段 */

#define UI_RGB565(hex) ((uint16_t)((((hex) >> 16) & 0xF8) << 8) | \
                        (uint16_t)((((hex) >> 8) & 0xFC) << 3) | \
                        (uint16_t)((hex) & 0xFF) >> 3)
#define C_BG     UI_RGB565(UI_C_BG_HEX)
#define C_CARD   UI_RGB565(UI_C_CARD_HEX)
#define C_BORDER UI_RGB565(UI_C_BORDER_HEX)
#define C_CLINE  UI_RGB565(UI_C_CLINE_HEX)
#define C_TX     UI_RGB565(UI_C_TX_HEX)
#define C_TX2    UI_RGB565(UI_C_TX2_HEX)
#define C_HERO   UI_RGB565(UI_C_HERO_HEX)
#define C_BLUE   UI_RGB565(UI_C_BLUE_HEX)
#define C_PRESS  UI_RGB565(UI_C_PRESS_HEX)
#define C_GREEN  UI_RGB565(UI_C_GREEN_HEX)
#define C_GRAY   UI_RGB565(UI_C_GRAY_HEX)
#define C_YELLOW UI_RGB565(UI_C_YELLOW_HEX)
#define C_RED    UI_RGB565(UI_C_RED_HEX)
#define C_REDDK  UI_RGB565(UI_C_REDDK_HEX)
#define C_ALMBG  UI_RGB565(UI_C_ALMBG_HEX)
#define C_ALMTX  UI_RGB565(UI_C_ALMTX_HEX)
#define C_WARNBG UI_RGB565(UI_C_WARNBG_HEX)
#define C_WARNTX UI_RGB565(UI_C_WARNTX_HEX)
#define C_WHITE  UI_RGB565(UI_C_WHITE_HEX)
#define C_TRACK  UI_RGB565(UI_C_TRACK_HEX)
#define C_TRACKFILL UI_RGB565(UI_C_TRACKFILL_HEX)

/* ===== 字号档位（1.2 固定五档，禁止其它尺寸） ===== */
typedef enum { UI_FS_HERO = 96, UI_FS_HERO2 = 48, UI_FS_TITLE = 32,
               UI_FS_BODY = 24, UI_FS_AUX = 20 } ui_font_size_t;

/* ===== 对比度冻结值（×100；tools/contrast_check.py 复核） ===== */
/* 大文本口径：hero(96/48) ≥3.0；24px 状态标签白字on色底 ≥3.0(WCAG large)；
 * 正文/辅助(20/24px on 中性底) ≥4.5 */
#define UI_CR_HERO_CARD   1598  /* 15.98 ≥4.5 */
#define UI_CR_TX_CARD     1322  /* 13.22 */
#define UI_CR_TX2_BG       479  /* 4.79 ≥4.5（加深后实测）*/
#define UI_CR_TX2_CARD     489  /* 4.89 */
#define UI_CR_ALMTX_ALMBG  680  /* 6.80 */
#define UI_CR_WARNTX_WARNBG 637 /* 6.37 */
#define UI_CR_WHITE_BLUE   517  /* 5.17 ≥4.5 */
#define UI_CR_WHITE_RED    483  /* 4.83 ≥4.5 */
#define UI_CR_WHITE_GREEN  330  /* 3.30 大文本口径 ≥3.0（24px 标签）*/
#define UI_CR_BLUE_CARD    466  /* 4.66 按钮蓝字/蓝边 */
#define UI_CR_HERO_BG     1566  /* 15.66 */
#define UI_CR_TX_PRESS    1203  /* 12.03 选中行文字 */

/* ===== 主页（第五章像素级） ===== */
#define UI_TOPBAR_H 56
#define R_MODE_PILL      24, 16, 160, 40      /* 模式胶囊 x,y,w,h */
#define R_BANNER         208, 8, 552, 56      /* 报警横幅（同级最高一条）*/
#define UI_LINKS_X       776                  /* 链路点起点 */
#define R_ESTOP_IC       940, 20, 24, 24      /* 急停小图标（非触摸，网格例外）*/
#define R_CARD_TUNE1     32, 88, 460, 240     /* 贴身水箱 hero 卡 */
#define R_CARD_TUNE2     532, 88, 460, 240    /* 环境水箱 hero 卡 */
#define R_CARD_BODY      32, 352, 460, 96     /* 体温卡 */
#define R_CARD_FAN       532, 352, 460, 96    /* 风扇卡 */
#define R_DOCK_TUNE      40, 472, 208, 96
#define R_DOCK_MODE      272, 472, 208, 96
#define R_DOCK_CONN      504, 472, 208, 96
#define R_DOCK_ESTOP     768, 472, 216, 112   /* 主操作 + 独占右下，永不变 */
#define R_POST_BADGE     128, 0, 640, 40      /* H6 自检角标（沿用）*/

/* hero 卡内部（相对卡片） */
#define UI_HERO_NUM_XY   24, 72               /* 96px 数值 */
#define UI_HERO_UNIT_DX  8
#define UI_HERO_FRESH_XY 396, 80              /* 新鲜度呼吸点 8px */
#define UI_HERO_SET_XY   24, 184              /* 设定行 24px */
#define UI_PILL_XY       348, 24              /* TEC 胶囊 88×40 */
/* 体温/风扇卡内部 */
#define UI_BODY_VAL_XY   120, 20              /* 48px 数值 */
#define UI_BODY_FRESH_XY 296, 40
#define UI_LOSTMARK_XY   348, 28              /* 失联角标 88×40 */
#define UI_FAN_TRACK_XY  308, 16              /* 开关轨道 128×64 球 64 */

/* ===== 调参页 ===== */
#define R_TUNE_BACK      24, 8, 120, 40
#define UI_CTX_XY        176, 24              /* 上下文行 24px */
#define UI_CLOCK_XY      896, 24              /* 时间戳 20px（降级二级）*/
#define UI_TUNE_ROW_N    4
#define UI_TUNE_ROW_X    32
#define UI_TUNE_ROW_W    960
#define UI_TUNE_ROW_H    88                   /* 裁决 3：96 放不下 */
#define UI_TUNE_ROW_Y0   56
#define UI_TUNE_ROW_GAP  16                   /* 裁决 3 */
#define R_TUNE_ZONE_MINUS 32, 472, 96, 96
#define R_TUNE_ZONE_PLUS  896, 472, 96, 96
#define R_TUNE_STEP      160, 488, 176, 64
#define R_TUNE_NEXT      360, 488, 240, 64    /* 第二页翻页（规范未定，决策后置）*/
#define R_TUNE_PREV      360, 488, 200, 64
#define R_TUNE_RESET     584, 488, 240, 64    /* 恢复默认入口（第二页）*/
/* 行内部（相对行）：左文字列 + 右滑条（80px 拇指任意位置不压文字）*/
#define UI_ROW_NAME_XY   24, 12               /* 24px */
#define UI_ROW_VAL_XY    24, 46               /* 32px */
#define UI_ROW_TRACK_XY  320, 36              /* 轨道 432×16 */
#define UI_ROW_KEYM_REL  792                    /* ± 键相对行 x */
#define UI_ROW_KEYP_REL  872
#define UI_ROW_TRACK_W   432
#define UI_ROW_THUMB_D   80                   /* 拇指球（第四章固定）*/
#define UI_ROW_KEYM_XY   792, 12              /* ± 64px 微调键 */
#define UI_ROW_KEYP_XY   872, 12
#define UI_SLIDER_CANCEL_DY 32                /* 拖出轨道 32px=取消（K4b）*/
#define UI_SLIDER_DEBOUNCE_MS 1000u            /* 松手 1s 后生效下发 */
#define UI_SLIDER_MIN_PERIOD_MS 50u            /* 跟手回显 ≤20Hz */

/* ===== 连接页 ===== */
#define R_QR_BOX         392, 88, 240, 240    /* 二维码 240px 居中 */
#define UI_QR_TXT_Y0     352                  /* 凭据行 24px 标签+20px ASCII */
#define R_CONN_BACK      40, 472, 208, 96     /* 与主页底栏同构 */

/* ===== 弹窗（居中卡+遮罩；双键间距 ≥48；确认/取消=主操作 88 高） ===== */
#define R_DLG_CARD       256, 136, 512, 320
#define UI_DLG_TITLE_XY  40, 32               /* 相对卡 32px */
#define UI_DLG_BODY_XY   40, 96               /* 24px */
#define R_DLG_OK         40, 208, 192, 88     /* 相对卡 */
#define R_DLG_CANCEL     280, 208, 192, 88    /* 间距 48 ✓ */
#define UI_BACK_HIT      24, 0, 120, 56       /* 返回命中区=整列（视觉仍 120×40）*/

/* ===== 交互时限（第四章） ===== */
#define UI_PRESS_FEED_MS 100u                 /* 触摸→变色 */
#define UI_REPEAT_FIRST_MS 400u               /* 长按首连发 */
#define UI_REPEAT_MS     150u                 /* 连发节奏 */

/* ===== 触摸会话参数（WP-M2，v10.11 T-3/M0）=====
 * 依据：合成人类参数（真击 80-200ms 按压）；阈值画在幽灵/真击分布间隙，
 * 真击补录复核前为工作值（结果文档 WP-M.md 标注数据局限，复验当天复核）。 */
#define UI_TOUCH_GHOST_MAX_MS  50u   /* 幽灵会话上限：时长<50ms+零位移+面积恒定 整会话丢弃 */
#define UI_TOUCH_JUMP_PX       200   /* 会话内单样本瞬移丢弃阈值（>人手极限 20px/ms）*/
#define UI_TOUCH_LIFT_GRACE_MS 20u   /* 抬起宽限（2 拍）：单帧 n==0 毛刺不拆会话 */

/* ===== 状态点锚定（v1.1 WP-L2/D4：正典 mock 真值，中心坐标；随字宽浮动已废） =====
 * mock/final/home.png 绿点聚类实测：(76,36)(780,31)(863,31)(cx+423,173)(333,397) */
#define UI_DOT_MODE_C   76, 36     /* 模式胶囊点 */
#define UI_DOT_M207_C   780, 31    /* 顶栏 M207 链路点 */
#define UI_DOT_ESP_C    863, 31    /* 顶栏 ESP 链路点 */
#define UI_DOT_TC_DX    423        /* TC 卡点 = 卡 x + 423 */
#define UI_DOT_TC_Y     173
#define UI_DOT_BODY_C   333, 397   /* 体温卡点 */

/* ===== 编译期断言（机器门禁，规范进代码不靠自觉） ===== */
#define UI_RECT_ASSERT_N(name, x, y, w, h, min_h) \
    _Static_assert((x) % UI_GRID == 0 && (y) % UI_GRID == 0 && \
                   (w) % UI_GRID == 0 && (h) % UI_GRID == 0, \
                   #name " off 8px grid"); \
    _Static_assert((h) >= (min_h), #name " touch height < min"); \
    _Static_assert((x) >= UI_GRID && (y) >= 0 && \
                   (x) + (w) <= UI_W && (y) + (h) <= UI_H, \
                   #name " out of screen or <8px edge")
/* 间接层：先展开 R_* 逗号宏再计数 */
#define UI_RECT_ASSERT(name, rect, min_h) UI_RECT_ASSERT_N(name, rect, min_h)

UI_RECT_ASSERT(mode_pill, R_MODE_PILL, 40);
UI_RECT_ASSERT(banner, R_BANNER, 56);
/* 容器卡（非触摸件）：x/y 落格 + 边界；w/h 按 Ch5 像素线框原值（460 非 8 倍数
 * 为规范原文，容器豁免网格——触摸件一律不豁免，见 UI_RECT_ASSERT）*/
#define UI_RECT_POS_N(name, x, y, w, h)     _Static_assert((x) >= 0 && (y) >= 0 && (x) + (w) <= UI_W && (y) + (h) <= UI_H,                    #name " out of screen")  /* 容器仅边界（Ch5 x=532 原文非格）*/
#define UI_RECT_POS(name, rect) UI_RECT_POS_N(name, rect)

/* 容器卡定位断言 */
UI_RECT_POS(card1, R_CARD_TUNE1);
UI_RECT_POS(card2, R_CARD_TUNE2);
UI_RECT_POS(card_body, R_CARD_BODY);
UI_RECT_POS(card_fan, R_CARD_FAN);
UI_RECT_ASSERT(dock_tune, R_DOCK_TUNE, 96);      /* 主操作 ≥88 → 底栏钮 96 */
UI_RECT_ASSERT(dock_mode, R_DOCK_MODE, 96);
UI_RECT_ASSERT(dock_conn, R_DOCK_CONN, 96);
UI_RECT_ASSERT(dock_estop, R_DOCK_ESTOP, 88);
UI_RECT_ASSERT(tune_back, R_TUNE_BACK, 40);      /* 次级导航 ≥40（辅助级）*/
UI_RECT_ASSERT(zone_minus, R_TUNE_ZONE_MINUS, 88);
UI_RECT_ASSERT(zone_plus, R_TUNE_ZONE_PLUS, 88);
UI_RECT_ASSERT(tune_step, R_TUNE_STEP, 64);
UI_RECT_ASSERT(tune_next, R_TUNE_NEXT, 64);
UI_RECT_ASSERT(tune_prev, R_TUNE_PREV, 64);
UI_RECT_ASSERT(tune_reset, R_TUNE_RESET, 64);
UI_RECT_ASSERT(qr_box, R_QR_BOX, 0);
UI_RECT_ASSERT(conn_back, R_CONN_BACK, 96);
UI_RECT_ASSERT_N(dlg_ok, 296, 344, 192, 88, 88);
UI_RECT_ASSERT_N(dlg_cancel, 536, 344, 192, 88, 88);

/* 底栏间距：24/24/56（急停加大间隔防误触）*/
_Static_assert(272 - (40 + 208) == 24, "dock gap1 != 24");
_Static_assert(504 - (272 + 208) == 24, "dock gap2 != 24");
_Static_assert(768 - (504 + 208) == 56, "estop gap != 56");
_Static_assert(768 + 216 <= UI_W - 40, "estop right margin");
/* 弹窗双键间距 ≥48（1.1 误触防线）*/
_Static_assert(280 - (40 + 192) == 48, "dialog btn gap != 48");
_Static_assert(140 + 208 + 88 + 24 <= UI_H, "dialog bottom margin");
/* 调参行 4 行总高 ≤ 底栏区（裁决 3 落地自检）*/
_Static_assert(UI_TUNE_ROW_Y0 + 4 * UI_TUNE_ROW_H + 3 * UI_TUNE_ROW_GAP + 16
               <= 472, "tune rows overflow dock zone");
/* 行内 ± 键与滑条列不重叠 */
_Static_assert(320 + 432 + 40 <= 792, "track+thumb collide keys");

#endif /* UI_LAYOUT_H */
