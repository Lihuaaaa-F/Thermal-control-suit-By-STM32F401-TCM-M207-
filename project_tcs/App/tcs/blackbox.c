/**
 * blackbox.c — T1 IWDG 复位黑匣子实现
 *
 * 复位裁决逻辑（docs/evidence/WP6_iwdg_defect.md）：
 *   - 复位后读 BKP4（上轮最后心跳）≈ 上轮 uptime → 判"喂狗从未生效"还是"中途停摆"
 *   - BKP6/BKP13 → 上轮是否 HardFault/Error_Handler 挂死等狗
 *   - BKP11（ISR 喂狗计数）在复位后停止增长 → 区分 ISR 臂是否也在跑
 * 仅用已核实的 CMSIS 符号（RTC->BKP0R..、RCC->RSR/APB4ENR、HAL_PWR_EnableBkUpAccess）。
 */
#include "blackbox.h"
#include "stm32h7xx_hal.h"
#include "iwdg.h"

/* ====== BKP 寄存器分配（索引 → RTC->BKPnR）====== */
#define BB_MAGIC_VAL 0xBB0C0DE5u
/*  0 magic  1 boot_cnt  2 prev_rsr(上轮启动RSR,boot时转存自3)
 *  3 cur_rsr(本轮启动RSR,趁console清除前抓)  4 hb_ms(心跳)  5 loop_passes
 *  6 fault_marker  7 fault_pc  8 fault_lr  9 fault_cfsr 10 fault_hfsr
 * 11 isr_feeds 12 hw_ok 13 fault_boot(故障发生时的boot_cnt) */

static volatile uint32_t s_loop_passes;
static volatile uint32_t s_isr_feeds;
static uint32_t s_boot_cnt;
static uint32_t s_prev_rsr, s_prev_hb, s_prev_loop, s_prev_fault;
static uint32_t s_last_hb_write;

void blackbox_boot(void)
{
    uint32_t old_boot;

    /* PWR 寄存器访问无需显式开钟（与 main.c HAL_PWREx_ConfigSupply 同前提） */
    HAL_PWR_EnableBkUpAccess();
#ifdef RCC_APB4ENR_RTCCAPBEN
    RCC->APB4ENR |= RCC_APB4ENR_RTCCAPBEN;
#else
    RCC->APB4ENR |= (1u << 16);            /* RTCCAPBEN，RM0433 */
#endif

    /* 自检：写读回路验证 BKP 通路（断电后首启 magic 不符则全清重建） */
    if (RTC->BKP0R != BB_MAGIC_VAL) {
        RTC->BKP1R = 0;  RTC->BKP2R = 0;  RTC->BKP3R = 0;
        RTC->BKP4R = 0;  RTC->BKP5R = 0;  RTC->BKP6R = 0;
        RTC->BKP7R = 0;  RTC->BKP8R = 0;  RTC->BKP9R = 0;
        RTC->BKP10R = 0; RTC->BKP11R = 0; RTC->BKP12R = 0;
        RTC->BKP13R = 0;
        RTC->BKP0R = BB_MAGIC_VAL;
    }
    RTC->BKP12R = 0xA55A1234u;
    RTC->BKP12R = (RTC->BKP12R == 0xA55A1234u) ? 1u : 0u;   /* 1=BKP 通路正常 */

    /* 转存上轮信息（先读后写，防覆盖） */
    old_boot     = RTC->BKP1R;
    s_prev_rsr   = RTC->BKP3R;
    s_prev_hb    = RTC->BKP4R;
    s_prev_loop  = RTC->BKP5R;
    s_prev_fault = (RTC->BKP13R == old_boot) ? RTC->BKP6R : 0u;

    s_boot_cnt = old_boot + 1u;
    RTC->BKP1R = s_boot_cnt;
    RTC->BKP3R = RCC->RSR;                 /* 本轮复位源（固件 console 稍后会清标志） */
    RTC->BKP11R = s_isr_feeds;             /* RAM 已清零，同步回 BKP */
    s_last_hb_write = 0;
}

void blackbox_loop_tick(void)
{
    s_loop_passes++;
}

void blackbox_heartbeat(uint32_t now_ms)
{
    if ((int32_t)(now_ms - s_last_hb_write) < 100) return;
    s_last_hb_write = now_ms;
    RTC->BKP4R = now_ms;
    RTC->BKP5R = s_loop_passes;
}

void blackbox_isr_feed(void)
{
    iwdg_refresh();                        /* 单寄存器写，ISR 安全 */
    s_isr_feeds++;
    RTC->BKP11R = s_isr_feeds;
}

void blackbox_fault(uint32_t marker, uint32_t pc, uint32_t lr,
                    uint32_t cfsr, uint32_t hfsr)
{
    RTC->BKP6R  = marker;
    RTC->BKP7R  = pc;
    RTC->BKP8R  = lr;
    RTC->BKP9R  = cfsr;
    RTC->BKP10R = hfsr;
    RTC->BKP13R = RTC->BKP1R;
    RTC->BKP4R  = HAL_GetTick();           /* 故障时刻 */
    RTC->BKP5R  = s_loop_passes;
}

/* ====== 页脚文本（纯 ASCII，8x16 字库）====== */
static char  s_buf[72];
static char *s_p;

static void put_ch(char c) { if (s_p < s_buf + sizeof(s_buf) - 1) *s_p++ = c; }
static void put_str(const char *s) { while (*s) put_ch(*s++); }
static void put_dec(uint32_t v)
{
    char t[10]; int n = 0;
    do { t[n++] = (char)('0' + v % 10u); v /= 10u; } while (v && n < 10);
    while (n) put_ch(t[--n]);
}
static void put_hex(uint32_t v, int width)
{
    static const char hx[] = "0123456789ABCDEF";
    for (int i = width - 1; i >= 0; i--) put_ch(hx[(v >> (i * 4)) & 0xFu]);
}

const char *blackbox_footer_text(uint32_t now_ms)
{
    s_p = s_buf;
    put_str("B");  put_dec(s_boot_cnt);
    put_str(" U"); put_dec(now_ms / 1000u); put_str("s");
    put_str(" L"); put_dec(s_loop_passes / 1000u); put_str("k");
    put_str(" I"); put_dec(s_isr_feeds);
    put_str(" F"); put_hex(s_prev_fault, 4);
    put_str(" R"); put_hex(s_prev_rsr, 8);
    put_str(" H"); put_dec(s_prev_hb / 1000u); put_str("s");
    *s_p = 0;
    return s_buf;
}
