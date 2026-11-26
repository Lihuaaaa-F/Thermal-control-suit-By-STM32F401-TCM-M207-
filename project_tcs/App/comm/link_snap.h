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

/* ====== m1（模式页，2026-11-26 用户定的新功能）：两路 TEC 的输出方向与电学量 ======
 * 取自 0x20 快照末尾 / 0x21 增量的两个新 mask 位（protocol/tcs_proto.h，TCS_PROTO_VER 2）。
 * main.c 维护，界面只读。链路断了（快照陈旧）由界面整体转灰——这里留下的是最后一帧的旧值。*/
typedef struct {
    uint8_t dir1, dir2;     /* TCS_TEC_DIR_*：0 暂停 1 制冷 2 制热 3 开着但不是"模式"页设的 */
    float   v1, i1;         /* TEC1 实际输出电压 V（制热为负）/ 实际输出电流 A */
    float   v2, i2;         /* TEC2 */
    uint8_t valid;          /* bit0 = TEC1 的电压电流有效；bit1 = TEC2。无效的那一路显示"未知" */
} tcs_link_tec_t;
void tcs_link_tec_copy(tcs_link_tec_t *out);

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

#ifdef __cplusplus
}
#endif
#endif /* LINK_SNAP_H */
