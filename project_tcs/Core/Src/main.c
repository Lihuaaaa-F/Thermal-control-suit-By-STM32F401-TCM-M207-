/**
 * main.c — 调温服 STM32H743IIT6 主程序（project_tcs，任务书 §3）
 *
 * H743 = 唯一数据源（参数注册表），ESP32 = 纯桥 + Web 服务器，屏与网页皆为视图。
 *
 * 主循环 10ms 节拍（SysTick 1ms 分频），时序预算 ≤8ms/节拍（§3.2 铁律）：
 *   喂狗 → comm_poll_tx → console_poll → m207_poll → 链路 RX 喂 comm → comm_tick
 *   → tcs_lvgl_poll → control_poll(500ms) → tmp117_poll(100ms) → post_poll(100ms,H6)
 *   → param_persist_flush(2s) → STATUS 帧(500ms) → KEY0 急停 → LED 心跳 → 等下一节拍
 * ISR 只做：UART RX 单字节入环形缓冲（USART1 console / USART2 link / USART3 m207）。
 *
 * keypad 不初始化：KP_COL2=PD5 与 USART2_TX(ESP32) 冲突（接线表 #3），只编译不链接执行。
 */
#include "main.h"
#include "stm32h7xx_it.h"
#include "stm32h7xx_hal.h"
#include "segger_rtt.h"
#include "proto.h"
#include "comm.h"
#include "tcs_proto.h"
#include "ltdc.h"
#include "ft5446.h"
#include "param_registry.h"
#include "control.h"
#include "m207.h"
#include "tmp117.h"
#include "console.h"
#if TCS_USE_LVGL
#include "tcs_lvgl.h"
#endif
#include "fan.h"
#include "eeprom.h"
#include "iwdg.h"
#include "post.h"
#include <string.h>

/* ====== 全局句柄 ====== */
UART_HandleTypeDef huart_console;   /* USART1 PA9/PA10 控制台 */
UART_HandleTypeDef huart_link;      /* USART2 PD5/PA3 ESP32 链路 */
UART_HandleTypeDef huart_m207;      /* USART3 PB10/PB11 M207 */
I2C_HandleTypeDef  hi2c_tmp117;     /* I2C1 PB8/PB9 TMP117 */

/* ====== 时间基准（SysTick 1ms）====== */
static volatile uint32_t g_tick_ms = 0;
uint32_t millis(void) { return g_tick_ms; }

/* 主循环活性圈数（H2/wp_check 活性证据用；volatile 供 probe-rs 直读）*/
volatile uint32_t g_stat_loop_passes = 0;
/* WP-C.5 DWT 性能计数（us；ui_ms 为 ms×10 保留 1 位小数）*/
volatile uint32_t g_stat_work_us_max = 0;
volatile uint32_t g_stat_work_us_avg = 0;
volatile uint32_t g_stat_ui_ms_x10 = 0;
volatile uint32_t g_stat_touch_us_max = 0;
volatile uint32_t g_stat_tick_ovf = 0;

static void perf_sample_work(uint32_t cy)
{
    uint32_t us = cy / (SystemCoreClock / 1000000u);
    if (us > g_stat_work_us_max) g_stat_work_us_max = us;
    g_stat_work_us_avg = (g_stat_work_us_avg * 7u + us) / 8u;
    if (us > 10000u) g_stat_tick_ovf++;
}

/* SysTick 1ms 双递增覆盖（GS 工程移植时曾丢失本函数 → 主循环卡死在节拍等待，
 * IWDG 32.7s 复位循环；根因记录 docs/evidence/WP6_iwdg_defect.md） */
void HAL_IncTick(void)
{
    uwTick++;
    g_tick_ms++;
}

/* ====== 注册表节拍化下发状态（P3）====== */
static uint8_t s_dump_active;
static int     s_dump_idx;

/* ====== 链路实例 ====== */
comm_link_t g_link;

/* ====== 前向声明 ====== */
void on_frame(const uint8_t *payload, size_t len);
void link_send_ack(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret);
static size_t proto_encode_frame_with_mt(uint8_t mt, uint8_t seq,
                                         const uint8_t *body, size_t blen,
                                         uint8_t *out, size_t cap);

/* ====== 链路 RX 环（ISR 单字节入，主循环出喂 comm）====== */
static uint8_t s_link_it_byte;               /* HAL_UART_Receive_IT 落点 */
#define LINK_RX_SIZE  512u
#define LINK_RX_MASK  (LINK_RX_SIZE - 1u)
static volatile uint16_t s_lrx_head, s_lrx_tail;
static uint8_t s_lrx[LINK_RX_SIZE];

void link_rx_isr_byte(uint8_t b)
{
    uint16_t next = (uint16_t)((s_lrx_head + 1u) & LINK_RX_MASK);
    if (next == s_lrx_tail) return;
    s_lrx[s_lrx_head] = b;
    s_lrx_head = next;
}

void link_rx_rearm(void)
{
    HAL_UART_Receive_IT(&huart_link, &s_link_it_byte, 1);
}

/* HAL 完成回调转发（it.c）：必须推 s_link_it_byte——即 Receive_IT 的落点。
 * 2026-10-30 真机 bug：it.c 曾推另一个从未写入的静态字节，链路每字节恒 0x00。*/
void link_rx_cplt(void)
{
    link_rx_isr_byte(s_link_it_byte);
    link_rx_rearm();
}

/* TEMP-DIAG3（2026-10-30）：PA3 板载逻辑分析仪——临时切 GPIO 输入，
 * 记录最多 512 个边沿的 DWT 周期时间戳（50ms 窗口），供探针直读波形。
 * 结束后恢复 AF7+上拉并重启接收。*/
volatile uint32_t g_wave_ts[512];
volatile uint16_t g_wave_n;
volatile uint8_t  g_wave_lvl0;
volatile uint32_t g_wave_span;

static void link_rx_pin_af(void)
{
    GPIO_InitTypeDef gi = {0};
    gi.Pin = GPIO_PIN_3;
    gi.Mode = GPIO_MODE_AF_PP;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gi.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gi);
}

