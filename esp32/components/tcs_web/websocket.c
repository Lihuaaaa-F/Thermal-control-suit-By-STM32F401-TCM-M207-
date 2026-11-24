/**
 * websocket.c — /ws 客户端管理 + 状态推送 + set/cmd 接收（契约见 WS_PROTOCOL.md v2）
 *
 * 数据源（U 线 W1 改）：全部取**本端权威**——状态取 g_tcs/控制状态机/锁存/布防/链路活性，
 * 注册表取本端参数表。此前状态取屏幕回传的 0x10：屏幕没连上网页就整页不可用，
 * 延迟还要多绕 0.5–1.2s。现在屏幕断连只体现为 st.screen=0，网页照常工作。
 *
 * 推送：
 *   st   100ms 采集；任一业务字段变化立即推，否则每 WS_PUSH_MS 推一帧（心跳）
 *   pv   任一来源改参（on_broadcast → ws_param_changed）；网页自己发的带 ref 回显
 *   ack  命令回执（带 ref）
 *   reg  连接建立时推一次（之后由 pv 增量更新，不重复推 8KB）
 *   log  连接建立推全量历史，之后只推增量；事件由 st 差分 + 显式记录产生
 * 接收：{"t":"set","id","v","ref"} / {"t":"cmd","c","ref"} / {"t":"ping","ref"}
 *
 * 发送一律经 httpd_queue_work 投递到 httpd 线程（官方线程安全路径）；
 * 纯逻辑（字段/差分/日志环）在 web_status.c，主机单测覆盖。
 */
#include "websocket.h"
#include "web_status.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "esp_log.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_app_desc.h"
#include "cJSON.h"
#include "link_task.h"
#include "param_registry.h"
#include "control.h"
#include "post.h"
#include "m207_service.h"
#include "softap.h"          /* 热点名/密码（设备页"如何连接"）*/
#include "esp_netif.h"

static const char *TAG = "tcs_ws";

#define WS_MAX_CLIENTS   4
#define WS_TICK_MS       100u     /* 采集 + 差分节拍 */
#define WS_PUSH_MS       500u     /* 无变化时的周期推送 */
#define WS_TX_MAX        16384u   /* 单帧上限（reg 全量约 10KB）*/

static httpd_handle_t s_hd;
static int s_fds[WS_MAX_CLIENTS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_fw[32];                  /* 本端固件版本（init 时取一次）；与 esp_app_desc_t.version[32] 同宽 */
static volatile uint32_t s_seq;        /* st 推送序号 */

/* ====== 客户端表 ====== */

static void fds_add(int fd)
{
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_fds[i] == fd) break;
        if (s_fds[i] <= 0) { s_fds[i] = fd; break; }
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

static int fds_snapshot(int *out, int only_fd)
{
    int n = 0;
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_fds[i] > 0 && (only_fd <= 0 || s_fds[i] == only_fd)) out[n++] = s_fds[i];
    taskEXIT_CRITICAL(&s_mux);
    return n;
}

/* ====== 发送（httpd 线程执行）====== */

typedef struct { size_t len; int only_fd; char json[]; } ws_tx_work_t;

static void ws_tx_work_fn(void *arg)
{
    ws_tx_work_t *w = arg;
    httpd_ws_frame_t pkt = {
        .final = true, .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)w->json, .len = w->len,
    };
    int fds[WS_MAX_CLIENTS];
    int n = fds_snapshot(fds, w->only_fd);
    for (int i = 0; i < n; i++)
        if (httpd_ws_send_frame_async(s_hd, fds[i], &pkt) != ESP_OK)
            fds_del(fds[i]);          /* 死连接除名（重连握手重新入册）*/
    free(w);
}

/* 投递一帧：only_fd>0 只发给该客户端，否则广播。无接收者时不分配（空闲零开销）。*/
static void ws_send_json(const char *json, size_t len, int only_fd)
{
    if (!s_hd || !json || !len || len > WS_TX_MAX) return;
    int fds[WS_MAX_CLIENTS];
    if (fds_snapshot(fds, only_fd) == 0) return;
    ws_tx_work_t *w = malloc(sizeof *w + len);
    if (!w) { ESP_LOGW(TAG, "tx drop: oom (%u bytes)", (unsigned)len); return; }
    memcpy(w->json, json, len);
    w->len = len;
    w->only_fd = only_fd;
    if (httpd_queue_work(s_hd, ws_tx_work_fn, w) != ESP_OK) free(w);
}

