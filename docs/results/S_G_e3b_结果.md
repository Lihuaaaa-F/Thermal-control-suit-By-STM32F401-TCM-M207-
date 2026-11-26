# S 线 S-G e3b 结果 — ESP32 网页控制台 / OTA 健壮性

- 工作树 `D:\Espressif\ws\tcs-g-e3`，分支 `fix/s-g-e3`，基座 `fix/s-u-int2` @ `de997cb`（已快进到位，没有再动）。
- 提交（按条目，每条一个）：`98640bb` X03-F-12 → `05c40b0` M08-F-12 → `86b0c2d` M14-F-13 → `72c1910` 源码门禁（前三条落在 IDF 绑定代码里的部分）→ `e74007d` M14-F-11 → `c7672f0` M14-F-12 → `f68535a` M10-F-06 → 本文件 → 审计产物（单独一个，排在最后）。
- 没有碰设备、没有 push、没有动 main。H743 文件、`esp32/components/tcs_core/`、`protocol/tcs_proto.h` 都没有改；`web_start` 的签名没有动。
- 用户决定不做的：P1-15 的 token 部分、M14-F-08 随机热点口令——没有实现，热点口令保持固定值 `12345678`。因此 webtest 没有任何用例需要因为 token 调整。
- 网页只读浏览的路径（打开网址 → 状态 / 曲线 / 日志）没有改：前端文件（`web_files/`）一个字节没动；`web_status.c` 只追加了一个 `/api/ota` 用的纯函数；`websocket.c` 只加了容量断言与 WS ota 的"忙"回执；`http_server.c` 只动了 `/api/ota`（诊断档才有）和把 httpd 的连接名额显式写成原来的默认值 7。

## 一、逐条结果