void link_wave_capture(void)
{
    GPIO_InitTypeDef gi = {0};
    gi.Pin = GPIO_PIN_3;
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    HAL_GPIO_Init(GPIOA, &gi);
    for (volatile int d = 0; d < 20000; d++) { }   /* 引脚切换稳定 */

    g_wave_n = 0;
    uint32_t last = (GPIOA->IDR >> 3) & 1u;
    g_wave_lvl0 = (uint8_t)last;
    uint32_t t0 = DWT->CYCCNT;
    uint32_t limit = SystemCoreClock / 4u;         /* 250 ms */
    while (g_wave_n < 512u) {
        uint32_t lv = (GPIOA->IDR >> 3) & 1u;
        if (lv != last) { g_wave_ts[g_wave_n++] = DWT->CYCCNT - t0; last = lv; }
        if ((DWT->CYCCNT - t0) > limit) break;
    }
    g_wave_span = DWT->CYCCNT - t0;

    link_rx_pin_af();
    link_rx_rearm();
}

void link_uart_error_recover(void)
{
    if (__HAL_UART_GET_FLAG(&huart_link, UART_FLAG_ORE))
        __HAL_UART_CLEAR_OREFLAG(&huart_link);
    HAL_UART_AbortReceive(&huart_link);
    link_rx_rearm();
}

/* TEMP-DIAG4（2026-10-30）：PD5(TX) 波形自证——中断发送 64B 0x55，
 * 同时以 GPIO 方式采样 PD5 边沿（须在起始后立即切输入，UART 仍会驱动
 * 已排队的最后几字节不成立，故改为：先 IT 发送，再立刻采样 8ms）。*/
volatile uint32_t g_txwave_n;
void link_tx_wave_capture(void)
{
    static uint8_t burst[64];
    for (int i = 0; i < 64; i++) burst[i] = 0x55;
    g_txwave_n = 0;
    HAL_UART_Transmit_IT(&huart_link, burst, sizeof(burst));
    /* 采样 PD5（AF 输出仍可通过 IDR 读回电平）*/
    uint32_t last = (GPIOD->IDR >> 5) & 1u;
    uint32_t t0 = DWT->CYCCNT;
    uint32_t limit = SystemCoreClock / 100u;   /* 10 ms */
    while ((g_txwave_n < 512u) && ((DWT->CYCCNT - t0) < limit)) {
        uint32_t lv = (GPIOD->IDR >> 5) & 1u;
        if (lv != last) { g_wave_ts[g_txwave_n++] = DWT->CYCCNT - t0; last = lv; }
    }
    uint32_t guard = SystemCoreClock / 100u;
    while (huart_link.gState != HAL_UART_STATE_READY && guard--) { }
}

/* TEMP-DIAG（2026-10-30）：屏链排障全局计数器（探针直读）*/
volatile uint32_t g_dbg_lrx_total;
volatile uint8_t  g_dbg_rx_ring[64];
volatile uint32_t g_dbg_rx_pos;

volatile uint32_t g_dbg_snap;
volatile uint32_t g_dbg_snap_gap_max;   /* 相邻 0x20 应用点最大间隔 ms（实时性）*/
volatile uint32_t g_dbg_snap_gap_last;  /* 最近一次 0x20 间隔 ms */
volatile uint32_t g_dbg_getreg;   /* 收到 GET_REGISTRY 次数 */
volatile uint32_t g_dbg_dumpdone; /* dump 完成次数 */



static void link_rx_drain(void)
{
    /* R2 真机自愈（2026-10-30）：USART2 首次 ORE 后 HAL 会关闭 RXNEIE
     * （CR1.RXNEIE=0），而错误回调未启用 → 链路 RX 永久失聪。
     * 与 console_poll 同款轮询自愈，但**限速 100ms**——持续 ORE 风暴
     * （如 RX 接线悬空/悬低）时逐拍 AbortReceive 会拖死主循环（真机实证）。*/
    {
        static uint32_t s_last_heal;
        uint32_t nowms = g_tick_ms;
        if ((__HAL_UART_GET_FLAG(&huart_link, UART_FLAG_ORE) ||
             (huart_link.Instance->CR1 & USART_CR1_RXNEIE_RXFNEIE) == 0u) &&
            (uint32_t)(nowms - s_last_heal) >= 100u) {
            s_last_heal = nowms;
            link_uart_error_recover();
        }
    }

    uint16_t guard = LINK_RX_SIZE + 2u;
    while (guard--) {
        if (s_lrx_head == s_lrx_tail) break;
        uint8_t b = s_lrx[s_lrx_tail];
        s_lrx_tail = (uint16_t)((s_lrx_tail + 1u) & LINK_RX_MASK);
        g_dbg_lrx_total++;   /* 链路 RX 字节总量（屏链诊断/ltx 自检核对用）*/
        g_dbg_rx_ring[g_dbg_rx_pos & 63u] = b;
        g_dbg_rx_pos++;
        /* TEMP-DIAG2（2026-10-30）：原始首 8 字节 + 首 0xAA 帧 32 字节取证 */
        {
            static uint8_t raw8[8]; static int rn; static int rdone;
            static uint8_t win[32]; static int wl; static int wdone;
            if (!rdone) {
                raw8[rn++] = b;
                if (rn == 8) { rdone = 1;
                    tcs_log("[R8]", "%02x %02x %02x %02x %02x %02x %02x %02x",
                            raw8[0],raw8[1],raw8[2],raw8[3],raw8[4],raw8[5],raw8[6],raw8[7]); }
            }
            if (!wdone) {
                if (b == 0xAA && wl == 0) { win[wl++] = b; }
                else if (wl > 0 && wl < 32) { win[wl++] = b; }
                if (wl == 32) { wdone = 1;
                    tcs_log("[A1]", "%02x %02x %02x %02x %02x %02x %02x %02x", win[0],win[1],win[2],win[3],win[4],win[5],win[6],win[7]);
                    tcs_log("[A2]", "%02x %02x %02x %02x %02x %02x %02x %02x", win[8],win[9],win[10],win[11],win[12],win[13],win[14],win[15]);
                    tcs_log("[A3]", "%02x %02x %02x %02x %02x %02x %02x %02x", win[16],win[17],win[18],win[19],win[20],win[21],win[22],win[23]);
                    tcs_log("[A4]", "%02x %02x %02x %02x %02x %02x %02x %02x", win[24],win[25],win[26],win[27],win[28],win[29],win[30],win[31]);
                }
            }
        }

        const uint8_t *pl; size_t plen; int dup;
        if (comm_rx_feed_byte(&g_link, b, &pl, &plen, &dup)) {
            on_frame(pl, plen);
        } else if (dup) {
            uint8_t ds, dm;
            if (comm_last_dup_info(&g_link, &ds, &dm))
                link_send_ack(ds, dm, 0, 0.0f);
        }
    }
}

