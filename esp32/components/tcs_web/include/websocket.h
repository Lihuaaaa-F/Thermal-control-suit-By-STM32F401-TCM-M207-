/**
 * websocket.h — /ws 推送与命令接收（任务书 §4.3 API）
 */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_http_server.h"

void   ws_init(httpd_handle_t hd);
esp_err_t ws_on_open(httpd_req_t *req);     /* GET /ws 握手完成 → 登记 fd + 首推注册表 */
esp_err_t ws_on_recv(httpd_req_t *req);     /* 数据帧：{"t":"set"/"cmd"} */

/* JSON 构造（REST 兜底与 WS 推送共用）*/
size_t ws_status_json(char *buf, size_t cap);
size_t ws_registry_json(char *buf, size_t cap);