| ID | 判定 | 改了什么 | 怎么验证的（命令与结果） | 遗留与原因 |
|---|---|---|---|---|
| X03-F-12（= P1-15 的 ②） | 已实施待真机 | **`esp32/main/main.c`**：`app_ota_trigger()` 改为返回 `esp_err_t`，入口用 `atomic_bool` 比较交换拿"单实例闸门"——拷贝与回滚共用；拷贝任务里所有没走到重启的出口都放开闸门。拷贝缓冲由 `static uint8_t buf[8192]` 改为 `heap_caps_malloc(8192, INTERNAL + 8BIT)`，先于 `esp_ota_begin` 申请、用完释放；写失败时 `esp_ota_abort()` 释放句柄（以前泄漏）。**新增 `esp32/main/ota_pure.[ch]`**（纯逻辑，主机可测）。**`http_server.c`**：`POST /api/ota` 忙 → 409 `{"t":"ota","started":false,"reason":"busy"}`，没做成 → 500（以前不论成败都回 `started:true`）。**`websocket.c`**：WS `{"t":"ota"}` 忙 → `{"t":"err","code":"server_busy",…}`。提交 `98640bb`、`72c1910`。 | 主机 `esp32/tests/test_ota_pure.c`（已接进 `esp32/tests/run_m207_tests.sh`，`run otapure`）。**修前**（闸门按旧语义"每次都放行"落盘）：`test_ota_pure: PASS=4 FAIL=4`——"second trigger … is refused (409)""third as well""gate reports busy""4 threads racing … exactly one wins, every round"全红，抢闸一轮打印 `race: 300 of 300 rounds had != 1 winner`。**修后**：`test_ota_pure: PASS=8 FAIL=0`。源码门禁 `tests/test_web_gates.py`：对基座 `de997cb` 的导出（`--esp-root`）这一组 10 条全红，当前全绿。诊断档增量构建 0 error 0 warning（stdatomic 在 xtensa 上链接通过；IDF 自带实现 `components/newlib/src/stdatomic.c`）。三档矩阵见第三节。 | 关单条件"并发 OTA 只跑一个，另一个 409"要在真机上发两次请求才能看到，本批次不碰设备。步骤见第五节。 |
| M08-F-12 | 已修并验证 | **`ota_pure.c`** 新增 `ota_image_len(part_size, rd, ctx, *len)`：读 24 字节镜像头 → 顺着段表逐段读 8 字节段头（每一步先比后读，不做可能回绕的加法）→ 校验字节所在的 16 字节块 → 32 字节 SHA-256（按镜像头第 23 字节 `hash_appended`）。与 IDF 算法同式（`bootloader_support/src/esp_image_format.c` 的 `process_segments` / `process_checksum` / `process_appended_hash_and_sig`）。**`main.c`** 的拷贝任务经 `ota_part_rd()`（`esp_partition_read`）调用；镜像头或段表不合法时返回原因码（`EREAD / EMAGIC / ESEGS / EBOUND / ETOOLONG`）并放弃拷贝——不擦对槽、不重启。提交 `05c40b0`、`72c1910`。 | **修前**（原逻辑原样搬进纯函数）：`test_ota_pure: PASS=13 FAIL=16`——"4 segments (12K/2K/8K/1K)""a different 2nd segment length gives a different image length""review's crafted header: first segment 128 KB""1 byte short: ETOOLONG""segment data runs past the partition"等全红。**修后**：`test_ota_pure: PASS=29 FAIL=0`，读函数全程没有收到越界请求（用例里的读函数对每次读做分区范围断言）。**真实固件对账**（`tests_otapure.exe <固件.bin>`，.bin 以 SHA-256 结尾，文件长度就是镜像全长）：诊断档 `computed=1168208 file=1168208 segments=6 reads=7`；矩阵产出的三档固件同样逐个相等——默认 1042704、诊断 1168912、现场 1155952。门禁 4 条：基座红 → 当前绿。 | 真机上自拷贝按新长度走一遍没做（见第五节）。与审查方案的两处差别见第二节 2。 |
| M14-F-11 | 已修未记 | **本体不是本批次修的**：U 线 `c380d88` 已把 `/ws` 路由改成 `.handle_ws_control_frames = false`（`esp32/components/tcs_web/http_server.c`，`/ws` 那一项；`WS_PROTOCOL.md:22` 已写明"服务端自己应答 PING/CLOSE"）。IDF v5.5.5 源码核实：`esp_http_server/src/httpd_ws.c:509-553`——该标志为 false 时 httpd 自己读掉 PING 并回同载荷的 PONG、读掉 CLOSE 并回 CLOSE；`httpd_parse.c:813-818`——PING / CLOSE 不会进处理函数。**本批次加的**（提交 `e74007d`）：① `web_selftest_ws.c` 新增 B2——发一个 RFC 6455 PING（载荷 `hb`），断言收到 PONG 且载荷相同；② 门禁 5 条：`/ws` 必须保持 `handle_ws_control_frames = false` 与 `ws_post_handshake_cb`，应用层不得自己构造 PONG / CLOSE 帧，webtest 里必须有 PING → PONG 用例。 | 门禁对 U 线修复之前的提交 `af1b116`（`c380d88` 的父提交）导出跑：3 条红（标志位、握手回调、webtest 用例）；对基座 `de997cb`：现状本身的 4 条已绿（即"审查之后已被别的提交修掉"），只有 webtest 用例那条红（本批次新加的）；当前 5 条全绿。诊断档增量构建 0 error 0 warning。 | B2 要在真机上跑 webtest 才算数（WS 应为 17/17）。没有按审查方案在 `ws_on_recv` 里再写一遍分流的理由见第二节 3。 |
| M14-F-12 | 已修未记 | **主体不是本批次修的**：U 线 `c380d88` 的 `ws_register()`——表满时回 `{"t":"err","code":"server_busy",…}` 并 `ws_drop()` 关会话（`websocket.c`，`WS_PROTOCOL.md:24` 已写明）。**审查方案里还没做的"一致性断言"本批次补上**（提交 `c7672f0`）：`websocket.h` 新增 `WS_HTTPD_MAX_SOCKETS 7`（httpd 的 `max_open_sockets`，等于 IDF 默认值）；`web_start()` 显式写入 `cfg.max_open_sockets`（行为不变）；`websocket.c` 两条 `_Static_assert`：① `WS_MAX_CLIENTS + 2 <= WS_HTTPD_MAX_SOCKETS`（表占满时还给普通 HTTP 请求留 2 个名额）；② `WS_HTTPD_MAX_SOCKETS + 3 <= CONFIG_LWIP_MAX_SOCKETS`（与 `httpd_main.c` 里 httpd 自己的运行期检查同一条：httpd 另用监听 + 两个 UDP 控制口）。门禁 6 条。 | **断言的判别性**：两条 `_Static_assert` 原样放进单独的 .c 用 `cl /std:c11` 编译——现配置（4 / 7 / 10）通过；`WS_MAX_CLIENTS=6` → `error C2338: static_assert failed: 'WS client table leaves no room for plain HTTP connections'`；`CONFIG_LWIP_MAX_SOCKETS=9` → `error C2338: static_assert failed: 'httpd sockets exceed the lwip socket table …'`。**门禁**：对 `af1b116` 这一组 6 条全红（含 `ws_register` 那条）；对基座 `de997cb` 只有 `ws_register` 一条绿（U 线已修）；当前 6 条全绿。诊断档增量构建 0 error 0 warning。 | "第 5 个客户端收到 busy 并断开、服务不崩"的真机验证仍未做：回环里造不出来（要 11 个 socket，webtest 第 G 段的注释），只能在设备外用 5 个页面连（第五节）。 |
| M14-F-13 | 已实施待真机 | **`web_status.c`** 新增纯函数 `web_ota_mode_value()`：只认精确的 `"0"`（拷贝 + 切换）/ `"1"`（回滚），其余 -1。**`http_server.c`** 的 `h_api_ota`：`httpd_req_get_url_query_str` → `httpd_query_key_value(q, "m", …)` → `web_ota_mode_value`；取不到或不合法 → 400 `{"t":"ota","started":false,"reason":"bad_m"}`，两种模式都不触发。**m 必填**（以前不带 query 时静默按 m=0 拷贝 + 重启）。`web_e2e.py` 的说明行与 `main.c` 头注释同步。提交 `86b0c2d`、`72c1910`。 | 主机 `esp32/tests/test_web_hardening.c`（`run webhard`）。**修前**（旧语义"以 1 开头就当回滚，其余拷贝"落盘）：`test_web_hardening: PASS=2 FAIL=12`——`m=10`、`m=11`、`m=1x`、`m=01`、`m=2`、`m=-1`、`m=0.0 / 1.0`、空值、没带 m、带空格、`true / false` 全红。**修后**：`test_web_hardening: PASS=14 FAIL=0`。门禁 3 条：基座红 → 当前绿。诊断档增量构建 0 error 0 warning（`/api/ota` 只在诊断档注册）。 | 真机上 `curl` 的 400 没做。与审查方案的差别（比 `atoi(val)==1` 更严）见第二节 1。 |
| M10-F-06 | 已修并验证 | ① **`esp32/components/tcs_wifi/softap.c`**：启动日志由 `pass=%s` 改为 `pass=***`。② **正典**：仓库里自述"本文档是唯一现行规格"的是 `docs/现行规格说明书_v1.2.md`——§6 加"热点口令与 NVS"条款（口令非机密：只挡偶然邻近的设备，不是访问控制；NVS 不加密；量产加密需产线烧录环节配合，列为后续项；网页不做鉴权是 2026-11-25 的产品决定），§10 加对应的后续项，变更史加第 10 条。提交 `f68535a`。 | 门禁 7 条：`softap.c` 不得有 `pass=%s`；`ESP_LOG` 各调用的实参里（去掉格式串后）不得出现 `pass` 变量；版本号最高的《现行规格说明书》里必须有"口令非机密""NVS 不加密""产线""后续项"。**对基座 `de997cb`**：5 条红；**当前**：全绿。诊断档增量构建 0 error 0 warning；三档矩阵见第三节。 | 历史证据文档里已经留存的口令（如 `docs/evidence/WP8-WP9_esp32.md`、`docs/复验指引卡_v4.md`）是历史记录，没有改写——要不要清由协调者定。真机启动日志应看到 `AP "TCS-xxxx" up, IP 192.168.4.1 pass=***`。 |