/* ====== 临界区 + UART 发送钩子（comm 层注入）====== */
static void crit_enter(void) { __disable_irq(); }
static void crit_exit(void)  { __enable_irq(); }

static void link_uart_send(const uint8_t *data, size_t len)
{
    HAL_UART_Transmit(&huart_link, (uint8_t *)data, (uint16_t)len, 20u);
}

static uint32_t now_ms_fn(void) { return g_tick_ms; }

static volatile uint8_t g_link_down_flag = 0;
static void on_link_down(void) { g_link_down_flag = 1; }

/* ====== 0x90 TCS_ACK 发送（txq 入队，主循环串行消费）====== */
void link_send_ack(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret)
{
    uint8_t pl[TCS_ACK_PL_LEN];
    tcs_ack_pl_t a = { acked_seq, acked_mt, status, ret };
    size_t n = tcs_pl_ack(pl, sizeof(pl), &a);
    uint8_t frame[MAX_FRAME];
    uint8_t seq = comm_next_seq(&g_link);
    size_t fn = proto_encode_frame_with_mt(MT_TCS_ACK, seq, pl, n, frame, sizeof(frame));
    if (fn) comm_txq_push(&g_link, frame, fn);
}

/* proto_encode_frame 只收裸 payload；这里组 "MT+Seq+payload" 再封装 */
static size_t proto_encode_frame_with_mt(uint8_t mt, uint8_t seq,
                                         const uint8_t *body, size_t blen,
                                         uint8_t *out, size_t cap)
{
    uint8_t payload[MAX_PAYLOAD];
    if (blen + 2 > MAX_PAYLOAD) return 0;
    payload[0] = mt;
    payload[1] = seq;
    memcpy(&payload[2], body, blen);
    return proto_encode_frame(payload, blen + 2, out, cap);
}

/* ====== 参数副作用钩子（param → 屏脏区/链路/M207/EEPROM）====== */
static void hook_broadcast(param_desc_t *p, uint8_t src)
{
    (void)src;
    /* 屏：LVGL 单栈 UI 是"拉"模型——tui_p0/p1_refresh 每 500ms 用 param_get/find 重读
     * （M4 ≤1s 同步预算内），不经过 dirty 标志（M05-F-10 已删该死字段）；
     * 链路：0x11 TCS_PARAM 帧（变更即推）*/
    uint8_t pl[TCS_PARAM_PL_LEN];
    tcs_param_pl_t pp = {
        .id = p->id, .type = p->type,
        .value = (p->type == PARAM_TYPE_F32) ? tcs_f2b(p->value) : (uint32_t)p->value,
    };
    size_t n = tcs_pl_param(pl, sizeof(pl), &pp);
    uint8_t frame[MAX_FRAME];
    size_t fn = proto_encode_frame_with_mt(MT_TCS_PARAM, comm_next_seq(&g_link),
                                           pl, n, frame, sizeof(frame));
    if (fn) comm_txq_push(&g_link, frame, fn);
}

/* ====== R2（v11.3 链路升级+协议换向）：快照接收状态 ======
 * 屏=唯一操作载体：显示全部取自 ESP32 权威快照（0x20/0x21）；
 * esp_link_ok=快照新鲜度（500ms 窗，v2.4 §13.4.6 销号）。*/
/* X01-F-02：报警位按"谁能产生"分区。对端快照到达时本机独占位必须保留。*/
#define ALM_SRC_LOCAL_H743   ((uint16_t)ALM_EEPROM)

static uint32_t s_snap_ms;      /* 最近 0x20 到达时刻（ms）*/
static uint32_t s_snap_seq;     /* 最近快照 seq（去重键）*/
static uint8_t  s_snap_have;
static tcs_snap_pl_t s_last_snap;   /* v2：屏为纯显示终端，链路存活期内以它为准 */

/* 快照/增量应用：显示字段直写 g_tcs；设定值经 param_link_sync 静默回灌本地注册表
 * （不触发广播/下发，防 0x11/0x12 成环）。*/
static void snap_apply(const tcs_snap_pl_t *s)
{
    g_tcs.tc1_act = s->tc1_act;
    g_tcs.tc2_act = s->tc2_act;
    g_tcs.body_temp = s->body;
    g_tcs.tec1_on = s->tec1; g_tcs.tec2_on = s->tec2; g_tcs.fan_on = s->fan;
    g_tcs.mode = s->mode;
    /* X01-F-02：整体覆盖会把"H743 独有的报警位"（ALM_EEPROM：参数持久化/镜像
     * 校验失败，只有 H743 能置）在 200ms 内静默抹掉，且绕过 ack。这里只接受
     * 对端负责的位，本机位保留。*/
    g_tcs.alarm_bits = (uint16_t)((s->alarm & (uint16_t)~ALM_SRC_LOCAL_H743)
                                  | (g_tcs.alarm_bits & ALM_SRC_LOCAL_H743));
    g_tcs.m207_online = (s->online & TCS_ONLINE_M207) ? 1u : 0u;
    g_tcs.body_sensor_ok = (s->online & TCS_ONLINE_BODY_SENSOR) ? 1u : 0u;
    param_link_sync(PARAM_ID_TC1_SET, s->tc1_set);
    param_link_sync(PARAM_ID_TC2_SET, s->tc2_set);
    s_last_snap = *s;
}

/* v2（H743=纯显示终端）：本地 control_poll/tmp117_poll/post_poll 会写 g_tcs 的
 * fan/alarm/body_sensor_ok 等字段，把链路快照值覆盖成 H743 本机（无外设）状态。
 * 链路 1s 存活期内重放最后快照，保证屏显 = ESP32/M207 真值。
 * 2026-10-30 真机实证：fan=1(ESP32 0%)、alarm=0x0001(ESP32 0x0000)、sensor=0。*/
static void snap_reassert(void)
{
    if (!s_snap_have) return;
    if ((uint32_t)(g_tick_ms - s_snap_ms) > 1000u) return;   /* 断链不重放旧值 */
    snap_apply(&s_last_snap);
    /* 本地急停自持位不得被远端快照抹掉（屏幕按下到 ESP32 回传 ALM_ESTOP 之间的窗口，
     * 以及断链期间）。2026-10-30 真机：急停后 alarm 被快照清成 0x0000。
     * R1 复核 R-03：超温同理——本地自持位也要在快照覆盖后补回来，否则
     * ESP32 还没跑到超温那一拍时，H743 的 SAFE 会在无 ack 的情况下自动退出。*/
    if (control_estop_latched())    g_tcs.alarm_bits |= ALM_ESTOP;
    if (control_overtemp_latched()) g_tcs.alarm_bits |= ALM_OVERTEMP;
    if (tcs_alarm_active(ALM_EEPROM)) g_tcs.alarm_bits |= ALM_EEPROM;
}

