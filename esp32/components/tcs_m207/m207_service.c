/**
 * m207_service.c — M207 串口服务 IDF 外壳（ESP32-S3 主控侧；R1b，任务书 v11.3）
 *
 * S-G e2（X06-F-05）起本文件只剩与硬件/RTOS 绑定的部分：UART2 驱动安装、50ms 服务任务、
 * 发送出口、上电回环自测、取证构建（应答器注入 / 真机探针）。
 * 全部决策逻辑——下行队列、在途写确认事务、轮询/探测状态机、应答行处置、统计——在
 * m207_core.c（主机可编译，逐拍可测）；对外 API 的声明仍在本文件配套的 m207_service.h，
 * 签名未变。
 *
 * 架构（第八章预算）：
 *   - 单任务 m207_svc 钉核 1、50ms 节流拍（vTaskDelayUntil）；TX 仲裁每拍至多 1 帧
 *     （重发 > 队列帧 > 轮询帧）→ 50ms 铁律结构性满足（正典 §4）。
 *   - RX=IDF 驱动 RX 环形缓冲（1024B，ISR 自动搬运）+ 事件队列；每拍无阻塞清空
 *     （57600 满速率 288B/拍 ≪ 环形容量）。TX=驱动 TX 环（256B），写不阻塞拍内
 *     → 拍 WCET ≤1ms 预算不靠阻塞发送。
 *   - 轮询：500ms 交替 TC1/TC2 TCACTTEMP?；应答窗 6 拍；5 连败离线
 *     （F5：ALM_M207 置位+停收新写+清队列/挂起；SAFE 输出由 control R1e 裁决）；
 *     恢复：1s 探测，复联后重同步（TCSW?/TCADJTEMP?+按需 TCSW=0，E-08 预研）。
 *   - 自测：m207_lb_boottest=[M207LB]（内部回环全套+物理短接探测）；
 *     CONFIG_TCS_M207_SELFLOOP 构建附带应答器注入全路径取证=[M207SL]。
 * 官方查证：检索 components.espressif.com+IDF v5.5.5 组件库 2026-10-26，无 M207 温控模块官方件（厂商私有协议，合法自研）；串口通道用官方 driver/uart；实现参考 IDF(D:/Espressif/frameworks/esp-idf-v5.5.5) examples/peripherals/uart/uart_async_rxtxtasks（异步收发+环形缓冲模式）
 */
#include "m207_service.h"
#include "m207_core.h"
#include "m207_proto.h"
#include "param_registry.h"
#include "control.h"
#include "post.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/uart.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_task_wdt.h"

static const char *TAG = "M207";

/* ====== 硬件与节律常量（接线表 v2.0 / 第八章 / 正典）====== */
#define M207_UART        UART_NUM_2
#define M207_TXD_GPIO    21
#define M207_RXD_GPIO    47
#define M207_BAUD        57600
#define M207_RXBUF       1024
#define M207_TXBUF       256
#define M207_EVTQ_LEN    20

#define M207_TICK_MS     50u     /* 队列服务节拍 = 铁律间隔 */
#define M207_TASK_PRIO   5
#define M207_TASK_CORE   1    /* 第八章：控制组钉核 1 */
#define M207_TASK_STACK  4096

static QueueHandle_t s_evt_q;        /* UART 事件队列 */
static uint8_t  s_started;

#if CONFIG_TCS_M207_SELFLOOP
/* ====== 应答器注入（仅取证构建；拦截服务下行帧，合成应答延迟到下一拍写出。
 * 真实模块应答必有处理延迟，0µs 贴写属非物理行为——且 hex 取证表明贴写 junction
 * 会绞掉自家回显的 \r（应答数据本身完好）；延迟应答即判别实验，也更贴近真机。）*/
