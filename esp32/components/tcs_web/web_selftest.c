/**
 * web_selftest.c — 网页端点自检（只在台架/现场档编译：CONFIG_TCS_DIAG 或 CONFIG_TCS_SIM）
 *
 * 为什么需要（U 线，2026-11-24）：开发机连不上设备热点（一连就断网，且本机 aTrust 拦截到
 * 192.168.4.1 的 TCP），网页在真机上到底能不能被手机取到，只能让固件**用自己的 HTTP 客户端
 * 回环访问自己**一遍，把每个端点的状态码和字节数打到串口上。
 * 只做 GET，不改任何状态；生产档（两个开关都关）整段不编译，命令也不注册。
 * WebSocket 部分在 web_selftest_ws.c（固件自己当 WS 客户端连自己），由 ws_selftest() 一并调用。
 * `webtest dump`：另把 /api/ 各接口的响应体原文转储到控制台（ws_dump），供电脑端回放进真实浏览器。
 */
#include "websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if CONFIG_TCS_DIAG || CONFIG_TCS_SIM

#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_netif_ip_addr.h"
#include "esp_log.h"
#include "sdkconfig.h"

typedef struct {
    int bytes;          /* 收到的响应体总字节数 */
    uint8_t *buf;       /* 非 NULL 时把响应体整段留在这里（放不下就截断）*/
    size_t len, cap;
} fetch_t;

/* 事件回调签名与状态码取法按 esp_http_client.h（IDF v5.5.5）：
 *   typedef esp_err_t (*http_event_handle_cb)(esp_http_client_event_t *evt);
 *   int esp_http_client_get_status_code(esp_http_client_handle_t client); */
static esp_err_t on_evt(esp_http_client_event_t *evt)
{
    fetch_t *f = evt->user_data;
    if (evt->event_id != HTTP_EVENT_ON_DATA) return ESP_OK;
    f->bytes += evt->data_len;
    if (f->buf && f->len + (size_t)evt->data_len <= f->cap) {
        memcpy(f->buf + f->len, evt->data, (size_t)evt->data_len);
        f->len += (size_t)evt->data_len;
    }
    return ESP_OK;
}

/* GET 一个端点，返回响应体字节数（失败 -1）；状态码经 code_out 带回 */
static int fetch(const char *base, const char *path, int *code_out, fetch_t *f)
{
    char url[80];
    snprintf(url, sizeof url, "%s%s", base, path);
    esp_http_client_config_t cfg = {
        .url = url, .event_handler = on_evt, .user_data = f,
        .timeout_ms = 3000, .buffer_size = 1024, .buffer_size_tx = 512,
    };
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return -1;
    esp_err_t err = esp_http_client_perform(h);
    *code_out = esp_http_client_get_status_code(h);
    esp_http_client_cleanup(h);
    return (err == ESP_OK) ? f->bytes : -1;
}

int ws_selftest(int dump)
{
    /* 用热点网口自己的地址（默认 192.168.4.1），不写死 */
    char ip_str[16] = "192.168.4.1";
    char base[48] = "http://192.168.4.1";
    esp_netif_t *nf = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip;
    if (nf && esp_netif_get_ip_info(nf, &ip) == ESP_OK)
        snprintf(ip_str, sizeof ip_str, IPSTR, IP2STR(&ip.ip));
    snprintf(base, sizeof base, "http://%s", ip_str);

    static const char *const paths[] = {
        "/", "/index.html", "/style.css", "/app.js", "/chart.js",
        "/api/status", "/api/registry", "/api/log", "/api/linkstats",
    };
    int pass = 0, n = (int)(sizeof paths / sizeof paths[0]);
    printf("webtest base=%s%s\r\n", base, dump ? " (dump)" : "");
    for (int i = 0; i < n; i++) {
        /* 接口（/api/ 下的路径）的响应体是 JSON 原文：dump 时留下来转储。静态资源就是仓库里内嵌的 .gz，
         * 字节是否一致由 web_gz 门禁管，这里不重复转储。*/
        uint8_t *body = (dump && strncmp(paths[i], "/api/", 5) == 0) ? malloc(WS_REG_BUF) : NULL;
        fetch_t f = { .buf = body, .cap = WS_REG_BUF };
        int code = 0, len = fetch(base, paths[i], &code, &f);
        int ok = (len > 0 && code == 200);
        if (ok) pass++;
        printf("  %-14s HTTP %d  %d bytes  %s\r\n", paths[i], code, len, ok ? "OK" : "FAIL");
        if (body) {
            char tag[24];
            snprintf(tag, sizeof tag, "H%s", paths[i]);
            ws_dump(tag, body, f.len);
            free(body);
        }
    }
    printf("webtest: %d/%d PASS (HTTP)\r\n", pass, n);
    /* WebSocket 部分：固件自己当客户端连自己（web_selftest_ws.c）。
     * 开发机连不上设备热点，/ws 在真机上的行为只能这样验。*/
    int wp = 0, wt = 0;
    int wrv = ws_selftest_ws(ip_str, 80, &wp, &wt, dump);
    printf("webtest: HTTP %d/%d, WS %d/%d -> %s\r\n", pass, n, wp, wt,
           (pass == n && wrv == 0) ? "ALL PASS" : "HAS FAILURES");
    return (pass == n && wrv == 0) ? 0 : -1;
}

#else /* 生产档 */

int ws_selftest(int dump) { (void)dump; return -1; }

#endif /* CONFIG_TCS_DIAG || CONFIG_TCS_SIM */