/* R2：屏侧写 M207 温度参数 → 链路 0x12 发 ESP32（权威→M207 verify）；
 * 链路未判活时退回本地旧路径（v2 接线后不可达，仅作降级痕迹）。*/
static uint8_t m207_redirect_send(uint16_t id, float v)
{
    /* 写路径不得以"读新鲜度(esp_link_ok)"为门：该标志按 500ms 快照窗判定，
     * 主循环被长时间占用（整页重绘/调试探针暂停）时会瞬时为 0，导致屏幕设参
     * 被静默丢弃并退回本机（v2 下 H743 无 M207）。
     * 2026-10-30 真机实证：p set 返回 ok 但 M207/ESP32 均为旧值。
     * 现在无条件入队：comm 层自带 ARQ×3 + 500ms 超时，链路死时由 txq 自然丢弃。*/
    uint8_t pl[TCS_PARAM_PL_LEN];
    tcs_param_pl_t pp = { .id = id, .type = PARAM_TYPE_F32, .value = tcs_f2b(v) };
    size_t n = tcs_pl_param(pl, sizeof(pl), &pp);
    if (!n) return 0;
    uint8_t frame[MAX_FRAME];
    size_t fn = proto_encode_frame_with_mt(MT_TCS_SET_PARAM, comm_next_seq(&g_link),
                                           pl, n, frame, sizeof(frame));
    if (!fn) return 0;
    return (comm_send(&g_link, frame, fn, MT_TCS_SET_PARAM) != 0xFFu) ? 1u : 0u;
}

/* v2：本地急停/确认 → 链路 0x15（ESP32 执行 TEC/风扇），ARQ 保证送达 */
static void link_send_cmd_upstream(uint8_t cmd)
{
    uint8_t pl[TCS_CMD_PL_LEN];
    pl[0] = cmd;
    uint8_t frame[MAX_FRAME];
    size_t fn = proto_encode_frame_with_mt(MT_TCS_CMD, comm_next_seq(&g_link),
                                           pl, TCS_CMD_PL_LEN, frame, sizeof(frame));
    if (fn) (void)comm_send(&g_link, frame, fn, MT_TCS_CMD);
}

static void hook_m207_set(uint8_t ch, float v)
{
    /* R2：经链路（ESP32 注册表→M207 verify）；断链才走旧本地下发 */
    if (m207_redirect_send(ch == 1 ? PARAM_ID_TC1_SET : PARAM_ID_TC2_SET, v)) return;
    (void)m207_set_temp(ch, v);
}
static void hook_m207_map_set(uint16_t pid, float v)
{
    if (m207_redirect_send(pid, v)) return;
    (void)m207_map_set(pid, v);
}
static void hook_mode_change(uint8_t m)        { control_on_mode_change(m); }
static int  hook_eeprom_commit(const uint8_t *img) { return eeprom_commit_image(img); }
static void ops_tec_sw(uint8_t ch, uint8_t on)   { (void)m207_tec(ch, on); }
static void ops_tec_temp(uint8_t ch, float v)    { (void)m207_set_temp(ch, v); }

