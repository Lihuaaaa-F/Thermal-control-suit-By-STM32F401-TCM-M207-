# WP8/WP9：ESP32-S3 端证据（2026-10-18）

## 编译（esp-dev-skill + esp32 附录闭环）

- ESP-IDF v5.5.5，`idf.py build` 通过（组件零警告）
- 组件：`tcs_link`（proto/comm 同源拷贝 + link_task）、`tcs_wifi`（softap）、`tcs_web`（http_server + websocket + gzip 内嵌网页）
- 产物 `build/tcs_wireless.bin` 926,928 B（ota_0/ota_1 各 3M 槽位充足）

## 烧录（COM3 = S3 板载 USB-Serial-JTAG）

```
esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash @flash_args
→ Hash of data verified. Hard resetting via RTS pin...
```

## 启动日志（COM3 115200，复位后 12s 抓取 → WP8_esp32_boot.log）

```
I (662) TCS: === 调温服 ESP32-S3 bridge boot ===
I (732) wifi:mode : softAP (3c:0f:02:d6:a8:a1)
I (742) tcs_wifi: AP "TCS-A8A1" up, IP 192.168.4.1 pass=12345678
I (742) tcs_link: link task up
I (742) tcs_link: started uart1 230400 baud tx=17 rx=18
I (742) tcs_web: httpd up on :80
I (742) TCS: WiFi AP + link + web all up
```

- WiFi AP：SSID `TCS-A8A1`（MAC 后 4 位）✓ 密码 12345678 ✓ IP 192.168.4.1 ✓
- 链路任务：UART1 @230400 GPIO17/18 ✓
- HTTP 服务器 :80 ✓（/ 静态 gzip、/api/status、/api/registry、/ws）

## WP9 OTA 接口自测

`main.c: ota_selftest_once()`：自 factory 分区启动时，向 ota_0 `esp_ota_begin(4096)` →
写 16×256B 模式 → `esp_ota_end` 拒绝非合法镜像（预期行为）→ `esp_ota_abort`。
启动 5s 后执行，日志 `ota_selftest PIPE OK: wrote 4KB, end rejects non-app as expected`。
（分区表已按 §4.4 定死：nvs 24K + otadata 8K + ota_0 3M + ota_1 3M + www 512K + user 剩余）

## 网页

- 资源 gzip 后 76KB 总量（预算 <150KB ✓）：index.html 971B、app.js 4.5KB、style.css 1.2KB、
  chart.umd.min.js（Chart.js v4.4.1 本地文件，无 CDN 引用）69KB
- 功能：双水温仪表盘 canvas、体温 30min 曲线（Chart.js）、TEC/风扇/模式开关、
  registry 驱动参数区（PF_WEB）+ M207 高级折叠区（PF_M207，D6）、报警横幅、
  红色大急停（二次确认）、断连灰罩+重连倒计时+「请靠近设备」、协议版本黄条
- M4 手机实测（连热点开网页、双向 ≤1s 同步、飞行模式灰罩）待手机在场执行

## 遗留（依赖飞线/实物）

- WP7 "ESP32 实接 link_ok=1"：需 H743↔ESP32 的 UART 飞线（接线表 #3/#4）完成后联测；
  PC 回环侧已由 proto_ref.py↔C 端字节级互测 + comm ARQ 单测覆盖
- M4/M5 手机端演示
