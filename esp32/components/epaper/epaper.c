#include "epaper.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "EPAPER";
static spi_device_handle_t s_spi;
static bool s_bus_ready;

esp_err_t epaper_init(void)
{
    if (s_bus_ready) {
        return ESP_OK;
    }

    spi_bus_config_t bus = {
        .mosi_io_num = CONFIG_EPAPER_MOSI_GPIO,
        .sclk_io_num = CONFIG_EPAPER_SCLK_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    /* 已经被别的外设占用时(INVALID_STATE)视为可复用,不视为错误 */
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    /* CS 用手动 GPIO 而非硬件片选: UC8151 一次操作 = "DC=0 发命令 + DC=1 发数据"
     * 的多字节序列,CS 须跨事务保持低;硬件 CS 每事务结束自动拉高,会把序列截断 */
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 2 * 1000 * 1000,   /* 首屏走 2M 求稳(UC8151 上限约 4.5M),点亮后再上调 */
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 4,
    };
    err = spi_bus_add_device(SPI2_HOST, &dev, &s_spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device: %s", esp_err_to_name(err));
        return err;
    }

    gpio_config_t out = {
        .pin_bit_mask = (1ULL << CONFIG_EPAPER_DC_GPIO) |
                        (1ULL << CONFIG_EPAPER_RST_GPIO) |
                        (1ULL << CONFIG_EPAPER_CS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out));
    /* 初始电平显式声明: CS=1(未选中) / DC=0 / RST=0(保持复位态,驱动移植第一步做复位脉冲) */
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 1);
    gpio_set_level(CONFIG_EPAPER_DC_GPIO, 0);
    gpio_set_level(CONFIG_EPAPER_RST_GPIO, 0);

    /* BUSY: UC8151 高电平=忙、低=空闲(注意 SSD1680/1681 系相反是低=忙;移植认准 V2 wiki)。
     * 面板会主动驱动此脚,不加上下拉;读恒 0 且无动作 = 屏未接 */
    gpio_config_t in = {
        .pin_bit_mask = 1ULL << CONFIG_EPAPER_BUSY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    s_bus_ready = true;
    ESP_LOGI(TAG, "[DISPLAY] bus ready: SCLK=%d MOSI=%d CS=%d DC=%d RST=%d BUSY=%d",
             CONFIG_EPAPER_SCLK_GPIO, CONFIG_EPAPER_MOSI_GPIO, CONFIG_EPAPER_CS_GPIO,
             CONFIG_EPAPER_DC_GPIO, CONFIG_EPAPER_RST_GPIO, CONFIG_EPAPER_BUSY_GPIO);
    ESP_LOGI(TAG, "[DISPLAY] %dx%d, draw ops 为桩(待移植 UC8151 命令序列)", EPAPER_WIDTH_PX, EPAPER_HEIGHT_PX);
    return ESP_OK;
}

/* ---- 桩实现: 屏到手后移植微雪 EPD_2in9_V2 命令序列,替换以下四个函数 ---- */

esp_err_t epaper_clear(void)
{
    ESP_LOGW(TAG, "epaper_clear: 桩(未移植驱动)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t epaper_draw_full(const uint8_t *fb)
{
    ESP_LOGW(TAG, "epaper_draw_full: 桩(未移植驱动), fb=%p", fb);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t epaper_draw_partial(const uint8_t *fb)
{
    ESP_LOGW(TAG, "epaper_draw_partial: 桩(未移植驱动), fb=%p", fb);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t epaper_deep_sleep(void)
{
    ESP_LOGW(TAG, "epaper_deep_sleep: 桩(未移植驱动)");
    return ESP_ERR_NOT_SUPPORTED;
}