/* ====== 链路帧分发（主循环上下文）====== */
void on_frame(const uint8_t *payload, size_t len)
{
    if (len < 2) return;
    uint8_t mt  = payload[0];
    uint8_t seq = payload[1];
    const uint8_t *body = &payload[2];
    size_t blen = len - 2;

    switch (mt) {
    case MT_TCS_PARAM: {             /* v2 补口：ESP32 权威 → 屏镜像（反向 0x11）
                                      * 网页/控制台改的非设定值参数在这里静默落表；
                                      * param_link_sync 不广播，故不会反向成环。*/
        tcs_param_pl_t pp;
        if (tcs_scan_param(body, blen, &pp) == 0) {
            float v = (pp.type == PARAM_TYPE_F32) ? tcs_b2f(pp.value) : (float)pp.value;
            param_link_sync(pp.id, v);
            tcs_log("[NET]", "mirror id=0x%02x v=%.2f", pp.id, (double)v);
        }
        break;
    }
    case MT_TCS_SET_PARAM: {
        tcs_param_pl_t pp;
        if (tcs_scan_param(body, blen, &pp) != 0) {
            link_send_ack(seq, mt, TCS_ACK_BAD_ID, 0.0f);
            break;
        }
        float v = (pp.type == PARAM_TYPE_F32) ? tcs_b2f(pp.value) : (float)pp.value;
        int rv = param_set(pp.id, v, PARAM_SRC_WEB);
        uint8_t st = TCS_ACK_OK;
        if (rv == 1) st = TCS_ACK_CLAMP;
        else if (rv < 0) st = (rv == -1) ? TCS_ACK_BAD_ID : TCS_ACK_BUSY;
        link_send_ack(seq, mt, st, param_get(pp.id));
        tcs_log("[NET]", "set id=0x%02x v=%.2f rv=%d", pp.id, (double)v, rv);
        break;
    }
    case MT_TCS_GET_REGISTRY: {
        /* 注册表全量 → 0x14 帧 ×N。v1.2 H10 全表 51 条（0x127 已入表） > txq 16 深 → 改节拍化
         * 下发（registry_dump_tick 每拍 2 帧，50 条≈250ms，前端零感知）。*/
        link_send_ack(seq, mt, TCS_ACK_OK, 0.0f);
        g_dbg_getreg++;
        s_dump_idx = 0;
        s_dump_active = 1;
        break;
    }
    case MT_TCS_SNAPSHOT: {          /* R2：200ms 全量快照 = 屏唯一读源 */
        g_dbg_snap++;
        tcs_snap_pl_t s;
        if (tcs_scan_snapshot(body, blen, &s) != 0) break;
        if (s.ver != TCS_PROTO_VER) break;
        /* 去重只认 seq 相等：UART 点对点链路保序，不存在乱序帧；
         * 单调递增判据会在对端重启（seq 归 1）后把之后所有快照当旧帧丢弃
         * ——2026-10-30 真机实证：s_snap_ms 冻结、esp_link_ok 恒 0、屏数据停更。*/
        if (s_snap_have && s.seq == s_snap_seq) break;
        if (!s_snap_have) {
            tcs_log("[NET]", "SNAP first seq=%u m207=%u tc1=%.1f tc2=%.1f",
                    (unsigned)s.seq, (unsigned)((s.online & TCS_ONLINE_M207) ? 1u : 0u),
                    (double)s.tc1_act, (double)s.tc2_act);
        }
        {
            uint32_t nowm = millis();
            if (s_snap_have) {
                uint32_t gp = (uint32_t)(nowm - s_snap_ms);
                g_dbg_snap_gap_last = gp;
                if (gp > g_dbg_snap_gap_max) g_dbg_snap_gap_max = gp;
            }
            s_snap_ms = nowm;
        }
        s_snap_have = 1;
        snap_apply(&s);
        break;
    }
    case MT_TCS_DELTA: {             /* R2：位图增量（设定值/teс/fan/mode/alarm/body/badge）*/
        tcs_delta_src_t d = {0};
        uint16_t mask = 0;
        if (tcs_scan_delta(body, blen, &d, &mask) != 0) break;
        if (mask & TCS_D_TC1_SET) param_link_sync(PARAM_ID_TC1_SET, d.tc1_set);
        if (mask & TCS_D_TC2_SET) param_link_sync(PARAM_ID_TC2_SET, d.tc2_set);
        if (mask & TCS_D_TEC) { g_tcs.tec1_on = (uint8_t)(d.tec & 1u);
                                g_tcs.tec2_on = (uint8_t)((d.tec >> 1) & 1u); }
        if (mask & TCS_D_FAN)     g_tcs.fan_on = d.fan;
        if (mask & TCS_D_MODE)  { g_tcs.mode = d.mode;
                                  param_link_sync(PARAM_ID_MODE, (float)d.mode); }
        if (mask & TCS_D_ALARM)   /* X01-F-02：与 0x20 同规——本机独占位不被覆盖 */
            g_tcs.alarm_bits = (uint16_t)((d.alarm & (uint16_t)~ALM_SRC_LOCAL_H743)
                                          | (g_tcs.alarm_bits & ALM_SRC_LOCAL_H743));
        if (mask & TCS_D_BODY)    g_tcs.body_temp = d.body;
        break;
    }
    case MT_TCS_CMD: {
        if (blen < 1) break;
        uint8_t cmd = body[0];
        uint8_t st = TCS_ACK_OK;
        switch (cmd) {
        case TCS_CMD_TEC1_ON:    control_manual_tec(1, 1); break;
        case TCS_CMD_TEC1_OFF:   control_manual_tec(1, 0); break;
        case TCS_CMD_TEC2_ON:    control_manual_tec(2, 1); break;
        case TCS_CMD_TEC2_OFF:   control_manual_tec(2, 0); break;
        case TCS_CMD_FAN_TOGGLE: control_manual_fan(g_tcs.fan_on ? 0u : 1u); break;
        case TCS_CMD_MODE_AUTO:   (void)param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB); break;
        case TCS_CMD_MODE_MANUAL: (void)param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_WEB); break;
        case TCS_CMD_ESTOP:      tcs_emergency_stop(); break;
        case TCS_CMD_ALARM_ACK:  tcs_alarm_ack(); break;
        case TCS_CMD_M207_SAVE:
            /* P3：保存到模块。R5（ESP32）接 M207 `!` 传输+逐参可存性；本册=帧序打通 */
            tcs_log("[NET]", "m207 save-all requested (R5 transport)");
            break;
        default: st = TCS_ACK_BAD_ID; break;
        }
        link_send_ack(seq, mt, st, 0.0f);
        tcs_log("[NET]", "cmd=%u st=%u", cmd, st);
        break;
    }
    default:
        break;   /* 0x10/0x11 不会从 ESP 发来；0x90 已被 comm 层消费 */
    }
}

/* ====== 注册表节拍化下发（P3：H10 全表 51 条（0x127 已入表），2 帧/拍防 txq 溢出）====== */
static void registry_dump_tick(void)
{
    if (!s_dump_active) return;
    int n = param_count();
    for (int k = 0; k < 1 && s_dump_idx < n; k++, s_dump_idx++) {   /* R1 P1-01：1 帧/拍，与 comm 消费速率相等 */
        const param_desc_t *p = param_at(s_dump_idx);
        tcs_reg_entry_t e;
        memset(&e, 0, sizeof(e));
        e.count = (uint8_t)n;
        e.idx   = (uint8_t)s_dump_idx;
        e.id = p->id; e.type = p->type; e.flags = p->flags;
        e.min = p->min; e.max = p->max; e.step = p->step;
        strncpy(e.key,  p->key,  sizeof(e.key) - 1);
        strncpy(e.name, p->name, sizeof(e.name) - 1);
        strncpy(e.unit, p->unit, sizeof(e.unit) - 1);
        uint8_t pl[250];
        size_t pn = tcs_pl_registry_entry(pl, sizeof(pl), &e);
        if (pn) {
            uint8_t frame[MAX_FRAME];
            size_t fn = proto_encode_frame_with_mt(MT_TCS_REGISTRY_ENTRY,
                                                   comm_next_seq(&g_link),
                                                   pl, pn, frame, sizeof(frame));
            if (fn) comm_txq_push(&g_link, frame, fn);
        }
    }
    if (s_dump_idx >= n) { s_dump_active = 0; g_dbg_dumpdone++; }
}

/* ====== 500ms STATUS 帧 ====== */
static uint32_t s_status_seq = 0;
static void status_tx(uint32_t now)
{
    static uint32_t last = 0;
    if (now - last < 500u) return;
    last = now;
    tcs_status_pl_t s = {
        .ver = TCS_PROTO_VER,
        .seq = ++s_status_seq,
        .tc1_act = g_tcs.tc1_act, .tc2_act = g_tcs.tc2_act, .body = g_tcs.body_temp,
        .tec1 = g_tcs.tec1_on, .tec2 = g_tcs.tec2_on, .fan = g_tcs.fan_on,
        .mode = g_tcs.mode, .alarm = g_tcs.alarm_bits,
        .online = (uint8_t)((g_tcs.m207_online ? TCS_ONLINE_M207 : 0)
                          | (g_tcs.body_sensor_ok ? TCS_ONLINE_BODY_SENSOR : 0)
                          | (g_tcs.esp_link_ok ? TCS_ONLINE_ESP_LINK : 0)),
        .rfu = 0,
    };
    uint8_t pl[TCS_STATUS_PL_LEN];
    size_t n = tcs_pl_status(pl, sizeof(pl), &s);
    uint8_t frame[MAX_FRAME];
    size_t fn = proto_encode_frame_with_mt(MT_TCS_STATUS, comm_next_seq(&g_link),
                                           pl, n, frame, sizeof(frame));
    if (fn) comm_txq_push(&g_link, frame, fn);
}

