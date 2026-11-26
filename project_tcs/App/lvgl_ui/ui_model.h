/**
 * ui_model.h — 界面的纯逻辑：连接视图 / 状态胶囊 / 横幅选型 / 确认反馈 / 主页差分
 *
 * 零 LVGL、零 HAL：输入是主循环喂进来的原始事实（ui_in_t），输出是可直接画的结构。
 * 这样"主控断了屏幕该显示什么""点确认后该提示什么"能在主机上单测（tests/test_ui_model.c），
 * 不再靠上板用眼睛猜。绘图在 tui*.c / ui_common.c。
 */
#ifndef UI_MODEL_H
#define UI_MODEL_H

#include <stdint.h>
#include <stddef.h>
#include "tcs_proto.h"       /* ALM_* */

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 输入：每拍喂进来的原始事实 ====== */
#define UI_LINK_STALE_MS   500u          /* 这么久没有主控快照 = 主控断开（与 g_tcs.esp_link_ok 同口径）*/
#define UI_LINK_NEVER      0xFFFFFFFFu   /* 上电后从未收到快照 */

typedef struct {
    uint32_t now_ms;
    uint32_t snap_age_ms;                /* 距最近一帧快照的毫秒数；UI_LINK_NEVER = 从未收到 */
    uint32_t snap_seq;                   /* 最近一帧快照的序号（确认反馈靠它判断"来过新帧"）*/
    uint8_t  m207_online;                /* 以下三项取自最近一帧快照：只在主控在线时有意义 */
    uint8_t  body_ok;
    uint8_t  sensor_open;
    uint16_t alarm;
    uint8_t  mode;                       /* 0 手动 / 1 自动 */
    uint8_t  fan, tec1, tec2;            /* 执行器开关（同样只在主控在线时有意义）*/
    float    tc1_act, tc2_act, body;    /* 两路水温与体温读数 */
    uint8_t  estop_local;               /* 屏端已记下急停（本地锁存，或报警位里有——含主控回传的）*/
    uint8_t  estop_peer;                /* 最近一帧主控快照里有急停位 = 主控已执行 */
    /* m1"模式"页（2026-11-26）：两路 TEC 的输出方向与电学量，取自主控快照（0x20 / 0x21）。
     * 方向是协议值 TCS_TEC_DIR_*（0 暂停 1 制冷 2 制热 3 开着但不是模式页设的）；只在主控在线时有意义。*/
    uint8_t  dir1, dir2;
    uint8_t  tec_valid;                 /* bit0 = TEC1 电压电流有效；bit1 = TEC2 */
    float    tec_v1, tec_i1;            /* V（制热为负）/ A */
    float    tec_v2, tec_i2;
} ui_in_t;

/* ====== 连接视图 ====== */
typedef enum { UI_LINK_UP = 0, UI_LINK_DOWN, UI_LINK_NEVER_SEEN } ui_link_t;
typedef enum { UI_SRC_OK = 0, UI_SRC_LOST, UI_SRC_UNKNOWN } ui_src_t;

typedef struct {
    ui_link_t link;      /* 主控（ESP32）链路 */
    ui_src_t  module;    /* 温控模块 + 水温探头：任一不正常即 LOST */
    ui_src_t  body;      /* 体温传感器 */
    uint32_t  down_ms;   /* 主控不在时已断开多久；从未收到 = 开机至今 */
    uint8_t   stale;     /* 1 = 屏上数值是陈旧数据（应转灰，不能当实时值看）*/
} ui_link_view_t;

ui_link_view_t ui_link_view(const ui_in_t *in);

/* 状态胶囊：文案 + 灯色 */
typedef enum { UI_LED_GREEN = 0, UI_LED_GRAY, UI_LED_YELLOW, UI_LED_RED } ui_led_t;

void ui_status_ctrl  (const ui_link_view_t *lv, char *text, size_t cap, ui_led_t *led);
void ui_status_module(const ui_link_view_t *lv, char *text, size_t cap, ui_led_t *led);

/* ====== 横幅 ====== */
typedef enum {
    UI_BN_NONE = 0,
    UI_BN_LINK_LOST,     /* 主控连接中断：本地判定，压过一切（其余状态都不可信）*/
    UI_BN_ESTOP_UNSENT,  /* 急停已记下但没送到主控（链路断着）：比"连接中断"本身更要紧 */
    UI_BN_ESTOP,         /* 急停：主控已执行，必须人工解除 */
    UI_BN_ESTOP_PENDING, /* 急停指令已发出，主控快照里还没有这一位 */
    UI_BN_OVERTEMP,      /* 超温：必须人工解除 */
    UI_BN_AUTO,          /* 体温 / 模块 / 探头：源恢复后自动恢复 */
    UI_BN_INFO,          /* 提示级：未布防时传感器不在、参数校验失败 */
} ui_bn_kind_t;

