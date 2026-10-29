#ifndef TCS_LVGL_H
#define TCS_LVGL_H
#include <stdint.h>
void tcs_lvgl_init(void);
void tcs_lvgl_poll(void);
void tcs_lvgl_estop_watch(void);   /* D-L7② 屏入口兜底（原始坐标层，独立于 LVGL 存活） */

/* TL-8：lv_timer_handler 单次耗时直方图（DWT µs；符号 g_tl8 由 probe-rs 直读）
 * v2（2026-10-29 修正）：v1 末桶为开口 ≥975µs，用该表算 P99.9 无法分辨 4ms 门限
 * （直方图在 4ms 处没有刻度，结论只可能"<1ms"），而 over_4000 又显示 2%~21% 超标
 * ——两个数互相矛盾。v2 增 1..63ms 逐毫秒尾桶 + ≥64ms 溢出桶，P99.9 判定改为
 * over_4000 与样本数直接比较（10000 样本时 P99.9≤4ms ⇔ over_4000≤10）。 */
typedef struct {
    uint32_t magic;        /* 0x544C3821 'TL8!' */
    uint32_t samples;      /* 已采样数（上限 10000 后停） */
    uint32_t bins[40];     /* [i*25,(i+1)*25)µs，i=0..39（i=39=[975,1000)） */
    uint32_t tail_ms[64];  /* [i,i+1)ms，i=1..63；tail_ms[0] 未用 */
    uint32_t over_64000;   /* ≥64ms */
    uint32_t over_4000;    /* ≥4ms（TL-8 门限计数） */
    uint32_t max_us;
    uint32_t done;         /* 1=采样完成 */
} tl8_t;
extern volatile tl8_t g_tl8;

/* ====== G4 测试钩子（probe 直写；=1 构建常驻，正常路径零影响）======
 * 2026-10-29：这些符号此前只存在于临时构建变体，源码里没有 → 测试不可复现
 * （g4_e04_tl9b.py 在现行 ELF 上取不到符号）。现固化为源内符号。 */
extern volatile uint32_t g_lvgl_freeze;     /* 1=停喂 lv_timer_handler（TL-9b 法1） */
extern volatile uint32_t g_lvgl_drain;      /* 1=开始耗尽池，2=已到 NULL（TL-9b 法2），0=释放 */
extern volatile uint32_t g_tap_inject[4];   /* {seq, touched, x, y} 合成触摸（触摸走查/边界点） */
extern volatile uint32_t g_tap_drop;        /* 1=模拟 indev 读失败（TL-13 丢帧注入） */
extern volatile uint32_t g_estop_calls;     /* tcs_emergency_stop 调用计数（E-04/TL-9b） */

/* 堆水位守卫统计（TL-9b 法2 修复的实证载体）：free 低于地板时跳过 handler，
 * 主循环继续跑 → 屏急停入口（独立原始坐标层）不被 LVGL 拖死。 */
typedef struct {
    uint32_t magic;        /* 'GRD1' 0x47524431 */
    uint32_t skip;         /* 因水位过低跳过 handler 的次数 */
    uint32_t min_free;     /* 观测到的最小 free_size */
    uint32_t floor_bytes;  /* 生效地板值 */
    uint32_t drain_done;   /* 耗尽注入已完成 1 */
    uint32_t recover;      /* 水位恢复后重新喂 handler 的次数 */
} tcs_lvgl_guard_t;
extern volatile tcs_lvgl_guard_t g_lvgl_guard;

/* 地板值：LVGL 正常渲染一次最多吃几 KB；低于此值继续喂 handler 有陷入
 * 内部分配死循环/长时间阻塞的风险（G4-TL9b 法2 实测：完全耗尽后主流停摆） */
#define TCS_LV_MEM_FLOOR_BYTES  (6u * 1024u)

#endif
