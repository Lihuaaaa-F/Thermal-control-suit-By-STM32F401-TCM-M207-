/**
 * link_task.c — H743↔ESP32 可靠链路任务（任务书 §4.2）
 *
 * UART1 @230400，TX=GPIO17（→H743 PA3 RX），RX=GPIO18（←H743 PD5 TX）。
 * UART 事件队列 → 任务内逐字节 comm_rx_feed_byte（GS §0E 模式：协议处理只在任务上下文）；
 * 100ms comm_tick（重传/断链）；发送经 comm txq，任务内 comm_poll_tx 串行消费。
 * comm 层 0x90 ACK 判定已由 H743 侧同源文件扩展（comm.c 同一份拷贝）。
 * 官方查证：检索组件库 2026-10-26 无官方件（双板私有协议，GS 血统）；UART 通道用官方 driver/uart，任务用官方 FreeRTOS；实现参考 IDF examples/system/freertos/（任务骨架模式）
 *
 * M11-F-15（S 线 e1，2026-11-26）：本文件原先 916 行、六类职责同处。现在只留
 * "UART 事件与任务循环 + comm 回调 + 启动"；收帧分发、注册表、演示、自测回环、上行发送
 * 分别在 link_dispatch.c / link_registry.c / link_demo.c / link_selftest.c / link_send.c，
 * 状态缓存与统计在 link_status.c。内部接口见 link_internal.h。纯重构，行为未变。
 */
#include "link_internal.h"
#include "proto.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "tcs_link";

static comm_link_t s_link;
static QueueHandle_t s_uart_queue;
static volatile uint32_t s_rx_bytes;   /* UART1 收到的字节总数（真机取证）*/
static volatile uint32_t s_rx_frames;  /* 解出的完整帧数 */
static uint32_t s_uart_txdrop;         /* 驱动 TX 环满丢弃（linkstat 可见）*/

comm_link_t *link_ctx(void) { return &s_link; }
uint32_t link_core_rx_bytes(void)  { return s_rx_bytes; }
uint32_t link_core_rx_frames(void) { return s_rx_frames; }
uint32_t link_core_txdrop(void)    { return s_uart_txdrop; }

/* 锁的实体在 link_status.c（s_mux）；comm_init 要的是两个无参函数指针（link_internal.h 有声明）*/

static uint32_t link_now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

uint32_t link_internal_now_ms(void) { return link_now_ms(); }

/* comm_tick 判 ARQ 全败（链路死）时调用：作废注册表收齐标记，重连后重新全量请求。
 * 只做这一件事——链路丢失按用户裁决「只报告」，不置任何锁存。*/
static void link_on_down(void)
{
    link_registry_progress_reset();
    ESP_LOGW(TAG, "link down (ARQ) -> registry resync armed");
}

/* M04-F-06：ARQ 判死之后对端的任何一帧合法帧到达 → 只报一条恢复日志（与上面那条"断"成对；
 * 屏上那个点走的是快照新鲜度，与这里无关）。*/
static void link_on_up(void)
{
    ESP_LOGW(TAG, "link up (peer frame received after ARQ failure)");
}

/* X01-F-03：应答结果（0x13 请求注册表 / 0x15 命令的 ACK）。status != OK 时至少留一条日志；
 * 计数在 comm 层（ack_nonok_cnt，linkstat 的 ackbad=）。成功应答静默（每 5s 一次注册表请求，
 * 每次都打日志会淹掉现场）。*/
static void link_on_ack_result(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret)
{
    if (status == TCS_ACK_OK) return;
    ESP_LOGW(TAG, "ACK rej mt=0x%02x seq=%u status=%u ret=%.2f",
             (unsigned)acked_mt, (unsigned)acked_seq, (unsigned)status, (double)ret);
}

