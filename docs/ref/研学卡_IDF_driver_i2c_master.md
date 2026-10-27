# 研学卡 · IDF driver/i2c_master（v5 新 API）（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **11** == 表行数 11 ✓

## ① 官方文档与版本章节

- 组件头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/esp_driver_i2c/include/driver/i2c_master.h`（v5 句柄式新 API；同目录 `i2c_types.h`；旧 `i2c_param_config`/`i2c_driver_install`/`i2c_master_cmd_begin` 属 deprecated，本库 0 调用——grep 复核 0 命中）
- **头注原文摘录（逐字，i2c_master.h L108，`i2c_new_master_bus`）**：
  > `* @brief Allocate an I2C master bus`
- **头注原文摘录（逐字，i2c_master.h L206，`i2c_master_transmit_receive` 返回值注）**：
  > `*      - ESP_ERR_TIMEOUT: Operation timeout(larger than xfer_timeout_ms) because the bus is busy or hardware crash.`

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/peripherals/i2c/i2c_basic/main/i2c_basic_example_main.c`（tmp117_service.c:4 头注自述参照）
- 同目录族实存：`i2c_eeprom/`、`i2c_slave_network_sensor/`、`i2c_tools/`

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rn "i2c_" esp32/components esp32/main --include=*.c
```
**命中 11 行 == 下表 11 行**（域：全部自研 tcs_tmp117，无 managed_components；I2C0 8/9 脚 @100kHz 接 TMP117 温度传感器）。

| 文件:行 | API | 语义 |
|---|---|---|
| esp32/components/tcs_tmp117/tmp117_service.c:4 | i2c_master（注释） | 头注自述用官方 v5 新 API+参照 i2c_basic（非调用） |
| esp32/components/tcs_tmp117/tmp117_service.c:12 | driver/i2c_master.h（include） | 头引入（非调用） |
| esp32/components/tcs_tmp117/tmp117_service.c:31 | i2c_master_bus_handle_t（类型声明） | 总线句柄静态声明（非调用） |
| esp32/components/tcs_tmp117/tmp117_service.c:32 | i2c_master_dev_handle_t（类型声明） | 器件句柄静态声明（非调用） |
| esp32/components/tcs_tmp117/tmp117_service.c:97 | i2c_master_transmit_receive | 写寄存器地址 1B+读温度 2B（带 xfer_timeout_ms） |
| esp32/components/tcs_tmp117/tmp117_service.c:144 | i2c_master_bus_config_t（类型声明） | 总线配置结构声明（非调用） |
| esp32/components/tcs_tmp117/tmp117_service.c:145 | .i2c_port（字段名） | 配置体指定 I2C 端口 0（子串命中） |
| esp32/components/tcs_tmp117/tmp117_service.c:152 | i2c_new_master_bus | 建 master 总线（SCL/SDA/时钟源） |
| esp32/components/tcs_tmp117/tmp117_service.c:157 | i2c_device_config_t（类型声明） | 器件配置结构声明（非调用） |
| esp32/components/tcs_tmp117/tmp117_service.c:162 | i2c_master_bus_add_device | 挂 TMP117 器件（7 位地址+超时）到总线 |
| esp32/components/tcs_tmp117/tmp117_service.c:171 | i2c_master_transmit_receive | 自测读 DEVID 寄存器探活 |

## ④ 对照结论

1. **v5 新 API 全面落地 ✓**：new_master_bus→bus_add_device→transmit_receive 三段式句柄管线，与官方 i2c_basic 示例同型；全库 0 处旧 API（i2c_param_config/i2c_driver_install/i2c_master_cmd_begin grep=0），符合平台附录 §9-6"旧 API 已废弃"纪律。
2. **transmit_receive 事务语义 ✓**：温度读（:97）与 DEVID 探活（:171）均为"写寄存器地址+读 N 字节"复合事务，超时参数显式（xfer_timeout_ms），对上头注 L206 超时返回语义；NACK（硬件未接 T1-T3）→掉线沿 ALM_BODY_SENSOR=F4 预期行为（main.c 注释在案）。
3. **常驻无卸载=注记非偏差**：总线/器件句柄与服务同生命周期，未调 i2c_del_master_bus/i2c_master_bus_rm_device（官方示例亦仅初始化一次）；当前无热插拔/复位需求，如二期加传感器重枚测再补删径。
4. **自研合法性留档 ✓**：tmp117_service.c:4 头注引 F5 证据（components.espressif.com 检索 2026-10-26：TMP117 仅 solderedelectronics GPL-3.0-only 第三方件→合法拒绝），故驱动层=官方 i2c_master + 自研器件协议层。
5. **偏差：无；待整改：无**。
