# 墨水屏核线卡（断电操作，5 分钟）

> 诊断结论：26 引脚全扫描零应答 = 面板未上电。按下面顺序查，命中即停。

## ① 万用表蜂鸣档：电源通路（最可能命中）

一根表笔点 **devkit 的 3V3 引脚**，另一根点**面板 FPC 转接处的 VCC 线末端**：
- 不响 → VCC 线断/错位/没插实 → 重插或换线
- 响 → 同法查 GND（devkit GND ↔ 面板 GND 线）

## ② FPC 圆点方向（次可能命中）

裸屏 FPC 边缘有**圆点/缺角 = 1 脚**。对照下表确认 1 脚接的是不是 3V3 侧（整个排线旋转 180° 是最高发错误）：

| FPC 脚 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 |
|---|---|---|---|---|---|---|---|---|
| 信号 | VCC | GND | DIN | CLK | CS | DC | RST | BUSY |
| devkit | 3V3 | GND | GPIO11 | GPIO12 | GPIO13 | GPIO14 | GPIO21 | GPIO39 |

## ③ 逐根通断

蜂鸣档依次量：11↔DIN、12↔CLK、13↔CS、14↔DC、21↔RST、39↔BUSY。杜邦线坏了很常见，坏哪根换哪根。

## ④ 复测

```bash
source ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/activate_idf.sh
cd /d/Espressif/ws/tcs/esp32
echo "CONFIG_EPAPER_TEST_MODE=5" >> sdkconfig
bash ~/.claude/skills/embedded-dev-loop/platforms/esp32/scripts/esp_loop.sh all
sed -i '/CONFIG_EPAPER_TEST_MODE=5/d' sdkconfig
```

出现 `[DISCOVER] 命中` = 接线找回（自动化会接管后续门禁）。仍无命中但 ①②③ 都过了 → 面板本身可能损坏，换板验证。
