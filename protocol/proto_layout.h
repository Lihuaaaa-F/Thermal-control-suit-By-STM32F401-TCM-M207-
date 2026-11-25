/* protocol/proto_layout.h — 协议结构布局的编译期契约（R1 AD-6）
 *
 * 为什么需要：`tcs_proto.h` 的载荷是**手工逐字节编码**的，但内部缓存用的是结构体
 * （如 tcs_reg_entry_t 在注册表里被整体拷贝）。如果两端编译器对同一结构体的
 * padding/对齐解释不同，就会出现"字节级协议一致、内存布局不一致"的隐患。
 * 审查 X01 曾用 offsetof 探针在 MSVC/ARM/Xtensa 三套工具链上验证过；这里把结论
 * 固化成编译期断言，任何一端改字段都会在构建时失败，而不是等到真机。
 *
 * 数值来源：arm-none-eabi-gcc 13.2.1 实测（2026-11-15，-mcpu=cortex-m7）。
 * 若确需改结构体：先改 protocol/tcs_proto.h，再同步本文件的期望值，并在提交信息
 * 里写 TCS_PROTO_VER 是否变化（AD-6）。
 */
#ifndef TCS_PROTO_LAYOUT_H
#define TCS_PROTO_LAYOUT_H

#include <stddef.h>
#include "tcs_proto.h"

#define TCS_LAYOUT_ASSERT(what, expr) _Static_assert((expr), "proto layout: " what)

/* 结构体大小（含 padding，不是线长）*/
TCS_LAYOUT_ASSERT("sizeof status",     sizeof(tcs_status_pl_t) == 28);
TCS_LAYOUT_ASSERT("sizeof param",      sizeof(tcs_param_pl_t) == 8);
TCS_LAYOUT_ASSERT("sizeof reg_entry",  sizeof(tcs_reg_entry_t) == 100);
TCS_LAYOUT_ASSERT("sizeof ack",        sizeof(tcs_ack_pl_t) == 8);
TCS_LAYOUT_ASSERT("sizeof snap",       sizeof(tcs_snap_pl_t) == 56);
TCS_LAYOUT_ASSERT("sizeof delta_src",  sizeof(tcs_delta_src_t) == 28);

/* 关键字段偏移*/
TCS_LAYOUT_ASSERT("off snap.ver",      offsetof(tcs_snap_pl_t, ver) == 0);
TCS_LAYOUT_ASSERT("off snap.seq",      offsetof(tcs_snap_pl_t, seq) == 4);
TCS_LAYOUT_ASSERT("off snap.tc1_act",  offsetof(tcs_snap_pl_t, tc1_act) == 12);
TCS_LAYOUT_ASSERT("off snap.tc1_set",  offsetof(tcs_snap_pl_t, tc1_set) == 24);
TCS_LAYOUT_ASSERT("off snap.alarm",    offsetof(tcs_snap_pl_t, alarm) == 36);
TCS_LAYOUT_ASSERT("off snap.fresh_ms", offsetof(tcs_snap_pl_t, fresh_ms) == 40);
/* m1"模式"页（2026-11-26）追加的字段：线上顺序 tec_dir(40) tec1_v(41) tec1_i(43) tec2_v(45) tec2_i(47)；
 * 结构体里 tec_dir 后有 1 字节对齐填充（手工逐字节编码，不经结构体内存上线路）*/
TCS_LAYOUT_ASSERT("off snap.tec_dir",  offsetof(tcs_snap_pl_t, tec_dir) == 44);
TCS_LAYOUT_ASSERT("off snap.tec1_v",   offsetof(tcs_snap_pl_t, tec1_v) == 46);
TCS_LAYOUT_ASSERT("off snap.tec1_i",   offsetof(tcs_snap_pl_t, tec1_i) == 48);
TCS_LAYOUT_ASSERT("off snap.tec2_v",   offsetof(tcs_snap_pl_t, tec2_v) == 50);
TCS_LAYOUT_ASSERT("off snap.tec2_i",   offsetof(tcs_snap_pl_t, tec2_i) == 52);
TCS_LAYOUT_ASSERT("off delta.tec_dir", offsetof(tcs_delta_src_t, tec_dir) == 18);
TCS_LAYOUT_ASSERT("off delta.tec1_v",  offsetof(tcs_delta_src_t, tec1_v) == 20);
TCS_LAYOUT_ASSERT("off reg.id",        offsetof(tcs_reg_entry_t, id) == 2);
TCS_LAYOUT_ASSERT("off reg.key",       offsetof(tcs_reg_entry_t, key) == 20);

/* 线长与版本（改这里等于改协议，必须双端同 commit）*/
TCS_LAYOUT_ASSERT("TCS_PROTO_VER",     TCS_PROTO_VER == 2);       /* m1"模式"页：1 → 2 */
TCS_LAYOUT_ASSERT("TCS_STATUS_PL_LEN", TCS_STATUS_PL_LEN == 19);
TCS_LAYOUT_ASSERT("TCS_PARAM_PL_LEN",  TCS_PARAM_PL_LEN == 7);
TCS_LAYOUT_ASSERT("TCS_ACK_PL_LEN",    TCS_ACK_PL_LEN == 7);
TCS_LAYOUT_ASSERT("TCS_SNAP_PL_LEN",   TCS_SNAP_PL_LEN == 49);    /* m1"模式"页：40 → 49 */

#endif /* TCS_PROTO_LAYOUT_H */
