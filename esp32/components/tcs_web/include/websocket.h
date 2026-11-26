/**
 * websocket.h — /ws 客户端表 + 推送 + 命令接收；REST 接口共用的 JSON 构造（契约：WS_PROTOCOL.md v2）
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "param_registry.h"

#define WS_MAX_CLIENTS   4          /* 同时在线的 WS 客户端上限（超出：回 server_busy 并关连接）*/
#define WS_REG_BUF       16384u     /* 注册表 JSON 的缓冲上限（现约 7KB）*/
/* httpd 的客户端连接名额（httpd_config_t.max_open_sockets）。等于 IDF 的默认值 7，web_start() 显式写入；
 * websocket.c 里的两条容量断言以它为准（M14-F-12）。*/
#define WS_HTTPD_MAX_SOCKETS 7

/* P1-12 的另一半（S 线 i3）：返回 ESP_OK = 推送任务已起；ESP_ERR_NO_MEM = 没建成（页面只收到第一帧推送）。
 * web_start() 把它带出去，app_main 的启动汇总据此置 ALM_SERVICE。*/
esp_err_t ws_init(httpd_handle_t hd);
esp_err_t ws_on_open(httpd_req_t *req);     /* GET /ws 握手完成 → 校验并入册 + 首推 reg/log/st */
esp_err_t ws_on_recv(httpd_req_t *req);     /* 完整的文本帧：{"t":"set"/"cmd"/"ping"} */
int       ws_client_count(void);            /* 客户端表里现有几条（自检用）*/

/* JSON 构造（REST 接口与首推共用）。buf 里是结尾带 NUL 的 C 串；返回串长，cap 不够或失败返回 0。*/
size_t ws_status_json(char *buf, size_t cap);        /* 一条 st（本端权威数据源）*/
size_t ws_registry_json(char *buf, size_t cap);      /* 一条 reg（本端参数表）*/
size_t ws_log_json(char *buf, size_t cap);           /* 一条 log：事件日志全量（≤50 条）*/
size_t ws_linkstats_json(char *buf, size_t cap);     /* 屏链路收发统计 + 热点凭据（/api/linkstats）*/

/* 任一来源改参后调用（main 的 on_broadcast 钩子）：推 pv、记事件日志、reg 缓存失配。*/
void ws_param_changed(const param_desc_t *p, uint8_t src);
void ws_registry_invalidate(void);

/* 网页端点自检（只在台架/现场档有实现；生产档恒返回 -1）：
 *   ws_selftest(dump) 回环 GET 全部 HTTP 端点并打印状态码/字节数，全 200 且 WS 部分全过返回 0
 *                     （web_selftest.c）；dump≠0 时把 /api/ 各接口的响应体与收到的每一帧原文转储
 *   ws_selftest_ws    固件自己当 WS 客户端连自己：握手/首推/回执/CLOSE/上限（web_selftest_ws.c）
 *   ws_dump           把一段原始字节按 base64 分块打到控制台，行格式见 web_selftest_ws.c
 *                     （电脑端用 esp32/tools/web_replay.py 解析并回放进真实浏览器）*/
int ws_selftest(int dump);
int ws_selftest_ws(const char *ip, int port, int *pass, int *total, int dump);
void ws_dump(const char *tag, const uint8_t *data, size_t len);
