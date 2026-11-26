/**
 * param_registry.h — 参数注册表 = 唯一数据源（任务书 §3.3 ★防返工核心）
 *
 * 一切界面（屏/网页/CLI）都从注册表生成；id 是协议/EEPROM 锚点，永不复用永不改义。
 * 本模块零 HAL 依赖（PC 单测直接编译）；板级副作用经 param_hooks_t 注入：
 *   - on_broadcast：屏脏区 + TCS_PARAM 链路帧（0x11）
 *   - on_m207_set ：PF_M207 温度参数下发 M207（异步）
 *   - on_mode_change：mode 变更通知 control
 *   - eeprom_commit：2s 防抖到点后的物理写（eeprom.c 实现；PC 测试桩计数）
 *
 * 硬编码红线：贴身水箱设定温度上限 45°C 在表内 [min,max]；TEC 制热 45.0/制冷 15.0
 * 是安全常量（control.c），不进参数表。
 */
#ifndef PARAM_REGISTRY_H
#define PARAM_REGISTRY_H

#include <stdint.h>
#include "tcs_profile.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 参数标志位 ====== */
typedef enum {
    PF_RW       = 0x01,
    PF_SCREEN   = 0x02,   /* 出现在触摸屏调参页（D6：M207 参数无此位）*/
    PF_WEB      = 0x04,   /* 出现在网页参数区 */
    PF_PERSIST  = 0x08,   /* EEPROM 持久化 */
    PF_M207     = 0x10,   /* 变更需下发 M207 */
    PF_DANGER   = 0x20,   /* 危险参数（网页二次确认；H10 v1.2 ✓ 列）*/
} param_flags_t;

/* 值类型（协议/EEPROM 共用编码）*/
#define PARAM_TYPE_F32   0
#define PARAM_TYPE_U8    1
#define PARAM_TYPE_U16   2
#define PARAM_TYPE_BTN   3   /* 动作项（P1-13：0x127）：只占注册表位置，写值由 CMD 承载 */

/* ====== 参数 id（永久分配，永不复用永不改义）====== */
#define PARAM_ID_TC1_SET        0x01
#define PARAM_ID_TC2_SET        0x02
#define PARAM_ID_BODY_HOT       0x03
#define PARAM_ID_BODY_COLD      0x04
#define PARAM_ID_HYST           0x05
#define PARAM_ID_MODE           0x06
#define PARAM_ID_SET_CONFIRM    0x07   /* 高温二次确认阈值 */
#define PARAM_ID_COMMISSIONED   0x08   /* 布防（WP-A.5）：0=未布防传感器报警只记日志，1=完整 FMEA */
/* ===== m1（模式页，2026-11-26 用户定的新功能）：两路 TEC 的手动输出（真值在 ESP32，这里是镜像）=====
 * 0 = 暂停、1 = 制冷、2 = 制热；不持久化（上电一律暂停）、不带 PF_WEB。"模式"页写它 → 0x12 → 主控。*/
#define PARAM_ID_TEC1_MANUAL    0x09
#define PARAM_ID_TEC2_MANUAL    0x0A
#define PARAM_ID_M207_BASE      0x100  /* M207 底层参数段（文档到后启用，WP9）*/

#define PARAM_CAPACITY      64    /* 注册表数组容量（条数不再手写：见 param_registry.c 的 PARAM_N_REGISTERED 与 _Static_assert）*/
#define PARAM_EEPROM_MAX    31    /* EEPROM count 字段上限（u8 纪录数；物理布局另限 30，见 param_serialize）*/
#define PARAM_PERSIST_DEBOUNCE_MS  2000u

/* EEPROM v2 布局（§3.3）：0x00 magic=0xA6 | 0x01 ver=2 | 0x02 count | 0x03 rfu
 * | 0x04.. N×8B 记录{id u16le,type u8,rfu u8,value u32le} | 0xF8 CRC8(0x00..0xF7)
 * | 0xF9-0xFF 保留（自测字节 0xF6，写后恢复）*/
