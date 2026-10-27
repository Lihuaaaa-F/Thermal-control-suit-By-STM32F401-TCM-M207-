# 研学卡 · IDF esp_http_server（含 WebSocket）（任务书 v13.4 §7b W8）

- 制卡：2026-10-27 · IDF v5.5.5 本体 `D:/Espressif/frameworks/esp-idf-v5.5.5` · 工程 `/d/Espressif/ws/tcs/esp32`
- ③ 自检：grep 命中 **44** == 表行数 44 ✓

## ① 官方文档与版本章节

- 组件头：`D:/Espressif/frameworks/esp-idf-v5.5.5/components/esp_http_server/include/esp_http_server.h`
- **头注原文摘录（逐字，esp_http_server.h L1769-1772，`httpd_ws_send_frame_async`——本卡核心口径）**：
  > `* @brief Low level send of a WebSocket frame out of the scope of current request`
  > `* using internally configured httpd send function`
  > `*`
  > `* This API should rarely be called directly, with an exception of asynchronous send using httpd_queue_work.`
  > （函数声明在 L1783：`esp_err_t httpd_ws_send_frame_async(httpd_handle_t hd, int fd, httpd_ws_frame_t *frame);`）

## ② 官方示例文件路径（本地实存，已 ls 验证）

- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/protocols/http_server/ws_echo_server/main/ws_echo_server.c`（WS 帧收发参照）
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/protocols/http_server/simple/main/main.c`（URI 注册/静态响应参照）
- `D:/Espressif/frameworks/esp-idf-v5.5.5/examples/protocols/http_server/async_handlers/main/main.c`（httpd_queue_work 异步工作参照）

## ③ 我们全部调用面清单

**grep 命令（仓库根执行）**：
```bash
grep -rn "httpd_" esp32/components esp32/main --include=*.c
```
**命中 44 行 == 下表 44 行**（域：全部自研 tcs_web，无 managed_components）。