/* 定长字符串字面量：长度由 strlen 求，避免手数字节数写错 */
static void ws_send_str(const char *json, int only_fd)
{
    if (json) ws_send_json(json, strlen(json), only_fd);
}

/* ====== 取数：本端权威 ====== */

static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000LL); }

/* g_tcs 由控制/链路/传感器多个任务写、没有公共锁：读两遍，一致才采信（撕裂读重试）。*/
static void state_read(tcs_state_t *out)
{
    tcs_state_t a, b;
    int tries = 4;
    do {
        a = g_tcs;
        b = g_tcs;
    } while (memcmp(&a, &b, sizeof a) != 0 && --tries);
    *out = a;
}

static void gather(web_status_t *s)
{
    tcs_state_t st;
    state_read(&st);
    int64_t us = esp_timer_get_time();
    uint32_t age = m207_temp_age_ms();
    memset(s, 0, sizeof *s);
    s->ts_ms   = (uint32_t)(us / 1000);
    s->tc1_act = st.tc1_act;
    s->tc2_act = st.tc2_act;
    s->body    = st.body_temp;
    s->age_ms  = (uint16_t)(age > 0xFFFFu ? 0xFFFFu : age);
    s->tc1_set = param_get(PARAM_ID_TC1_SET);
    s->tc2_set = param_get(PARAM_ID_TC2_SET);
    s->tec1    = st.tec1_on;
    s->tec2    = st.tec2_on;
    s->fan     = st.fan_on;
    s->mode    = st.mode;
    s->state   = (uint8_t)control_state();
    s->alarm   = st.alarm_bits;
    s->latch   = control_safety_latched();
    s->armed   = (uint8_t)post_arm_state();
    s->m207       = st.m207_online;
    s->body_ok    = st.body_sensor_ok;
    s->sens_open  = st.m207_sensor_open;
    s->screen  = link_alive() ? 1u : 0u;
    s->up_s    = (uint32_t)(us / 1000000LL);
    s->reset   = (uint8_t)esp_reset_reason();
    s->heap    = esp_get_free_heap_size();
}

/* ====== JSON 构造 ====== */

/* 一条 st（含 t/seq/ver/fw）。返回字节数；失败 0。buf 由调用方提供（不嵌套分配大缓冲）。*/
static size_t status_json(char *buf, size_t cap, uint32_t seq, const web_status_t *s)
{
    if (!buf || !s) return 0;
    cJSON *root = cJSON_CreateObject();
    if (!root) return 0;
    size_t r = 0;
    do {
        if (!cJSON_AddStringToObject(root, "t", "st") ||
            !cJSON_AddNumberToObject(root, "seq", seq) ||
            !cJSON_AddNumberToObject(root, "ver", TCS_PROTO_VER) ||
            !cJSON_AddStringToObject(root, "fw", s_fw) ||
            web_status_fill_json(s, root) != 0) break;
        char *j = cJSON_PrintBuffered(root, 768, 0);
        if (!j) break;
        size_t len = strlen(j);
        if (len < cap) { memcpy(buf, j, len); r = len; }
        cJSON_free(j);
    } while (0);
    cJSON_Delete(root);
    return r;
}

size_t ws_status_json(char *buf, size_t cap)
{
    web_status_t s;
    gather(&s);
    return status_json(buf, cap, s_seq, &s);
}

/* reg 帧缓存：def=当前值随任何改参变化 → ws_param_changed() 递增代数；
 * 代未变直接复用缓存串，变了才重建（重建在锁外，换入在锁内）。*/
static char *s_reg_cache;
static size_t s_reg_cache_len;
static volatile uint32_t s_reg_gen;
static uint32_t s_reg_built_gen;
static portMUX_TYPE s_reg_mux = portMUX_INITIALIZER_UNLOCKED;

void ws_registry_invalidate(void)
{
    taskENTER_CRITICAL(&s_reg_mux);
    s_reg_gen++;
    taskEXIT_CRITICAL(&s_reg_mux);
}

/* 注册表 JSON 直接出自本端参数表（权威）。R3-A-3：50 条≈7KB 不能开在 httpd 任务栈上，
 * 也不能用 static（WS 推送任务与 /api/registry 处理任务可能并发重建）→ 一次性堆分配。*/
