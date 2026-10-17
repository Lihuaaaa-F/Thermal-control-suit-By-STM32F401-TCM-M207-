/**
 * keypad.c — 4×4 矩阵键盘扫描驱动
 *
 * 引脚：行 PC1-PC4 推挽，列 PD3/PD4/PD5/PD7 输入+内部下拉。
 * 扫描：逐行拉低，读列。列读到高 = 该行该列有按下。
 * 去抖：连续 KEYPAD_DEBOUNCE 次扫到同一键，且上一状态为"松开"，触发一次 KEY_DOWN。
 *       键松开后允许下一次触发（不连按；要连按自己改 release 标志逻辑）。
 *
 * 键值编号：row*4 + col（0..15）。
 *   row0(C0): 起飞/巡查/急停/返航
 *   row1(C1): 俯角降落/确认禁飞/清除禁飞/全清
 *   row2(C2): 发统计请求/发状态请求/保存回看/调出回看
 *   row3(C3): 1/2/3/取消
 * （键值→命令映射在 main.c 的 keypad_dispatch，不在本文件，便于改映射）
 *
 * 不依赖 LTDC/触摸就绪，协议层照跑。
 */
#include "keypad.h"
#include "stm32h7xx_hal.h"

static int g_ready = 0;

/* 去抖状态机 */
static uint8_t  g_last_key      = KEYPAD_NONE;   /* 上一次确认的键（0xFF=无）*/
static uint8_t  g_candidate     = KEYPAD_NONE;   /* 候选键（去抖中）*/
static uint8_t  g_cand_count    = 0;             /* 候选连续命中次数 */
static uint8_t  g_pressed_flag  = 0;             /* 当前键是否处于"已触发按下"状态 */
static uint32_t g_last_scan_ms  = 0;

int keypad_init(void)
{
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* 行：推挽输出，默认拉高（不选中）*/
    GPIO_InitTypeDef gi = {0};
    gi.Pin   = KP_ROW_PINS;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_PULLUP;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(KP_ROW_PORT, &gi);
    HAL_GPIO_WritePin(KP_ROW_PORT, KP_ROW_PINS, GPIO_PIN_SET);

    /* 列：输入 + 内部下拉（无键按下时读 0；按下经行低电平……见下）*/
    gi.Pin   = KP_COL_PINS;
    gi.Mode  = GPIO_MODE_INPUT;
    gi.Pull  = GPIO_PULLDOWN;
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(KP_COL_PORT, &gi);

    g_ready = 1;
    return 0;
}

int keypad_ready(void) { return g_ready; }

/* 一次物理扫描，返回当前按下的键值（0..15），无键返回 KEYPAD_NONE。
 * 逻辑：行逐个拉低，列读电平。
 *   —— 接线方式 1（本板）：行输出拉低选中，列上拉，按键把行列短接 → 按下时列被拉低。
 *      但本驱动列配的是"内部下拉"，所以需要行输出"高"选中、按键把高电平灌进列。
 *   为兼容两种键盘，这里用"行拉高选中 + 列下拉读高"判定，与 keypad.h 列下拉配置匹配。
 */
static uint8_t scan_once(void)
{
    static const uint16_t rows[4] = { KP_ROW0, KP_ROW1, KP_ROW2, KP_ROW3 };
    static const uint16_t cols[4] = { KP_COL0, KP_COL1, KP_COL2, KP_COL3 };

    /* 先全行拉低（空闲），再逐行扫描 */
    HAL_GPIO_WritePin(KP_ROW_PORT, KP_ROW_PINS, GPIO_PIN_RESET);

    for (int r = 0; r < 4; r++) {
        /* 选中第 r 行：该行拉高，其余行拉低 */
        HAL_GPIO_WritePin(KP_ROW_PORT, KP_ROW_PINS, GPIO_PIN_RESET);
        HAL_GPIO_WritePin(KP_ROW_PORT, rows[r], GPIO_PIN_SET);
        /* 短延时让电平稳定（GPIO 翻转几 ns 即可，这里给点裕量）*/
        for (volatile int d = 0; d < 20; d++) {}

        for (int c = 0; c < 4; c++) {
            if (HAL_GPIO_ReadPin(KP_COL_PORT, cols[c]) == GPIO_PIN_SET) {
                HAL_GPIO_WritePin(KP_ROW_PORT, KP_ROW_PINS, GPIO_PIN_RESET);
                return (uint8_t)(r * 4 + c);
            }
        }
    }
    HAL_GPIO_WritePin(KP_ROW_PORT, KP_ROW_PINS, GPIO_PIN_RESET);
    return KEYPAD_NONE;
}

uint8_t keypad_scan(uint32_t now_ms)
{
    if (!g_ready) return KEYPAD_NONE;
    if (now_ms - g_last_scan_ms < KEYPAD_SCAN_MS) return KEYPAD_NONE;
    g_last_scan_ms = now_ms;

    uint8_t cur = scan_once();

    if (cur == g_candidate) {
        if (g_cand_count < 0xFF) g_cand_count++;
        /* 连续去抖命中，且当前未处于"已按下"状态 → 触发一次按下事件 */
        if (g_cand_count >= KEYPAD_DEBOUNCE && cur != KEYPAD_NONE && !g_pressed_flag) {
            g_pressed_flag = 1;
            g_last_key = cur;
            return cur;               /* 返回有效按键事件 */
        }
    } else {
        g_candidate = cur;
        g_cand_count = 0;
        if (cur == KEYPAD_NONE) {
            g_pressed_flag = 0;       /* 松开，允许下一次触发 */
        }
    }
    return KEYPAD_NONE;
}