static struct {
    uint8_t silent;        /* 1=应答静默（拔线仿真）*/
    uint8_t wrong_echo;    /* 0=关 1=下一次查询回显错值 2=恒错值 */
    uint8_t tcsw[3];
    float   set1, act1;
    char    pend_line[48]; /* 延迟到下一拍写出的应答行 */
    uint8_t pend_len;
} s_rsp;
static void m207_rsp_on_tx(const char *cmd);
static void m207_rsp_flush(void);      /* 服务任务每拍开头调用 */
void m207_rsp_fault(int on) { s_rsp.silent = on ? 1u : 0u; }
void m207_rsp_wrong(int mode) { s_rsp.wrong_echo = (uint8_t)mode; }
#endif

/* ====== 发送出口 / 时钟（交给 m207_core）====== */
/* R1 P1-07：非阻塞写。uart_write_bytes 在 TX 环满时会阻塞（模块离线时可达 10s），
 * 会把 m207 任务拖死并触发 TWDT 重启；这里空间不足就报 -1，由核心计 txdrop。*/
static int m207_tx_now(void *ctx, const char *cmd, size_t len)
{
    (void)ctx;
    size_t free_sz = 0;
    if (uart_get_tx_buffer_free_size(M207_UART, &free_sz) != ESP_OK || free_sz < len)
        return -1;
    uart_write_bytes(M207_UART, cmd, len);
#if CONFIG_TCS_M207_SELFLOOP
    m207_rsp_on_tx(cmd);
#endif
    return 0;
}

static int64_t m207_now_us(void *ctx)
{
    (void)ctx;
    return esp_timer_get_time();
}

/* ====== RX 清空（事件驱动 + 拍内兜底）====== */
static void m207_rx_drain(uint32_t now_ms)
{
    uint8_t buf[256];
    for (;;) {
        int n = uart_read_bytes(M207_UART, buf, sizeof(buf), 0);
        if (n <= 0) break;
#if CONFIG_TCS_M207_RAWLOG
        {
            char hx[3 * 40 + 8];
            int o = 0;
            for (int i = 0; i < n && i < 40; i++)
                o += snprintf(hx + o, sizeof(hx) - o, "%02X ", buf[i]);
            ESP_LOGI("M207R", "rx %dB: %s", n, hx);
        }
#endif
        m207_core_rx(buf, (size_t)n, now_ms);
    }
}

/* ====== 服务任务（钉核 1；50ms 节拍；事件驱动 RX）====== */
static void m207_task(void *arg)
{
    esp_task_wdt_add(NULL);   /* R1 P1-07：关键任务纳入 TWDT（原来只有 ctrl_svc）*/
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    uart_event_t ev;
    int64_t last_proc_us = 0;
    for (;;) {
        esp_task_wdt_reset();
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(M207_TICK_MS));
        /* R1 修复 M12-F-07：vTaskDelayUntil 的追赶语义会在"上一拍被饿"后连跑两拍，
         * 使下行间隔 <50ms，违反正典 §4 铁律。这里吞掉过早的追赶拍（不消费队列、不发帧）。*/
        {
            int64_t now_us = esp_timer_get_time();
            if (last_proc_us && (now_us - last_proc_us) < 50000) continue;
            last_proc_us = now_us;
        }
#if CONFIG_TCS_M207_SELFLOOP
        m207_rsp_flush();                /* 延迟应答：上一拍拦截的应答本拍写出 */
#endif
        int64_t t0_us = esp_timer_get_time();
        uint32_t now_ms = (uint32_t)((uint64_t)t0_us / 1000u);
        m207_core_tick_begin(now_ms);

        /* 1) RX：先清事件（UART_DATA→读环形），防 50ms 窗积压 */
        while (xQueueReceive(s_evt_q, &ev, 0) == pdTRUE) {
            if (ev.type == UART_DATA) {
                m207_rx_drain(now_ms);
            } else if (ev.type == UART_FIFO_OVF || ev.type == UART_BUFFER_FULL) {
                ESP_LOGW(TAG, "uart overflow ev=%d → flush", (int)ev.type);
                uart_flush_input(M207_UART);
                m207_core_rx_flush();    /* M12-F-10：已经不完整的半行一并丢弃（计 linedrop）*/
            }
        }
        m207_rx_drain(now_ms);           /* 拍内兜底（事件丢失也无损）*/

        /* 2) 在途事务超时 + 3) TX 仲裁（每拍至多 1 帧）*/
        m207_core_tick_end(t0_us);

        /* 4) perf 统计（第八章：队列服务+节流 ≤1ms/拍，轮询状态机 ≤0.3ms）*/
        m207_core_perf_t pf;
        if (m207_core_perf_take(&pf)) {
            UBaseType_t stk = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG,
                     "[M207P] tick_max=%luous (fsm_max=%luous; budget 1000/300) "
                     "tx=%lu rxline=%lu perr=%lu nak=%u vok=%u rsend=%u off=%u "
                     "qdrop=%u wdrop=%u stk=%uW",
                     (unsigned long)pf.tick_max_us,
                     (unsigned long)pf.fsm_max_us,
                     (unsigned long)pf.tx, (unsigned long)pf.rx_lines,
                     (unsigned long)pf.parse_err, (unsigned)pf.nak,
                     (unsigned)pf.verify_ok, (unsigned)pf.resend,
                     (unsigned)pf.offline, (unsigned)pf.qdrop,
                     (unsigned)pf.wdrop, (unsigned)stk);
        }
    }
}

