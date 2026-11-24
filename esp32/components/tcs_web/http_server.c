/**
 * http_server.c — Web 服务器（任务书 §4.3；端点清单见 WS_PROTOCOL.md §1）
 *
 * 静态：index.html / style.css / app.js / chart.js（gzip 内嵌；设备热点无外网，禁 CDN）
 * REST：/api/status（一条 st）/api/registry（一条 reg）/api/log（一条 log）
 *       /api/linkstats（屏链路收发统计）
 * WS：  /ws → websocket.c
 * 资源一律 no-cache：响应没有 ETag/版本号，固件升级后不能让浏览器沿用旧脚本。
 */
#include "http_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "websocket.h"
#include "web_status.h"     /* WEB_LOG_JSON_CAP（日志全量的缓冲上限）*/

static const char *TAG = "tcs_web";

/* ====== gzip 内嵌资源（CMake EMBED_FILES 生成符号）====== */
extern const uint8_t index_html_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_gz_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_gz_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_gz_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_gz_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_gz_end");
extern const uint8_t chart_js_start[]   asm("_binary_chart_js_gz_start");
extern const uint8_t chart_js_end[]     asm("_binary_chart_js_gz_end");

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    const char    *ctype;      /* Content-Type（含 charset）*/
} asset_t;

/* 路由共用一个处理函数：具体资源经 httpd_uri_t.user_ctx 传入 */
static const asset_t k_index = { index_html_start, index_html_end, "text/html; charset=utf-8" };
static const asset_t k_style  = { style_css_start,  style_css_end,  "text/css; charset=utf-8" };
static const asset_t k_app    = { app_js_start,     app_js_end,     "application/javascript; charset=utf-8" };
static const asset_t k_chart  = { chart_js_start,   chart_js_end,   "application/javascript; charset=utf-8" };

static esp_err_t h_asset(httpd_req_t *req)
{
    const asset_t *a = req->user_ctx;
    if (!a) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no asset");
    httpd_resp_set_type(req, a->ctype);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)a->start, a->end - a->start);
}

/* 大 JSON（reg 约 10KB、log 约 9KB）在堆上序列化：httpd 任务栈只有 8KB */
typedef struct {
    size_t (*build)(char *, size_t);
    size_t   cap;
} big_json_t;

static const big_json_t k_reg_json = { ws_registry_json, 16384u };
static const big_json_t k_log_json = { ws_log_json, WEB_LOG_JSON_CAP };

static esp_err_t h_api_big(httpd_req_t *req)
{
    const big_json_t *b = req->user_ctx;
    if (!b) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no builder");
    char *buf = malloc(b->cap);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    size_t n = b->build(buf, b->cap);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t rv = n ? httpd_resp_send(req, buf, n)
                     : httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json too large");
    free(buf);
    return rv;
}

static esp_err_t h_api_status(httpd_req_t *req)
{
    char buf[768];
    size_t n = ws_status_json(buf, sizeof buf);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return n ? httpd_resp_send(req, buf, n)
             : httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json too large");
}

static esp_err_t h_api_linkstats(httpd_req_t *req)
{
    char buf[512];
    size_t n = ws_linkstats_json(buf, sizeof buf);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return n ? httpd_resp_send(req, buf, n)
             : httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json too large");
}

/* WP-F.4 OTA 触发：POST /api/ota?m=0（自拷贝+切换）/ m=1（回滚）。仅台架/出厂构建注册。*/
#if CONFIG_TCS_DIAG
extern void app_ota_trigger(int rollback);
static esp_err_t h_api_ota(httpd_req_t *req)
{
    char q[32] = {0};
    int m = 0;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK && strstr(q, "m=1"))
        m = 1;
    app_ota_trigger(m);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"t\":\"ota\",\"started\":true}");
}
#endif /* CONFIG_TCS_DIAG: /api/ota */

/* /ws：握手 + 收发都进 websocket.c */
static esp_err_t h_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ws_on_open(req);
    return ws_on_recv(req);
}

static const httpd_uri_t s_uris[] = {
    { .uri = "/",             .method = HTTP_GET, .handler = h_asset, .user_ctx = (void *)&k_index },
    { .uri = "/index.html",   .method = HTTP_GET, .handler = h_asset, .user_ctx = (void *)&k_index },
    { .uri = "/style.css",    .method = HTTP_GET, .handler = h_asset, .user_ctx = (void *)&k_style },
    { .uri = "/app.js",       .method = HTTP_GET, .handler = h_asset, .user_ctx = (void *)&k_app },
    { .uri = "/chart.js",     .method = HTTP_GET, .handler = h_asset, .user_ctx = (void *)&k_chart },
    { .uri = "/api/status",   .method = HTTP_GET, .handler = h_api_status },
    { .uri = "/api/registry", .method = HTTP_GET, .handler = h_api_big, .user_ctx = (void *)&k_reg_json },
    { .uri = "/api/log",      .method = HTTP_GET, .handler = h_api_big, .user_ctx = (void *)&k_log_json },
    { .uri = "/api/linkstats",.method = HTTP_GET, .handler = h_api_linkstats },
#if CONFIG_TCS_DIAG
    /* R1 P1-15：OTA 无鉴权（同 AP 任意手机可 POST 触发自拷贝+重启），
     * 只在台架/出厂构建注册；生产固件不暴露该端点。*/
    { .uri = "/api/ota",      .method = HTTP_POST, .handler = h_api_ota },
#endif
    { .uri = "/ws",           .method = HTTP_GET, .handler = h_ws,
      .is_websocket = true, .handle_ws_control_frames = true },
};

static httpd_handle_t s_httpd;

void web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = sizeof(s_uris) / sizeof(s_uris[0]) + 2;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&s_httpd, &cfg));
    for (size_t i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++)
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &s_uris[i]));
    ws_init(s_httpd);
    ESP_LOGI(TAG, "httpd up on :80 (%u uris)", (unsigned)(sizeof(s_uris) / sizeof(s_uris[0])));
}