typedef enum { UI_BN_TAP_NONE = 0, UI_BN_TAP_ACK, UI_BN_TAP_HINT } ui_bn_tap_t;

typedef struct {
    ui_bn_kind_t kind;
    uint8_t      red;            /* 1 红底（需要人处理）；0 黄底（会自己好 / 提示）*/
    ui_bn_tap_t  tap;            /* 点击行为：发确认 / 只给提示 / 无 */
    char         title[40];      /* 第一行（UTF-8：一个汉字 3 字节，别按字数开小了）*/
    char         detail[80];     /* 第二行：告诉使用者现在怎样、接下来会怎样 */
    uint8_t      extra;          /* 除本条外还有几条报警 */
} ui_banner_t;

/* 选一条最该给使用者看的横幅。没有任何要提示的 → kind = UI_BN_NONE。*/
void ui_banner_select(const ui_in_t *in, const ui_link_view_t *lv, ui_banner_t *out);
/* 点击横幅后浮出的提示语 */
const char *ui_banner_tap_hint(const ui_banner_t *b);

/* ====== 点击"解除 / 恢复"后的反馈 ====== */
#define UI_ACK_SETTLE_MS  450u          /* 点击后至少等这么久（≥2 个快照周期）再下结论 */
#define UI_ACK_TIMEOUT_MS 800u          /* 这么久还没来任何新快照 = 主控没回话 */

typedef struct {
    uint8_t  waiting;
    uint32_t sent_ms;
    uint32_t seq_at_send;        /* 发送时的快照序号：变了才说明"主控处理过了"*/
} ui_ack_t;

typedef enum {
    UI_ACK_WAIT = 0,             /* 还在等主控回话 */
    UI_ACK_ALL_CLEAR,            /* 报警全部解除 */
    UI_ACK_PARTIAL,              /* 有残留（still 里是没解除的位）*/
    UI_ACK_NO_REPLY,             /* 主控没回话（链路断）*/
} ui_ack_result_t;

void            ui_ack_start(ui_ack_t *a, const ui_in_t *in);
ui_ack_result_t ui_ack_poll(ui_ack_t *a, const ui_in_t *in, uint16_t *still);
/* 放弃等待（切页、关弹窗时）：之后不再出任何提示 */
void            ui_ack_cancel(ui_ack_t *a);
/* 没解除的报警位 → 人话原因（"体温传感器仍失联"）；没有可解释的位返回 NULL */
const char *ui_ack_reason(uint16_t still, const ui_link_view_t *lv);

/* ====== 主页差分：哪些块需要重画 ====== */
typedef struct {
    ui_link_t link;
    ui_src_t  module;
    ui_src_t  body;
    uint8_t   mode;
    uint8_t   fan;
    uint8_t   tec1;
    uint8_t   tec2;
    uint8_t   estop;             /* 急停已记下（主控断了也算）：急停键变灰 */
} ui_home_t;

#define UI_H_LINK   (1u << 0)   /* 顶栏状态条（主控 / 模块两枚胶囊一起重画）*/
#define UI_H_MODULE (1u << 1)   /* 模块在线态：水箱数值 "--.-"、TEC 胶囊 */
#define UI_H_BODY   (1u << 2)   /* 体温传感器态：体温卡样式 */
#define UI_H_MODE   (1u << 3)
#define UI_H_FAN    (1u << 4)
#define UI_H_TEC1   (1u << 5)
#define UI_H_TEC2   (1u << 6)
#define UI_H_ESTOP  (1u << 7)

void     ui_home_state(const ui_in_t *in, const ui_link_view_t *lv, ui_home_t *out);
uint32_t ui_home_diff(const ui_home_t *was, const ui_home_t *now);   /* 位或：UI_H_* */

/* ====== m1"模式"页（用户 2026-11-26 定的新功能）======
 * 首页的模式键改成"显示当前模式、点击进模式页"；模式页上手动模式两路 TEC 各点"制冷 / 暂停 / 制热"，
 * 手动 / 自动两页都显示功率。这里只做判定与文案，画在 tui_p4_mode.c。 */

/* 首页模式键：文字恒为当前模式（"手动" / "自动"）；主控断开时不可点（文字由页面保持上一次的）*/
void ui_mode_key(uint8_t online, uint8_t mode, char *text, size_t cap, uint8_t *enabled);