/* ====== 时钟：HSE 25MHz → 480MHz + PLL2(FMC 220M)（沿用 GS 实测配置）====== */
static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    if (HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY) != HAL_OK) Error_Handler();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

    /* PLL1: 25/5×192/2 = 480MHz；Q=/4=240M 给 SDMMC */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE | RCC_OSCILLATORTYPE_LSI;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.LSIState = RCC_LSI_ON;     /* IWDG 用 LSI */
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 5;
    RCC_OscInitStruct.PLL.PLLN = 192;
    RCC_OscInitStruct.PLL.PLLP = 2;
    RCC_OscInitStruct.PLL.PLLQ = 4;
    RCC_OscInitStruct.PLL.PLLR = 2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK
                                | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2
                                | RCC_CLOCKTYPE_D3PCLK1 | RCC_CLOCKTYPE_D1PCLK1;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;     /* HCLK=240 */
    RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;    /* APB=120 */
    RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
    RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
    RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) Error_Handler();

    /* PLL2 → FMC 220M（SDRAM kernel clock）*/
    RCC_PeriphCLKInitTypeDef pclk = {0};
    pclk.PeriphClockSelection = RCC_PERIPHCLK_FMC;
    pclk.PLL2.PLL2M = 25;
    pclk.PLL2.PLL2N = 440;
    pclk.PLL2.PLL2P = 2;
    pclk.PLL2.PLL2R = 2;
    pclk.PLL2.PLL2RGE = RCC_PLL2VCIRANGE_0;
    pclk.PLL2.PLL2VCOSEL = RCC_PLL2VCOWIDE;
    pclk.PLL2.PLL2FRACN = 0;
    pclk.FmcClockSelection = RCC_FMCCLKSOURCE_PLL2;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk) != HAL_OK) Error_Handler();
    /* PLL3 在 ltdc_init 内配（LTDC PCLK=50M）*/
}

/* ====== GPIO：LED + KEY0 ====== */
static void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef gi = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    gi.Pin = LED1_Pin | LED2_Pin;
    gi.Mode = GPIO_MODE_OUTPUT_PP;
    gi.Pull = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gi);
    HAL_GPIO_WritePin(GPIOB, LED1_Pin | LED2_Pin, GPIO_PIN_SET);

    gi.Pin = KILL_KEY_Pin;
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(GPIOA, &gi);
}

/* ====== UART 通用初始化 ====== */
static void uart_gpio_af_pull(GPIO_TypeDef *port, uint32_t pins, uint32_t af, uint32_t pull);
static void uart_gpio_af( GPIO_TypeDef *port, uint32_t pins, uint32_t af)
{
    uart_gpio_af_pull(port, pins, af, GPIO_NOPULL);
}

/* RX 侧须上拉（UART 空闲电平=高）：断线/浮空时不产生无休止 framing 错误流 */
static void uart_gpio_af_pull(GPIO_TypeDef *port, uint32_t pins, uint32_t af, uint32_t pull)
{
    GPIO_InitTypeDef gi = {0};
    gi.Pin = pins;
    gi.Mode = GPIO_MODE_AF_PP;
    gi.Pull = pull;
    gi.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gi.Alternate = af;
    HAL_GPIO_Init(port, &gi);
}

static void uart_common_init(UART_HandleTypeDef *h, USART_TypeDef *inst, uint32_t baud,
                             IRQn_Type irq)
{
    h->Instance = inst;
    h->Init.BaudRate = baud;
    h->Init.WordLength = UART_WORDLENGTH_8B;
    h->Init.StopBits = UART_STOPBITS_1;
    h->Init.Parity = UART_PARITY_NONE;
    h->Init.Mode = UART_MODE_TX_RX;
    h->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    h->Init.OverSampling = UART_OVERSAMPLING_16;
    h->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    h->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    if (HAL_UART_Init(h) != HAL_OK) Error_Handler();
    /* FIFO 模式（GS 同款）：16B 容忍启动期线路扰动，防 ORE 打死 RX */
    if (HAL_UARTEx_SetTxFifoThreshold(h, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_SetRxFifoThreshold(h, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK) Error_Handler();
    if (HAL_UARTEx_EnableFifoMode(h) != HAL_OK) Error_Handler();
    HAL_NVIC_SetPriority(irq, 6, 0);
    HAL_NVIC_EnableIRQ(irq);
}

static void MX_UART_Init(void)
{
    /* USART1 PA9/PA10 AF7 控制台 */
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    uart_gpio_af    (GPIOA, GPIO_PIN_9,  GPIO_AF7_USART1);
    uart_gpio_af_pull(GPIOA, GPIO_PIN_10, GPIO_AF7_USART1, GPIO_PULLUP);
    uart_common_init(&huart_console, USART1, CONSOLE_BAUD, USART1_IRQn);

    /* USART2 PD5(TX) AF7 ESP32 链路（接线表 #3）；
     * RX 缺省 PA3（#4）；-DLINK_RX_PD6 切 PD6（USART2_RX 备选映射，2026-10-30
     * 屏链排障用：若 PA3 线不可达/不可靠可改接 PD6 免改更多代码）*/
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    uart_gpio_af(GPIOD, GPIO_PIN_5, GPIO_AF7_USART2);
#ifdef LINK_RX_PD6
    uart_gpio_af_pull(GPIOD, GPIO_PIN_6, GPIO_AF7_USART2, GPIO_PULLUP);
#else
    uart_gpio_af_pull(GPIOA, GPIO_PIN_3, GPIO_AF7_USART2, GPIO_PULLUP);
#endif
    uart_common_init(&huart_link, USART2, LINK_BAUD, USART2_IRQn);

    /* USART3 PB10/PB11 AF7 M207（接线表 #1/#2）*/
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    uart_gpio_af    (GPIOB, GPIO_PIN_10, GPIO_AF7_USART3);
    uart_gpio_af_pull(GPIOB, GPIO_PIN_11, GPIO_AF7_USART3, GPIO_PULLUP);
    uart_common_init(&huart_m207, USART3, M207_BAUD, USART3_IRQn);
}

/* ====== I2C1 PB8/PB9 AF4 @100kHz（TMP117，接线表 #5/#6）====== */
void HAL_I2C_MspInit(I2C_HandleTypeDef *hi2c)
{
    if (hi2c->Instance == I2C1) {
        __HAL_RCC_I2C1_CLK_ENABLE();
        __HAL_RCC_GPIOB_CLK_ENABLE();
        GPIO_InitTypeDef gi = {0};
        gi.Pin = GPIO_PIN_8 | GPIO_PIN_9;
        gi.Mode = GPIO_MODE_AF_OD;
        gi.Pull = GPIO_PULLUP;          /* 板上无上拉 → 飞线 4.7kΩ 外接；内部上拉兜底 */
        gi.Speed = GPIO_SPEED_FREQ_LOW;
        gi.Alternate = GPIO_AF4_I2C1;
        HAL_GPIO_Init(GPIOB, &gi);
    }
}

static void MX_I2C1_Init(void)
{
    hi2c_tmp117.Instance = I2C1;
    hi2c_tmp117.Init.Timing = 0x10C0ECFF;   /* 100kHz @ kernel 120M（CubeMX 常用值）*/
    hi2c_tmp117.Init.OwnAddress1 = 0;
    hi2c_tmp117.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c_tmp117.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c_tmp117.Init.OwnAddress2 = 0;
    hi2c_tmp117.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
    hi2c_tmp117.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c_tmp117.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c_tmp117) != HAL_OK) Error_Handler();
    HAL_NVIC_SetPriority(I2C1_EV_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(I2C1_EV_IRQn);
}

void Error_Handler(void)
{
    __disable_irq();
    SEGGER_RTT_WriteString("[ERROR] ERROR_HANDLER entered (WP-C: blackbox removed)\n");
    while (1) {
        HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin);
        for (volatile int i = 0; i < 1000000; i++) {}
    }
}

