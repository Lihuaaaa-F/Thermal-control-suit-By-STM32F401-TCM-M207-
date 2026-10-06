# 调温服 Thermal Control Suit

STM32F401 主控(温控) + ESP32-S3 N16R8 无线端(WiFi+BLE)。

- `esp32/` — ESP-IDF v5.5.x 工程。sdkconfig.defaults / partitions-16mb.csv 已按 N16R8(16MB Flash + 8MB Octal PSRAM)配好;改配置只改 sdkconfig.defaults,不手改 sdkconfig。
- `esp32/components/epaper/` — 微雪 2.9" V2 墨水屏(UC8151,296×128)驱动组件。引脚 SCLK=12/MOSI=11/CS=13/DC=14/RST=21/BUSY=39(Kconfig 可改);**屏到手后把微雪 wiki 的 EPD_2in9_V2 命令序列移植进 epaper.c,替换四个桩函数**;GPIO1-10 预留给未来 NTC(ADC1)。
- STM32 端(CubeMX/Keil)待建:建前清单与纪律见 embedded-dev-loop skill 的 platforms/stm32/。
- 开发纪律与工具:见 skill `embedded-dev-loop`(根内核 + 平台附录);ESP32 日常闭环:
  `bash ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh all`
