/**
 * websocket.h — /ws 推送与命令接收 + REST 共用的 JSON 构造（契约：WS_PROTOCOL.md v2）
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_http_server.h"
#include "param_registry.h"

void      ws_init(httpd_handle_t hd);
esp_err_t ws_on_open(httpd_req_t *req);     /* GET /ws 握手完成 → 登记 fd + 首推 reg/log/st */
esp_err_t ws_on_recv(httpd_req_t *req);     /* 数据帧：{"t":"set"/"cmd"/"ping"} */

/* JSON 构造（REST 兜底与 WS 推送共用）。返回字节数；cap 不够或失败返回 0。*/
size_t ws_status_json(char *buf, size_t cap);        /* 一条 st（本端权威数据源）*/
size_t ws_registry_json(char *buf, size_t cap);      /* 一条 reg（本端参数表）*/
size_t ws_log_json(char *buf, size_t cap);           /* 一条 log：事件日志全量（≤50 条）*/
size_t ws_linkstats_json(char *buf, size_t cap);     /* 屏链路收发统计（透传给 /api/linkstats）*/

/* 任一来源改参后调用（main 的 on_broadcast 钩子）：推 pv、记事件日志、reg 缓存失配。*/
void ws_param_changed(const param_desc_t *p, uint8_t src);
void ws_registry_invalidate(void);
