/* lv_conf.h — G4 候选终版（闪屏 Z18+Z22 已修且用户确认；官方双警告合入清零） */
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(disable: 4201)
#endif
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>

#define LV_COLOR_FORMAT_DEFAULT LV_COLOR_FORMAT_RGB565
#define LV_USE_OS               LV_OS_NONE
#define LV_MEM_SIZE             (96U * 1024U)   /* 池在 AXIRAM（.axi_pool 段） */
#define LV_DEF_REFR_PERIOD      16

#define LV_USE_LOG              0
#define LV_USE_CHECK_ARG        0               /* 警告1清零：调试件关断 */
#define LV_DRAW_LAYER_SIMPLE_BUF_SIZE (64U * 1024U)  /* 警告2清零：≥屏 1/20（60KB） */
#define LV_USE_SYSMON           1
/* 2026-11-23 现场：叠层画在右下角 (905..1023,565..595)，压住急停键一角——交付档
 * 不应有调试叠层。取证档（BENCH/SIM）保留，G4① 的 FPS/CPU 口径不受影响。*/
#ifndef TCS_PROFILE
#include "tcs_profile.h"
#endif
#define LV_USE_PERF_MONITOR     (TCS_PROFILE != 0)   /* 真机 FPS/CPU 叠层（G4① 取证口径，仅台架档） */

#define LV_FONT_MONTSERRAT_14 1          /* 默认字体 */
#define LV_FONT_MONTSERRAT_20 1          /* benchmark 硬性要求 */
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_26 1

#define LV_USE_DEMO_WIDGETS     1      /* benchmark 硬依赖（一致性检查） */
#define LV_USE_DEMO_BENCHMARK   1      /* G4② 四组数字 */

/* LVGL 池 → AXIRAM（.axi_pool 段；AXI 默认缓存图 + CACR WT ⇒ 一致，CPU-only 访问） */
extern uint8_t tcs_lv_axi_pool[96U * 1024U] __attribute__((section(".axi_pool"), aligned(32)));
#define LV_MEM_POOL_ALLOC(size) ((void *)(uintptr_t)tcs_lv_axi_pool)

#endif /*LV_CONF_H*/