/* ====== 公共 API（生命周期；写路径与状态读侧在 m207_core.c）====== */
int m207_service_start(void)
{
    if (s_started) return 0;
    uart_config_t cfg = {
        .baud_rate = M207_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(M207_UART, M207_RXBUF, M207_TXBUF,
                                        M207_EVTQ_LEN, &s_evt_q, 0));
    ESP_ERROR_CHECK(uart_param_config(M207_UART, &cfg));
    ESP_ERROR_CHECK(uart_set_pin(M207_UART, M207_TXD_GPIO, M207_RXD_GPIO,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    /* 首写病理吸收（hex 取证 2026-10-26：装驱动后首个 TX 前导坏字节+丢帧尾 \r，
     * 后续写全部字节完好）。空行先导帧消耗之；空行对真机模块无副作用（正典 §1 空行非法即忽略）。*/
    uart_write_bytes(M207_UART, "\r", 1);
    uart_wait_tx_done(M207_UART, pdMS_TO_TICKS(50));
    uart_flush_input(M207_UART);

    m207_core_init(&(m207_core_io_t){
        .tx = m207_tx_now,
        .now_us = m207_now_us,
    });
    s_started = 1;

    if (xTaskCreatePinnedToCore(m207_task, "m207_svc", M207_TASK_STACK, NULL,
                                M207_TASK_PRIO, NULL, M207_TASK_CORE) != pdPASS)
        return -1;
    ESP_LOGI(TAG, "service started uart%d %d baud tx=%d rx=%d core=%d",
             (int)M207_UART, M207_BAUD, M207_TXD_GPIO, M207_RXD_GPIO,
             M207_TASK_CORE);
    return 0;
}

/* ===================================================================== */
/*                 上电回环自测（独占 UART2，[M207LB]）                    */
/* ===================================================================== */
/* 帧表与判定在 m207_core.c（主机可测）。M12-F-13：过线的只有 ? 查询——模块接着时，
 * 这些帧经 TX 引脚真实下发，查询不会改它的任何参数；解析样本（REPLY 行、ERRCODE 全表）
 * 不过线，只在板上逐字节喂进行拼装器再解析。 */

static void lb_run_suite(const char *tag)
{
    static m207_lineasm_t s_lb_asm;      /* 自检独占 UART2 期间使用，不碰服务的行拼装器 */
    int pass = 0, fail = 0;
    char line[M207_LINE_MAX + 1];
    uint8_t rxb[128];
    size_t i;

    /* A) 过线：查询帧经真 UART 外设 / 真驱动 / 行拼装 / 解析器，回显逐字节相同 */
    for (i = 0; i < m207_lb_wire_n(); i++) {
        const char *f = m207_lb_wire_frame(i);
        char tx[M207_LINE_MAX + 2];
        int got = 0, n, ok;
        snprintf(tx, sizeof(tx), "%s\r", f);
        uart_write_bytes(M207_UART, tx, strlen(tx));
        vTaskDelay(pdMS_TO_TICKS(20));
        line[0] = '\0';                  /* M12-F-13：没收到回显时打印空串，不是栈上的残留 */
        m207_lineasm_reset(&s_lb_asm);
        while ((n = uart_read_bytes(M207_UART, rxb, sizeof(rxb), 0)) > 0) {
            for (int k = 0; k < n; k++)
                if (m207_lineasm_feed(&s_lb_asm, rxb[k], line, sizeof(line) - 1))
                    got = 1;
        }
        line[M207_LINE_MAX] = '\0';
        ok = got && m207_lb_echo_ok(f, line);
        if (ok) {
            pass++;
            ESP_LOGI(TAG, "[M207LB] %s %u/%u query echo byte-identical PASS",
                     tag, (unsigned)(i + 1), (unsigned)m207_lb_wire_n());
        } else {
            fail++;
            ESP_LOGE(TAG, "[M207LB] %s %u/%u FAIL (got='%s')",
                     tag, (unsigned)(i + 1), (unsigned)m207_lb_wire_n(), line);
        }
    }

    /* B) 不过线：解析样本逐字节喂进行拼装器再解析（REPLY 行、ERRCODE 全表 0..8）*/
    m207_lineasm_reset(&s_lb_asm);
    for (i = 0; i < m207_lb_parse_n(); i++) {
        const char *f = m207_lb_parse_frame(i);
        int ok = 0;
        line[0] = '\0';
        for (const char *p = f; *p; p++)
            m207_lineasm_feed(&s_lb_asm, (uint8_t)*p, line, sizeof(line) - 1);
        /* 样本不带行终结符：补一个 \r，行拼装器才出帧 */
        ok = m207_lineasm_feed(&s_lb_asm, '\r', line, sizeof(line) - 1) &&
             strcmp(line, f) == 0 && m207_lb_parse_ok(line);
        if (ok) {
            pass++;
            ESP_LOGI(TAG, "[M207LB] %s parse %u/%u PASS (%s)",
                     tag, (unsigned)(i + 1), (unsigned)m207_lb_parse_n(), f);
        } else {
            fail++;
            ESP_LOGE(TAG, "[M207LB] %s parse %u/%u FAIL (%s -> '%s')",
                     tag, (unsigned)(i + 1), (unsigned)m207_lb_parse_n(), f, line);
        }
    }

    ESP_LOGI(TAG, "[M207LB] RESULT %s pass=%d fail=%d %s",
             tag, pass, fail, fail ? "FAIL" : "PASS");
}

void m207_lb_boottest(void)
{
    QueueHandle_t q = NULL;
    uart_config_t cfg = {
        .baud_rate = M207_BAUD, .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE, .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE, .source_clk = UART_SCLK_DEFAULT,
    };
    if (uart_driver_install(M207_UART, 2048, 512, 8, &q, 0) != ESP_OK) {
        ESP_LOGE(TAG, "[M207LB] driver install FAIL");
        return;
    }
    uart_param_config(M207_UART, &cfg);
    uart_set_pin(M207_UART, M207_TXD_GPIO, M207_RXD_GPIO,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    vTaskDelay(pdMS_TO_TICKS(10));

    /* A) 内部回环全套（真 UART 外设/真驱动/真帧；WP-F.1 同款，无杜邦线）*/
    uart_set_loop_back(M207_UART, true);
    /* 先导帧：吸收"驱动安装+回环使能后首个 TX 不排空"的一次性损耗
     * （2026-10-26 实测：无先导时第 1 帧恒丢、后续全过；空行不产生解析帧）*/
    uart_write_bytes(M207_UART, "\r", 1);
    vTaskDelay(pdMS_TO_TICKS(60));
    uart_flush_input(M207_UART);
    vTaskDelay(pdMS_TO_TICKS(10));
    lb_run_suite("int");

    /* B) 物理短接探测：GPIO21↔47 杜邦在位则自动跑物理全套。
     * M12-F-13：探测帧也改成查询——短接时回显的是我们自己的 TX，查询帧照样逐字节相同；
     * 模块接着时，它收到的是一条查询而不是一次写。 */
    uart_set_loop_back(M207_UART, false);
    uart_flush_input(M207_UART);
    const char *probe = "TC1:TCACTTEMP?\r";
    uart_write_bytes(M207_UART, probe, strlen(probe));
    vTaskDelay(pdMS_TO_TICKS(60));
    uint8_t echo[64];
    int n = uart_read_bytes(M207_UART, echo, sizeof(echo) - 1, 0);
    if (n >= (int)strlen(probe) - 1 && memcmp(echo, probe, (size_t)n) == 0) {
        ESP_LOGI(TAG, "[M207LB] physical jumper 21↔47 detected → full suite");
        lb_run_suite("phy");
    } else {
        ESP_LOGI(TAG, "[M207LB] physical probe: no echo (%dB) — GPIO21↔47 未短接，"
                      "物理回环挂账（接线表 v2.0 收尾项）", n);
    }

    uart_set_loop_back(M207_UART, false);
    uart_driver_delete(M207_UART);
    vTaskDelay(pdMS_TO_TICKS(10));
}

/* ===================================================================== */
/*            R1b 取证专用：应答器注入全路径（CONFIG_TCS_M207_SELFLOOP）  */
/* ===================================================================== */
#if CONFIG_TCS_M207_SELFLOOP

/* 应答器：拦截服务 TX（m207_tx_now 调用），按正典 §2 合成应答。
 * 派发用参数名前缀+终结符直比（不用 sscanf 字面量——尾部字面量失配不影响
 * 已完成的 %d 赋值计数，==1 恒真会把一切命令派进第一分支，2026-10-26 实测踩坑）。
 * 查询→TCx:*=值；设定→CMD:REPLY=1（正典：设定不回显参数名！）；
 * wrong_echo=1 单次错值（重发路径）/2 恒错值（NAK 路径）；
 * silent=1 应答静默（拔线仿真 → 5 连败离线）。 */
static void rsp_stage(const char *s, size_t n)
{
    if (n >= sizeof(s_rsp.pend_line)) n = sizeof(s_rsp.pend_line) - 1;
    memcpy(s_rsp.pend_line, s, n);
    s_rsp.pend_len = (uint8_t)n;
}

static void m207_rsp_flush(void)
{
    if (s_rsp.pend_len) {
        uart_write_bytes(M207_UART, s_rsp.pend_line, s_rsp.pend_len);
        s_rsp.pend_len = 0;
    }
}

static void m207_rsp_on_tx(const char *cmd)
{
    if (s_rsp.silent) return;
    if (s_rsp.act1 == 0.0f) s_rsp.act1 = 25.0f;
    if (!(cmd[0] == 'T' && cmd[1] == 'C' &&
          (cmd[2] == '1' || cmd[2] == '2') && cmd[3] == ':')) return;
    int ch = cmd[2] - '0';
    const char *p = &cmd[4];                 /* 参数段起点 */
    char line[48];
    if (strncmp(p, "TCACTTEMP?", 10) == 0) {
        float sp = s_rsp.set1 ? s_rsp.set1 : 25.0f;
        s_rsp.act1 += (sp - s_rsp.act1) * 0.15f;
        snprintf(line, sizeof(line), "TC%d:TCACTTEMP=%.1f\r", ch, (double)s_rsp.act1);
        rsp_stage(line, strlen(line));
    } else if (strncmp(p, "TCADJTEMP?", 10) == 0) {
        float echo = s_rsp.set1;
        if (s_rsp.wrong_echo) {
            echo += 0.5f;
            if (s_rsp.wrong_echo == 1) s_rsp.wrong_echo = 0;
        }
        snprintf(line, sizeof(line), "TC%d:TCADJTEMP=%.2f\r", ch, (double)echo);
        rsp_stage(line, strlen(line));
    } else if (strncmp(p, "TCADJTEMP=", 10) == 0) {
        s_rsp.set1 = strtof(p + 10, NULL);   /* strtof：IDF newlib 下比 sscanf %%f 可靠 */
        ESP_LOGI(TAG, "[RSP] set ch=%d v=%.2f -> REPLY=1", ch, (double)s_rsp.set1);
        rsp_stage("CMD:REPLY=1\r", 12);
    } else if (strncmp(p, "TCSW=", 5) == 0) {
        if (ch == 1 || ch == 2) s_rsp.tcsw[ch] = (uint8_t)atoi(p + 5);
        rsp_stage("CMD:REPLY=1\r", 12);
    } else if (strncmp(p, "TCSW?", 5) == 0) {
        snprintf(line, sizeof(line), "TC%d:TCSW=%u\r", ch,
                 (unsigned)((ch == 1 || ch == 2) ? s_rsp.tcsw[ch] : 0u));
        rsp_stage(line, strlen(line));
    }
}

static int s_sl_pass, s_sl_fail;
#define SL_CHECK(cond, name) do { \
    if (cond) { s_sl_pass++; ESP_LOGI(TAG, "[M207SL] %s PASS", name); } \
    else      { s_sl_fail++; ESP_LOGE(TAG, "[M207SL] %s FAIL", name); } \
} while (0)

static void sl_wait_ms(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

void m207_selfloop_run(void)
{
    ESP_LOGI(TAG, "[M207SL] === selfloop evidence start (uart internal loopback) ===");
    m207_rsp_fault(0);
    m207_rsp_wrong(0);
    uart_set_loop_back(M207_UART, true);

    /* 1) 在线检测（轮询→应答→online 沿）*/
    uint32_t wait = 0;
    while (!m207_online() && wait < 4000) { sl_wait_ms(100); wait += 100; }
    SL_CHECK(m207_online(), "1 online-first");
    tcs_alarm_ack();   /* 清启动期无模块离线沿：保证 6/7 断言的是本序列产生的沿 */

    /* 2) 温度缓存路径 */
    sl_wait_ms(1500);
    SL_CHECK(m207_core_have_temp(1) && m207_core_have_temp(2), "2 temp-cache tc1/tc2");

    /* 3) 写后读回：一次成功 */
    uint16_t vok0 = m207_stats_verify_ok();
    (void)m207_set_temp(1, 30.5f);
    wait = 0;
    while (m207_stats_verify_ok() == vok0 && wait < 2000) { sl_wait_ms(100); wait += 100; }
    SL_CHECK(m207_stats_verify_ok() == vok0 + 1, "3 verify-ok set 30.5");

    /* 4) 错值回显 → 自动重发 1 次 → 成功 */
    uint16_t rs0 = m207_stats_resend();
    m207_rsp_wrong(1);
    (void)m207_set_temp(1, 31.0f);
    wait = 0;
    while (m207_stats_resend() == rs0 && wait < 2500) { sl_wait_ms(100); wait += 100; }
    sl_wait_ms(1500);
    SL_CHECK(m207_stats_resend() == rs0 + 1 &&
             m207_stats_verify_ok() == vok0 + 2, "4 mismatch-resend-then-ok");

    /* 5) 恒错回显 → 重发仍错 → NAK 记录 */
    uint16_t nak0 = m207_stats_nak();
    m207_rsp_wrong(2);
    (void)m207_set_temp(1, 32.0f);
    wait = 0;
    while (m207_stats_nak() == nak0 && wait < 3000) { sl_wait_ms(100); wait += 100; }
    m207_rsp_wrong(0);
    SL_CHECK(m207_stats_nak() == nak0 + 1, "5 nak-after-retry");

    /* 6) 拔线仿真 → 5 连败离线 + ALM_M207（F5）*/
    uint16_t off0 = m207_stats_offline();
    m207_rsp_fault(1);
    wait = 0;
    while (m207_stats_offline() == off0 && wait < 6000) { sl_wait_ms(100); wait += 100; }
    SL_CHECK(m207_stats_offline() == off0 + 1 && !m207_online() &&
             m207_alarm_active(), "6 offline-5fails-ALM_M207");

    /* 7) 复线恢复 → 探测上线 + 报警粘滞 */
    m207_rsp_fault(0);
    wait = 0;
    while (!m207_online() && wait < 5000) { sl_wait_ms(100); wait += 100; }
    SL_CHECK(m207_online() && m207_alarm_active(), "7 recover-alarm-sticky");

    /* 8) 人工 ack → 报警清除（F5：复联+ack）*/
    tcs_alarm_ack();
    SL_CHECK(!m207_alarm_active(), "8 ack-clears-ALM_M207");

    /* 9) 停写语义：离线时写请求被拒（-2）*/
    m207_rsp_fault(1);
    wait = 0;
    while (m207_stats_offline() == off0 + 1 && wait < 6000) { sl_wait_ms(100); wait += 100; }
    int rc = m207_set_temp(2, 25.0f);
    SL_CHECK(rc == -2, "9 write-rejected-while-offline");

    /* 收场：应答器退场 + 回环关闭（真机无模块 → 回到预期离线态）*/
    m207_rsp_fault(1);
    uart_set_loop_back(M207_UART, false);
    ESP_LOGI(TAG, "[M207SL] RESULT pass=%d fail=%d %s", s_sl_pass, s_sl_fail,
             s_sl_fail ? "FAIL" : "PASS");
    ESP_LOGI(TAG, "[M207SL] === selfloop evidence end ===");
}
#endif /* CONFIG_TCS_M207_SELFLOOP */

/* ===================================================================== */
/*      R1b 真机取证（CONFIG_TCS_M207_REALPROBE；M5-M7 接通后清账用）     */
/* ===================================================================== */
#if CONFIG_TCS_M207_REALPROBE
#include "driver/gpio.h"

/* P-2 软件子集：RS232 静态判活。
 * ① 真模块上电时其 TX 恒发 mark(负压)，经 MAX3232 后 TTL 侧(GPIO47)必读高——
 *    读低即模块 TX 未到达（未供电/断路；注意：即便 M5/M6 对调，对端有电也读高）。
 * ② 二级判别（内部上拉）：GPIO47 读低时挂 45k 上拉复测——仍低=MAX3232 在主动驱动
 *    低电平（=有电但 RIN 悬空，M6 支路未真正接到有电模块）；变高=MAX3232 输出
 *    高阻（=MAX3232 未上电/损坏，查 M3 3V3）。
 * GPIO21 为 TX 脚（输入缓冲未开），软件读数无效——TX 判活由模块是否应答决定。 */
static void realprobe_p2_levels(void)
{
    int rx_hi = 0, rx_pu_hi = 0;
    for (int i = 0; i < 100; i++) {
        rx_hi += gpio_get_level(M207_RXD_GPIO);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    gpio_pullup_en(M207_RXD_GPIO);
    vTaskDelay(pdMS_TO_TICKS(5));
    for (int i = 0; i < 100; i++) {
        rx_pu_hi += gpio_get_level(M207_RXD_GPIO);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    const char *v = (rx_hi >= 95) ?
        "RS232 对端在线（模块供电+M6 支路通）——若仍无应答再查 TX 侧/M5" :
        (rx_hi <= 5 && rx_pu_hi <= 5) ?
        "MAX3232 有电且输出恒低=RIN 悬空：模块未供电，或 M6/M7 未真正接通到模块引脚" :
        (rx_hi <= 5 && rx_pu_hi >= 95) ?
        "MAX3232 输出高阻=其自身未上电/损坏：查 M3（ESP32 3V3→MAX3232 VCC）" :
        "RXD 抖动——接触不良";
    ESP_LOGI(TAG, "[P2] GPIO47(RX) idle high=%u/100；挂 45k 上拉后 high=%u/100 → %s",
             (unsigned)rx_hi, (unsigned)rx_pu_hi, v);
}

void m207_realprobe_run(void)
{
    ESP_LOGI(TAG, "[RP] === real-machine probe start ===");
    realprobe_p2_levels();

    uint32_t w = 0;
    while (!m207_online() && w < 6000) { vTaskDelay(pdMS_TO_TICKS(100)); w += 100; }
    ESP_LOGI(TAG, "[RP] online=%u (after %lums)", m207_online(), (unsigned long)w);
    if (!m207_online()) {
        ESP_LOGW(TAG, "[RP] 模块无应答——按 [P2] 结论查供电/接线/R-K1 一次对调法");
        return;
    }

    /* 故障码读取（用户问询：模块报错灯亮）：位语义见 tc_hlp.xml——
     * 8 位，正常=1 故障=0：bit0 外部/bit1 过压/bit2 过流/bit3 过温/
     * bit4 传感器开路/bit5 负载短路/bit6 负载开路；全正常=127。
     * STATUS=当前态，LOG=上电以来锁存。 */
    (void)m207_send_raw("TC1:TCERRORSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC2:TCERRORSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC1:TCERRORLOG?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC1:TCOTPSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC1:TCOCPSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("TC1:TCRTOCPSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("PCB:TMACTTEMP?");
    vTaskDelay(pdMS_TO_TICKS(500));
    (void)m207_send_raw("PCB:TMOTPSTATUS?");
    vTaskDelay(pdMS_TO_TICKS(500));

    /* C3 变体 + C1 别名 + V-5 ERRORCODE 注入（只读/安全值；TCSW 全程 0）*/
    (void)m207_send_raw("SYSTEM:SYSMODEL?");    /* C3 型号变体裁定 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCADJTEMP?");
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCADJUSTTEMP?");   /* C1 别名裁定 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("FOO:BAR=1");           /* V-5 期望 err=0 未知模块 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:NOTEXIST?");       /* 期望 err=2 未知参数 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("GARBAGE");             /* 期望 err=6 语法 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCACTTEMP=999");   /* 只读禁写（实录回码 3/4 依实现）*/
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCADJTEMP=999");   /* 期望 err=4 可写参数越界拒绝 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCADJTEMP?");      /* 回读护栏：确认 999 未被钳位接受 */
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCSW?@0#FF");      /* 期望 err=7 坏 XOR 校验（#FF 几乎必错）*/
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCADJTEMP!");      /* 期望 err=8 SAVE ACK（保存当前值=幂等）*/
    vTaskDelay(pdMS_TO_TICKS(800));
    (void)m207_send_raw("TC1:TCACTTEMP!");      /* 不可存参数保存——正典§5 实录（回 1/3/2 依实现）*/
    vTaskDelay(pdMS_TO_TICKS(800));

    /* COMPUSHLIST 自发现试跑（正典 §7；行超 64B 由 RAWLOG hex 兜底）*/
    (void)m207_send_raw("PCRS232:COMPUSHLIST=1");
    vTaskDelay(pdMS_TO_TICKS(4000));

    /* E-23 提前：节流压测 10 连写（R1b 形态；期望 0 丢 0 NAK 全确认）*/
    uint16_t v0 = m207_stats_verify_ok(), n0 = m207_stats_nak(), d0 = m207_stats_wdrop();
    for (int i = 0; i < 10; i++) {
        int rc = m207_set_temp(1, 25.0f + (i % 2));
        if (rc) ESP_LOGW(TAG, "[RP] burst enqueue rc=%d i=%d", rc, i);
    }
    vTaskDelay(pdMS_TO_TICKS(4000));
    ESP_LOGI(TAG, "[RP] burst: vok+%u nak+%u wdrop+%u (expect 10/0/0)",
             (unsigned)(m207_stats_verify_ok() - v0),
             (unsigned)(m207_stats_nak() - n0),
             (unsigned)(m207_stats_wdrop() - d0));
    ESP_LOGI(TAG, "[RP] safe state: tec1=%u tec2=%u (expect 0/0)", 
             g_tcs.tec1_on, g_tcs.tec2_on);

    /* E-06 前半：双通道真值 60s 录帧（每 5s 一行；值差/合理性由 H6.7 服务侧把关）*/
    for (int t = 5; t <= 60; t += 5) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ESP_LOGI(TAG, "[E06] t=%ds tc1=%.2f tc2=%.2f online=%u nak=%u",
                 t, (double)m207_last_temp(1), (double)m207_last_temp(2),
                 m207_online(), m207_stats_nak());
    }
    ESP_LOGI(TAG, "[RP] === realprobe end ===");
}
#endif /* CONFIG_TCS_M207_REALPROBE */
