/**
 * main.c — 调温服 STM32H743IIT6 主程序（project_tcs，任务书 §3）
 *
 * H743 = 唯一数据源（参数注册表），ESP32 = 纯桥 + Web 服务器，屏与网页皆为视图。
 *
 * 主循环 10ms 节拍（SysTick 1ms 分频），时序预算 ≤8ms/节拍（§3.2 铁律）：
 *   喂狗 → comm_poll_tx → console_poll → m207_poll → 链路 RX 喂 comm → comm_tick
 *   → tcs_ui_poll → control_poll(500ms) → tmp117_poll(100ms) → param_persist_flush(2s)
 *   → STATUS 帧(500ms) → KEY0 急停 → LED 心跳 → 等下一节拍
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
#include "tcs_ui.h"
#include "fan.h"
#include "eeprom.h"
#include "iwdg.h"
#include "blackbox.h"
#include <string.h>

/* ====== 全局句柄 ====== */
UART_HandleTypeDef huart_console;   /* USART1 PA9/PA10 控制台 */
UART_HandleTypeDef huart_link;      /* USART2 PD5/PA3 ESP32 链路 */
UART_HandleTypeDef huart_m207;      /* USART3 PB10/PB11 M207 */
I2C_HandleTypeDef  hi2c_tmp117;     /* I2C1 PB8/PB9 TMP117 */

/* ====== 时间基准（SysTick 1ms）====== */
static volatile uint32_t g_tick_ms = 0;
uint32_t millis(void) { return g_tick_ms; }

/* SysTick 1ms 双递增覆盖（GS 工程移植时曾丢失本函数 → 主循环卡死在节拍等待，
 * IWDG 32.7s 复位循环；根因记录 docs/evidence/WP6_iwdg_defect.md） */
void HAL_IncTick(void)
{
    uwTick++;
    g_tick_ms++;
}

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

void link_uart_error_recover(void)
{
    if (__HAL_UART_GET_FLAG(&huart_link, UART_FLAG_ORE))
        __HAL_UART_CLEAR_OREFLAG(&huart_link);
    HAL_UART_AbortReceive(&huart_link);
    link_rx_rearm();
}

