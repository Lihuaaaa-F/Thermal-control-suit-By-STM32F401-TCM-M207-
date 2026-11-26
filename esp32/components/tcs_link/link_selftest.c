/**
 * link_selftest.c — 诊断静默 / 芯片内回环 / 开机自环自测（M11-F-15 拆分，S 线 e1）
 *
 * 从 link_task.c 拆出的第 6 块，两段：
 *   ① 链路诊断（只在 CONFIG_TCS_SIM / CONFIG_TCS_DIAG 构建里；PROD 没有下面任何符号）：
 *      link_mute_ms(N)  —— N 毫秒内既不发也不收（收到的字节直接丢弃）。不拔线就能诱导
 *                          "屏链路断开再恢复"，用来验证两端的恢复逻辑；物理层仍要人工拔线。
 *      link_loopback(1) —— 芯片内部把 TX 接到 RX：本端发什么就收什么，是确定性的回声源，
 *                          用来在真机上验证方向闸门（echo 计数应增长，不得出现参数写入）。
 *   ② WP-F.1 真机自环自测（[LPBK]，三档都编译）：UART1 芯片内 TX→RX 回环，真驱动 / 真帧 /
 *      真协议栈。在 link_start 之前调用（独占 UART1，~2.5s）。
 */
#include "link_internal.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "tcs_link";

#if CONFIG_TCS_SIM || CONFIG_TCS_DIAG
static volatile uint32_t s_mute_until_ms;
static volatile uint8_t  s_loopback;
static uint32_t s_mute_rxdrop;

static uint32_t diag_now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000LL); }
uint8_t  link_muted(void)            { return (int32_t)(s_mute_until_ms - diag_now_ms()) > 0; }
void     link_mute_ms(uint32_t ms)   { s_mute_until_ms = diag_now_ms() + ms; }
uint32_t link_mute_rxdrop(void)      { return s_mute_rxdrop; }
void     link_mute_add_rxdrop(uint32_t n) { s_mute_rxdrop += n; }
esp_err_t link_loopback(uint8_t on)
{
    s_loopback = on ? 1u : 0u;
    return uart_set_loop_back(LINK_UART, on ? true : false);
}
#else
uint8_t  link_muted(void)            { return 0; }
void     link_mute_add_rxdrop(uint32_t n) { (void)n; }
#endif

/* ===================================================================== */
/* WP-F.1 真机自环自测：UART1 芯片内 TX→RX 回环，真驱动 / 真帧 / 真协议栈。
 * 在 link_start 之前调用（独占 UART1，~2.5s）；证据 = [LPBK] 日志行。
 *
 * 2026-11-25（R5-01）改为两个链路实例共用这一条回环线：A 扮演屏（H743），B 扮演主控
 * （ESP32，与真链路同身份）。线上的字节同时喂给两端，每一端只认对端发来的帧，自己发出的
 * 那一份被各自的方向闸门丢弃——这正是方向闸门在真链路要做的事。
 * 此前这里只有一个实例自问自答：第 4 项只数"交付"的份数，重传副本全被去重，恒为 1，
 * 每次开机都报 "[LPBK] ARQ retransmit FAIL"、"RESULT pass=3 fail=1 FAIL"。*/
/* ===================================================================== */
static comm_link_t s_lbk_a, s_lbk_b;

static int lbk_send_fn(const uint8_t *d, size_t n)
{
    uart_write_bytes(LINK_UART, d, n);
    return 0;
}

typedef struct { int deliv, dups; } lbk_rx_t;

/* 读回环线上的字节，逐字节同时喂给两端 */
static void lbk_drain(lbk_rx_t *a, lbk_rx_t *b)
{
    uint8_t buf[256];
    int n = uart_read_bytes(LINK_UART, buf, sizeof(buf), 0);
    for (int i = 0; i < n; i++) {
        const uint8_t *pl; size_t plen; int dup;
        if (comm_rx_feed_byte(&s_lbk_a, buf[i], &pl, &plen, &dup)) a->deliv++;
        else if (dup) a->dups++;
        if (comm_rx_feed_byte(&s_lbk_b, buf[i], &pl, &plen, &dup)) b->deliv++;
        else if (dup) b->dups++;
    }
}

