# porting-ref —— 移植参考源码（勿直接编译）

来源：微雪官方仓 [waveshareteam/e-Paper](https://github.com/waveshareteam/e-Paper)（RaspberryPi_JetsonNano/c/lib/e-Paper/），版权归微雪，**仅作本组件 chip 层移植参考**。

## 文件与选用规则

| 文件 | 对应屏 | 关键差异 |
|---|---|---|
| `EPD_2in9bc.c/h` | 2.9" (B) 经典版（UC8151） | `Init/Clear/Display(black,ry)/Sleep`，无快刷 |
| `EPD_2in9b_V4.c/h` | 2.9" (B) V4 版 | 额外 `Init_Fast/Clear_Fast/Display_Fast` |

**子型号判定**（S1/S2 执行）：
1. 屏背面丝印 / 微雪订单页wiki链接（V4 页面会标注 V4）
2. S3 三色条实测：V4 支持 Display_Fast（波形不同），经典版只走标准波形

两版公共事实（已核实签名）：显示函数均为**双平面** `Display(const UBYTE *blackimage, const UBYTE *ryimage)`，ry = 红/黄平面。

## 规格书（权威硬件事实）

`2.9inch-e-paper-b-v3-specification.pdf`（微雪官方，Rev3.1，50 页）：**UC8151D**；裸屏 FPC **24 脚**（p6 脚表：接口脚 8-14、电源 15-17、升压脚 2/3/5/20-24 外置）；BUSY_N 低有效、BS=L 选 4 线 SPI、写 SPI ≤20MHz / 读 ≤2.5MHz、VCI 2.3-3.6V、工作温度 0-40°C、深睡 2µA。
