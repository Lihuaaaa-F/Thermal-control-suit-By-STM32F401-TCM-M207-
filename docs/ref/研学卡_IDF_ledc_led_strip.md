# 研学卡 · IDF ledc + espressif/led_strip（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **20** == 表行数 20 ✓

## ① 官方文档与版本章节

- LEDC 组件头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/esp_driver_ledc/include/driver/ledc.h`（v5.5 实体在 esp_driver_ledc，复审表 D4 勘正）
- **头注原文摘录（逐字，ledc.h L286-287，`ledc_set_duty`）**：
  > `* @brief LEDC set duty and hpoint value`
  > `*        Only after calling ledc_update_duty will the duty update.`
- **头注原文摘录（逐字，ledc.h L162）**：
  > `* @brief LEDC update channel parameters`
- led_strip 组件头（**managed_components 域**，非 IDF 本体）：`/d/Espressif/ws/tcs/esp32/managed_components/espressif__led_strip/include/led_strip.h`（同目录 `led_strip_rmt.h`/`led_strip_types.h`）；版本：`esp32/main/idf_component.yml:17` 锁 `espressif/led_strip: ^3.0.0`（fan_service.c:3 头注记 v3.1.1，commit b741ef2，F1 换装）。
- **led_strip 头注原文摘录（逐字，led_strip.h L94，`led_strip_refresh`）**：
  > `* @note After updating the LED colors in the memory, a following invocation of this API is needed to flush colors to strip.`

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/peripherals/ledc/ledc_basic/main/ledc_basic_example_main.c`（fan_service.c:3 头注自述参照）
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/peripherals/ledc/ledc_fade/main/`（族实存）
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/peripherals/ledc/ledc_gamma_curve_fade/main/`（族实存）
- led_strip 官方组件自带示例（managed_components 域）：`/d/Espressif/ws/tcs/esp32/managed_components/espressif__led_strip/examples/`（RMT 后端用法）

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rn "ledc_\|led_strip_" esp32/components esp32/main --include=*.c
```
**命中 20 行 == 下表 20 行**。域分两半：**ledc_*（IDF 本体组件 esp_driver_ledc）13 行 + led_strip_*（managed_components：espressif__led_strip）7 行**，全部位于自研 tcs_fan。

| 文件:行 | API（域） | 语义 |
|---|---|---|
| esp32/components/tcs_fan/fan_service.c:3 | ledc/led_strip（注释） | 头注自述官方查证+F1 换装+参照 ledc_basic（非调用） |
| esp32/components/tcs_fan/fan_service.c:24 | s_ledc_ready（变量名） | LEDC 通道就绪标志声明（子串命中） |
| esp32/components/tcs_fan/fan_service.c:40 | s_ledc_ready（变量引用） | 判"目标是风扇脚且 LEDC 已配"（子串命中） |
| esp32/components/tcs_fan/fan_service.c:41 | ledc_stop（IDF） | WS2812 借脚前停 LEDC 输出（idle=1） |
| esp32/components/tcs_fan/fan_service.c:44 | led_strip_handle_t（类型声明，managed） | 灯带句柄声明（非调用） |
| esp32/components/tcs_fan/fan_service.c:45 | led_strip_config_t（类型声明，managed） | 灯带配置声明（非调用） |
| esp32/components/tcs_fan/fan_service.c:50 | led_strip_rmt_config_t（类型声明，managed） | RMT 后端配置声明（10MHz 官方默认） |
| esp32/components/tcs_fan/fan_service.c:54 | led_strip_new_rmt_device（managed） | 建 RMT 灯带设备 |
| esp32/components/tcs_fan/fan_service.c:60 | led_strip_set_pixel（managed） | 写像素 0 颜色（GRB） |
| esp32/components/tcs_fan/fan_service.c:62 | led_strip_refresh（managed） | 阻塞发送刷新到灯珠 |
| esp32/components/tcs_fan/fan_service.c:64 | led_strip_del（managed） | 用毕即删灯带设备 |
| esp32/components/tcs_fan/fan_service.c:73 | ledc_channel_config_t（类型声明，IDF） | 借脚后重绑通道配置声明（非调用） |
| esp32/components/tcs_fan/fan_service.c:78 | ledc_channel_config（IDF） | 借脚结束重绑风扇通道（timer 不变） |
| esp32/components/tcs_fan/fan_service.c:101 | ledc_timer_config_t（类型声明，IDF） | 定时器配置声明（非调用） |
| esp32/components/tcs_fan/fan_service.c:108 | ledc_timer_config（IDF） | 配 25kHz/10bit 定时器 |
| esp32/components/tcs_fan/fan_service.c:113 | s_ledc_ready（变量引用） | 置通道就绪门（子串命中） |
| esp32/components/tcs_fan/fan_service.c:114 | ledc_channel_config_t（类型声明，IDF） | 初始通道配置声明（非调用） |
| esp32/components/tcs_fan/fan_service.c:123 | ledc_channel_config（IDF） | 绑 CH0/GPIO48/初始 duty=0（关） |
| esp32/components/tcs_fan/fan_service.c:140 | ledc_set_duty（IDF） | 写新占空比（低=开换算） |
| esp32/components/tcs_fan/fan_service.c:141 | ledc_update_duty（IDF） | 立即生效（与 :140 配对） |

## ④ 对照结论

1. **set→update 配对 ✓（W10 #31-32）**：fan_service.c:140-141 两行紧邻，逐字对上 ledc.h L287"Only after calling ledc_update_duty will the duty update"；全库仅此一处 set_duty，无裸 set 漏 update。
2. **timer→channel 顺序 ✓（W10 #29）**：先 ledc_timer_config（:108）后 ledc_channel_config（:123），返值均判；初始 duty=0=引脚恒高=风扇关（F13 安全默认，低=开逻辑）。
3. **借脚重绑 ✓（W10 #30）**：WS2812 清灯借 GPIO48 时 ledc_stop（:41）让脚→灯帧发完 ledc_channel_config（:78）原 timer 重绑恢复——复审表 #30 认定"重复 config=合法改绑路径"。
4. **led_strip 官方件换装 ✓（F1，managed_components 域）**：手搓 RMT 编码被证伪（0.2-0.4µs T0H 全被读成 1，:43 注释在案）后换 espressif/led_strip ^3.0.0；new→set_pixel→refresh→del 一周期内即建即删（:54-:64），refresh 阻塞语义对上头注 L94；借脚后 GPIO 收成输出高防浮空锁存（:65-69 注释与实现一致）。
5. **示例对照 ✓**：LEDC 主链对照 ledc_basic（timer/channel/duty 三段）；led_strip 用法对照组件自带 examples（RMT 后端）；官方 ledc_fade/gamma 示例未用=无渐变需求，不算缺失。
6. **偏差：无；待整改：无**。