| 文件:行 | API | 语义 |
|---|---|---|
| esp32/components/tcs_web/http_server.c:26 | httpd_req_t（形参类型） | send_gz 辅助函数签名（非调用） |
| esp32/components/tcs_web/http_server.c:29 | httpd_resp_set_type | gzip 资源设 Content-Type |
| esp32/components/tcs_web/http_server.c:30 | httpd_resp_set_hdr | 追加 Content-Encoding: gzip |
| esp32/components/tcs_web/http_server.c:31 | httpd_resp_set_hdr | 追加 Cache-Control: no-cache |
| esp32/components/tcs_web/http_server.c:32 | httpd_resp_send | 发内嵌 gzip 资源体 |
| esp32/components/tcs_web/http_server.c:35 | httpd_req_t（形参类型） | h_index 签名（非调用） |
| esp32/components/tcs_web/http_server.c:38 | httpd_req_t（形参类型） | h_appjs 签名（非调用） |
| esp32/components/tcs_web/http_server.c:41 | httpd_req_t（形参类型） | h_style 签名（非调用） |
| esp32/components/tcs_web/http_server.c:44 | httpd_req_t（形参类型） | h_chart 签名（非调用） |
| esp32/components/tcs_web/http_server.c:47 | httpd_req_t（形参类型） | h_api_status 签名（非调用） |
| esp32/components/tcs_web/http_server.c:51 | httpd_resp_set_type | status API 设 application/json |
| esp32/components/tcs_web/http_server.c:52 | httpd_resp_send | 发状态 JSON |
| esp32/components/tcs_web/http_server.c:55 | httpd_req_t（形参类型） | h_api_registry 签名（非调用） |
| esp32/components/tcs_web/http_server.c:58 | httpd_resp_send_err | 分配失败回 HTTPD_500 |
| esp32/components/tcs_web/http_server.c:60 | httpd_resp_set_type | registry API 设 json |
| esp32/components/tcs_web/http_server.c:61 | httpd_resp_send | 发注册表 JSON（失败走 err 分支） |
| esp32/components/tcs_web/http_server.c:68 | httpd_req_t（形参类型） | h_api_ota 签名（非调用） |
| esp32/components/tcs_web/http_server.c:72 | httpd_req_get_url_query_str | 解 ota 查询串 |
| esp32/components/tcs_web/http_server.c:77 | httpd_resp_set_type | ota API 设 json |
| esp32/components/tcs_web/http_server.c:78 | httpd_resp_sendstr | 回 ota started:true |
| esp32/components/tcs_web/http_server.c:82 | httpd_req_t（形参类型） | h_ws 签名（WS 升级入口，非调用） |
| esp32/components/tcs_web/http_server.c:88 | httpd_uri_t（类型声明） | URI 表 s_uris 声明（非调用） |
| esp32/components/tcs_web/http_server.c:101 | httpd_handle_t（类型声明） | 服务器实例句柄（非调用） |
| esp32/components/tcs_web/http_server.c:105 | httpd_config_t（类型声明） | HTTPD_DEFAULT_CONFIG 配置声明 |
| esp32/components/tcs_web/http_server.c:109 | httpd_start | 起服务器实例 |
| esp32/components/tcs_web/http_server.c:111 | httpd_register_uri_handler | 循环注册 s_uris 各路由 |
| esp32/components/tcs_web/websocket.c:5 | httpd_queue_work（注释） | 文件头注记录 W10#18 修复口径（非调用） |
| esp32/components/tcs_web/websocket.c:29 | httpd_handle_t（类型声明） | WS 模块持有的服务器句柄（非调用） |
| esp32/components/tcs_web/websocket.c:55 | httpd_ws_send_frame_async（注释） | 注释引官方头注"rarely called directly"（非调用） |
| esp32/components/tcs_web/websocket.c:56 | httpd_queue_work（注释） | 同上注释段（非调用） |
| esp32/components/tcs_web/websocket.c:58 | httpd_queue_work（注释） | 同上注释段（非调用） |
| esp32/components/tcs_web/websocket.c:67 | httpd_ws_frame_t（类型声明） | ws_tx_work_fn 内帧构造（非调用） |
| esp32/components/tcs_web/websocket.c:80 | httpd_ws_send_frame_async | httpd 线程内（queue_work 投递）按 fd 广播 |
| esp32/components/tcs_web/websocket.c:92 | httpd_queue_work | 推送任务把发送工作投递到 httpd 线程 |
| esp32/components/tcs_web/websocket.c:273 | httpd_handle_t（形参类型） | ws_init(hd) 签名（非调用） |
| esp32/components/tcs_web/websocket.c:281 | httpd_req_t（形参类型） | ws_on_open 签名（非调用） |
| esp32/components/tcs_web/websocket.c:283 | httpd_req_to_sockfd | 握手处理内取客户端 fd |
| esp32/components/tcs_web/websocket.c:290 | httpd_ws_frame_t（类型声明） | on_open 注册表首推帧构造（非调用） |
| esp32/components/tcs_web/websocket.c:294 | httpd_ws_send_frame_async | 握手处理器（=httpd 上下文）直推注册表 |
| esp32/components/tcs_web/websocket.c:300 | httpd_req_t（形参类型） | ws_on_recv 签名（非调用） |
| esp32/components/tcs_web/websocket.c:302 | httpd_ws_frame_t（类型声明） | on_recv 帧声明（非调用） |
| esp32/components/tcs_web/websocket.c:305 | httpd_ws_recv_frame | 先探长（max_len=0 得 pkt.len） |
| esp32/components/tcs_web/websocket.c:307 | httpd_req_to_sockfd | 收帧失败摘除该 fd |
| esp32/components/tcs_web/websocket.c:312 | httpd_ws_recv_frame | 按 pkt.len 实收载荷 |

## ④ 对照结论

1. **httpd_ws_send_frame_async 上下文口径 ✓（W2 已修，W10 #18→分流核销）**：官方原文（①L1772）"should rarely be called directly, with an exception of asynchronous send using httpd_queue_work"。历史违约=推送任务直调 async（复审表 #18 ⚠→W2）；**现行实现已修**：推送任务经 `httpd_queue_work`（websocket.c:92）把 `ws_tx_work_fn` 投递到 httpd 线程，在投递体内调 async（:80）=官方豁免路径；ws_on_open 握手处理器（:294）本就运行在 httpd 上下文=复审表 #19 ✅ 合法域。
2. **WS 收帧两段式 ✓**：先 `httpd_ws_recv_frame(req,&pkt,0)` 探长再按 `pkt.len` 实收（:305/:312），与官方 ws_echo_server.c:153/168 逐行同型；过长帧（≥128B）丢弃不回帧。
3. **服务器骨架 ✓**：HTTPD_DEFAULT_CONFIG→httpd_start→循环 register_uri_handler（:105-111），静态 gzip 资源 resp_set_hdr("Content-Encoding","gzip") 三件套，对照 simple 示例无偏差。
4. **fd 生命周期 ✓**：握手 to_sockfd 入表、收帧失败 fds_del（:283/:307），广播前锁内遍历快照集合（结合 FreeRTOS 卡临界区行）。
5. **偏差：无；待整改：无**。httpd_stop 未调用=服务器常驻同生命周期，与示例同型。
