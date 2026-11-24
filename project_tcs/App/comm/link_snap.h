/**
 * link_snap.h — 屏侧对"主控快照"的接收状态（main.c 维护；界面只读）
 * 界面靠它区分"实时数据"与"陈旧数据"，不再让页面自己去碰 main.c 的静态变量。
 */
#ifndef LINK_SNAP_H
#define LINK_SNAP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* age_ms：距最近一帧 0x20 快照的毫秒数；seq：那一帧的序号。
 * have=0 表示上电后还没收到过（此时 age_ms/seq 无意义）。*/
void tcs_link_snap_info(uint32_t *age_ms, uint32_t *seq, uint8_t *have);

/* 最近一帧快照里有急停位 = 主控已执行急停（区别于屏端自己置的本地自持位）。没收到过快照 → 0。*/
uint8_t tcs_link_snap_peer_estop(void);

#ifdef __cplusplus
}
#endif
#endif /* LINK_SNAP_H */