#define EE_IMG_SIZE     256
#define EE_MAGIC_V2     0xA6
#define EE_LAYOUT_VER   2
#define EE_CRC_ADDR     0xF8
#define EE_SELFTST_ADDR 0xF6

typedef struct {
    uint16_t    id;             /* 协议/EEPROM 锚点 */
    const char *key;            /* "tc1_set"，CLI/JSON 用 */
    const char *name;           /* UTF-8 中文显示名 */
    const char *unit;           /* "°C" / "" */
    uint8_t     type;           /* PARAM_TYPE_* */
    uint8_t     flags;          /* PF_* 组合 */
    float       min, max, step;
    float       value;          /* RAM 当前值 */
    uint8_t     dirty_persist;  /* 待写 EEPROM */
} param_desc_t;

/* ====== 只读状态快照（§3.3，不入写表）====== */
typedef struct {
    float    tc1_act, tc2_act;    /* M207 回读实际水温 °C */
    float    body_temp;           /* TMP117 滑动均值 °C */
    uint8_t  tec1_on, tec2_on;    /* TCSW 状态 */
    uint8_t  fan_on;
    uint8_t  mode;                /* 0手动 1自动（param mode 镜像）*/
    uint16_t alarm_bits;          /* §6.2 位定义 */
    uint8_t  m207_online;
    uint8_t  body_sensor_ok;
    uint8_t  esp_link_ok;
    uint32_t fw_ver;              /* BCD 0x010000 = v1.0.0 */
} tcs_state_t;

extern tcs_state_t g_tcs;

/* ====== 板级副作用注入（main 里装配；PC 测试不装=NULL 全跳过）====== */
typedef struct {
    void (*on_broadcast)(param_desc_t *p, uint8_t src);
    void (*on_m207_set)(uint8_t ch, float v);
    void (*on_m207_map_set)(uint16_t pid, float v);  /* H10：0x100+ 映射表参数下发 */
    void (*on_mode_change)(uint8_t m);
    int  (*eeprom_commit)(const uint8_t *img);   /* 传整 256B 镜像（含 CRC），物理写 */
    uint8_t src_touch, src_console, src_web;     /* src 标记（回显来源用）*/
} param_hooks_t;

/* src 约定值（回显"来源"字段）*/
#define PARAM_SRC_TOUCH   1
#define PARAM_SRC_CONSOLE 2
#define PARAM_SRC_WEB     3
#define PARAM_SRC_DEFAULT 4
#define PARAM_SRC_LINK    5   /* 来自链路对端（屏幕写参数）；用于抑制回推成环 */

void  param_registry_init(const param_hooks_t *h);   /* 建表+装配钩子+装默认值 */
int   param_count(void);                     /* 已注册参数条数 */
const param_desc_t *param_at(int idx);       /* 按序访问（注册表遍历/网页列表）*/
param_desc_t *param_find(uint16_t id);       /* O(N) 线性（≤32 项）*/
const param_desc_t *param_find_by_key(const char *key);

/**
 * 写参数唯一路径（触摸/网页/CLI 同走这里，F7 并发写天然串行化）。
 * clamp→相等短路→写→置脏→广播→返回 0。
 * 返回：0=ok；1=clamp 生效（回显 clamp 后值）；-1=坏 id；-2=约束拒绝
 *      （body_cold<body_hot 强制：写任一侧校验另一侧，违反拒写）。
 */
int   param_set(uint16_t id, float v, uint8_t src);
float param_get(uint16_t id);

/* 2s 防抖增量写 EEPROM：主循环节拍调用。有脏参且到点 → 序列化 → eeprom_commit。 */
void  param_persist_flush(uint32_t now_ms);
/* 立即固化（p save / 调参页保存按钮）。返回 0=已提交，1=无脏参，-1=commit 失败。 */
int   param_persist_now(void);
/* M05-F-02：把全部 PF_PERSIST 参数置脏并立即落盘（工厂复位/恢复默认用）。
 * 原实现 load_defaults 会清 dirty → persist_now 直接返回「无脏」，实际什么都没写。
 * PROD 档无本地持久化，恒返回 1。*/
