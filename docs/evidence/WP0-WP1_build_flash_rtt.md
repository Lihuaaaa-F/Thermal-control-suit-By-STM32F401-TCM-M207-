# WP0–WP6 H743 固件：构建、烧录、运行时证据（2026-10-18）

## 编译（gcc skill 闭环）

- 工具链：arm-none-eabi-gcc 13.2.rel1 + CMake 4.4 + Ninja，`-Wall -Wextra -O1/-Os` 全开
- 结果：**0 代码警告**（唯一提示 = 链接器 RWX LOAD segment 良性告警，GS 工程同款）
- 产物：`build/tcs_main.elf/.hex/.bin`
- 体积：FLASH 104,228 B / 2MB（4.97%），DTCM 19,928 B / 128KB（15.2%）

```
Memory region         Used Size  Region Size  %age Used
           FLASH:      104228 B         2 MB      4.97%
         DTCMRAM:       19928 B       128 KB     15.20%
objcopy OK: tcs_main.hex tcs_main.bin
```

## 烧录 + 校验（probe-rs，DAPLink CMSIS-DAP @SWD）

```
probe-rs download build/tcs_main.elf --chip STM32H743II --probe 0d28:0204
    → Finished in 13.58s
probe-rs verify   build/tcs_main.elf --chip STM32H743II --probe 0d28:0204
    → Verification successful
```

## 运行时证据（RTT，probe-rs attach）

```
[SYSTEM] boot reset-src=PIN (RSR=0x04460000)
[SYSTEM] boot project_tcs fw=v1.0.0 HCLK=480 MHz        ← 480MHz 主频（pyocd 读
                                                          SystemCoreClock=480000000 复核 ✓）
RTT: mpu+d-cache write-through ok
RTT: pll3 ltdc pclk=50M ok
RTT: sdram selftest=1                                    ← SDRAM ✓
[SYSTEM] ltdc init rv=0 ready=1                          ← LTDC+背光 ✓（屏幕点亮待人工目视）
RTT: ft5446 ver=0001 fingers=ff ver_ok=1
[SYSTEM] ft5446 init rv=0 ready=1                        ← 触摸 ✓
RTT: eeprom online magic=0xa5                            ← AT24C02 在线（读到 GS 固件旧数据）
RTT: eeprom selftest OK (0xF6 wr/rd/restore)             ← 自测字节 0xF6 写读恢复 ✓
[WARN] eeprom cfg invalid -> defaults                    ← 旧布局 CRC 不符→默认值降级 ✓（§3.3）
[APP] ui init page=home lcd=1 touch=1                    ← 主页绘制完成
[SYSTEM] init done, entering main loop
```

## 控制台（COM4 CH340 ↔ USART1 PA9/PA10 @115200）

TX 链路已实测（上方 `[SYSTEM]` 行即经 USART1 输出）。**命令交互被板级问题阻塞**，见
`WP6_iwdg_defect.md` 附录 B：本板 CH340X 自动下载电路连至 NRST/BOOT0，串口口打开/
线序翻转会对 NRST 施加脉冲，且 DTR/RTS 电平决定复位瞬间 BOOT0 采样（DTR=True→Flash
启动；False→ROM bootloader）。规避方法：先关串口复位、再开串口取日志；或硬件断开
自动下载电路。RX 环形缓冲经 pyocd 实证收到上位机字节（rx_head 0→5）。

## M0 引脚复核（替代 CubeMX GUI 的数据手册核验）

接线表 v1 七脚映射按 RM0433 AF 表逐一核对，并在 `MX_UART_Init`/`MX_I2C_Init` 中以
`GPIO_AF7_USARTx / GPIO_AF4_I2C1` 配置；pyocd 运行时读回 GPIOA AFRH：PA10 AF=7 ✓、
USART1 CR1 UE/RE/TE=1 ✓。PB10/11、PD5、PB8/9、PB14 同法配置（GS 审计已核空脚）。
*CubeMX GUI 绿灯复核仍建议由用户在首个硬件联调日执行一次（备选 UART5 PC12/PD2 预案不变）。*

## 遗留（M1 门禁未闭环项）

1. 屏幕静态主页**人工目视**（agent 无摄像头）：LTDC/SDRAM/主页绘制链路已全通。
2. 控制台命令交互：待自动下载电路断开或换独立 USB-TTL。
