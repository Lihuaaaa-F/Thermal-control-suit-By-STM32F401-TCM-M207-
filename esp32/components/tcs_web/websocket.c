/**
 * websocket.c — /ws 客户端管理 + 500ms 状态推送 + set/cmd 下行（任务书 §4.2/4.3）
 *
 * 推送语义：
 *   {"t":"st",...}      每 500ms（0x10 快照 + boot_s）
 *   {"t":"pv",id,v,src} 参数变更（src=h743 触摸/CLI；src=web_ack 网页自己 set 的回显，
 *                        clamped=1 时前端 toast「已限幅至 x」）
 *   {"t":"reg",list}    注册表（连上即推 + 变更即推）
 * 接收：{"t":"set","id":1,"v":30.5} → link_set_param（ACK 必达）；
 *       {"t":"cmd","c":8} → link_send_cmd（8=急停，与屏同一 tcs_emergency_stop）。
 */
#include "websocket.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <math.h>
#include "link_task.h"

static const char *TAG = "tcs_ws";

#define WS_MAX_CLIENTS 4
#define WS_PUSH_MS     500

static httpd_handle_t s_hd;
static int s_fds[WS_MAX_CLIENTS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* 网页 set 期望回显（web_ack 判定）*/
static uint16_t s_expect_id;
static int64_t  s_expect_us;

static void fds_add(int fd)
{
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_fds[i] == fd) { taskEXIT_CRITICAL(&s_mux); return; }
        if (s_fds[i] <= 0) { s_fds[i] = fd; taskEXIT_CRITICAL(&s_mux); return; }
    }
    taskEXIT_CRITICAL(&s_mux);
}

static void fds_del(int fd)
{
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_fds[i] == fd) s_fds[i] = -1;
    taskEXIT_CRITICAL(&s_mux);
}

static int fds_send(const char *json, size_t len)
{
    httpd_ws_frame_t pkt = {
        .final = true, .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)json, .len = len,
    };
    /* 快照后发送：临界区只做拷贝（旧版循环内 EXIT/ENTER/continue 会打穿
     * 自旋锁配对——空槽触发 assert，演示模式 500ms 推送首次踩中）*/
    int fds[WS_MAX_CLIENTS];
    int n = 0;
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_fds[i] > 0) fds[n++] = s_fds[i];
    taskEXIT_CRITICAL(&s_mux);

    int alive = 0;
    for (int i = 0; i < n; i++) {
        if (httpd_ws_send_frame_async(s_hd, fds[i], &pkt) != ESP_OK) {
            fds_del(fds[i]);
        } else {
            alive++;
        }
    }
    return alive;
}

/* ====== JSON 构造 ====== */
size_t ws_status_json(char *buf, size_t cap)
{
    const link_status_t *s = link_status();
    int n;
    if (!s->valid) {
        n = snprintf(buf, cap, "{\"t\":\"st\",\"valid\":false}");
    } else {
        bool sensor_ok = (s->online & TCS_ONLINE_BODY_SENSOR) != 0;
        n = snprintf(buf, cap,
                     "{\"t\":\"st\",\"valid\":true,\"ver\":1,\"seq\":%lu,"
                     "\"tc1_act\":%.1f,\"tc2_act\":%.1f,\"body\":%.1f,"
                     "\"body_sensor_ok\":%s,"
                     "\"tec1\":%u,\"tec2\":%u,\"fan\":%u,\"mode\":%u,"
                     "\"alarm\":%u,\"online\":%u,\"boot_s\":%lld}",
                     (unsigned long)s->seq, s->tc1_act, s->tc2_act, s->body,
                     sensor_ok ? "true" : "false",
                     s->tec1, s->tec2, s->fan, s->mode,
                     s->alarm, s->online,
                     (long long)(esp_timer_get_time() / 1000000LL));
    }
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

size_t ws_registry_json(char *buf, size_t cap)
{
    link_reg_t regs[LINK_REGISTRY_MAX];
    int n = link_get_registry(regs, LINK_REGISTRY_MAX);
    size_t o = 0;
    o += snprintf(buf + o, cap - o, "{\"t\":\"reg\",\"list\":[");
    for (int i = 0; i < n && o < cap - 128; i++) {
        const tcs_reg_entry_t *e = &regs[i].e;
        float def = link_param_value(e->id);
        if (isnan(def)) def = e->min;   /* 无缓存（未收到 0x11）→ 滑条置 min */
        o += snprintf(buf + o, cap - o,
                      "%s{\"id\":%u,\"type\":%u,\"flags\":%u,"
                      "\"min\":%.3g,\"max\":%.3g,\"step\":%.3g,\"def\":%.3g,"
                      "\"key\":\"%s\",\"name\":\"%s\",\"unit\":\"%s\"}",
                      i ? "," : "", e->id, e->type, e->flags,
                      e->min, e->max, e->step, def, e->key, e->name, e->unit);
    }
    o += snprintf(buf + o, cap - o, "]}");
    return o;
}

static void push_pv(uint16_t id, float v, const char *src, int clamped)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf),
                     "{\"t\":\"pv\",\"id\":%u,\"v\":%.3f,\"src\":\"%s\"%s}",
                     id, v, src, clamped ? ",\"clamped\":true" : "");
    if (n > 0) fds_send(buf, (size_t)n);
}

