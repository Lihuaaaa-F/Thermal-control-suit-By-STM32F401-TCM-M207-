/* ===================================================================
 * protocol/tcs_version.h — 固件版本的**唯一源**（S 线 e3a，审查条目 X06-F-09）
 *
 * 改版本号只改下面四个宏。其余地方都从它们推导，不要在别处再写版本号：
 *   - ESP32 应用版本（esp_app_desc_t.version，网页"设备"页与屏幕连接页显示的就是它）：
 *     esp32/CMakeLists.txt 启动时读本文件生成 PROJECT_VER；
 *   - 数值化的 fw_ver（BCD：主.次.修订 → 0xMMmmpp，屏幕的版本串由它拼出来）：
 *     TCS_FW_VER_BCD，esp32/components/tcs_core/param_registry.c 用；
 *   - 一致性检查 / 代为修改：tools/version_bump.py（--check / --set X.Y.Z[-后缀]）。
 *
 * 背景（2026-11-25 上机）：屏幕打印 tcs v1.2.0（来自 ESP32 报上来的 fw_ver = 0x010200），
 * 而 ESP32 应用版本是 1.3.0-rc1——同一个固件两个版本号。
 * 说明：BCD 只能表达 主.次.修订，后缀（-rc1 之类）只在应用版本字符串里。
 * 两端共用一份：H743 侧已改为在 project_tcs/App/tcs/param_registry.c 里相对包含本文件
 * （2026-11-28，TCS_FW_VER_BCD），与 ESP32 同源；version_bump.py 同时校验两端。
 * =================================================================== */
#ifndef TCS_VERSION_H
#define TCS_VERSION_H

#define TCS_VER_MAJOR   1
#define TCS_VER_MINOR   3
#define TCS_VER_PATCH   0
#define TCS_VER_SUFFIX  "-rc1"

/* 以下两个是推导量，不要手改 */
#define TCS_FW_VER_BCD  ((((uint32_t)TCS_VER_MAJOR) << 16) | (((uint32_t)TCS_VER_MINOR) << 8) \
                        | (uint32_t)TCS_VER_PATCH)
#define TCS_VER_XSTR_(x) #x
#define TCS_VER_XSTR(x)  TCS_VER_XSTR_(x)
#define TCS_VER_STR      TCS_VER_XSTR(TCS_VER_MAJOR) "." TCS_VER_XSTR(TCS_VER_MINOR) "." \
                         TCS_VER_XSTR(TCS_VER_PATCH) TCS_VER_SUFFIX

#endif /* TCS_VERSION_H */
