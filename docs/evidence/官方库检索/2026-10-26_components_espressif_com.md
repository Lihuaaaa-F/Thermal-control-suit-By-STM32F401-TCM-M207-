# components.espressif.com 检索留档（F5 证据本体）

- 检索日期: 2026-10-26（W0 红队 must-fix 1 补证；原清单 v5 F5 行检索发生于同日早期，本留档为可复核重取）
- 检索方式: WebFetch 抓取页面并结构化提取（页面为注册表公开页，无需登录）
- 复跑: 打开下列 URL 任一浏览器/HTTP 客户端可见同样内容

## ① espressif/esp_lcd_touch_ft5x06（FT5446 同族官方触摸件）

URL: https://components.espressif.com/components/espressif/esp_lcd_touch_ft5x06

- 组件全名: espressif/esp_lcd_touch_ft5x06
- 最新版本: **1.1.1**（标注 Latest）
- 维护方: Espressif Systems（官方 espressif 命名空间）
- 简述: "ESP LCD Touch FT5x06 - touch controller FT5x06"（esp_lcd_touch 框架，I2C 实测支持，SPI 仅理论）
- 支持目标: all targets
- 添加命令: idf.py add-dependency "espressif/esp_lcd_touch_ft5x06^1.1.1"
- **判定**: 存在官方件 ✓（与清单 F5 行"v1.1.1"断言一致）；面向 ESP32 esp_lcd 栈，H743 侧仅作寄存器时序对照（v13.4 §2.4 物料获取归 W1）

## ② TMP117 检索（关键词 tmp117）

URL: https://components.espressif.com/components?q=tmp117

- 结果数: **1**
- 唯一组件: solderedelectronics/soldered-tmp117-esp-idf-component v1.0.0
  - 作者/命名空间: solderedelectronics（第三方）
  - 许可证: **GPL-3.0-only**
  - 描述: Soldered Temperature Sensor TMP117 Breakout component（绑定其自家 Breakout 板）
  - 下载量: 20
- **判定**: 无 espressif 官方 TMP117 件；唯一第三方件 GPL-3.0-only → 合法拒绝（GPL 污染+板绑定），与清单 F5 行断言一致 → 自研 tmp117 服务（参考 TI 数据手册时序）维持
