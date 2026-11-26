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
#include "control.h"        /* r7f（R7-07）：OTA 触发前查两路 TEC 的命令意图（control_tec_cmd）*/

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

/* 大 JSON（reg 现约 7KB、log 最坏约 9KB）在堆上序列化：httpd 任务栈只有 8KB */
typedef struct {
    size_t (*build)(char *, size_t);
    size_t   cap;
} big_json_t;

static const big_json_t k_reg_json = { ws_registry_json, WS_REG_BUF };
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
    /* 常态约 250 字节；上限按"热点名 32 + 密码 64 个字符全部是要 6 字节转义的字符"留：
     * 1024 - 计数约 220 = 800 > 96 × 6 + 100 */
    char buf[1024];
    size_t n = ws_linkstats_json(buf, sizeof buf);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    return n ? httpd_resp_send(req, buf, n)
             : httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "json too large");
}

/* WP-F.4 OTA 触发：POST /api/ota?m=0（自拷贝+切换）/ ?m=1（回滚）。m 必填。仅台架/出厂构建注册。*/
#if CONFIG_TCS_DIAG
/* 实现在 main.c（组件不能依赖 main，这里只声明）。返回 ESP_OK = 已受理；
 * ESP_ERR_INVALID_STATE = 已有一次 OTA 在进行（X03-F-12）；其余 = 这次没做成。 */
extern esp_err_t app_ota_trigger(int rollback);
static esp_err_t h_api_ota(httpd_req_t *req)
{
    /* M14-F-13：m 用 httpd_query_key_value 取、再精确比对（web_status.c 的 web_ota_mode_value）。
     * 以前是 strstr(q, "m=1")：?m=10、?m=1x 也当回滚；没带 query 或 query 超过 31 字节时读取失败，
     * 静默按 m=0 走"拷贝 + 重启"。现在取不到、或不是精确的 "0" / "1"，一律 400、什么都不做——
     * 两种模式都会重启设备，输入不合法时不该执行任何一种。*/
    char q[32] = {0}, val[8] = {0};
    int m = -1;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "m", val, sizeof(val)) == ESP_OK)
        m = web_ota_mode_value(val);
    if (m < 0) {
        httpd_resp_set_status(req, HTTPD_400);
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"t\":\"ota\",\"started\":false,\"reason\":\"bad_m\"}");
    }
    /* r7f（独立复核 R7-07，2026-11-26）：拷贝 / 回滚都会重启设备；两路 TEC 的命令意图必须都是关，
     * 否则如实回 outputs_on——不触发、不重启，也不"先自动急停"（那会在升级后留一个要人工确认的急停）。*/
    if (!web_ota_outputs_stopped((int)control_tec_cmd(1), (int)control_tec_cmd(2))) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_sendstr(req, "{\"t\":\"ota\",\"started\":false,\"reason\":\"outputs_on\"}");
    }
    esp_err_t rv = app_ota_trigger(m);
    httpd_resp_set_type(req, "application/json");
    if (rv == ESP_OK)
        return httpd_resp_sendstr(req, "{\"t\":\"ota\",\"started\":true}");
    /* X03-F-12：并发 OTA 只跑一个。409 = 上一次还没做完（拷贝进行中，或回滚后的 1s 重启窗口里）；
     * 500 = 这次没做成（没有对槽 / 任务或内存没申请到 / otadata 写失败）。以前不论成败都回 started:true。 */
    httpd_resp_set_status(req, rv == ESP_ERR_INVALID_STATE ? "409 Conflict" : HTTPD_500);
    return httpd_resp_sendstr(req, rv == ESP_ERR_INVALID_STATE
                                    ? "{\"t\":\"ota\",\"started\":false,\"reason\":\"busy\"}"
                                    : "{\"t\":\"ota\",\"started\":false,\"reason\":\"failed\"}");
}
#endif /* CONFIG_TCS_DIAG: /api/ota */

/* /ws 上收到的帧（握手之后 httpd 把这条连接上的每个帧都交给它）→ websocket.c */
static esp_err_t h_ws(httpd_req_t *req)
{
    return ws_on_recv(req);
}

/* httpd 握手成功后**不会**调用 URI 的 handler（esp_http_server 源码：
 * "If the request is websocket handshake, then do not call the uri->handler"），
 * 只在打开了这个 Kconfig 选项时调用 ws_post_handshake_cb。客户端的登记与首推都在那个回调里；
 * 少了它，页面能连上（101）却永远收不到注册表、状态和任何回执——2026-11-24 真机回环自检发现，
 * 之前只验过 HTTP 端点、浏览器测的又是模拟器，所以一直没暴露。*/
#if !CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT
#error "CONFIG_HTTPD_WS_POST_HANDSHAKE_CB_SUPPORT=y is required (see sdkconfig.defaults): without it no WebSocket client is ever registered"
#endif

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
      /* control frames 交给 httpd 自己应答：PING 回 PONG、CLOSE 回 CLOSE 并关连接。
       * 以前设成 true，处理函数会把 CLOSE 的状态码当 JSON 解析、回一帧 err，客户端表里
       * 还留着这个 fd（独立复核 P1-1）。*/
      .is_websocket = true, .handle_ws_control_frames = false,
      .ws_post_handshake_cb = ws_on_open },
};

static httpd_handle_t s_httpd;

void web_start(void)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_uri_handlers = sizeof(s_uris) / sizeof(s_uris[0]) + 2;
    cfg.stack_size = 8192;
    /* 客户端连接名额显式写出（等于 IDF 的默认值 7，行为不变）：websocket.c 里的容量断言以它为准（M14-F-12）*/
    cfg.max_open_sockets = WS_HTTPD_MAX_SOCKETS;
    /* 连接数吃紧时挤掉最久没活动的连接。WS 会话每收到一帧推送都会刷新自己的计数
     * （websocket.c），所以先被挤的是半死的普通 HTTP 连接，不是正在看页面的手机。*/
    cfg.lru_purge_enable = true;
    /* 一条半死连接的阻塞发送最多卡 httpd 任务这么久（默认 5s：期间所有推送和 HTTP 请求都停）*/
    cfg.send_wait_timeout = 2;
    ESP_ERROR_CHECK(httpd_start(&s_httpd, &cfg));
    for (size_t i = 0; i < sizeof(s_uris) / sizeof(s_uris[0]); i++)
        ESP_ERROR_CHECK(httpd_register_uri_handler(s_httpd, &s_uris[i]));
    ws_init(s_httpd);
    ESP_LOGI(TAG, "httpd up on :80 (%u uris)", (unsigned)(sizeof(s_uris) / sizeof(s_uris[0])));
}
