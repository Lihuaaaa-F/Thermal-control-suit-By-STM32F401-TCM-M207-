/* UC8151 芯片命令层实现 —— 命令序列移植自 porting-ref/EPD_2in9bc.c(V3.0, 2019)
 * 工程修正(相对参考代码): ①所有 busy 等待带超时(参考为无界死等)
 *                        ②平面传输用单事务 DMA(参考逐字节 CS 翻转,慢且无意义)
 *                        ③POF 后统一收尾,不留带电态 */
#include "uc8151_bc.h"
#include "epaper_io.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"

static const char *TAG = "UC8151";

esp_err_t uc8151_bc_reset(void)
{
    gpio_set_level(CONFIG_EPAPER_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    gpio_set_level(CONFIG_EPAPER_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(CONFIG_EPAPER_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    return ESP_OK;
}

esp_err_t uc8151_bc_wait_idle(uint32_t timeout_ms, bool require_busy)
{
    /* 开漏语义(v1.2 判别实验定论): BUSY 忙=拉低,空闲=释放(上拉读高)。
     * 等待"出现低(忙)→回高(空闲)";require_busy=true(刷新)必须见到忙相 */
    bool saw_busy = false;
    uint32_t waited = 0;
    int64_t busy_start = 0;

    while (waited < timeout_ms) {
        int lv = gpio_get_level(CONFIG_EPAPER_BUSY_GPIO);
        if (lv == 0) {
            if (!saw_busy) {
                saw_busy = true;
                busy_start = esp_timer_get_time() / 1000;
            }
        } else if (saw_busy) {
            ESP_LOGI(TAG, "忙相 %lld ms 后回空闲", (long long)((esp_timer_get_time() / 1000) - busy_start));
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
    if (!saw_busy) {
        if (!require_busy) {
            ESP_LOGW(TAG, "未见忙相(命令瞬时完成或芯片无响应),按通过继续");
            return ESP_OK;
        }
        ESP_LOGE(TAG, "刷新命令后 %lu ms 未见忙相(芯片无响应)", (unsigned long)timeout_ms);
    } else {
        ESP_LOGE(TAG, "忙相 %lu ms 未结束(超时)", (unsigned long)timeout_ms);
    }
    return ESP_ERR_TIMEOUT;
}

esp_err_t uc8151_bc_init(void)
{
    esp_err_t err = uc8151_bc_reset();
    if (err != ESP_OK) return err;

    if ((err = epaper_write_cmd(0x06)) != ESP_OK) return err;   /* BOOSTER_SOFT_START */
    epaper_write_data(0x17); epaper_write_data(0x17); epaper_write_data(0x17);

    if ((err = epaper_write_cmd(0x04)) != ESP_OK) return err;   /* POWER_ON */
    if ((err = uc8151_bc_wait_idle(UC8151_BUSY_TIMEOUT_MS, false)) != ESP_OK) return err;

    if ((err = epaper_write_cmd(0x00)) != ESP_OK) return err;   /* PANEL_SETTING */
    if ((err = epaper_write_data(0x8F)) != ESP_OK) return err;

    if ((err = epaper_write_cmd(0x50)) != ESP_OK) return err;   /* VCOM_AND_DATA_INTERVAL */
    if ((err = epaper_write_data(0x77)) != ESP_OK) return err;

    if ((err = epaper_write_cmd(0x61)) != ESP_OK) return err;   /* TCON_RESOLUTION 128x296 */
    epaper_write_data(0x80); epaper_write_data(0x01); epaper_write_data(0x28);

    if ((err = epaper_write_cmd(0x82)) != ESP_OK) return err;   /* VCM_DC_SETTING */
    return epaper_write_data(0x0A);
}

esp_err_t uc8151_bc_display(const uint8_t *black, const uint8_t *red, uint32_t plane_bytes)
{
    if (!black || !red) {
        return ESP_ERR_INVALID_ARG;
    }
    esp_err_t err;
    if ((err = epaper_write_cmd(0x10)) != ESP_OK) return err;   /* data start: 黑平面 */
    if ((err = epaper_write_data_buf(black, plane_bytes)) != ESP_OK) return err;
    if ((err = epaper_write_cmd(0x92)) != ESP_OK) return err;   /* partial out(参考序列原样) */

    if ((err = epaper_write_cmd(0x13)) != ESP_OK) return err;   /* data start: 红平面 */
    if ((err = epaper_write_data_buf(red, plane_bytes)) != ESP_OK) return err;
    if ((err = epaper_write_cmd(0x92)) != ESP_OK) return err;

    if ((err = epaper_write_cmd(0x12)) != ESP_OK) return err;   /* DISPLAY_REFRESH */
    return uc8151_bc_wait_idle(UC8151_REFRESH_TIMEOUT_MS, true);
}

esp_err_t uc8151_bc_clear(uint32_t plane_bytes)
{
    esp_err_t err;
    if ((err = epaper_write_cmd(0x10)) != ESP_OK) return err;
    for (uint32_t i = 0; i < plane_bytes; i++) {
        if ((err = epaper_write_data(0xFF)) != ESP_OK) return err;   /* 参考 Clear 逐字节 0xFF(1=白) */
    }
    if ((err = epaper_write_cmd(0x13)) != ESP_OK) return err;
    for (uint32_t i = 0; i < plane_bytes; i++) {
        if ((err = epaper_write_data(0xFF)) != ESP_OK) return err;
    }
    if ((err = epaper_write_cmd(0x12)) != ESP_OK) return err;
    return uc8151_bc_wait_idle(UC8151_REFRESH_TIMEOUT_MS, true);
}

esp_err_t uc8151_bc_sleep(void)
{
    esp_err_t err;
    if ((err = epaper_write_cmd(0x02)) != ESP_OK) return err;   /* POWER_OFF */
    if ((err = uc8151_bc_wait_idle(UC8151_BUSY_TIMEOUT_MS, false)) != ESP_OK) return err;
    if ((err = epaper_write_cmd(0x07)) != ESP_OK) return err;   /* DEEP_SLEEP */
    return epaper_write_data(0xA5);                             /* check code */
}
