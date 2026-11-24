/* tcs_lvgl.c — G4 LVGL 栈初始化/节拍/急停兜底
 * 急停兜底（D-L7②）：每 10ms 节拍在主循环上下文直接读 FT5446 原始坐标，
 * 热区命中（底栏急停键 TUI_ESTOP_*）→ 直接调 tcs_emergency_stop()。
 * 与 LVGL 事件循环零调用关系：lv_timer_handler 卡死/内存耗尽时本入口照常工作。 */
#include "tcs_lvgl.h"
#include "lvgl.h"
#include "lv_demos.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ft5446.h"
#include "control.h"
#include <string.h>
#include "tui.h"           /* TUI_ESTOP_*（急停热区矩形，与主页按钮共用一处定义）*/
#include "stm32h7xx_hal.h"

volatile tl8_t g_tl8;   /* TL-8 采样块（probe-rs 经符号直读） */

/* ====== G4 测试钩子（2026-10-29 源内固化）======
 * 此前 g_lvgl_freeze/g_lvgl_drain/g_tap_inject/g_estop_calls 只存在于临时构建变体，
 * 源码里没有 → g4_e04_tl9b.py 在现行 ELF 上取不到符号，测试不可复现。 */
volatile uint32_t g_lvgl_freeze;
volatile uint32_t g_lvgl_drain;
volatile uint32_t g_tap_inject[4];
volatile uint32_t g_tap_drop;
volatile uint32_t g_estop_calls;

/* U-6：M207 最近错误码（m207.c 在 =1 构建写入；UI 翻成人话） */
volatile uint8_t g_m207_last_errcode;

volatile tcs_lvgl_guard_t g_lvgl_guard;

/* LVGL 渲染心跳：每次 lv_timer_handler() 真正执行 +1（低水位被跳过时不计）。
 * tcs_lvgl_estop_watch 用它判断"屏入口兜底"该不该抢在弹窗之前直接急停。*/
volatile uint32_t g_lvgl_beat;

/* 耗尽注入分配块登记：drain 归 0 时全部释放，验证 UI 可恢复 */
#define DRAIN_MAX 96
static void * s_drain_ptr[DRAIN_MAX];
static uint32_t s_drain_n;
static uint32_t s_blocked;   /* 水位守卫是否处于阻断态（统计恢复沿用） */

#if defined(TCS_LVGL_SCENE_HELLO)
static lv_obj_t * g_Lb, * g_Lc;
static void font_matrix_tick(lv_timer_t * tm)
{
    LV_UNUSED(tm);
    static uint32_t n;
    n++;
    char b[16];
    snprintf(b, sizeof(b), "%d.%d", (int)(n % 100), (int)(n % 10));
    lv_label_set_text(g_Lb, b);
    lv_label_set_text(g_Lc, "--.-");
}
#endif

/* G5 预算埋点：局部重绘（≤6ms 门）/全屏重绘（≤25ms 门）µs 级，probe 直读 */
volatile struct {
    uint32_t magic;      /* 0x42444737 'BDG7' */
    uint32_t part_us_max, part_n;
    uint32_t full_us_max, full_n;
} g_budget;

/* TL-2 真机版：页建删 ×1000 内存零增长（自动翻页，probe 读 g_tl2） */
#if defined(TCS_LVGL_SCENE_TL2)
volatile struct {
    uint32_t magic;      /* 0x544C3202 */
    uint32_t cycles, done;
    uint32_t used0, frag0, used1, frag1;
} g_tl2;
static void tl2_cycle(lv_timer_t * tm)
{
    LV_UNUSED(tm);
    extern void tui_request(tui_page_t p);
    if (g_tl2.done) return;
    if (g_tl2.cycles == 0) {
        lv_mem_monitor_t m;
        lv_mem_monitor(&m);
        g_tl2.used0 = m.total_size - m.free_size;
        g_tl2.frag0 = m.frag_pct;
    }
    g_tl2.cycles++;
    tui_request((tui_page_t)(g_tl2.cycles % 4));
    if (g_tl2.cycles >= 1000u) {
        lv_timer_del(tm);   /* 删除自身 timer 由 LVGL 延后安全处理 */
        lv_mem_monitor_t m;
        lv_mem_monitor(&m);
        g_tl2.used1 = m.total_size - m.free_size;
        g_tl2.frag1 = m.frag_pct;
        g_tl2.done = 1;
    }
}
#endif