size_t ws_registry_json(char *buf, size_t cap)
{
    taskENTER_CRITICAL(&s_reg_mux);
    const char *hit = (s_reg_cache && s_reg_built_gen == s_reg_gen) ? s_reg_cache : NULL;
    size_t hit_len = s_reg_cache_len;
    if (hit && hit_len < cap) memcpy(buf, hit, hit_len);
    taskEXIT_CRITICAL(&s_reg_mux);
    if (hit) return hit_len < cap ? hit_len : 0;

    uint32_t g0 = s_reg_gen;      /* 数据快照前代：构建窗口内到达的事件必须使缓存失配 */
    int n = param_count();
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) { cJSON_Delete(root); cJSON_Delete(arr); return 0; }
    do {
        if (!cJSON_AddStringToObject(root, "t", "reg") ||
            !cJSON_AddItemToObject(root, "list", arr)) break;
        for (int i = 0; i < n; i++) {
            const param_desc_t *p = param_at(i);
            cJSON *it = cJSON_CreateObject();
            if (!it) break;
            cJSON_AddNumberToObject(it, "id",     p->id);
            cJSON_AddNumberToObject(it, "type",   p->type);
            cJSON_AddNumberToObject(it, "flags",  p->flags);
            cJSON_AddNumberToObject(it, "min",    p->min);
            cJSON_AddNumberToObject(it, "max",    p->max);
            cJSON_AddNumberToObject(it, "step",   p->step);
            cJSON_AddNumberToObject(it, "def",    p->value);     /* 当前值（字段名沿用 v1）*/
            cJSON_AddStringToObject(it, "key",  p->key  ? p->key  : "");
            cJSON_AddStringToObject(it, "name", p->name ? p->name : "");
            cJSON_AddStringToObject(it, "unit", p->unit ? p->unit : "");
            cJSON_AddItemToArray(arr, it);
        }
        char *j = cJSON_PrintBuffered(root, 16384, 0);
        if (!j) break;
        size_t len = strlen(j);
        taskENTER_CRITICAL(&s_reg_mux);
        char *old = s_reg_cache;
        s_reg_cache = j;
        s_reg_cache_len = len;
        s_reg_built_gen = g0;
        taskEXIT_CRITICAL(&s_reg_mux);
        if (old) cJSON_free(old);
        if (len >= cap) return 0;
        memcpy(buf, j, len);
        return len;
    } while (0);
    cJSON_Delete(root);
    return 0;
}

size_t ws_log_json(char *buf, size_t cap)
{
    return web_log_json(0, web_log_total(), buf, cap);
}

size_t ws_linkstats_json(char *buf, size_t cap)
{
    link_stats_t st;
    link_stats_get(&st);
    /* 顺带带上热点凭据与网址：网页"设备"页给别的手机看（它们不在 st 里：不常变，不必每 500ms 推）*/
    char ssid[33] = {0}, pass[65] = {0}, url[40] = "http://192.168.4.1";
    (void)softap_credentials(ssid, sizeof ssid, pass, sizeof pass);
    esp_netif_t *nf = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    esp_netif_ip_info_t ip;
    if (nf && esp_netif_get_ip_info(nf, &ip) == ESP_OK)
        snprintf(url, sizeof url, "http://" IPSTR, IP2STR(&ip.ip));
    /* 顺序即 web_link_keys；序列化与转义在 web_link_json（纯逻辑，主机有单测）*/
    const uint32_t cnt[WEB_LINK_N] = {
        st.rx_bytes, st.rx_frames, st.crc_err, st.len_err, st.gap_resets, st.dup,
        st.retry, st.txq_drop, st.uart_txdrop, st.echo_drop, st.pending_evict, st.proto_mismatch,
    };
    return web_link_json(cnt, ssid, pass, url, buf, cap);
}

/* ====== pv / ack ====== */

static void push_pv(uint16_t id, float v, const char *src, int clamped,
                    uint32_t ref, int ok, int err)
{
    char buf[160];
    int n = snprintf(buf, sizeof buf,
                     "{\"t\":\"pv\",\"id\":%u,\"v\":%.3f,\"src\":\"%s\",\"clamped\":%d,"
                     "\"ref\":%lu,\"ok\":%d,\"err\":%d}",
                     (unsigned)id, (double)(isfinite(v) ? v : 0.0f), src ? src : "system",
                     clamped ? 1 : 0, (unsigned long)ref, ok, err);
    if (n > 0 && (size_t)n < sizeof buf) ws_send_json(buf, (size_t)n, 0);
}

