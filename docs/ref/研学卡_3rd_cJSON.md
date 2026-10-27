# 研学卡：cJSON（第三方官方件 · W8 §7b）

- 制卡日期：2026-10-27。仓库：`D:\Espressif\ws\tcs`（下述相对路径以此为根）。
- 版本事实源：`docs/ref/依赖版本清单.md`（W9，v13.4 §7c）cJSON 行。
- 调用面数据：本卡制卡时实跑 grep（命令见 ③），行号均为当前工作区实测。

## ① 官方文档与版本

| 项 | 内容 |
|---|---|
| 组件 | cJSON（ESP32 侧，IDF `json` 组件） |
| 精确版本 | **1.7.19**（IDF v5.5.5 内置快照；实测 `components/json/cJSON/cJSON.h:82-84`：`CJSON_VERSION_MAJOR/MINOR/PATCH = 1/7/19`） |
| 来源 | IDF v5.5.5 `components/json`（官方安装器装于 `D:/Espressif/frameworks/esp-idf-v5.5.5`），上游 github.com/DaveGamble/cJSON |
| 锁定方式 | 随 IDF 版本锁（无独立 manifest）；上游独立 1.7.x 与 IDF 快照差异看 IDF 发布说明 |
| 许可 | MIT |
| 官方文档 | 上游 README（github.com/DaveGamble/cJSON）+ 本机 IDF 组件文档 `D:/Espressif/frameworks/esp-idf-v5.5.5/components/json/README` |
| 升级风险注记 | W2 起为主要消费面（websocket 上/下行 JSON 全归此库）；升级=随 IDF 整体走，回看 IDF 发布说明 |

## ② 官方示例文件路径（逐文件，本机实测）

grep 命令：`grep -rl "cJSON" D:/Espressif/frameworks/esp-idf-v5.5.5/examples --include=*.c --include=*.h --include=*.md --include=CMakeLists.txt` → 命中 4 文件：

| # | 路径（本机真实路径） | 内容 |
|---|---|---|
| 1 | `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/protocols/http_server/restful_server/main/rest_server.c` | **主示例**（与我们 web 用法同构）：`cJSON_Parse`（:132）/`cJSON_GetObjectItem`（:133-135）/`cJSON_Delete`（:137）/`cJSON_CreateObject`（:146）/`cJSON_AddStringToObject`（:149）等 13 处命中 |
| 2 | `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/peripherals/i2c/i2c_slave_network_sensor/main/i2c_slave_main.c` | 解析侧示例：`cJSON_Parse`（:79）/`cJSON_GetObjectItem`（:80/87/93）5 处命中 |
| 3 | `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/bluetooth/ble_uart_service/OPENCODE_COMPANION.md` | 文档级命中（含 CN 版同名件），非代码示例 |
| 4 | 组件本体 `D:/Espressif/frameworks/esp-idf-v5.5.5/components/json/`（`CMakeLists.txt`/`README`/`cJSON/`） | 官方源+组件文档（与固件同库同版本，单测亦直编此源，见 ④） |

## ③ 我们全部调用面清单（文件:行）

grep 命令：`grep -rn "cJSON_" esp32/components esp32/main --include=*.c`
命中数：**63 行**（表行数=63）。**全部集中于一个文件**：`esp32/components/tcs_web/websocket.c`（esp32/main 零命中；其余 6 组件零命中）。