static void link_rx_drain(void)
{
    uint16_t guard = LINK_RX_SIZE + 2u;
    while (guard--) {
        if (s_lrx_head == s_lrx_tail) break;
        uint8_t b = s_lrx[s_lrx_tail];
        s_lrx_tail = (uint16_t)((s_lrx_tail + 1u) & LINK_RX_MASK);
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
    /* 屏：ui_sync_state 500ms 扫描 g_tcs（M4 ≤1s 同步预算内）；
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

static void hook_m207_set(uint8_t ch, float v) { (void)m207_set_temp(ch, v); }
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
        /* 注册表全量 → 0x14 帧 ×N（txq 8 深：7 参数 + 本 ACK = 8 恰好）*/
        link_send_ack(seq, mt, TCS_ACK_OK, 0.0f);
        int n = param_count();
        for (int i = 0; i < n; i++) {
            const param_desc_t *p = param_at(i);
            tcs_reg_entry_t e;
            memset(&e, 0, sizeof(e));
            e.count = (uint8_t)n;
            e.idx   = (uint8_t)i;
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
static void uart_gpio_af(GPIO_TypeDef *port, uint32_t pins, uint32_t af)
{
    GPIO_InitTypeDef gi = {0};
    gi.Pin = pins;
    gi.Mode = GPIO_MODE_AF_PP;
    gi.Pull = GPIO_NOPULL;
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
    uart_gpio_af(GPIOA, GPIO_PIN_9 | GPIO_PIN_10, GPIO_AF7_USART1);
    uart_common_init(&huart_console, USART1, CONSOLE_BAUD, USART1_IRQn);

    /* USART2 PD5(TX)/PA3(RX) AF7 ESP32 链路（接线表 #3/#4：混搭映射合法）*/
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    uart_gpio_af(GPIOD, GPIO_PIN_5, GPIO_AF7_USART2);
    uart_gpio_af(GPIOA, GPIO_PIN_3, GPIO_AF7_USART2);
    uart_common_init(&huart_link, USART2, LINK_BAUD, USART2_IRQn);

    /* USART3 PB10/PB11 AF7 M207（接线表 #1/#2）*/
    __HAL_RCC_USART3_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    uart_gpio_af(GPIOB, GPIO_PIN_10 | GPIO_PIN_11, GPIO_AF7_USART3);
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
    blackbox_fault(0xE110u, 0, 0, 0, 0);   /* T1：留痕后停摆等狗 */
    __disable_irq();
    SEGGER_RTT_WriteString("ERROR_HANDLER\n");
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

    blackbox_boot();   /* T1 黑匣子：抢在 console 清 RSR 之前抓复位源与上轮现场 */

    SystemClock_Config();
    MX_GPIO_Init();
    MX_UART_Init();
    MX_I2C1_Init();

    console_init(&huart_console);
    console_print_reset_source();
    tcs_log("[SYSTEM]", "boot project_tcs fw=v1.0.0 HCLK=%lu MHz",
            (unsigned long)(SystemCoreClock / 1000000U));

    iwdg_init();   /* ~2s */

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
    param_registry_init(&(param_hooks_t){
        .on_broadcast  = hook_broadcast,
        .on_m207_set   = hook_m207_set,
        .on_mode_change= hook_mode_change,
        .eeprom_commit = hook_eeprom_commit,
    });
    if (ee_rv == 0) {
        eeprom_selftest();
        uint8_t img[EE_IMAGE_SIZE];
        if (eeprom_read_image(img) == 0) {
            if (param_deserialize(img) != 0) {
                tcs_log("[WARN]", "eeprom cfg invalid -> defaults");
                tcs_alarm_raise(ALM_EEPROM);
            } else {
                tcs_log("[SYSTEM]", "eeprom params loaded");
            }
        }
    } else {
        tcs_log("[WARN]", "eeprom offline -> RAM only");
        tcs_alarm_raise(ALM_EEPROM);
    }

    /* 安全控制 + 外设桥 */
    control_init(&(control_ops_t){
        .fan_set  = fan_set,
        .tec_sw   = ops_tec_sw,
        .tec_temp = ops_tec_temp,
        .on_state = NULL,
    });
    m207_init(&huart_m207);
    tmp117_init(&hi2c_tmp117);
    fan_init();

    comm_init(&g_link, link_uart_send, now_ms_fn, on_link_down, crit_enter, crit_exit);

#ifdef ENABLE_LCD
    tcs_ui_init();
#endif
    tcs_log("[SYSTEM]", "init done, entering main loop");

    uint32_t last_tick = 0;
    uint8_t led = 0;
    static uint32_t last_led_ms = 0;

    while (1) {
        uint32_t now = g_tick_ms;

        iwdg_refresh();
        blackbox_loop_tick();
        blackbox_heartbeat(now);
        comm_poll_tx(&g_link);           /* 链路 TX 串行消费（唯一 HAL_UART_Transmit 处之一）*/
        console_poll();
        m207_poll(now);
        link_rx_drain();
        comm_tick(&g_link);              /* 重传检查（主循环上下文）*/

#ifdef ENABLE_LCD
        tcs_ui_poll(now);
#endif
        control_poll(now);
        tmp117_poll(now);
        param_persist_flush(now);
        status_tx(now);
        kill_key_poll();

        if (g_link_down_flag) {
            g_link_down_flag = 0;
            tcs_log("[NET]", "esp link down (3 retries)");
        }
        g_tcs.esp_link_ok = (g_link.link_ok != 0) ? 1u : 0u;

        if (now - last_led_ms >= 500u) {
            last_led_ms = now;
            HAL_GPIO_WritePin(LED1_GPIO_Port, LED1_Pin, (led ^= 1) ? GPIO_PIN_RESET : GPIO_PIN_SET);
        }
        /* 10ms 节拍（SysTick 分频）：预算内工作完成后等待下一拍 */
        while (g_tick_ms - last_tick < 10u) { }
        last_tick = g_tick_ms;
    }
}