static void push_ack(int cmd, int rv, uint32_t ref, int only_fd)
{
    char buf[96];
    int n = snprintf(buf, sizeof buf, "{\"t\":\"ack\",\"c\":%d,\"rv\":%d,\"ref\":%lu}",
                     cmd, rv, (unsigned long)ref);
    if (n > 0 && (size_t)n < sizeof buf) ws_send_json(buf, (size_t)n, only_fd);
}

/* ====== 参数变更（任一来源；main 的 on_broadcast 钩子调用）====== */

/* 网页 set 的关联信息：只在 httpd 任务里写、并在同一次 param_set() 的同步回调里读。*/
static uint32_t s_web_ref;
static float    s_web_want;
static uint8_t  s_web_seen;

/* 改参来源：协议里的 src 标签（给前端）与中文名（给日志）。表外一律按"系统"。*/
static const struct { uint8_t src; const char *tag; const char *zh; } k_src_name[] = {
    { PARAM_SRC_WEB,     "web",     "网页" },
    { PARAM_SRC_CONSOLE, "console", "控制台" },
    { PARAM_SRC_TOUCH,   "screen",  "屏幕" },
    { PARAM_SRC_LINK,    "screen",  "屏幕" },
};
static const char *src_name(uint8_t src, int zh)
{
    for (size_t i = 0; i < sizeof k_src_name / sizeof k_src_name[0]; i++)
        if (k_src_name[i].src == src) return zh ? k_src_name[i].zh : k_src_name[i].tag;
    return zh ? "系统" : "system";
}

/* 数值按步进定小数位 + 单位，供日志显示 */
static void fmt_param(char *out, size_t cap, const param_desc_t *p)
{
    int dec = (p->step >= 1.0f) ? 0 : (p->step >= 0.1f) ? 1 : (p->step >= 0.01f) ? 2 : 3;
    snprintf(out, cap, "%.*f%s%s", dec, (double)p->value,
             (p->unit && p->unit[0]) ? " " : "", p->unit ? p->unit : "");
}

void ws_param_changed(const param_desc_t *p, uint8_t src)
{
    if (!p) return;
    uint32_t ref = 0;
    int clamped = 0;
    if (src == PARAM_SRC_WEB && s_web_ref) {
        ref = s_web_ref;
        s_web_seen = 1;
        clamped = (p->value != s_web_want);
    }
    push_pv(p->id, p->value, src_name(src, 0), clamped, ref, 1, 0);
    char val[32];
    fmt_param(val, sizeof val, p);
    web_log_addf(now_ms(), 'i', "param", "%s：%s 改为 %s", src_name(src, 1),
                 p->name ? p->name : p->key, val);
    ws_registry_invalidate();       /* reg 的 def=当前值变了，缓存失配 */
}

/* ====== WS 推送任务 ====== */

static void ws_push_task(void *arg)
{
    (void)arg;
    static char buf[768];              /* st 最坏约 450B；任务栈 4096 放不下，改 static */
    web_status_t prev, cur, last_pushed;
    uint8_t have_prev = 0, have_pushed = 0;
    uint32_t last_push_ms = 0, log_sent = 0;
    esp_task_wdt_add(NULL);            /* R1 P1-07：关键任务纳入 TWDT */
    for (;;) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(WS_TICK_MS));
        gather(&cur);

        /* ① 状态差分 → 事件日志（其它任务写入的条目也在这里一并增量推送）*/
        web_status_diff_log(have_prev ? &prev : NULL, &cur);
        prev = cur;
        have_prev = 1;
        uint32_t total = web_log_total();
        if (total > log_sent) {
            size_t need = 64u + (size_t)(total - log_sent) * WEB_LOG_ENTRY_MAX;
            char *jb = malloc(need);
            if (jb) {
                size_t n = web_log_json(log_sent, total, jb, need);
                if (n) ws_send_json(jb, n, 0);
                free(jb);
            }
            log_sent = total;
        }

        /* ② st：变了立刻推，否则每 WS_PUSH_MS 一帧 */
        if (!have_pushed || web_status_changed(&last_pushed, &cur) ||
            (uint32_t)(cur.ts_ms - last_push_ms) >= WS_PUSH_MS) {
            size_t n = status_json(buf, sizeof buf, ++s_seq, &cur);
            if (n) ws_send_json(buf, n, 0);
            last_pushed = cur;
            have_pushed = 1;
            last_push_ms = cur.ts_ms;
        }
    }
}