typedef enum {
    UI_TD_UNKNOWN = 0,   /* 主控断开 / 没数据 */
    UI_TD_PAUSE,
    UI_TD_COOL,
    UI_TD_HEAT,
    UI_TD_EXT,           /* 通道开着，但不是"模式"页设的方向（网页 / 台架的旧命令）*/
} ui_tec_dir_t;

typedef struct {
    ui_link_t    link;
    uint8_t      manual;            /* 1 = 手动模式（主控在线时才有意义）*/
    uint8_t      safe;              /* 1 = 主控在安全态 / 急停 / 超温 / 模块或探头报警未解除 */
    uint8_t      estop;             /* 急停已记下：急停键变灰（主控断了也按屏端自己的记号画）*/
    ui_tec_dir_t dir[2];            /* 下标 0 = TEC1，1 = TEC2；主控断开 = UI_TD_UNKNOWN */
    uint8_t      elec_ok[2];        /* 1 = 该路电压电流有效（可以显示功率）*/
    uint8_t      keys_on;           /* 1 = 三个方向键可点（手动 + 主控在线 + 不在安全态）*/
    uint8_t      fan, fan_locked;   /* fan_locked：有 TEC 在制冷 / 制热，风扇被联动、不可关 */
} ui_mode_state_t;

#define UI_M_LINK   (1u << 0)   /* 链路在/不在：整页重画 */
#define UI_M_MODE   (1u << 1)   /* 手动 / 自动：版面不同 → 整页重建 */
#define UI_M_SAFE   (1u << 2)   /* 安全态变了：方向键能不能点跟着变 */
#define UI_M_ESTOP  (1u << 3)
#define UI_M_DIR1   (1u << 4)
#define UI_M_DIR2   (1u << 5)
#define UI_M_ELEC1  (1u << 6)
#define UI_M_ELEC2  (1u << 7)
#define UI_M_FAN    (1u << 8)

/* 协议里的方向值 → 枚举（大于 3 的都算"开着但不是模式页设的"）*/
ui_tec_dir_t ui_mode_dir_of(uint8_t d);
void         ui_mode_state(const ui_in_t *in, const ui_link_view_t *lv, ui_mode_state_t *out);
uint32_t     ui_mode_diff(const ui_mode_state_t *was, const ui_mode_state_t *now);   /* 位或：UI_M_* */

/* 方向键的样式 */
#define UI_MD_BTN_NORMAL  0     /* 普通样式 */
#define UI_MD_BTN_ON      1     /* 当前生效的方向：高亮 */
#define UI_MD_BTN_OFF     2     /* 主控断开：置灰不可点 */
int ui_mode_btn_style(const ui_mode_state_t *s, ui_tec_dir_t btn);

/* 一行的状态文字："暂停" / "制冷中" / "制热中" / "输出中" / "未知" */
const char *ui_mode_dir_text(ui_tec_dir_t d);

/* 功率主数字（W，一位小数）与电压电流小字（各两位）。数据缺失或主控断开 → "未知"，返回 0 */
int ui_mode_power(const ui_in_t *in, const ui_mode_state_t *s, int ch,
                  char *power, size_t pcap, char *vi, size_t vcap);

/* 点方向键 / 风扇键之前先问：返回 NULL = 可以发；否则返回该提示的话（不发帧）*/
const char *ui_mode_tap_block(const ui_mode_state_t *s);
const char *ui_mode_fan_block(const ui_mode_state_t *s);

/* 点"制冷 / 制热 / 暂停"之后：以主控回读的实际输出为准，不抢先高亮 */
typedef struct {
    uint8_t  waiting;
    uint32_t sent_ms;
    uint32_t seq_at_send;        /* 点击那一刻的快照序号：变了才说明来过新帧 */
    uint8_t  dir;                /* 要的方向（协议值 0 / 1 / 2）*/
} ui_mode_cmd_t;

typedef enum {
    UI_MC_WAIT = 0,
    UI_MC_DONE,                  /* 主控回读已经是这个方向 */
    UI_MC_NO_REPLY,              /* 主控没回话（链路断 / 没来新快照）*/
    UI_MC_SAFE,                  /* 安全态下不可操作 */
    UI_MC_NOT_MANUAL,            /* 不在手动模式 */
    UI_MC_REJECTED,              /* 其它原因没接受 */
} ui_mode_cmd_result_t;

void                  ui_mode_cmd_start(ui_mode_cmd_t *c, const ui_in_t *in, uint8_t dir);
ui_mode_cmd_result_t  ui_mode_cmd_poll(ui_mode_cmd_t *c, const ui_in_t *in, ui_tec_dir_t now);
const char           *ui_mode_cmd_hint(ui_mode_cmd_result_t r);

#ifdef __cplusplus
}
#endif
#endif /* UI_MODEL_H */