/* ====== KEY0(PA1) 急停键轮询（板载，按下=高，去抖 ~10ms）====== */
static void kill_key_poll(void)
{
    static uint8_t prev = 0, cand = 0, cnt = 0;
    static uint32_t last = 0;
    if (g_tick_ms - last < 5) return;
    last = g_tick_ms;
    uint8_t now = (HAL_GPIO_ReadPin(KILL_KEY_GPIO_Port, KILL_KEY_Pin) == GPIO_PIN_SET) ? 1 : 0;
    if (now == cand) {
        if (cnt < 0xFF) cnt++;
        if (cnt >= 2 && now == 1 && prev == 0) {
            prev = 1;
            tcs_emergency_stop();
            tcs_log("[APP]", "KEY0 estop");
        }
    } else {
        cand = now;
        cnt = 0;
        if (now == 0) prev = 0;
    }
}

/* ====== main ====== */
int main(void)
{
    HAL_Init();
    SEGGER_RTT_Init();

    SystemClock_Config();
    MX_GPIO_Init();
    MX_UART_Init();
    MX_I2C1_Init();

    console_init(&huart_console);
    console_print_reset_source();
    tcs_log("[SYSTEM]", "boot project_tcs fw=v1.2.0 HCLK=%lu MHz",
            (unsigned long)(SystemCoreClock / 1000000U));

    iwdg_init();   /* ~2s */
    /* H3 发现的总线怪象根因：探针会话挂起 CPU 时 IWDG 继续计数 → 2s 打狗复位
     * （EEPROM/触摸位 bang NAK、丢保存、M207 瞬断、wp_check 计数跳变同源）。
     * 调试停顿期冻结 IWDG（仅 halt 时生效，现场运行不受影响）。 */
    DBGMCU->APB4FZ1 |= DBGMCU_APB4FZ1_DBG_IWDG1;

    /* 显示（失败不阻塞协议/控制）*/
#ifdef ENABLE_LCD
    int lcd_rv = ltdc_init();
    tcs_log("[SYSTEM]", "ltdc init rv=%d ready=%d", lcd_rv, lcd_ready());
#endif
    int tp_rv = ft5446_init();
    tcs_log("[SYSTEM]", "ft5446 init rv=%d ready=%d", tp_rv, touch_ready());
    /* keypad 有意不初始化：KP_COL2=PD5 归 USART2_TX（接线表 #3），文件仅保留编译 */

    /* EEPROM → 参数注册表（离线/CRC 坏不阻塞，默认值 + ALM_EEPROM）*/
    int ee_rv = eeprom_init();
    uint8_t ee_cfg_invalid = 0;
    param_registry_init(&(param_hooks_t){
        .on_broadcast   = hook_broadcast,
        .on_m207_set    = hook_m207_set,
        .on_m207_map_set= hook_m207_map_set,
        .on_mode_change = hook_mode_change,
        .eeprom_commit  = hook_eeprom_commit,
    });
    /* v2：急停/报警确认必须同步到 ESP32（TEC/风扇执行端）*/
    control_set_cmd_hook(link_send_cmd_upstream);
    if (ee_rv == 0) {
        eeprom_selftest();
        uint8_t img[EE_IMAGE_SIZE];
        if (eeprom_read_image(img) == 0) {
            if (param_deserialize(img) != 0) {
                ee_cfg_invalid = 1;   /* control_init 会清报警位，标志后置（时序 bug 修复）*/
                tcs_log("[WARN]", "eeprom cfg invalid -> defaults");
            } else {
                tcs_log("[SYSTEM]", "eeprom params loaded");
            }
        }
    } else {
        ee_cfg_invalid = 1;
        tcs_log("[WARN]", "eeprom offline -> RAM only");
    }

    /* 安全控制 + 外设桥 */
    control_init(&(control_ops_t){
        .fan_set  = fan_set,
        .tec_sw   = ops_tec_sw,
        .tec_temp = ops_tec_temp,
        .on_state = NULL,
    });
    if (ee_cfg_invalid) tcs_alarm_raise(ALM_EEPROM);   /* control_init 之后置位才不被清 */
    m207_init(&huart_m207);
    tmp117_init(&hi2c_tmp117);
    fan_init();

    comm_init(&g_link, link_uart_send, now_ms_fn, on_link_down, crit_enter, crit_exit);

#ifdef ENABLE_LCD
    tcs_lvgl_init();     /* LVGL 单栈（2026-11-15：旧 tcs_ui 已删除） */
#endif
    post_init(ee_cfg_invalid);   /* H6：六项自检（M207 项宽限窗内异步裁决）*/
    tcs_log("[SYSTEM]", "init done, entering main loop");

    uint32_t last_tick = 0;
    uint8_t led = 0;
    static uint32_t last_led_ms = 0;

    /* WP-C.5：DWT 周期计数器使能（每拍工作/UI 重绘/触摸轮询实测）*/
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    while (1) {
        uint32_t now = g_tick_ms;
        uint32_t cy0 = DWT->CYCCNT;
        g_stat_loop_passes++;

        iwdg_refresh();
        comm_poll_tx(&g_link);           /* 链路 TX 串行消费（唯一 HAL_UART_Transmit 处之一）*/
        console_poll();
        m207_poll(now);
        registry_dump_tick();    /* P3：注册表全量节拍化下发 */
        /* v2 架构：M207 挂在 ESP32 侧，H743 不再有本地 M207。
         * 实际水温唯一来源 = 0x20 快照（snap_apply 已写入 g_tcs）。
         * 2026-10-30 真机 bug：此处用本地 m207_last_temp 覆盖，屏上水温恒 0。*/
        link_rx_drain();
        comm_tick(&g_link);              /* 重传检查（主循环上下文）*/

#ifdef ENABLE_LCD
        tcs_lvgl_estop_watch();   /* D-L7② 屏入口兜底：先于 LVGL 的原始坐标判定 */
        tcs_lvgl_poll();
#endif
        control_poll(now);
        tmp117_poll(now);
        post_poll(now);          /* H6：M207 宽限裁决 + 角标 + 布防三态机 100ms */
        param_persist_flush(now);
        kill_key_poll();
        snap_reassert();         /* v2：本地轮询后回灌 ESP32 快照（屏显一致性）*/
        /* R1 修复 P1（M01-F-02）：status_tx 必须在 snap_reassert 之后——原来先打包 0x10，
         * 会把 H743 本机 body_sensor_ok=0（v2 无 TMP117）发给 ESP32，网页误报传感器离线。*/
        status_tx(now);

        /* R1 修复 P0-02：急停意图重发（对端确认前持续 200ms 重发）。
         * 背景：急停只有一跳 0x15（ARQ 500ms×3），断链时直接丢失且恢复后不重发。
         * 策略（用户裁决 2026-11-15）：链路丢失只报告、不进 SAFE——所以这里做"意图重发"
         * 而不做"断链即停机"。
         * R-04：重发条件用 control_estop_tx_pending()（意图未确认），**不是**本地自持位——
         * 否则断链期间按屏上"确认"会清自持，意图就永久丢了。
         * 确认口径：0x20 快照 alarm 含 ALM_ESTOP（对端已执行）→ confirm；
         * 对端随后 ack 会让该位消失，本端据此清自持（control_estop_clear），避免两端拉锯。*/
        {
            static uint32_t s_estop_tx_ms;
            static uint8_t  s_estop_seen_remote;   /* 曾在对端快照看到 ESTOP（含确认后）*/
            static uint8_t  s_estop_warned;
            static uint32_t s_estop_retry;
            uint8_t link_fresh = (s_snap_have && (uint32_t)(now - s_snap_ms) <= 500u) ? 1u : 0u;
            /* N-02：seen_remote 的生命周期不能跟 pending 绑——确认 pending 后仍要能观察
             * "对端随后 ack（ESTOP 消失）"，否则那条分支永远不可达、两端状态分叉。
             * 只有"本地不再自持"时才整体复位。*/
            if (!control_estop_latched()) {
                s_estop_seen_remote = 0;
            }
            if (!link_fresh) {
                if (control_estop_tx_pending() && !s_estop_warned) {
                    s_estop_warned = 1;
                    tcs_log("[SAFE]", "estop pending: esp link down, delivery unconfirmed (report only)");
                }
            } else if (s_last_snap.alarm & ALM_ESTOP) {
                s_estop_seen_remote = 1;      /* 对端已执行（本轮急停的确认证据）*/
                s_estop_warned = 0;
                if (control_estop_tx_pending()) control_estop_tx_confirm();   /* R-04 */
            } else if (s_estop_seen_remote) {
                control_estop_clear();        /* 对端已 ack（曾现又消失）→ 本端同步解除自持 */
                s_estop_seen_remote = 0;
            } else if (control_estop_tx_pending()) {
                if ((uint32_t)(now - s_estop_tx_ms) >= 200u) {
                    s_estop_tx_ms = now;
                    s_estop_retry++;
                    /* R-19：对端在、但一直没上报 ESTOP 时不能静默 5Hz 重发。每 25 次（≈5s）报一次 */
                    if ((s_estop_retry % 25u) == 1u)
                        tcs_log("[SAFE]", "estop unconfirmed: retry=%lu (link up, peer silent)",
                                (unsigned long)s_estop_retry);
                    link_send_cmd_upstream(8);    /* 重发意图（幂等）*/
                }
            } else {
                s_estop_warned = 0;
                s_estop_retry = 0;
            }
        }

        /* N-01：超温停机请求的投递重发（与急停同一 200ms 节拍，但确认口径独立：
         * 对端快照出现 ALM_OVERTEMP 即认为已锁存；本地 ack 清隐患时该请求作废）。*/
        {
            static uint32_t s_ot_tx_ms;
            uint8_t link_fresh = (s_snap_have && (uint32_t)(now - s_snap_ms) <= 500u) ? 1u : 0u;
            if (control_overtemp_tx_pending() && link_fresh) {
                if (s_last_snap.alarm & ALM_OVERTEMP) {
                    control_overtemp_tx_confirm();
                } else if ((uint32_t)(now - s_ot_tx_ms) >= 200u) {
                    s_ot_tx_ms = now;
                    link_send_cmd_upstream(TCS_CMD_OVERTEMP_STOP);
                }
            }
        }

        perf_sample_work(DWT->CYCCNT - cy0);
        if (g_link_down_flag) {
            g_link_down_flag = 0;
            tcs_log("[NET]", "esp link down (3 retries)");
        }
        /* R2（v2.4 §13.4.6）：判活口径=快照新鲜度 500ms 窗（0x20@200ms 连续到达）；
         * comm 层 link_ok(ARQ 活性)保留内部使用，不再直接映射屏点 */
        g_tcs.esp_link_ok = (s_snap_have &&
                             (uint32_t)(now - s_snap_ms) <= 500u) ? 1u : 0u;

        if (now - last_led_ms >= 500u) {
            last_led_ms = now;
            HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, (led ^= 1) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }
        /* 10ms 节拍（SysTick 分频）：预算内工作完成后等待下一拍 */
        while (g_tick_ms - last_tick < 10u) { }
        last_tick = g_tick_ms;
    }
}
