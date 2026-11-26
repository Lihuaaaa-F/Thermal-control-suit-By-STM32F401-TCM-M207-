/**
 * http_server.h — Web 服务器启动
 */
#pragma once
#include "esp_err.h"

/* P1-12 的另一半（S 线 i3）：httpd 起不来仍是 ESP_ERROR_CHECK（照旧中止）；WebSocket 推送任务
 * 没建成时返回 ws_init() 的 ESP_ERR_NO_MEM，app_main 的启动汇总据此置 ALM_SERVICE。*/
esp_err_t web_start(void);