**判定计数**：已修并验证 2（M08-F-12、M10-F-06）/ 已实施待真机 2（X03-F-12、M14-F-13）/ 已修未记 2（M14-F-11、M14-F-12）/ 不适用 0 / 未做 0。

## 二、与审查方案不一样的地方（都是"有理由才改"）

1. **M14-F-13：不合法的 `m` 一律 400，不是 `atoi(val)==1`。** 方案下 `?m=10` 会落到"拷贝 + 重启"，`?m=1x` 仍当回滚。两种模式都会重启设备，输入不合法时不该执行任何一种；审查对这条的原始批评也正是"解析失败的方向落在更危险的那一侧"。连带：不带 `m` 也回 400（方案里 `httpd_query_key_value` 取不到键就是失败）。`web_e2e.py` 只用 `?m=1`，不受影响。关单条件写的是"`?m=10` 走升级而非回滚"——现在是 400，请确认可接受。
2. **M08-F-12：保留手写解析，没有改用 `esp_image_get_metadata()`；头 / 段表不合法时放弃拷贝，不再"整槽兜底"。** 理由：审查自己给的验证方法是"把 `ota_image_size` 的逻辑抽成纯函数喂构造头"，换成官方调用之后主机上就没有可测的纯函数了；另外不需要给 `main` 组件加 `bootloader_support` 依赖。"整槽兜底"的实际效果是擦写 3 MB 之后在 `esp_ota_end` 校验失败，不如直接放弃。
3. **M14-F-11：没有在 `ws_on_recv` 里补 PING / PONG / CLOSE 分流。** 现状（IDF 自己应答）已经是真机 webtest 验证过的路径；再加一段应用层分流，要么是把标志改回 true 去换一条没上过真机的路径，要么是一段永远执行不到的代码。所以改成"把现状钉住"：门禁 + webtest 用例。
4. **M14-F-12：一致性断言对的是 httpd 的 `max_open_sockets`。** IDF 5.x 里没有 `CONFIG_HTTPD_MAX_OPEN_SOCKETS` 这个配置项，它是 `httpd_config_t` 的字段（默认 7）；所以把它写成宏 `WS_HTTPD_MAX_SOCKETS` 并在 `web_start()` 显式赋值，断言才能落在编译期。

