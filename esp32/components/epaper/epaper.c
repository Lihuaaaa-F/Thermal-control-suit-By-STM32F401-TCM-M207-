/* epaper facade: 对上稳定 API,向下组合 io(本文件)+chip(uc8151_bc.c)+paint(独立)
 * 状态机: init → awake ⇄ asleep(深睡); clear/draw 在睡态下自动唤醒(auto-wake,防调用方踩状态) */
#include "epaper.h"
#include "epaper_io.h"
#include "uc8151_bc.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

static const char *TAG = "EPAPER";
static spi_device_handle_t s_spi;
static bool s_bus_ready;
static bool s_chip_ready;   /* 芯片命令序列已 init(≠ MCU 侧总线 ready) */
static bool s_asleep;

spi_device_handle_t epaper_spi(void)
{
    return s_spi;
}

/* ---- io 原语(chip 层经 epaper_io.h 共用): CS 手动跨事务,DC 区分命令/数据 ---- */

esp_err_t epaper_write_cmd(uint8_t cmd)
{
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 0);
    gpio_set_level(CONFIG_EPAPER_DC_GPIO, 0);
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 1);
    return err;
}

esp_err_t epaper_write_data(uint8_t data)
{
    spi_transaction_t t = { .length = 8, .tx_buffer = &data };
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 0);
    gpio_set_level(CONFIG_EPAPER_DC_GPIO, 1);
    esp_err_t err = spi_device_polling_transmit(s_spi, &t);
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 1);
    return err;
}

esp_err_t epaper_write_data_buf(const uint8_t *buf, uint32_t len)
{
    /* 单事务整平面传输: 4736B@2MHz ≈ 19ms 阻塞可接受;DMA 自动启用 */
    spi_transaction_t t = { .length = len * 8, .tx_buffer = buf };
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 0);
    gpio_set_level(CONFIG_EPAPER_DC_GPIO, 1);
    esp_err_t err = spi_device_transmit(s_spi, &t);
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 1);
    return err;
}

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

    /* CS 手动 GPIO: UC8151 一次操作 = "DC=0 命令 + DC=1 数据"序列,CS 须跨事务保持低 */
    spi_device_interface_config_t dev = {
        .clock_speed_hz = CONFIG_EPAPER_SPI_HZ,
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
    gpio_set_level(CONFIG_EPAPER_CS_GPIO, 1);
    gpio_set_level(CONFIG_EPAPER_DC_GPIO, 0);
    gpio_set_level(CONFIG_EPAPER_RST_GPIO, 0);

    /* BUSY: 开漏输出(v1.2 判别实验证实: 释放态悬空,忙=拉低,空闲=高)。
     * 裸屏直连无转接板上拉,必须启用内部上拉,否则空闲态电平不定 */
    gpio_config_t in = {
        .pin_bit_mask = 1ULL << CONFIG_EPAPER_BUSY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in));

    s_bus_ready = true;
    ESP_LOGI(TAG, "[DISPLAY] bus ready: SCLK=%d MOSI=%d CS=%d DC=%d RST=%d BUSY=%d @%dHz",
             CONFIG_EPAPER_SCLK_GPIO, CONFIG_EPAPER_MOSI_GPIO, CONFIG_EPAPER_CS_GPIO,
             CONFIG_EPAPER_DC_GPIO, CONFIG_EPAPER_RST_GPIO, CONFIG_EPAPER_BUSY_GPIO,
             CONFIG_EPAPER_SPI_HZ);

    /* 芯片命令序列初始化(复位+PON+PSR/TCON)——S2 首测教训: 只备总线不发序列,
     * 面板未上电未配置,刷新命令会卡死 BUSY */
    err = uc8151_bc_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "chip init 失败: %s", esp_err_to_name(err));
        return err;
    }
    s_chip_ready = true;
    s_asleep = false;
    ESP_LOGI(TAG, "[DISPLAY] chip init ok(UC8151 序列)");
    return ESP_OK;
}

static esp_err_t ensure_awake(void)
{
    if (s_chip_ready && !s_asleep) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "chip %s -> 重新 init(深睡后只能靠复位唤醒)", s_asleep ? "深睡中" : "未初始化");
    esp_err_t err = uc8151_bc_init();
    if (err == ESP_OK) {
        s_chip_ready = true;
        s_asleep = false;
    }
    return err;
}

/* ---- S1 门自检(v1.2): 复位脉冲 + 最小命令交换 PON→POF ---- */
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

    esp_err_t err = epaper_write_cmd(0x04);   /* PON */
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "[TEST] PON 发送失败: %s", esp_err_to_name(err));
        return err;
    }

    bool saw_low = false, high_after_low = false;
    for (int i = 0; i < 320; i++) {   /* 3.2s */
        if (gpio_get_level(CONFIG_EPAPER_BUSY_GPIO) == 0) {
            saw_low = true;   /* 忙(应答 PON) */
        } else if (saw_low) {
            high_after_low = true;   /* 忙毕回空闲 */
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    epaper_write_cmd(0x02);   /* POF 收尾 */

    if (high_after_low) {
        *out = EPAPER_BUS_OK;
    } else if (saw_low) {
        *out = EPAPER_BUS_STUCK_BUSY;
    } else {
        *out = EPAPER_BUS_NO_ACTIVITY;
    }
    ESP_LOGI(TAG, "[TEST] S1 bus selftest(PON/POF): %s",
             *out == EPAPER_BUS_OK ? "OK(面板应答 PON 并回空闲)" :
             *out == EPAPER_BUS_STUCK_BUSY ? "STUCK_BUSY(忙死,查接线)" : "NO_ACTIVITY(芯片零响应=未接/坏)");
    return ESP_OK;
}

/* ---- 刷屏 API(睡态自动唤醒) ---- */

esp_err_t epaper_clear(void)
{
    if (!s_bus_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ensure_awake();
    if (err != ESP_OK) return err;
    return uc8151_bc_clear(EPAPER_PLANE_BYTES);
}

esp_err_t epaper_draw_full(const uint8_t *black_plane, const uint8_t *red_plane)
{
    if (!s_bus_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ensure_awake();
    if (err != ESP_OK) return err;
    return uc8151_bc_display(black_plane, red_plane, EPAPER_PLANE_BYTES);
}

esp_err_t epaper_deep_sleep(void)
{
    if (!s_bus_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = uc8151_bc_sleep();
    if (err == ESP_OK) {
        s_asleep = true;
        ESP_LOGI(TAG, " 深睡(唤醒=下次刷屏自动 init)");
    }
    return err;
}