int link_peer_age_ms(uint32_t *age_ms)
{
    /* m1（模式页）：屏链路是否断开的事实来源。peer_seen / peer_ms 由 comm 层在方向闸门之后
     * 记（本端回送帧、方向不符的帧、坏帧都不算），这里只做时基换算。
     * 留在本文件是为了与《屏链路断开 3 秒 = 急停》的源码门禁（tests/test_m1_gates.py）的
     * "如实转出、时基用 link_now_ms" 口径一致；其余状态访问器在 link_status.c。*/
    uint32_t last = 0;
    if (!comm_peer_last_ms(&s_link, &last)) return 0;
    if (age_ms) *age_ms = (uint32_t)(link_now_ms() - last);
    return 1;
}

/* 返回 0 = 这一帧已交给驱动；非 0 = 没有上线（X03-F-09：让 comm 层知道，计入 tx_fail_cnt）*/
static int link_uart_send(const uint8_t *d, size_t n)
{
    /* R1 P1-07：非阻塞写——TX 环满时丢弃该帧（comm 层 ARQ 会重传），
     * 不让 uart_write_bytes 阻塞把 link 任务拖死触发 TWDT。*/
#if CONFIG_TCS_SIM || CONFIG_TCS_DIAG
    if (link_muted()) return 1;             /* 诊断静默期：不上线。no-ARQ 帧丢弃，ARQ 帧由 comm 层超时重传 */
#endif
    size_t free_sz = 0;
    if (uart_get_tx_buffer_free_size(LINK_UART, &free_sz) == ESP_OK && free_sz >= n) {
        uart_write_bytes(LINK_UART, d, n);     /* 帧已由 comm 层带上方向位 */
        return 0;
    }
    s_uart_txdrop++;
    if ((s_uart_txdrop % 50u) == 1u) ESP_LOGW(TAG, "link tx ring full, dropped %lu frames", (unsigned long)s_uart_txdrop);
    return 1;
}