/* LVGL 池宿主（AXIRAM；声明与段属性见 lv_conf.h LV_MEM_POOL_ALLOC） */
uint8_t tcs_lv_axi_pool[96U * 1024U] __attribute__((section(".axi_pool"), aligned(32)));

extern uint8_t tcs_lv_axi_pool[96U * 1024U];
void tcs_lvgl_init(void)
{
    memset(tcs_lv_axi_pool, 0, sizeof(tcs_lv_axi_pool));   /* G5-Z24：灭 AXI 池旧固件残留（label 文本缓冲在池内） */
    lv_init();
    lv_tick_set_cb(HAL_GetTick);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    g_tl8.magic = 0x544C3821u;   /* 'TL8!' */
    lv_port_disp_init();
    lv_port_indev_init();
#if defined(TCS_LVGL_SCENE_BENCH)
    lv_demo_benchmark();       /* G4② 台架场景（-DTCS_LVGL_SCENE_BENCH 构建变体） */
#elif defined(TCS_LVGL_SCENE_HELLO)
    lv_obj_t * scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0xEEF1F5), 0);
    extern const lv_font_t tcs_font_lv96;
    extern const lv_font_t tcs_font_lv48;
    extern const lv_font_t tcs_font_lv24;
    lv_obj_t * a = lv_label_create(scr);
    lv_obj_remove_style_all(a);
    lv_obj_set_style_text_font(a, &tcs_font_lv96, 0);
    lv_obj_set_style_text_color(a, lv_color_hex(0x111827), 0);
    lv_label_set_text(a, "--.- 16.3");
    /* 四象限矩阵：A=本 label(静态直接屏) B=500ms set_text C=--.- set_text D=新屏+load */
    static lv_obj_t * La, * Lb, * Lc, * Ld;
    La = lv_label_create(scr);
    lv_obj_remove_style_all(La);
    lv_obj_set_style_text_font(La, &tcs_font_lv96, 0);
    lv_label_set_text(La, "88.8");
    lv_obj_set_pos(La, 560, 80);
    Lb = lv_label_create(scr);
    lv_obj_remove_style_all(Lb);
    lv_obj_set_style_text_font(Lb, &tcs_font_lv96, 0);
    lv_label_set_text(Lb, "77.7");
    lv_obj_set_pos(Lb, 40, 300);
    Lc = lv_label_create(scr);
    lv_obj_remove_style_all(Lc);
    lv_obj_set_style_text_font(Lc, &tcs_font_lv96, 0);
    lv_label_set_text(Lc, "66.6");
    lv_obj_set_pos(Lc, 560, 300);
    Ld = NULL;   /* D 在新屏构建 */
    g_Lb = Lb; g_Lc = Lc;
    lv_timer_create(font_matrix_tick, 500, NULL);
    lv_obj_set_pos(a, 40, 80);
    lv_obj_t * b = lv_label_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_style_text_font(b, &tcs_font_lv48, 0);
    lv_obj_set_style_text_color(b, lv_color_hex(0x111827), 0);
    lv_label_set_text(b, "36.8");
    lv_obj_set_pos(b, 40, 260);
    lv_obj_t * c = lv_label_create(scr);
    lv_obj_remove_style_all(c);
    lv_obj_set_style_text_font(c, &tcs_font_lv24, 0);
    lv_obj_set_style_text_color(c, lv_color_hex(0x111827), 0);
    lv_label_set_text(c, "24px 手动");
    lv_obj_set_pos(c, 40, 380);
    lv_obj_t * l = lv_label_create(scr);
    lv_label_set_text(l, "TCS font bisect");
    lv_obj_set_pos(l, 40, 20);
#else
    tui_init();                /* G5：实时 UI 层（8 页） */
    g_budget.magic = 0x42444737u;
    {   /* G5 探针 v2：前 24 子几何快照 */
        extern volatile uint32_t g_ui_probe[80];
        lv_obj_t * scr = lv_screen_active();
        lv_obj_update_layout(scr);   /* 强制布局计算（裸读否则全 0） */
        uint32_t n = lv_obj_get_child_count(scr);
        g_ui_probe[0] = 0x55495052u;
        g_ui_probe[1] = n;
        uint32_t k = 2;
        for (uint32_t i = 0; i < n && i < 24 && k + 3 < 80; i++) {
            lv_obj_t * c = lv_obj_get_child(scr, i);
            g_ui_probe[k++] = lv_obj_get_x(c);
            g_ui_probe[k++] = lv_obj_get_y(c);
            g_ui_probe[k++] = lv_obj_get_width(c) | (lv_obj_get_height(c) << 16);
        }
    }
