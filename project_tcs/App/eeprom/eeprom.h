/**
 * eeprom.h — AT24C02 EEPROM（参数存储 v2 布局，扩展自 GS magic+CRC 模式，§3.3）
 *
 * 硬件：AT24C02 (256B) @ 0x50，软件 bit-bang I2C SCL=PH4/SDA=PH5（与触摸
 * PD12/13、TMP117 硬件 I2C1 PB8/9 均为独立总线）。**离线不阻塞**（沿用 GS 降级设计）。
 *
 * v2 布局（param_registry 序列化/校验，物理 IO 在本文件）：
 *   0x00 magic=0xA6 | 0x01 layout_ver=2 | 0x02 count | 0x03 rfu
 *   0x04.. N×8B 记录 {id u16le, type u8, rfu u8, value u32le}（≤30 条）
 *   0xF8 CRC8（覆盖 0x00..0xF7，proto_crc8 复用）
 *   0xF9-0xFF 保留；**自测字节 0xF6**（GS 用过 0xF0，防两固件互踩；
 *   0xF6 在 CRC 覆盖区内 → 自测后必须恢复原值，否则参数被判坏）
 */
#ifndef TCS_EEPROM_H
#define TCS_EEPROM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EE_IMAGE_SIZE 256

/* 初始化软件 I2C + 探测在线（写地址 ACK 探测）。0=在线 -1=离线（降级空操作）。 */
int  eeprom_init(void);
int  eeprom_ready(void);

/* 整片读 256B → img。0=ok -1=离线/IO 错。 */
int  eeprom_read_image(uint8_t img[EE_IMAGE_SIZE]);

/* 提交镜像（param_persist 调用）：按 8B 页对齐增量写“与上次的差异页”+CRC 页。
 * 页写 20ms 级阻塞发生在 2s 防抖后（§12.3-C5 允许，不在热路径）。0=ok。 */
int  eeprom_commit_image(const uint8_t img[EE_IMAGE_SIZE]);

/* 写读自测：0xF6 写 0x55→0xAA→恢复原值（保 CRC 有效）。0=通过。 */
int  eeprom_selftest(void);

#ifdef __cplusplus
}
#endif
#endif /* TCS_EEPROM_H */