void ws_init(httpd_handle_t hd)
{
    s_hd = hd;
    memset(s_fds, 0xFF, sizeof(s_fds));   /* -1 */
    snprintf(s_fw, sizeof s_fw, "%s", esp_app_get_description()->version);
    web_log_reset();
    web_log_addf(now_ms(), 'i', "boot", "主控启动：固件 %s", s_fw);
    xTaskCreate(ws_push_task, "tcs_ws", 4096, NULL, 4, NULL);
}

/* ====== 握手 / 接收 ====== */

/* 在 httpd 处理函数里直接发给刚握手的 fd（官方口径：httpd 上下文可直调）*/
static void send_now(int fd, const char *json, size_t len)
{
    if (!json || !len) return;
    httpd_ws_frame_t pkt = {
        .final = true, .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json, .len = len,
    };
    if (httpd_ws_send_frame_async(s_hd, fd, &pkt) != ESP_OK) fds_del(fd);
}

/* 大帧（reg 全量 / 日志全量）：堆上序列化，发完即释放（httpd 栈 8KB 放不下）*/
static void send_big(int fd, size_t (*build)(char *, size_t), size_t cap)
{
    char *buf = malloc(cap);
    if (!buf) return;
    size_t n = build(buf, cap);
    if (n) send_now(fd, buf, n);
    free(buf);
}

esp_err_t ws_on_open(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    fds_add(fd);
    ESP_LOGI(TAG, "ws client fd=%d", fd);
    web_log_addf(now_ms(), 'i', "ws_open", "网页已连接");
    send_big(fd, ws_registry_json, 16384);       /* 注册表（本端权威）*/
    send_big(fd, ws_log_json, WEB_LOG_JSON_CAP);     /* 事件日志全量 */
    {   /* 立刻给一帧状态，页面不必等下一个推送节拍 */
        char buf[768];
        size_t n = ws_status_json(buf, sizeof buf);
        send_now(fd, buf, n);
    }
    return ESP_OK;
}

static void handle_set(const cJSON *root, int fd)
{
    const cJSON *id = cJSON_GetObjectItem(root, "id");
    const cJSON *v  = cJSON_GetObjectItem(root, "v");
    const cJSON *r  = cJSON_GetObjectItem(root, "ref");
    if (!cJSON_IsNumber(id) || !cJSON_IsNumber(v)) {
        ws_send_str("{\"t\":\"err\",\"code\":\"bad_set\",\"msg\":\"set 需要 id 与 v\"}", fd);
        return;
    }
    uint16_t sid = (uint16_t)id->valueint;
    uint32_t ref = cJSON_IsNumber(r) ? (uint32_t)r->valuedouble : 0u;
    float want = (float)v->valuedouble;

    /* 关联信息在 param_set() 的同步回调（ws_param_changed）里消费 */
    s_web_ref = ref;
    s_web_want = want;
    s_web_seen = 0;
    int prv = param_set(sid, want, PARAM_SRC_WEB);
    s_web_ref = 0;
    if (prv == 0 && !s_web_seen) {
        push_pv(sid, param_get(sid), "web", 0, ref, 1, 0);     /* 值未变：没有广播，也要回执 */
    } else if (prv < 0) {
        push_pv(sid, param_get(sid), "web", 0, ref, 0, prv);   /* 只读/非法/约束拒绝 */
    }
    ESP_LOGI(TAG, "ws set id=0x%02x v=%.3f rv=%d -> %.3f", sid, (double)want, prv,
             (double)param_get(sid));
}

