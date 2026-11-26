/* lv_port_disp.c — PARTIAL 渲染 + 脏区双写两块帧缓冲（G4-Z21b 起）
 *
 * 机制（2026-11-27 按 M03-F-06 校正，与代码一致）：
 *   LVGL PARTIAL 只把脏区渲染进 SDRAM 里的 chunk（CHUNK_BUF，不碰帧缓冲）→
 *   flush_cb 按脏区逐行 memcpy 进 FRONT/BACK **两块**帧缓冲 → flush_ready。
 *   LTDC 始终扫描 FRONT（ltdc.c 初始化时设死），BACK 保持逐字节一致
 *   （fb_dump/工具与任何翻转路径读哪块都对）。本文件**没有** VBR / 隐藏帧
 *   翻转路径：早期设计（渲染进隐藏帧、末次 flush 在消隐期切 CFBAR）已随
 *   LVGL 迁移废弃，相关 static 指针 s_hidden 于 2026-11-24 删除（S 线 C01）。
 * 为什么这样：LVGL v9.6 DIRECT 双缓冲不交替渲染目标（渲染目标钉死 buf1）→
 * 每帧渲染落进正被 LTDC 扫描的缓冲=滑动/动画闪频真根因（用户目检+probe 双
 * 证据）。PARTIAL 把渲染目标与帧缓冲解耦（渲染进 chunk），脏区再双写回两块
 * 帧缓冲；这是 2026-11-23 起现场验收通过的显示路径（整页/局部刷新均正常）。
 * 帧缓冲零 CPU 读（写入直透 SDRAM），无 cache 维护需求（init 处防御性清一次）。 */
#include "lv_port_disp.h"
#include "ltdc.h"
#include "stm32h7xx_hal.h"
#include <string.h>

/* M03-F-06 清理（2026-11-27）：这里原有一行未使用的 extern g_shown_fb 声明
 * （旧 VBR 方案遗留，本文件无读者）。显示帧地址以 ltdc.c 的 g_shown_fb 为准，
 * fb_dump 等工具经 nm 直读，不需要本文件的声明。 */

/* G5-Z27（终极正解）：chunk 放 SDRAM 空闲区并扩为整屏高——任何刷新 area 永不
 * 被 chunk 行数截断（此前 60 行 chunk 遇大 area 时，超出行把 chunk 后的 AXIRAM
 * 数据当像素拷进 fb=滑动/角标闪烁残影家族的最后一个根因）。SDRAM 与 LTDC/FMC
 * 共享带宽与已删除的旧 tcs_ui 直绘同量级；Z18 防御性 cache 清理对 SDRAM 同样保留。 */
#define CHUNK_BUF ((uint16_t *)0xC0258000)       /* 两块 fb(2×1.2MB) 之后的 1.2MB 空闲 */
#define CHUNK_LINES 600
/* 2026-11-24（S 线 C01）：这里原有一个从未被使用的 static 指针 s_hidden（早期双帧方案的遗留），
 * 是每档构建 1 条"未使用变量"告警的来源，删掉。显示帧地址以 ltdc.c 的 g_shown_fb 为准。 */

static void flush_cb(lv_display_t * disp, const lv_area_t * area, uint8_t * px_map)
{
    int32_t aw = lv_area_get_width(area);
    int32_t ah = lv_area_get_height(area);
    /* 2026-11-23 现场实测修复（阶段 0 基线抓到）：LVGL 9 PARTIAL 模式下，draw_buf
     * 在每次 refr_area 被 reshape 成「区域宽 × 区域高、行距 = 区域宽」
     * （lv_refr.c: refr_area → layer_reshape_draw_buf(LV_STRIDE_AUTO)），px_map 从
     * 偏移 0 起紧密排列，而 flush 收到的 area 是绝对屏幕坐标。
     * 原实现按「整屏宽行距 + 绝对坐标偏移」取源——只有整屏 area（x1=y1=0、宽=1024）
     * 时两种算法才碰巧等价；任何局部刷新都从 chunk 里无关位置取像素拷回标签区。
     * 后果：首帧之后所有数值/横幅/状态变化都不上屏（现场实测：体温 36.8→38.5，
     * g_tcs 已变而帧缓冲字节级不变；hero 停在开机时的灰色 "--.-"）。整页重建走
     * 整屏 area 所以正常 → 表现为「切页能刷新、数字不动、横幅点不掉」。
     * 脏区双写两块 fb 的做法保留（Z22）。CHUNK_LINES=600 保证单条带，无需处理
     * 多条带的 partial_y_offset。*/
    uint32_t src_stride = lv_draw_buf_width_to_stride((uint32_t)aw, LV_COLOR_FORMAT_RGB565);
    uint16_t * dst1 = LCD_FRONT_BUF + (uint32_t)area->y1 * LCD_WIDTH + area->x1;
    uint16_t * dst2 = LCD_BACK_BUF + (uint32_t)area->y1 * LCD_WIDTH + area->x1;
    const uint8_t * src = px_map;
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