## 三、本批次验证总结果

- **ESP32 构建矩阵**（`bash tools/build_matrix.sh --esp32-only --diag --site`，先 source IDF，`IDF_BUILD_JOBS=2`，整个批次只跑了这一次）：
  ```
  [esp32-default] exit=0 error=0 warning=0
  [esp32-diag] exit=0 error=0 warning=0
  [esp32-site] exit=2 error=2 warning=0
  build_matrix: FAIL
  ```
  现场档的两行 error 是同一次失败：IDF 自带文件 `components/esp_lcd/rgb/esp_lcd_panel_rgb.c:722` 上 GCC 自身崩溃（`internal compiler error: Segmentation fault`，`during RTL pass: ira`），与 S-G e3a 结果文档第三节记录的是同一处，与本批次改动无关（同一份文件在默认档、诊断档编译正常）。矩阵脚本在这一步中途退出。随后用矩阵自己那份配置从断点续编：`idf.py -B build-matrix-site -D SDKCONFIG=sdkconfig.matrix.site build` → rc=0、error 0、warning 0。
  交付档干净核对（矩阵脚本没走到，手工按脚本里同样的参数执行 `tools/site/verify_prod_clean.py`）：默认档 `PASS（…ESP32 命令集=无台架命令）`、诊断档 `PASS（…命令集=全部台架命令）`、现场档 `PASS（…命令集含 linkloop,linkmute,webtest）`。
  三档固件：默认 1042704 字节、诊断 1168912、现场 1155952。
  没有做的：可复现比对（`--twice`）、H743 四档从零构建（本批次没有改 H743 文件）——留给合并后的全量矩阵。