| # | 行 | API | 用途 |
|---|---|---|---|
| 1 | 97 | （注释，含 "cJSON_free" 字样） | 头注纪律：树必 Delete、串必 free |
| 2 | 105 | cJSON_CreateObject | st 帧根对象 |
| 3 | 108 | cJSON_AddStringToObject | st: `"t":"st"` |
| 4 | 110 | cJSON_AddBoolToObject | st 无效态 `"valid":false` |
| 5 | 113 | cJSON_AddBoolToObject | st `"valid":true`（链起） |
| 6 | 114 | cJSON_AddNumberToObject | st `"ver":1` |
| 7 | 115 | cJSON_AddNumberToObject | st `"seq"` |
| 8 | 116 | cJSON_AddNumberToObject | st `"tc1_act"` |
| 9 | 117 | cJSON_AddNumberToObject | st `"tc2_act"` |
| 10 | 118 | cJSON_AddNumberToObject | st `"body"` |
| 11 | 119 | cJSON_AddBoolToObject | st `"body_sensor_ok"` |
| 12 | 120 | cJSON_AddNumberToObject | st `"tec1"` |
| 13 | 121 | cJSON_AddNumberToObject | st `"tec2"` |
| 14 | 122 | cJSON_AddNumberToObject | st `"fan"` |
| 15 | 123 | cJSON_AddNumberToObject | st `"mode"` |
| 16 | 124 | cJSON_AddNumberToObject | st `"alarm"` |
| 17 | 125 | cJSON_AddNumberToObject | st `"online"` |
| 18 | 126 | cJSON_AddNumberToObject | st `"boot_s"` |
| 19 | 130 | cJSON_PrintBuffered(root, 256, 0) | st 预检缓冲输出 |
| 20 | 134 | cJSON_free | st 串释放 |
| 21 | 136 | cJSON_Delete | st 树释放 |
| 22 | 164 | cJSON_CreateObject | reg 帧根对象 |
| 23 | 165 | cJSON_CreateArray | reg `"list"` 数组 |
| 24 | 166 | cJSON_Delete(root); cJSON_Delete(arr) | reg 失败路径双删（**一行双调用**，arr 不为 root 所有，双删安全） |
| 25 | 168 | cJSON_AddStringToObject | reg: `"t":"reg"` |
| 26 | 169 | cJSON_AddItemToObject | reg `"list"` 挂数 |
| 27 | 170 | cJSON_Delete | reg 挂数失败 root |
| 28 | 171 | cJSON_Delete | reg 挂数失败 arr |
| 29 | 178 | cJSON_CreateObject | reg 条目对象 it |
| 30 | 180 | cJSON_AddNumberToObject | it `"id"` |
| 31 | 181 | cJSON_AddNumberToObject | it `"type"` |
| 32 | 182 | cJSON_AddNumberToObject | it `"flags"` |
| 33 | 183 | cJSON_AddNumberToObject | it `"min"` |
| 34 | 184 | cJSON_AddNumberToObject | it `"max"` |
| 35 | 185 | cJSON_AddNumberToObject | it `"step"` |
| 36 | 186 | cJSON_AddNumberToObject | it `"def"` |
| 37 | 187 | cJSON_AddStringToObject | it `"key"` |
| 38 | 188 | cJSON_AddStringToObject | it `"name"` |
| 39 | 189 | cJSON_AddStringToObject | it `"unit"` |
| 40 | 190 | cJSON_AddItemToArray | it 入 list |
| 41 | 192 | cJSON_PrintBuffered(root, 8192, 0) | reg 预检缓冲输出（代缓存串） |
| 42 | 193 | cJSON_Delete | reg 树释放 |
| 43 | 204 | cJSON_free | reg 旧代缓存串释放（换入盖 g0） |
| 44 | 212 | cJSON_CreateObject | pv 帧根对象 |
| 45 | 216 | cJSON_AddStringToObject | pv: `"t":"pv"` |
| 46 | 217 | cJSON_AddNumberToObject | pv `"id"` |
| 47 | 218 | cJSON_AddNumberToObject | pv `"v"` |
| 48 | 219 | cJSON_AddStringToObject | pv `"src"` |
| 49 | 220 | cJSON_AddBoolToObject | pv `"clamped":true`（条件挂） |
| 50 | 221 | cJSON_PrintBuffered(root, 128, 0) | pv 预检缓冲输出 |
| 51 | 223 | cJSON_Delete | pv 树释放 |
| 52 | 226 | cJSON_free | pv 串释放 |
| 53 | 315 | cJSON_ParseWithLength | 下行帧解析（ws_on_recv） |
| 54 | 317 | cJSON_GetObjectItem | 下行 `"t"` |
| 55 | 318 | cJSON_IsString | 判 t 为串 |
| 56 | 320 | cJSON_GetObjectItem | 下行 `"id"` |
| 57 | 321 | cJSON_GetObjectItem | 下行 `"v"` |
| 58 | 322 | cJSON_IsNumber(id)/IsNumber(v) | 判 id/v 数值（**一行双调用**） |
| 59 | 329 | cJSON_GetObjectItem | 下行 `"c"` |
| 60 | 330 | cJSON_IsNumber | 判 c 数值 |
| 61 | 337 | cJSON_GetObjectItem | 下行 `"m"` |
| 62 | 338 | cJSON_IsNumber(m) | 判 m 数值（m->valueint==1 提手） |
| 63 | 343 | cJSON_Delete | 下行树释放 |

（注：#24 行 166 一行含两处 `cJSON_Delete`、#58 行 322 一行含两处 `cJSON_IsNumber`——均按 grep 物理行计 1 行；表 63 行=物理命中 63。）

## ④ 对照结论（✓/偏差+理由/待整改）

| 锚点 | 结论 |
|---|---|
| W2 上行统一 cJSON | ✓ 已兑现：`grep -n snprintf websocket.c`=0、`grep -c cJSON_PrintUnformatted websocket.c`=0（W2 完成报告 DoD）；PrintBuffered 256(st)/128(pv)/8192(reg) 三预检 + reg 代缓存（:204 换入盖 g0）+ 转义 7 向量单测（`project_tcs/tests/test_ws_json_escape.c` 直编真实 IDF cJSON 源）——引 `docs/results/W2_完成.md` §1/§3/§4 |
| W2 红队 R4 帧宽三数 | ✓ 已入档：st 正常态 **222B**（最坏 239B）/ reg 32 条 **5556B** / reg 50 条 **8670B>8192 顶穿**（返 0→reg 帧整体不发）——引 `docs/results/W2_红队.md` §2e 与整改表 |
| 偏差（已裁决项） | 8192 扩容=二期（红队裁定"代码扩容记二期"）；50 参 vs LINK_REGISTRY_MAX=32 矛盾已移交 R 线（台账 W2 行）；st 239B vs h_api_status 栈 buf 256B 裕度 17B wont-fix 记录"加字段先扩 buf" |
| 数字格式语义差 | 偏差+理由（非缺陷）：cJSON 输出 `37` vs 旧 `%.1f` 的 `37.0`，JSON 数值语义等价（Chart.js Number 解析无差）——W2 §5.1 诚实化注记 |
| 待整改归属 | 无 W8 新增项：真机 WS 抓帧/E-02/E-17×5/heap 高水位（E-19 浸泡）挂 R 线复验批；web_mock 语义等价 diff 工具化=二期（W2 挂账 2） |