#if defined(TCS_LVGL_SCENE_TL2)
    g_tl2.magic = 0x544C3202u;
    lv_timer_create(tl2_cycle, 120, NULL);   /* 1000 翻页 ≈ 2min */
#endif
#endif
}


void tcs_lvgl_poll(void)
{
    lv_mem_monitor_t mm;

    /* ⓪ 内存耗尽注入（TL-9b 法2 用；正常运行为 0） */
    if (g_lvgl_drain == 1u) {
        while (s_drain_n < DRAIN_MAX) {
            void * p = lv_malloc(1024);
            if (!p) break;
            s_drain_ptr[s_drain_n++] = p;
        }
        g_lvgl_drain = 2u;
        g_lvgl_guard.drain_done = 1u;
    } else if (g_lvgl_drain == 0u && s_drain_n) {
        while (s_drain_n) lv_free(s_drain_ptr[--s_drain_n]);
    }

    /* ① 堆水位守卫（TL-9b 法2 修复）：free 低于地板时不再喂 handler。
     * 原行为：池完全耗尽后 lv_timer_handler 内部长时间阻塞 → 主循环停摆 →
     * 屏急停入口不可达，最终靠 IWDG 2s 复位（超出 500ms 门）。现在改为跳过
     * handler 保主循环，急停入口（独立原始坐标层）照常每 10ms 判定；水位恢复
     * 后自动续喂。渲染降级属于设计内安全行为，非静默失败。 */
    lv_mem_monitor(&mm);
    if (g_lvgl_guard.magic != 0x47524431u) {
        g_lvgl_guard.magic = 0x47524431u;
        g_lvgl_guard.floor_bytes = TCS_LV_MEM_FLOOR_BYTES;
        g_lvgl_guard.min_free = mm.free_size;
    }
    if (mm.free_size < g_lvgl_guard.min_free)
        g_lvgl_guard.min_free = mm.free_size;

    if (g_lvgl_freeze || mm.free_size < TCS_LV_MEM_FLOOR_BYTES) {
        g_lvgl_guard.skip++;
        s_blocked = 1;
        return;   /* 冻结/低水位：主循环继续（estop_watch 在其他调用点照常） */
    }
    if (s_blocked) {          /* 只统计"恢复"这个沿，避免每拍自增 */
        s_blocked = 0;
        g_lvgl_guard.recover++;
    }

    uint32_t cy0 = DWT->CYCCNT;
    g_lvgl_beat++;
    lv_timer_handler();
#if !defined(TCS_LVGL_SCENE_BENCH) && !defined(TCS_LVGL_SCENE_HELLO)
    tui_tick();   /* 延后导航/500ms 数据刷新（LVGL 回调上下文外） */
#endif
    uint32_t cyc = DWT->CYCCNT - cy0;
#if !defined(TCS_LVGL_SCENE_BENCH) && !defined(TCS_LVGL_SCENE_HELLO)
    {   /* 预算归因：上一拍是否整页重建 */
        extern int tui_last_was_rebuild(void);
        uint32_t us = cyc / (SystemCoreClock / 1000000U);
        if (g_budget.magic == 0x42444737u) {
            if (tui_last_was_rebuild()) {
                if (us > g_budget.full_us_max) g_budget.full_us_max = us;
                g_budget.full_n++;
            } else {
                if (us > g_budget.part_us_max) g_budget.part_us_max = us;
                g_budget.part_n++;
            }
        }
    }
#endif
    /* TL-8 v2：0..999µs 25µs 分桶 + 1..63ms 逐毫秒尾桶 + ≥64ms 溢出桶。
     * 旧版末桶开口 ≥975µs，无法在 4ms 处判定，P99.9 结论不成立。 */
    if(g_tl8.done == 0 && g_tl8.magic == 0x544C3821u) {
        uint32_t us = cyc / (SystemCoreClock / 1000000U);
        if (us >= 1000u) {
            uint32_t ms = us / 1000u;
            if (ms >= 64u) g_tl8.over_64000++;
            else           g_tl8.tail_ms[ms]++;
        } else {
            g_tl8.bins[us / 25u]++;
        }
        if(us > g_tl8.max_us) g_tl8.max_us = us;
        if(us >= 4000U) g_tl8.over_4000++;
        if(++g_tl8.samples >= 10000U) g_tl8.done = 1;
    }
}