- **主机全量** `bash tests/run_all.sh`（在 `project_tcs/`）：
  ```
  [audit] PASS (报告与代码一致: 272 files, 0 uncovered, 0 unclassified)
  test_ota_pure: PASS=29 FAIL=0
  test_web_status: PASS=230 FAIL=0
  test_web_hardening: PASS=14 FAIL=0
  test_web_gates: PASS
  esp32-tests: ALL GREEN
  run_all: ALL GREEN
  ```
- **网页既有测试**：`python tests/mutation_web.py` → `[current] PASS=230 FAIL=0`、30 个变异全部 CAUGHT、`mutation_web: ALL AS EXPECTED`（退出码 0）。`web_mock`：起模拟器后取 5 个静态页与 4 个 `/api/*`（全 200）、WS 握手 101、首帧序列 `['reg', 'log', 'log', 'st', 'st']`、`/__audit` 正常——`smoke_web_mock: PASS=13 FAIL=0`（冒烟脚本不入库）。`web_mock.py` 是纯 Python 模拟器，不编译本批次改的任何 C 文件，它的结果只说明前端契约没被碰坏。
- **审计门禁**：新增 5 个文件后先 `python esp32/tools/official_lib_audit.py --update-manifest`（清单 267 → 272 个文件，0 uncovered，0 unclassified；`docs/` 不在审计枚举范围内，本文件不占清单条目），再不带参数跑一次重新生成报告；`--check-report` → `[audit] PASS (报告与代码一致: 272 files, 0 uncovered, 0 unclassified)`。清单与机器报告单独一个提交。
- **过程中失败过的（如实）**：
  1. 现场档的编译器崩溃，见上（已知的环境问题，续编即过）。
  2. 我在 `websocket.c` 的一行注释里写了 `/api/*`，`-Werror=comment` 报 `"/*" within comment`，构建失败一次；改成文字后通过（提交 `c7672f0` 之前）。
  3. M08 的差分用例最初取第二段 200 / 201 字节——两者 16 字节对齐后总长相同，是用例取值错了（被测实现是对的）；改成 200 / 216。
  4. MSVC 14.42 的 `/std:c11` 不带 C11 原子，主机用例要加 `/experimental:c11atomics`（第一次探测编译失败；加上后 `first=1 second=0 third=1`）。这一条写在 `run_m207_tests.sh` 的注释里。

## 四、有风险、需要人看的点

1. **OTA 自拷贝的长度算法对真实固件一直是错的，影响比审查描述的大。** 旧代码只读开头的 256 字节，而 esp_app 镜像的段头排在各自段数据的后面：真实固件第一段就有十几 KB，所以从第 2 个段头起读的都是缓冲之外的栈内存，"长度"是随机的（要么被钳成整槽 3 MB，要么是个小于镜像的数）；尾部公式还多算 1 字节。`docs/` 里找不到任何一次真机自拷贝的串口记录（搜 `ota: copy` / `ota OK` / `ota ROLLBACK` 只命中一个备份固件里的字符串）。新算法只在主机上对三个真实 .bin 核对过，真机上一次没跑。
2. **`/api/ota` 不带 `m` 现在回 400**，以前是拷贝 + 重启。只影响诊断 / 台架构建（交付档不注册这个端点）。台架上如果有人靠"不带参数"触发自拷贝，要改成 `?m=0`。
3. **socket 预算是满的**：httpd 的 7 个连接名额 + 它自己用的 3 个 = 10 = `CONFIG_LWIP_MAX_SOCKETS`。httpd 满员时热点 DHCP 服务没有余量。现有配置，本批次没有改，只是用断言钉住"不许再涨"。`WS_MAX_CLIENTS = 4` 让它平时到不了 7。
4. **第 5 个标签页的提示是错的**：`app.js` 对任何 `err` 帧都提示"设备无法解析上一条指令"，`server_busy` 也是它，然后按 1/2/4/8/10 s 倒计时重连。行为（拒掉 + 自动重连）是对的，文案误导。按"网页先不改"没有动，请用户定要不要改这一句。
5. **WS 的 `{"t":"ota","m":1.5}` 仍会被当成回滚**（`cJSON` 的 `valueint` 把 1.5 截成 1）。这是 M14-F-13 在 WS 侧的同类问题，不在条目范围内，没有改；要收紧的话是 `websocket.c` 里一行。
6. **webtest 的 WS 用例数由 16 变 17**（新增 B2）。别的自动检查没有依赖这个数。
7. **审计报告里的类别计数是易变量**（"构建产物 / 日志"一类随工作树里有没有构建目录而变），合并时由合并的人统一重新生成，说明里已经写过。