void link_loopback_run(void)
{
    QueueHandle_t q = NULL;
    uart_config_t cfg = {
        .baud_rate = LINK_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(LINK_UART, 2048, 2048, 20, &q, 0);
    uart_param_config(LINK_UART, &cfg);
    uart_set_pin(LINK_UART, LINK_TXD, LINK_RXD, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_set_loop_back(LINK_UART, true);      /* ESP-IDF 内部回环：真机无杜邦线 */
    vTaskDelay(pdMS_TO_TICKS(20));

    comm_init(&s_lbk_a, lbk_send_fn, link_internal_now_ms, NULL, NULL, NULL, TCS_SIDE_H743);
    comm_init(&s_lbk_b, lbk_send_fn, link_internal_now_ms, NULL, NULL, NULL, TCS_SIDE_ESP32);
    uint8_t frame[MAX_FRAME];
    uint8_t payload[MAX_PAYLOAD];
    lbk_rx_t ra = { 0, 0 }, rb = { 0, 0 };
    int pass = 0, fail = 0;

    /* 1) 帧回环：屏发 STATUS，TX→RX 原样解出 1 帧（主控端）；屏自己那一份被方向闸门丢弃 */
    tcs_status_pl_t st = { .ver = 1, .seq = 0x11223344, .tc1_act = 25.5f,
                           .tc2_act = 24.5f, .body = 36.8f, .tec1 = 0, .tec2 = 0,
                           .fan = 0, .mode = 0, .alarm = 0, .online = 3, .rfu = 0 };
    size_t pn = tcs_pl_status(payload + 2, sizeof(payload) - 2, &st);
    payload[0] = MT_TCS_STATUS; payload[1] = 0x5A;   /* 自环夹具：屏方向的帧，不带方向位 */
    size_t fn = proto_encode_frame(payload, pn + 2, frame, sizeof(frame));
    uart_write_bytes(LINK_UART, frame, fn);
    vTaskDelay(pdMS_TO_TICKS(50));
    lbk_drain(&ra, &rb);
    if (rb.deliv == 1 && ra.deliv == 0 && s_lbk_a.echo_cnt == 1) {
        pass++; ESP_LOGI(TAG, "[LPBK] frame loopback PASS (controller decoded 1 frame, screen dropped own echo)");
    } else {
        fail++; ESP_LOGE(TAG, "[LPBK] frame loopback FAIL (controller=%d screen=%d echo=%lu)",
                         rb.deliv, ra.deliv, (unsigned long)s_lbk_a.echo_cnt);
    }

    /* 2) 重复帧去重：同帧再发 → 主控端判重复，不交付 */
    uart_write_bytes(LINK_UART, frame, fn);
    vTaskDelay(pdMS_TO_TICKS(50));
    lbk_drain(&ra, &rb);
    if (rb.deliv == 1 && rb.dups >= 1) { pass++; ESP_LOGI(TAG, "[LPBK] dup dedup PASS (dup=%d)", rb.dups); }
    else { fail++; ESP_LOGE(TAG, "[LPBK] dup dedup FAIL (frames=%d dups=%d)", rb.deliv, rb.dups); }

    /* 3) 坏 CRC 丢弃：翻转 1 字节 → 两端都静默丢弃 */
    frame[fn - 1] ^= 0xFF;
    uart_write_bytes(LINK_UART, frame, fn);
    vTaskDelay(pdMS_TO_TICKS(50));
    lbk_drain(&ra, &rb);
    if (rb.deliv == 1 && ra.deliv == 0) { pass++; ESP_LOGI(TAG, "[LPBK] bad-crc drop PASS"); }
    else { fail++; ESP_LOGE(TAG, "[LPBK] bad-crc drop FAIL (controller=%d screen=%d)", rb.deliv, ra.deliv); }

    /* 4) ARQ 重传观测：主控发 0x13（屏不应答）→ 500ms 超时 ×3 重传 → 屏端收到 4 份
     *    （1 份交付 + 3 份被去重）；主控端自己发出的这 4 份一份也不得被当成屏帧。*/
    payload[0] = MT_TCS_GET_REGISTRY; payload[1] = 0xA7;
    fn = proto_encode_frame(payload, 2, frame, sizeof(frame));
    comm_send(&s_lbk_b, frame, fn, MT_TCS_GET_REGISTRY, NULL);
    lbk_rx_t ra0 = ra, rb0 = rb;
    for (int i = 0; i < 22; i++) {            /* 2.2s > 500ms×3+裕量 */
        vTaskDelay(pdMS_TO_TICKS(100));
        comm_tick(&s_lbk_b);
        comm_poll_tx(&s_lbk_b);
        lbk_drain(&ra, &rb);
    }
    int copies = (ra.deliv - ra0.deliv) + (ra.dups - ra0.dups);
    int self_got = rb.deliv - rb0.deliv;
    if (copies >= 3 && self_got == 0) {
        pass++;
        ESP_LOGI(TAG, "[LPBK] ARQ retransmit PASS (%d copies rx by screen: %d delivered + %d dedup'd)",
                 copies, ra.deliv - ra0.deliv, ra.dups - ra0.dups);
    } else {
        fail++;
        ESP_LOGE(TAG, "[LPBK] ARQ retransmit FAIL (copies=%d self_got=%d)", copies, self_got);
    }

    uart_set_loop_back(LINK_UART, false);
    uart_driver_delete(LINK_UART);
    ESP_LOGI(TAG, "[LPBK] RESULT pass=%d fail=%d %s", pass, fail, fail ? "FAIL" : "PASS");
}
