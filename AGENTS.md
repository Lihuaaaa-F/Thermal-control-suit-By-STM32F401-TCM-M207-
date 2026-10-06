# 调温服 Thermal Control Suit

STM32F401 主控(温控) + ESP32-S3 N16R8 无线端(WiFi+BLE)。

- `esp32/` — ESP-IDF v5.5.x 工程。sdkconfig.defaults / partitions-16mb.csv 已按 N16R8(16MB Flash + 8MB Octal PSRAM)配好;改配置只改 sdkconfig.defaults,不手改 sdkconfig。
- `esp32/components/epaper/` — 微雪 WFT0290CZ10 三色墨水屏(红/黑/白,**UC8151D**,296×128)组件。**执行依据 = `docs/epaper-workflow.md` v1.4（S0-S6 门禁+验收表+诊断记录）**。命令层已移植(uc8151_bc.c),paint 层七段数字渲染,epaper.h 双平面 API;引脚 SCLK=12/MOSI=11/CS=13/DC=14/RST=21/BUSY=39(**BUSY 开漏需上拉,代码已启用内部上拉**);测试走 Kconfig EPAPER_TEST_MODE(0-6);**当前阻塞:S0——裸屏 FPC 实为 24 脚(规格书已核,存 porting-ref/),升压电路外置;按 docs/wiring-check.md 第⓪步判明单元,纯裸屏须补 Module (B)/Driver HAT**;push 策略:feature 分支单元完成+审查后即 push;GPIO1-10 预留 NTC(ADC1)。
- STM32 端(CubeMX/Keil)待建:建前清单与纪律见 embedded-dev-loop skill 的 platforms/stm32/。
- 开发纪律与工具:见 skill `embedded-dev-loop`(根内核 + 平台附录);ESP32 日常闭环:
  `bash ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh all`