#define TCS_ESTOP_GRACE_MS       5u      /* 至少跨一个主循环节拍再看心跳（watch 在 poll 之前调用）*/
#define TCS_ESTOP_FORCE_HOLD_MS   600u    /* 按住这么久还没弹窗 → 判 LVGL 半死，直接急停*/

void tcs_lvgl_estop_watch(void)
{
    touch_event_t ev;
    static uint8_t  s_in_zone, s_fired;
    static uint32_t s_since, s_beat0;
    /* 合成触摸（probe/console 注入；正常运行 g_tap_inject[0] 恒 0）与真实触摸走**同一条**
     * 门控。2026-11-23 之前注入是"seq 一变就急停"的旁路，走查测的不是真手指那条路径。
     * mode 1 = 150ms 按压；mode 2 = 持续按住直到 mode 改 0（与 lv_port_indev 口径一致）。*/
    static uint32_t s_seq, s_synth_until;
    uint32_t now = HAL_GetTick();
    uint32_t seq = g_tap_inject[0];
    if (seq != s_seq) {
        s_seq = seq;
        s_synth_until = (g_tap_inject[1] == 1u) ? now + 150u : 0u;
    }
    uint8_t  pressed;
    uint32_t x, y;
    if (g_tap_inject[1] == 2u || (s_synth_until && (int32_t)(s_synth_until - now) > 0)) {
        pressed = 1;
        x = g_tap_inject[2];
        y = g_tap_inject[3];
    } else {
        if (ft5446_read(&ev) != 0) { s_in_zone = 0; return; }
        pressed = (ev.valid && ev.touched) ? 1u : 0u;
        x = ev.x;
        y = ev.y;
    }

    /* D-L7② 屏入口兜底：热区=底栏急停键（768,472,216,112），主循环节拍判定，不依赖 LVGL。
     * 2026-11-23 现场修正边界：规范是"急停二次确认（弹窗）"（UI 规范 §、规格书
     * "屏弹窗"），正常路径由 LVGL 弹"确认急停？"。原实现不分情况先触发 → 一按就急停，
     * 弹窗反而在急停之后才弹出，还把红色横幅挡住（现场实测）。现在：
     *   - LVGL 心跳停了（卡死/低水位跳过 handler）→ 按下后下一个节拍直接急停（兜底本职）；
     *   - LVGL 活着 → 交给弹窗确认；按住 ≥TCS_ESTOP_FORCE_HOLD_MS 仍无弹窗（半死）→ 直接急停，
     *     且只在首页生效（用户 2026-11-24 确认保留长按直接急停）——其它页面这块区域是别的控件
     *     （调参页的 + 键就在这里），不能触发。
     * 每次按压只触发一次：重发由主循环 200ms pending 块负责（R-15），本机动作幂等。*/
    if (!pressed || x < TUI_ESTOP_X || x >= TUI_ESTOP_X + TUI_ESTOP_W ||
        y < TUI_ESTOP_Y || y >= TUI_ESTOP_Y + TUI_ESTOP_H) {
        s_in_zone = 0;
        return;
    }
    if (!s_in_zone) {
        s_in_zone = 1;
        s_fired = 0;
        s_since = now;
        s_beat0 = g_lvgl_beat;
    }
    if (s_fired || (uint32_t)(now - s_since) < TCS_ESTOP_GRACE_MS) return;
    uint8_t lvgl_alive = (g_lvgl_beat != s_beat0);
    uint8_t held = ((uint32_t)(now - s_since) >= TCS_ESTOP_FORCE_HOLD_MS);
    if (!lvgl_alive || (held && !tui_dlg_active() && tui_current() == TUI_HOME)) {
        s_fired = 1;
        tcs_emergency_stop();
    }
}

void tcs_lvgl_note_state_change(void)
{
    g_ui_state_change_ms = HAL_GetTick();
    g_ui_state_change_seq++;        /* 每记一次换一个号：界面据此区分"快照带来的变化"与本地置位 */
}