/* —— 任务 —— */
static void link_task(void *arg)
{
    (void)arg;
    esp_task_wdt_add(NULL);   /* R1 P1-07：关键任务纳入 TWDT（原来只有 ctrl_svc）*/
    uart_event_t evt;
    uint8_t buf[256];
    uint32_t last_tick = 0;
    uint32_t last_reg_req = 0;
    uint32_t last_echo = 0;    /* R5-01：上一圈的本端回送帧数（只用来触发告警）*/

    ESP_LOGI(TAG, "link task up");
    /* 握手：要注册表全量（无 STATUS 前每 2s 重试一次）*/
    for (int i = 0; i < 5 && !link_status_buf()->valid; i++) {
        esp_task_wdt_reset();   /* 握手最长 2.5s，必须喂狗（TWDT 3s）*/
        link_request_registry();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    while (1) {
        esp_task_wdt_reset();
        if (xQueueReceive(s_uart_queue, &evt, pdMS_TO_TICKS(50))) {
            if (evt.type == UART_DATA) {
                int n = uart_read_bytes(LINK_UART, buf, sizeof(buf), 0);
#if CONFIG_TCS_SIM || CONFIG_TCS_DIAG
                if (link_muted()) { if (n > 0) link_mute_add_rxdrop((uint32_t)n); n = 0; }
#endif
                if (n > 0) s_rx_bytes += (uint32_t)n;
                for (int i = 0; i < n; i++) {
                    const uint8_t *pl; size_t plen; int dup;
                    /* R5-01：本端发出的帧被回送时 comm_rx_feed_byte 返回 0（方向闸门在 comm 层，
                     * ACK / 去重 / 交付之前），这里不需要、也不再有任何按长度或时刻的过滤。*/
                    if (comm_rx_feed_byte(&s_link, buf[i], &pl, &plen, &dup)) {
                        s_rx_frames++;
                        link_dispatch_frame(pl, plen);
                    } else if (dup) {
                        /* R1 修复 M11-F-04：重复帧也回 ACK（幂等）。
                         * 原来丢弃且不回 ACK，发送端 ARQ 3 次后误判链路失败。*/
                        uint8_t ds, dm;
                        if (comm_last_dup_info(&s_link, &ds, &dm))
                            link_send_ack(ds, dm, TCS_ACK_OK, 0.0f);
                    }
                }
            } else if (evt.type == UART_FIFO_OVF || evt.type == UART_BUFFER_FULL) {
                uart_flush_input(LINK_UART);
                ESP_LOGW(TAG, "uart overflow flushed");
            }
        }
        uint32_t now = link_now_ms();
        if (now - last_tick >= 100) {
            last_tick = now;
            comm_tick(&s_link);
            /* 2026-11-28：每 100ms 只出一帧 = 消费 10 帧/s，低于稳态生产 ~11.3 帧/s → 队列贴满、
             * ACK 排到队尾超时、屏侧 ARQ 重传被误判"被拒"。每拍排空一小批（4 帧 ≈ 12ms 线时）。*/
            comm_poll_tx_burst(&s_link, 4u);
        }
        /* R5-01：本端回送帧只在接线/串扰异常时出现。每 20 帧告警一次，提示查接线。*/
        if (s_link.echo_cnt != last_echo) {
            last_echo = s_link.echo_cnt;
            if ((last_echo % 20u) == 1u)
                ESP_LOGW(TAG, "self-echo on RX dropped=%lu (check H743 TX wire / crosstalk)",
                         (unsigned long)last_echo);
        }
        if (!link_alive()) link_registry_progress_reset();   /* 对端重启/断链 → 重新要全量 */
        if (now - last_reg_req >= 5000 && !link_registry_done()) {
            last_reg_req = now;
            link_request_registry();
        }
#if CONFIG_TCS_DEMO
        /* WP-F.2 演示模式：真链路离线时 500ms 模拟步进（0x10 快照同构）*/
        static uint32_t last_demo = 0;
        if (link_demo_takeover() && now - last_demo >= 500) {
            last_demo = now;
            link_demo_step();
        }
#endif
    }
}

esp_err_t link_start(void)
{
    memset(link_status_buf(), 0, sizeof(link_status_t));
    link_registry_init();                   /* WP-F.2/F3：内置快照先行 */
    link_demo_boot();                       /* 演示宽限起点（PROD 档空实现）*/
    /* 2026-11-19：原来 on_link_down 传 NULL——ARQ 判死后没人记账，重连不重问注册表。
     * 现在断链即作废注册表收齐状态，重连后由 5s 请求/心跳自动补齐。*/
    comm_init(&s_link, link_uart_send, link_now_ms, link_on_down,
              link_crit_enter, link_crit_exit, TCS_SIDE_ESP32);
    comm_set_on_link_up(&s_link, link_on_up);   /* M04-F-06：与 on_link_down 成对的"恢复"日志 */
    /* X01-F-03（2026-11-26）：0x90 的 status/ret 交给这里。本端发出时登记过 pending 的是
     * 0x13（请求注册表）与 0x15（命令）；非 OK 的应答至少要在日志与 linkstat 里看得见。*/
    comm_set_on_ack_result(&s_link, link_on_ack_result);

    uart_config_t cfg = {
        .baud_rate = LINK_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(LINK_UART, LINK_RXBUF, LINK_TXBUF, LINK_EVT_QUEUE,
                                        &s_uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(LINK_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(LINK_UART, LINK_TXD, LINK_RXD,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* P1-12 的另一半（S 线 i3）：任务创建的结果带出去——原来是 void 接口，启动汇总只能记成"未核实"。*/
    if (xTaskCreate(link_task, "tcs_link", 4096, NULL, 5, NULL) != pdPASS) {
        ESP_LOGE(TAG, "link task not created: no screen link (snapshots / commands will not pass)");
        uart_driver_delete(LINK_UART);
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "started uart1 %d baud tx=%d rx=%d", LINK_BAUD, LINK_TXD, LINK_RXD);
    return ESP_OK;
}
