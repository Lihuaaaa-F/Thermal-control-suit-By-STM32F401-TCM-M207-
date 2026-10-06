# 墨水屏核线卡（先做第 ⓪ 步！断电操作）

> 诊断结论：26 引脚全扫描零应答。规格书核验（`porting-ref/2.9inch-e-paper-b-v3-specification.pdf`，Rev3.1，UC8151D）后问题定性升级：
> **裸屏 FPC 是 24 脚，接口脚在 9-14，升压电路全部在屏外**。此前"8 脚 FPC 圆点=1 脚"的说法是错的（1 脚实为 NC）。
> 8 根杜邦线直连裸屏 = 无论怎么接都不可能工作。

## ⓪ 判明你手上的单元（30 秒，决定后面走不走得通）

- **模块版**（2.9inch e-Paper Module (B)）：屏贴在一块小 PCB 上，板边一排 **8 根 2.54mm 排针**，丝印 VCC GND DIN CLK CS DC RST BUSY → 走 ①②③
- **裸屏 + 驱动板**：屏是独立的，另有一块含升压电路的转接/驱动板 → 接好驱动板后走 ①②③
- **只有裸屏**（2.9inch e-Paper (B)，型号 WFT0290CZ10）：仅一条 0.5mm 24 脚 FPC 金手指、无任何 PCB → **停止核线**。升压前级（GDR/RESE/VDHR/VGH/VGL/VDL/VCOM 的产生电路）在驱动板上，杜邦线无法替代，需购 Module (B) 或 Driver HAT（微雪产品页原话：raw display without driver board, recommends Module (B) or Driver HAT）
- 分不清：数金手指/排针数量，**8 = 模块形态可用，24 = 裸屏**

## ① 万用表蜂鸣档：电源通路（模块形态最可能命中）

一根表笔点 **devkit 的 3V3 引脚**，另一根点**驱动板 VCC 排针**：
- 不响 → VCC 线断/错位/没插实 → 重插或换线
- 响 → 同法查 GND（devkit GND ↔ 驱动板 GND）

## ② 排针对照（以驱动板丝印为准）

| 排针 | VCC | GND | DIN | CLK | CS | DC | RST | BUSY |
|---|---|---|---|---|---|---|---|---|
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

## 附：裸屏 24 脚真表（规格书 p6，防止再按"8 脚序"接线）

| 脚 | 信号 | 脚 | 信号 | 脚 | 信号 | 脚 | 信号 |
|---|---|---|---|---|---|---|---|
| 1 | NC（悬空） | 7 | TSDA | 13 | SCL | 19 | VPP（仅 OTP） |
| 2 | GDR | 8 | BS（L=4 线 SPI） | 14 | SDA | 20 | VDH |
| 3 | RESE | 9 | BUSY_N（低=忙） | 15 | VDDIO | 21 | VGH |
| 4 | NC（悬空） | 10 | RST_N（低有效） | 16 | VCI | 22 | VDL |
| 5 | VDHR | 11 | DC | 17 | GND | 23 | VGL |
| 6 | TSCL | 12 | CSB（低选通） | 18 | VDD | 24 | VCOM |

警示：1/4 脚 NC 悬空勿接；19 VPP 只用于 OTP 烧写（勿上电）；2/3/5/20-24 脚属于驱动板升压电路，MCU 直连无效。
MCU 实际要接的只有：8(→GND)、9、10、11、12、13、14、15(→3V3)、16(→3V3)、17(→GND)，且仍需驱动板产生高压轨。
