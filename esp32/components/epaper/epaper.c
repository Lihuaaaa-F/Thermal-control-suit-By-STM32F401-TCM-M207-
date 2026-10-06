#include "epaper.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
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
    /* 已被别的外设占用(INVALID_STATE)视为可复用 */
    esp_err_t err = spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "spi_bus_initialize: %s", esp_err_to_name(err));
        return err;
    }

    /* CS 用手动 GPIO 而非硬件片选: UC8151 一次操作 = "DC=0 命令 + DC=1 数据"
     * 多字节序列,CS 须跨事务保持低;硬件 CS 每事务结束自动拉高会截断序列 */
    spi_device_interface_config_t dev = {
        .clock_speed_hz = 2 * 1000 * 1000,   /* 首屏 2M 求稳,UC8151 规格上限约 4.5M,S4 门升频 */
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
    /* 初始电平显式: CS=1(未选中) DC=0 RST=0(保持复位,自检/驱动先做复位脉冲) */
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 1);
    gpio_set_level(CONFIG_EPAPER_DC_GPIO, 0);
    gpio_set_level(CONFIG_EPAPER_RST_GPIO, 0);

    /* BUSY: UC8151 高电平=忙、低=空闲(SSD1680/1681 系相反,勿混)。面板主动驱动,不加上下拉 */
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
    return ESP_OK;
}

/* ---- UC8151 命令原语(io 层): 手动 CS 跨事务,DC 区分命令/数据 ---- */
static esp_err_t uc8151_write_cmd(uint8_t cmd)
{
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 0);
    gpio_set_level(CONFIG_EPAPER_DC_GPIO, 0);
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 1);
    return err;
}

static esp_err_t uc8151_write_data(uint8_t data)
{
    spi_transaction_t t = { .length = 8, .tx_buffer = &data };
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 0);
    gpio_set_level(CONFIG_EPAPER_DC_GPIO, 1);
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 1);
    return err;
}

/* S1 门自检(v1.2): 复位脉冲后发最小命令交换 PON(0x04)→等 BUSY 拉高回落→POF(0x02)。
 * 只复位不发命令会误判——UC8151 的 BUSY 需 PON 才拉起(工作流 D1 修正) */
esp_err_t epaper_selftest_bus(epaper_bus_test_t *out)
{
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_bus_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    gpio_set_level(CONFIG_EPAPER_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CONFIG_EPAPER_RST_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(CONFIG_EPAPER_RST_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    esp_err_t err = uc8151_write_cmd(0x04);   /* PON: power on */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[TEST] PON 发送失败: %s", esp_err_to_name(err));
        return err;
    }

    bool saw_high = false, ready_after_high = false;
    for (int i = 0; i < 320; i++) {   /* 320 x 10ms = 3.2s */
        if (gpio_get_level(CONFIG_EPAPER_BUSY_GPIO)) {
            saw_high = true;
        } else if (saw_high) {
            ready_after_high = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uc8151_write_cmd(0x02);   /* POF: power off(无论结果都收尾,不留带电态) */

    if (ready_after_high) {
        *out = EPAPER_BUS_OK;
    } else if (saw_high) {
        *out = EPAPER_BUS_STUCK_BUSY;
    } else {
        *out = EPAPER_BUS_NO_ACTIVITY;
    }
    ESP_LOGI(TAG, "[TEST] S1 bus selftest(PON/POF): %s",
             *out == EPAPER_BUS_OK ? "OK(面板活+命令通)" :
             *out == EPAPER_BUS_STUCK_BUSY ? "STUCK_BUSY(3s不回落,查接线)" : "NO_ACTIVITY(芯片对命令零响应=未接/坏)");
    return ESP_OK;
}

/* ---- chip 层桩: S2 移植 porting-ref 命令序列后替换 ---- */

esp_err_t epaper_clear(void)
{
    ESP_LOGW(TAG, "epaper_clear: 桩(未移植驱动)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t epaper_draw_full(const uint8_t *black_plane, const uint8_t *red_plane)
{
    ESP_LOGW(TAG, "epaper_draw_full: 桩(未移植驱动), black=%p red=%p", black_plane, red_plane);
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t epaper_deep_sleep(void)
{
    ESP_LOGW(TAG, "epaper_deep_sleep: 桩(未移植驱动)");
    return ESP_ERR_NOT_SUPPORTED;
}
