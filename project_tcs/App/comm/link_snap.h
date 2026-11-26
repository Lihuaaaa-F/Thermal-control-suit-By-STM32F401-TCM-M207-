/**
 * link_snap.h — 屏侧对"主控快照"的接收状态（main.c 维护；界面只读）
 * 界面靠它区分"实时数据"与"陈旧数据"，不再让页面自己去碰 main.c 的静态变量。
 */
#ifndef LINK_SNAP_H
#define LINK_SNAP_H

#include <stdint.h>
#include "tcs_proto.h"     /* m1（模式页）：TCS_TEC_DIR_* / TCS_ELEC_*_UNKNOWN（协议唯一源）*/

#ifdef __cplusplus
extern "C" {
#endif

/* age_ms：距最近一帧 0x20 快照的毫秒数；seq：那一帧的序号。
 * have=0 表示上电后还没收到过（此时 age_ms/seq 无意义）。*/
void tcs_link_snap_info(uint32_t *age_ms, uint32_t *seq, uint8_t *have);

/* 最近一帧快照里有急停位 = 主控已执行急停（区别于屏端自己置的本地自持位）。没收到过快照 → 0。*/
uint8_t tcs_link_snap_peer_estop(void);

/* ====== S 线 i3（条目 11）：协议版本不配对的屏端表现 ======
 * 主控能经网页 OTA 升级，屏只能经探针烧录。协议版本一旦不同，屏按"ver 不符"丢弃全部 0x20 快照，
 * 屏看到的就是"主控连接中断"——行为安全（执行器/模式显示未知、按钮不可点、没有急停），但提示不贴切。
 * 这里把"被版本校验拒掉的快照"单独计数，界面据此把横幅换成"主控固件与屏不匹配"。*/
uint32_t tcs_link_snap_ver_rejects(void);
/* 计数怎么用（"在涨"的判定）是界面纯逻辑，在 ui_model.h 的 ui_vr_mismatch()；这里只管读数 */

/* ====== m1（模式页，2026-11-26 用户定的新功能）：两路 TEC 的输出方向与电学量 ======
 * 取自 0x20 快照末尾 / 0x21 增量的两个新 mask 位（protocol/tcs_proto.h，TCS_PROTO_VER 2）。
 * main.c 维护，界面只读。链路断了（快照陈旧）由界面整体转灰——这里留下的是最后一帧的旧值。*/
typedef struct {
    uint8_t dir1, dir2;     /* TCS_TEC_DIR_*：0 暂停 1 制冷 2 制热 3 开着但不是"模式"页设的 */
    float   v1, i1;         /* TEC1 实际输出电压 V（制热为负）/ 实际输出电流 A */
    float   v2, i2;         /* TEC2 */
    uint8_t valid;          /* bit0 = TEC1 的电压电流有效；bit1 = TEC2。无效的那一路显示"未知" */
    /* ---- S 线 i3（2026-11-26；P1-09 / X03-F-08 收口）：0x20 快照末尾的 3 字节原样落镜像 ----
     * cmd 命令意图 / act 模块回显（各 bit0=TEC1 bit1=TEC2）；known 该路开关状态是否可信（0 = 未知）。
     * 界面规则：known 位是 0 的那一路显示"未知"，不得显示"运行 / 停止"；主控在但 M207 离线时就是这种情形。*/
    uint8_t cmd, act, known;
} tcs_link_tec_t;
void tcs_link_tec_copy(tcs_link_tec_t *out);

/* ====== X01-F-04（S 线 e1，2026-11-26）：0x20 的 fresh_ms / badge 终于有人消费 ======
 * 修复前：0x20 每 200ms 带着 fresh_ms（M207 温度缓存龄）与 badge（布防角标）来，H743 解出来
 * 就扔在 s_last_snap 里，没有任何消费者；屏幕温度卡永远显示主控最后一次给的数（可能早已陈旧），
 * 布防状态（含失联报警）永远到不了屏。
 *
 * 语义（与生产端 esp32/main/tcs_snap.c 对照）：
 *   fresh_ms = m207_temp_age_ms()，**较旧那一路**温度缓存的龄；0xFFFF = 离线/未知。
 *              主控温度轮询是 500ms/通道，正常值 ≤ 约 1000ms。
 *   badge    = post_arm_state()：0 = 未布防，1 = 已布防，2 = 布防后失联报警。
 *              ⚠ 与屏本地 POST 角标（post_badge_state：0 未开始 / 1 显示中 / 2 已淡出）
 *              **同名异义**，不许接同一个控件——这是修复前的第二个坑。
 *
 * 界面怎么显示（界面层不在本批次范围，方案写在 docs/results/S_G_e1_结果.md）：
 *   温度卡：stale = 1 时整卡转灰 + 文案"数据陈旧"（沿用现有"连接中断"那套灰化样式），
 *           不显示具体数字（0xFFFF 更不能按毫秒显示成 65.5s）。
 *   布防角标：独立控件（与 POST 角标分开），badge 2 用红底"布防失联"、1 用"已布防"、0 不显示。*/
#define LINK_SNAP_FRESH_STALE_MS   3000u   /* 温度龄超过 3 个轮询周期 = 陈旧（500ms/通道，较旧者约 1000ms）*/
#define LINK_SNAP_AGE_STALE_MS     2000u   /* 距最近一帧快照超过 2s = 链路层面的陈旧（X01 关单条件）*/

typedef struct {
    uint16_t fresh_ms;   /* 最近一帧的 M207 温度缓存龄（0xFFFF = 未知/离线）*/
    uint8_t  badge;      /* 布防角标请求（post_arm_t 语义：0/1/2）*/
    uint8_t  have;       /* 上电后收到过快照（否则 fresh_ms/badge 无意义）*/
    uint8_t  stale;      /* 收到那一帧时按 fresh_ms 判定的陈旧（链龄要用 tcs_link_stale_at 现算）*/
} tcs_link_fresh_t;

/* 收到 0x20：整块刷新（fresh_ms + badge）*/
static inline void tcs_link_fresh_apply(tcs_link_fresh_t *f, uint16_t fresh_ms, uint8_t badge)
{
    f->fresh_ms = fresh_ms;
    f->badge    = (uint8_t)(badge & 0x03u);
    f->have     = 1;
    f->stale    = (uint8_t)((fresh_ms == 0xFFFFu || fresh_ms > LINK_SNAP_FRESH_STALE_MS) ? 1u : 0u);
}

/* 收到 0x21 的 TCS_D_BADGE：只换 badge，fresh_ms 与陈旧判定不变 */
static inline void tcs_link_fresh_badge(tcs_link_fresh_t *f, uint8_t badge)
{
    f->badge = (uint8_t)(badge & 0x03u);
}

/* 界面绘制时现算：fresh_ms 陈旧、快照本身超过 2s 没来、或还没收到过快照 → 陈旧。
 * （snap_age_ms 取自 tcs_link_snap_info 的 age_ms；"断链 2s 后出现数据陈旧"就是这条。）*/
static inline uint8_t tcs_link_stale_at(uint16_t fresh_ms, uint32_t snap_age_ms, uint8_t have)
{
    if (!have) return 1;
    if (snap_age_ms > LINK_SNAP_AGE_STALE_MS) return 1;
    if (fresh_ms == 0xFFFFu || fresh_ms > LINK_SNAP_FRESH_STALE_MS) return 1;
    return 0;
}

/* 主控那一帧的 freshness 镜像（整块拷贝，与 tcs_link_tec_copy 同规：界面只读）*/
void tcs_link_fresh_copy(tcs_link_fresh_t *out);

/* m1（模式页）：屏写参数的"补发"入口——同一帧等 ACK 的 0x12（策略在 App/comm/link_send.c），
 * 不经本地注册表的相等短路。只在"镜像里已经是这个值、但主控回读不是"时用（tui_p4_mode.c 的 mode_write）。
 * 返回 0 = 已入队并登记等待确认；-1 = 组帧失败。交付档以外（台架 / 仿真）同样走这一条。*/
int tcs_link_write_param(uint16_t id, uint8_t type, float value);

/* 下面两条是纯逻辑（主机用例 tests/test_proto_m1.c 直接编）：main.c 收到 0x20 / 0x21 时调用。 */
/* 输出方向：0x20.tec_dir / 0x21 的 TCS_D_TEC_DIR 字段（低半字节第一路、高半字节第二路）*/
static inline void tcs_link_tec_set_dirs(tcs_link_tec_t *t, uint8_t tec_dir)
{
    t->dir1 = tcs_tec_dir_get(tec_dir, 1);
    t->dir2 = tcs_tec_dir_get(tec_dir, 2);
}

/* 电学量：定点原值（0.01 V / 0.01 A）还原成 V / A；电压或电流是"未知"值的那一路 valid 位清零 */
static inline void tcs_link_tec_set_elec(tcs_link_tec_t *t, int16_t v1, uint16_t i1,
                                         int16_t v2, uint16_t i2)
{
    t->v1 = (float)v1 * 0.01f;  t->i1 = (float)i1 * 0.01f;
    t->v2 = (float)v2 * 0.01f;  t->i2 = (float)i2 * 0.01f;
    t->valid = (uint8_t)((((v1 != TCS_ELEC_V_UNKNOWN) && (i1 != TCS_ELEC_I_UNKNOWN)) ? 1u : 0u)
                       | (((v2 != TCS_ELEC_V_UNKNOWN) && (i2 != TCS_ELEC_I_UNKNOWN)) ? 2u : 0u));
}

/* S 线 i3：TEC 开关的命令意图 / 模块回显 / 是否可信（0x20 末尾的 3 字节）。只镜像到低两位。*/
static inline void tcs_link_tec_set_state(tcs_link_tec_t *t, uint8_t cmd, uint8_t act, uint8_t known)
{
    t->cmd   = (uint8_t)(cmd & 0x03u);
    t->act   = (uint8_t)(act & 0x03u);
    t->known = (uint8_t)(known & 0x03u);
}

#ifdef __cplusplus
}
#endif
#endif /* LINK_SNAP_H */