static void handle_cmd(const cJSON *root, int fd)
{
    const cJSON *c = cJSON_GetObjectItem(root, "c");
    const cJSON *r = cJSON_GetObjectItem(root, "ref");
    if (!cJSON_IsNumber(c)) {
        ws_send_str("{\"t\":\"err\",\"code\":\"bad_cmd\",\"msg\":\"cmd 需要 c\"}", fd);
        return;
    }
    int cv = c->valueint;
    uint32_t ref = cJSON_IsNumber(r) ? (uint32_t)r->valuedouble : 0u;
    int rv;
    if (cv == TCS_CMD_M207_SAVE) {
        rv = m207_save_all();                          /* 10：返回下发条数 */
        web_log_addf(now_ms(), rv < 0 ? 'w' : 'i', "m207_save",
                     rv < 0 ? "保存到模块失败（模块离线）" : "高级参数已下发模块保存：%d 条", rv);
    } else {
        rv = control_apply_cmd((uint8_t)cv);           /* 与屏幕 0x15 同一份语义 */
        if (cv == TCS_CMD_ESTOP || cv == TCS_CMD_ALARM_ACK) {
            web_log_addf(now_ms(), 'i', "web_cmd", "%s", cv == TCS_CMD_ESTOP
                         ? "网页：急停" : "网页：确认恢复");
        }
    }
    ESP_LOGI(TAG, "ws cmd=%d rv=%d -> state=%d alarm=0x%04X",
             cv, rv, (int)control_state(), (unsigned)g_tcs.alarm_bits);
    push_ack(cv, rv, ref, fd);     /* 只回给发起者：别的页面不该收到不属于自己的 ref */
}

esp_err_t ws_on_recv(httpd_req_t *req)
{
    httpd_ws_frame_t pkt = { 0 };
    uint8_t tmp[128];
    pkt.payload = tmp;
    int fd = httpd_req_to_sockfd(req);
    esp_err_t rv = httpd_ws_recv_frame(req, &pkt, 0);   /* 先探长：pkt.len */
    if (rv != ESP_OK) {
        fds_del(fd);
        return rv;
    }
    if (pkt.len >= sizeof(tmp)) {
        /* R1 修复 M14-F-02：超长帧不能只 return——载荷还在 socket 里，
         * 下一轮会把载荷首字节当帧头解析（流错位 + 越界读）。直接关连接最安全。*/
        fds_del(fd);
        if (s_hd) httpd_sess_trigger_close(s_hd, fd);
        ESP_LOGW(TAG, "ws frame too long (%u >= %u), session closed",
                 (unsigned)pkt.len, (unsigned)sizeof(tmp));
        return ESP_FAIL;
    }
    if (pkt.len == 0) return ESP_OK;   /* 空帧：不二次调用 recv（否则 IDF 会吞掉下一帧头）*/
    pkt.payload = tmp;
    rv = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (rv != ESP_OK) return rv;

    cJSON *root = cJSON_ParseWithLength((const char *)tmp, pkt.len);
    if (!root) {
        ws_send_str("{\"t\":\"err\",\"code\":\"bad_json\",\"msg\":\"无法解析\"}", fd);
        return ESP_OK;
    }
    const cJSON *t = cJSON_GetObjectItem(root, "t");
    const cJSON *r = cJSON_GetObjectItem(root, "ref");
    const char *ts = cJSON_IsString(t) ? t->valuestring : "";
    if (strcmp(ts, "set") == 0) {
        handle_set(root, fd);
    } else if (strcmp(ts, "cmd") == 0) {
        handle_cmd(root, fd);
    } else if (strcmp(ts, "ping") == 0) {
        char buf[80];
        int n = snprintf(buf, sizeof buf, "{\"t\":\"pong\",\"ts\":%lu,\"ref\":%lu}",
                         (unsigned long)now_ms(),
                         (unsigned long)(cJSON_IsNumber(r) ? r->valuedouble : 0.0));
        if (n > 0 && (size_t)n < sizeof buf) ws_send_json(buf, (size_t)n, fd);
    }
#if CONFIG_TCS_DIAG
    else if (strcmp(ts, "ota") == 0) {
        /* WP-F.4：m=0 自拷贝+切换；m=1 回滚（boot 指回对槽）*/
        extern void app_ota_trigger(int rollback);
        const cJSON *m = cJSON_GetObjectItem(root, "m");
        int rb = (cJSON_IsNumber(m) && m->valueint == 1) ? 1 : 0;
        ESP_LOGI(TAG, "ws ota trigger rollback=%d", rb);
        app_ota_trigger(rb);
    }
#endif /* CONFIG_TCS_DIAG: ws ota */
    else {
        ws_send_str("{\"t\":\"err\",\"code\":\"unknown_type\",\"msg\":\"未知消息类型\"}", fd);
    }
    cJSON_Delete(root);
    return ESP_OK;
}
