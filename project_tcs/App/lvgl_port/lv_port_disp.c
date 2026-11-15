/* lv_port_disp.c — G4-Z21b：PARTIAL 渲染 + 隐藏帧 memcpy + VBR 上屏
 *
 * 实证（G4-Z21 前身）：LVGL v9.6 DIRECT 双缓冲不交替渲染目标（g_shown_fb 钉死
 * buf1）→ 每帧渲染落进正被 LTDC 扫描的缓冲=滑动/动画闪频真根因（用户目检+probe
 * 双证据）。本模式为 MCU 标准无撕裂画法：
 *   LVGL 渲染 → AXI 小块（不碰 fb）→ flush 逐行 memcpy 进「隐藏帧」→
 *   末次 flush 触发 VBR 上屏（消隐期重载，无撕裂）→ 隐藏/显示角色互换。
 * 帧缓冲零 CPU 读（写入直透 SDRAM），无 cache 维护需求（init 处防御性清一次）。 */
#include "lv_port_disp.h"
#include "ltdc.h"
#include "stm32h7xx_hal.h"
#include <string.h>

extern void ltdc_show_buffer(uint16_t * p);   /* ltdc.c：CFBAR+VBR 无撕裂上屏 */
extern volatile uint32_t g_shown_fb;          /* ltdc.c：当前显示帧地址（fb_dump 同源） */

/* G5-Z27（终极正解）：chunk 放 SDRAM 空闲区并扩为整屏高——任何刷新 area 永不
 * 被 chunk 行数截断（此前 60 行 chunk 遇大 area 时，超出行把 chunk 后的 AXIRAM
 * 数据当像素拷进 fb=滑动/角标闪烁残影家族的最后一个根因）。SDRAM 与 LTDC/FMC
 * 共享带宽与已删除的旧 tcs_ui 直绘同量级；Z18 防御性 cache 清理对 SDRAM 同样保留。 */
#define CHUNK_BUF ((uint16_t *)0xC0258000)       /* 两块 fb(2×1.2MB) 之后的 1.2MB 空闲 */
#define CHUNK_LINES 600
static uint16_t * s_hidden = LCD_BACK_BUF;       /* 隐藏帧（=非显示缓冲） */

static void flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    /* G4-Z22（角标区闪烁根因）：PARTIAL+交替翻转架构下，每帧都更新的区域
     * （perf 角标）两 fb 各持交替历史 → 翻转交替显示旧/新=闪。修复=脏区
     * 双写两块 fb（两 fb 恒等→交替与否则无视觉差异；VBR 上屏保无撕裂）。 */
    int32_t aw = area->x2 - area->x1 + 1;
    int32_t ah = area->y2 - area->y1 + 1;
    /* G4-Z23（闪屏+字形压扁共同根因）：PARTIAL 的 px_map 行距=整屏宽（draw_buf 以
     * 整屏宽 × chunk 行创建，lv_display_set_buffers_with_stride 实证），此前按 area
     * 宽度取行距 → 窄区域被纵向压扁（96px 大字成小横杠；全宽区域碰巧正确）。 */
    uint32_t src_stride = lv_draw_buf_width_to_stride((uint32_t)LCD_WIDTH, LV_COLOR_FORMAT_RGB565);
    /* G4-Z26：px_map=chunk 基址 + 区域按 chunk 高度取模（跨 chunk 大区分段渲染，
     * 每段从 chunk 第 0 行重写；src 行号须对 60 取模，否则越界读 chunk 后 AXIRAM） */
    /* G5-Z28（终极修复）：px_map=chunk 基址，LVGL 按「绝对屏幕坐标」渲染写入
     * （实证：chunk 全屏化后 hero 区出现 TC1 卡复制=拷了 chunk 低位行）。
     * src 按 area 绝对坐标偏移取行。 */
    uint16_t * dst1 = LCD_FRONT_BUF + (uint32_t)area->y1 * LCD_WIDTH + area->x1;
    uint16_t * dst2 = LCD_BACK_BUF + (uint32_t)area->y1 * LCD_WIDTH + area->x1;
    uint8_t * src = px_map + (uint32_t)area->y1 * src_stride + (uint32_t)area->x1 * 2;
    for(int32_t y = 0; y < ah; y++) {
        memcpy(dst1, src, (size_t)aw * 2);
        memcpy(dst2, src, (size_t)aw * 2);
        dst1 += LCD_WIDTH;
        dst2 += LCD_WIDTH;
        src += src_stride;
    }
    lv_display_flush_ready(disp);
}

void lv_port_disp_init(void)
{
    /* G4-Z18：启动期 DMA2D 清屏绕 D-Cache，fb 残留陈旧行——防御性清一次
     *（PARTIAL 模式对 fb 只写不读，实际无维护需求，保留为口径一致） */
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)LCD_FRONT_BUF,
                                      (int32_t)(LCD_WIDTH * LCD_HEIGHT * 2));
    SCB_CleanInvalidateDCache_by_Addr((uint32_t *)LCD_BACK_BUF,
                                      (int32_t)(LCD_WIDTH * LCD_HEIGHT * 2));
    /* G5-Z31：清除 SDRAM 旧固件画面残留（烧录不清 SDRAM，旧 benchmark 帧在
     * LVGL 首帧未覆写前透出）。bg=#EEF1F5 → RGB565 0x9DE9 */
    {
        uint16_t bg565 = 0x9DE9u;
        for (uint32_t i = 0; i < (uint32_t)LCD_WIDTH * LCD_HEIGHT; i++) {
            LCD_FRONT_BUF[i] = bg565;
            LCD_BACK_BUF[i] = bg565;
        }
    }

    lv_display_t * disp = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    lv_display_set_flush_cb(disp, flush_cb);
    lv_display_set_buffers(disp, CHUNK_BUF, NULL,
                           (uint32_t)LCD_WIDTH * CHUNK_LINES * 2,
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
}
