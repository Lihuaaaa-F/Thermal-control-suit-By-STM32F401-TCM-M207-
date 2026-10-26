/**
 * tcs_snap.h — R2 链路快照发送（0x20 全量 200ms + 0x21 位图增量；任务书第十一章）
 *
 * 核 0，200ms 节拍（第八章预算：编码发送 ≤0.5ms，[SNAPP] 周期 perf 行）。
 * 0x20=fire-and-forget 全量快照；0x21=与上次发送的差异位图（变化即发，先于 0x20）。
 * 旧屏固件对未知消息号静默丢弃（switch 无 default）→ 前向兼容，真机验证 R2 证据。
 */
#ifndef TCS_SNAP_H
#define TCS_SNAP_H

#ifdef __cplusplus
extern "C" {
#endif

int tcs_snap_start(void);            /* 起快照任务（核 0，200ms）。0=ok */
uint32_t tcs_snap_enc_max_us(void);  /* 编码+发送耗时峰值（预算 500µs）*/

#ifdef __cplusplus
}
#endif
#endif /* TCS_SNAP_H */