/* link 事件回调（PARAM/REG 变更）：0x11 → pv 推送
 * src 判定：与网页最近 set 匹配（3s 内）→ web_ack（前端同步+clamp toast），
 * 否则 h743（触摸/CLI 改的，网页控件跟随）。clamp 信息从 0x90 ACK 取。 */
static int64_t s_seen_param_us;

static void on_link_event(void)
{
    const link_param_change_t *p = link_last_param();
    if (p->us == 0 || p->us == s_seen_param_us) return;
    s_seen_param_us = p->us;

    int clamped = 0;
    const link_ack_t *ack = link_last_param() ? link_last_ack() : NULL;
    if (ack && ack->id == p->id && ack->us >= p->us - 100000LL
        && esp_timer_get_time() - ack->us < 5000000LL
        && ack->status == TCS_ACK_CLAMP)
        clamped = 1;

    const char *src = "h743";
    if (s_expect_id && p->id == s_expect_id
        && esp_timer_get_time() - s_expect_us < 3000000LL
        && p->us >= s_expect_us) {
        src = "web_ack";
        s_expect_id = 0;
    }
    push_pv(p->id, p->v, src, clamped);
}

/* 500ms 推送任务 */
static void ws_push_task(void *arg)
{
    char buf[4096];
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(WS_PUSH_MS));
        size_t n = ws_status_json(buf, sizeof(buf));
        if (n) fds_send(buf, n);
    }
}

void ws_init(httpd_handle_t hd)
{
    s_hd = hd;
    memset(s_fds, 0xFF, sizeof(s_fds));   /* -1 */
    link_on_event(on_link_event);
    xTaskCreate(ws_push_task, "tcs_ws", 4096, NULL, 4, NULL);
}

esp_err_t ws_on_open(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    fds_add(fd);
    ESP_LOGI(TAG, "ws client fd=%d", fd);
    /* 连上即推注册表（握手全量兜底：H743 侧 0x14 缓存）*/
    char *buf = malloc(8192);
    if (buf) {
        size_t n = ws_registry_json(buf, 8192);
        httpd_ws_frame_t pkt = {
            .final = true, .type = HTTPD_WS_TYPE_TEXT,
            .payload = (uint8_t *)buf, .len = n,
        };
        httpd_ws_send_frame_async(s_hd, fd, &pkt);
        free(buf);
    }
    return ESP_OK;
}

esp_err_t ws_on_recv(httpd_req_t *req)
{
    httpd_ws_frame_t pkt = { 0 };
    uint8_t tmp[128];
    pkt.payload = tmp;
    esp_err_t rv = httpd_ws_recv_frame(req, &pkt, 0);   /* 先探长：pkt.len */
    if (rv != ESP_OK) {
        fds_del(httpd_req_to_sockfd(req));
        return rv;
    }
    if (pkt.len >= sizeof(tmp)) return ESP_OK;   /* 过长丢弃 */
    pkt.payload = tmp;
    rv = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (rv != ESP_OK) return rv;

    cJSON *root = cJSON_ParseWithLength((const char *)tmp, pkt.len);
    if (!root) return ESP_OK;
    cJSON *t = cJSON_GetObjectItem(root, "t");
    if (t && cJSON_IsString(t)) {
        if (strcmp(t->valuestring, "set") == 0) {
            cJSON *id = cJSON_GetObjectItem(root, "id");
            cJSON *v  = cJSON_GetObjectItem(root, "v");
            if (cJSON_IsNumber(id) && cJSON_IsNumber(v)) {
                s_expect_id = (uint16_t)id->valueint;
                s_expect_us = esp_timer_get_time();
                link_set_param((uint16_t)id->valueint, (float)v->valuedouble);
                ESP_LOGI(TAG, "ws set id=%u v=%.2f", (unsigned)id->valueint, v->valuedouble);
            }
        } else if (strcmp(t->valuestring, "cmd") == 0) {
            cJSON *c = cJSON_GetObjectItem(root, "c");
            if (cJSON_IsNumber(c)) {
                link_send_cmd((uint8_t)c->valueint);
                ESP_LOGI(TAG, "ws cmd=%d", c->valueint);
            }
        } else if (strcmp(t->valuestring, "ota") == 0) {
            /* WP-F.4：m=0 自拷贝+切换；m=1 回滚（boot 指回对槽）*/
            extern void app_ota_trigger(int rollback);
            cJSON *m = cJSON_GetObjectItem(root, "m");
            int rb = (cJSON_IsNumber(m) && m->valueint == 1) ? 1 : 0;
            ESP_LOGI(TAG, "ws ota trigger rollback=%d", rb);
            app_ota_trigger(rb);
        }
    }
    cJSON_Delete(root);
    return ESP_OK;
}
