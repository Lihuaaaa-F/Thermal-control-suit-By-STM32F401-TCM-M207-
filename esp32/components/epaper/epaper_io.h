#pragma once
/* 组件内部共享头: io 层原语(chip 层与 facade 共用,不对组件外暴露)
 * CS 手动跨事务;DC=0 命令 / DC=1 数据 */
#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"

spi_device_handle_t epaper_spi(void);
esp_err_t epaper_write_cmd(uint8_t cmd);
esp_err_t epaper_write_data(uint8_t data);
esp_err_t epaper_write_data_buf(const uint8_t *buf, uint32_t len);
