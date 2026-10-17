/**
 * http_server.c — Web 服务器（任务书 §4.3）
 * 静态：index.html/app.js/style.css/chart.umd.min.js（gzip EMBED，AP 无外网禁 CDN）
 * REST：/api/status（0x10 快照 JSON 兜底）、/api/registry（注册表 JSON）
 * WS：/ws → websocket.c
 */
#include "http_server.h"
#include <string.h>
#include "esp_http_server.h"
#include "esp_log.h"
#include "websocket.h"

static const char *TAG = "tcs_web";

/* ====== gzip 内嵌资源（CMake EMBED_FILES 生成符号）====== */
extern const uint8_t index_html_start[] asm("_binary_index_html_gz_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_gz_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_gz_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_gz_end");
extern const uint8_t style_css_start[]  asm("_binary_style_css_gz_start");
extern const uint8_t style_css_end[]    asm("_binary_style_css_gz_end");
extern const uint8_t chart_js_start[]   asm("_binary_chart_umd_min_js_gz_start");
extern const uint8_t chart_js_end[]     asm("_binary_chart_umd_min_js_gz_end");

static esp_err_t send_gz(httpd_req_t *req, const uint8_t *start, const uint8_t *end,
                         const char *ctype)
{
    httpd_resp_set_type(req, ctype);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    return httpd_resp_send(req, (const char *)start, end - start);
}

static esp_err_t h_index(httpd_req_t *req)
{ return send_gz(req, index_html_start, index_html_end, "text/html"); }

static esp_err_t h_appjs(httpd_req_t *req)
{ return send_gz(req, app_js_start, app_js_end, "application/javascript"); }

static esp_err_t h_style(httpd_req_t *req)
{ return send_gz(req, style_css_start, style_css_end, "text/css"); }

static esp_err_t h_chart(httpd_req_t *req)
{ return send_gz(req, chart_js_start, chart_js_end, "application/javascript"); }

static esp_err_t h_api_status(httpd_req_t *req)
{
    char buf[256];
    size_t n = ws_status_json(buf, sizeof(buf));
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, buf, n);
}

static esp_err_t h_api_registry(httpd_req_t *req)
{
    char *buf = malloc(8192);
    if (!buf) return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
    size_t n = ws_registry_json(buf, 8192);
    httpd_resp_set_type(req, "application/json");
    esp_err_t rv = httpd_resp_send(req, buf, n);
    free(buf);
    return rv;
}

/* /ws：握手 + 收发都进 websocket.c */
static esp_err_t h_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) return ws_on_open(req);
    return ws_on_recv(req);
}

static const httpd_uri_t s_uris[] = {
    { .uri = "/",               .method = HTTP_GET, .handler = h_index },
    { .uri = "/index.html",     .method = HTTP_GET, .handler = h_index },
    { .uri = "/app.js",         .method = HTTP_GET, .handler = h_appjs },
    { .uri = "/style.css",      .method = HTTP_GET, .handler = h_style },
    { .uri = "/chart.umd.min.js", .method = HTTP_GET, .handler = h_chart },
    { .uri = "/api/status",     .method = HTTP_GET, .handler = h_api_status },
    { .uri = "/api/registry",   .method = HTTP_GET, .handler = h_api_registry },
    { .uri = "/ws",             .method = HTTP_GET, .handler = h_ws,
      .is_websocket = true, .handle_ws_control_frames = true },
};

static httpd_handle_t s_httpd;

void web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = 10;
    cfg.stack_size = 8192;
    cfg.lru_purge_enable = true;
    ESP_ERROR_CHECK(httpd_start(&s_httpd, &cfg));
    for (size_t i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++)
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &s_uris[i]));
    ws_init(s_httpd);
    ESP_LOGI(TAG, "httpd up on :80");
}