## 五、真机步骤（协调者做；本批次没有碰设备）

### 1. webtest 应当看到什么（诊断档或现场档）

```
webtest base=http://192.168.4.1
  /               HTTP 200  … bytes  OK        ← 这 9 行与之前完全相同
  …
webtest: 9/9 PASS (HTTP)
  ws loopback 192.168.4.1:80
    handshake: 101 Switching Protocols                 OK
    reg numbers carry no float32 tail                  OK
    first frames in order: reg, log, st                OK
    current hyst / body_cold read back from reg        OK
    ping -> pong carries ref                           OK
    WS PING frame -> PONG frame with the same payload  OK      ← 新增的一行
    set (same value) -> pv ok=1 with my ref            OK
    …（其余 10 行与之前相同）
  ws loopback: 17/17 PASS                                      ← 原来是 16/16
webtest: HTTP 9/9, WS 17/17 -> ALL PASS
```

因为加了 token 需要调整的用例：没有（token 不做）。如果 B2 是红的（`WS PING frame -> PONG frame with the same payload` 那一行显示 FAIL），说明 httpd 的自动应答没有生效，把 webtest 那一段串口原样贴回来。

### 2. OTA（只在诊断档；每一步都会让设备重启，加热不要进行）

```bash
# m 不合法 → 400，串口没有任何 ota 行
curl -i -X POST "http://192.168.4.1/api/ota?m=10"     # 400 {"t":"ota","started":false,"reason":"bad_m"}
curl -i -X POST "http://192.168.4.1/api/ota"          # 400（m 必填）

# 并发只跑一个：两条紧接着发（拷贝约 10 s，窗口足够）
curl -i -X POST "http://192.168.4.1/api/ota?m=0" & curl -i -X POST "http://192.168.4.1/api/ota?m=0"
```

应当看到：一个 `200 {"t":"ota","started":true}`，一个 `409 {"t":"ota","started":false,"reason":"busy"}`；串口只有一行

```
I (…) TCS: ota: copy ota_0@0x00020000 img=1141KB -> ota_1
W (…) TCS: ota refused: another OTA is in progress
I (…) TCS: ota OK: boot -> ota_1, restart in 1s
```

（`img=` 是镜像实际长度，诊断档固件是 1168912 字节 → 1141KB；以前这里会是 3072KB 或一个随机的数。）重启后运行分区应是 `ota_1`。回滚：`curl -i -X POST "http://192.168.4.1/api/ota?m=1"` → 200，串口 `ota ROLLBACK: boot ota_1 -> ota_0, restart in 1s`，重启后回到 `ota_0`。WS 同理：连发两次 `{"t":"ota"}`，第二次收到 `{"t":"err","code":"server_busy",…}`。

### 3. 手机验收（面向用户，6 步）

1. 手机连热点 `TCS-xxxx`（口令没变），浏览器打开 `192.168.4.1`——首页的状态、曲线、日志和之前一样。
2. 进"参数"页，把一个可改参数调一步、再调回原值——数值和提示与之前一样。
3. 进"设备"页——热点信息和固件版本正常显示；进"日志"页，能看到"网页已连接"。
4. 按"急停"再"确认恢复"——与之前一样。
5. 锁屏 30 秒再回来，或切到别的应用再回来——页面自动重连，数据恢复刷新。
6. （验 M14-F-12，可选）同一台手机再开 4 个标签页都打开控制台：连本页共 5 个里前 4 个正常；第 5 个拿不到状态、按 1/2/4/8/10 秒倒计时重连（提示文字是"设备无法解析上一条指令"——文案问题见第四节 4）。关掉其中一个标签页后，它应在 10 秒内自动连上。
