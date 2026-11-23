/**
 * tcs_profile.h — 构建档位派生（单一来源，2026-11-23 复核 A2）
 *
 * 背景：TCS_PROFILE 由 CMake 传入（PROD=0/BENCH=1/SIM=2），但 Keil/裸编译不会定义它。
 * 之前 `TCS_H743_LINK_ONLY` 只在 CMakeLists 里定义——裸编译时该宏未定义，
 * `#if TCS_H743_LINK_ONLY` 变成 0（台架语义：跑本地闭环），与 console 里
 * "TCS_PROFILE 缺省=0(PROD)" 的默认相反，恰好重现"本地轮询抢写快照"的旧问题。
 *
 * 规则：宏缺省时按**交付语义**（PROD）派生；台架必须显式 -DTCS_PROFILE=1。
 */
#ifndef TCS_PROFILE
#define TCS_PROFILE 0            /* 缺省 = PROD（与 console.c 的档位默认一致）*/
#endif

#ifndef TCS_H743_LINK_ONLY
#define TCS_H743_LINK_ONLY (TCS_PROFILE == 0)
#endif