int   param_persist_all(void);
uint32_t param_persist_fail_count(void);   /* R1 P1-04：连续落盘失败次数 */

/* r7f（独立复核 R7-12，2026-11-26）：交付档的邮箱命令 p set 该不该拒。
 * prod != 0（TCS_PROFILE < 1 的交付档）时，执行器类参数一律拒：0x06 控制模式 / 0x09、0x0A 两路手动输出
 * ——它们与交付档已经删掉的 mode / tec / fan 命令等效（console.c 的 TCS_PROFILE >= 1 那一段）。
 * 非执行器参数照旧放行：屏幕调参页本来就能写它们，邮箱不多开任何能力。
 * 返回 1 = 拒；0 = 放行。纯逻辑，主机用例 tests/test_param_registry.c。*/
int param_mailbox_blocked(uint16_t id, uint8_t prod);

/* 广播（param_set 内部已调；模式等非参数变更可手动调）*/
void  param_broadcast(param_desc_t *p, uint8_t src);

/* R2：链路快照回灌（ESP32 权威值 → 本地显示副本；无副作用无广播）*/
void  param_link_sync(uint16_t id, float v);
/* v2 补口：对端（ESP32 权威）参数镜像落表；NaN/未知 id 拒绝。
 * 抽成函数以便 PC 单测（main.c 不可测）。*/
int   param_mirror_apply(uint16_t id, float v);

/* R9-05（独立复核 R9，2026-11-26）：屏可写参数被主控拒绝后的收敛。
 *  - param_link_sync / param_mirror_apply 会把"从主控回来的值"记成权威值影子；
 *  - 写被拒（BAD_ID / BUSY / 跨参数约束拒绝…）时用 param_revert_to_authority 立刻把屏上的值换回影子，
 *    不再只覆盖 tc1_set / tc2_set 两个、其余等镜像泵下一轮（51 条约 41 秒）；
 *  - param_note_write_result 由 main.c 的应答回调转述：rejected != 0 = 这次写被拒（允许同值再发一次），
 *    0 = 成功/已收下（清掉标记，同值写回到"本地短路"）。
 * 三个都只动本地显示副本，不驱动执行器、不广播。主机用例 tests/test_param_registry.c。*/
int   param_revert_to_authority(uint16_t id);      /* 1 = 有权威值并已回写；0 = 还没有权威值 */
void  param_note_write_result(uint16_t id, uint8_t rejected);
/* X01-F-02：对端快照报警位与本机独占位合并（本机位不被整字覆盖）。*/
uint16_t alarm_merge_peer(uint16_t remote, uint16_t local_owned, uint16_t local_now);

void  param_load_defaults(void);
/* 上电恢复：img 由 eeprom.c 读出；magic/ver/CRC 任一不符 → 默认值 + 返回 -1
 * （加载入口在 main.c：读镜像 → param_deserialize → 返回值不对外就置 ALM_EEPROM。
 *   M05-F-16：吞返回值的 param_load_eeprom() 已删。）*/
int   param_deserialize(const uint8_t *img);

/* ====== EEPROM v2 序列化（纯函数，PC 单测 §5.1）====== */
/* 把全部 PF_PERSIST 参数打包成 256B 镜像（magic/ver/count/记录/CRC 全填）。
 * 返回 0=ok；-1=持久化参数 >30 条（0x04..0xF7 共 244B = 30 条物理上限，
 * 任务书 count 字段口径 ≤31，取更严者防 CRC 区被记录覆盖）。 */
int   param_serialize(uint8_t img[EE_IMG_SIZE]);

#ifdef __cplusplus
}
#endif
#endif /* PARAM_REGISTRY_H */
