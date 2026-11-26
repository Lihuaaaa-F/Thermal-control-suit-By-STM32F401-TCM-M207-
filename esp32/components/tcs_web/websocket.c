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
 *   reg  连接建立时推一次（之后由 pv 增量更新，不重复推 7KB）
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
#include "sdkconfig.h"       /* CONFIG_LWIP_MAX_SOCKETS（下面的容量断言）*/

static const char *TAG = "tcs_ws";

#define WS_TICK_MS       100u     /* 采集 + 差分节拍 */
#define WS_PUSH_MS       500u     /* 无变化时的周期推送 */
#define WS_TX_MAX        WS_REG_BUF   /* 单帧上限（reg 全量是最大的一帧，现约 7KB）*/

/* M14-F-12：三处上限必须一致，改了哪一处都要在编译期被发现（主机侧 tests/test_web_gates.py 用同样的式子再核一遍）。
 * ① 客户端表占满时，httpd 还得给普通 HTTP 请求（取页面、/api 下的接口）留出名额；
 * ② 与 httpd 自己的运行期检查是同一条（esp_http_server/src/httpd_main.c）：httpd 另用 3 个 socket
 *    （监听 + 两个 UDP 控制口），加起来不能超过 lwip 的 socket 总数。*/
_Static_assert(WS_MAX_CLIENTS + 2 <= WS_HTTPD_MAX_SOCKETS,
               "WS client table leaves no room for plain HTTP connections");
_Static_assert(WS_HTTPD_MAX_SOCKETS + 3 <= CONFIG_LWIP_MAX_SOCKETS,
               "httpd sockets exceed the lwip socket table (httpd_start would fail at runtime)");

static httpd_handle_t s_hd;
static int s_fds[WS_MAX_CLIENTS];
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static char s_fw[32];                  /* 本端固件版本（init 时取一次）；与 esp_app_desc_t.version[32] 同宽 */
static volatile uint32_t s_seq;        /* st 推送序号 */

/* ====== 客户端表 ====== */

/* 这个 fd 现在是不是一条活着的 WebSocket 会话。表里的 fd 可能已经关闭、又被一条新的普通
 * HTTP 连接复用：往那种 fd 上发 WS 帧，数据会写进别人的 HTTP 响应里。
 * httpd_ws_send_frame_async 自己只查"有没有会话"，不查"是不是 WS"——所以每次发之前都先问
 * 这一句（独立复核 P1-1）。*/
static int fd_is_ws(int fd)
{
    return (fd > 0 && s_hd && httpd_ws_get_fd_info(s_hd, fd) == HTTPD_WS_CLIENT_WEBSOCKET) ? 1 : 0;
}

/* 入册。返回 0=成功，-1=表满，或者这条会话根本不是 WebSocket（不带 Upgrade 头的普通
 * GET /ws 也会走到握手处理函数来）。*/
static int fds_add(int fd)
{
    if (!fd_is_ws(fd)) return -1;
    int rv = -1;
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) {
        if (s_fds[i] == fd) { rv = 0; break; }
        if (s_fds[i] <= 0) { s_fds[i] = fd; rv = 0; break; }
    }
    taskEXIT_CRITICAL(&s_mux);
    return rv;
}

static void fds_del(int fd)
{
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_fds[i] == fd) s_fds[i] = -1;
    taskEXIT_CRITICAL(&s_mux);
}

int ws_client_count(void)
{
    int n = 0;
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++) n += (s_fds[i] > 0);
    taskEXIT_CRITICAL(&s_mux);
    return n;
}

static int fds_has(int fd)
{
    int in = 0;
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS && !in; i++) in = (s_fds[i] == fd);
    taskEXIT_CRITICAL(&s_mux);
    return in;
}

/* 取一份表快照：only_fd>0 只要它；否则要全部、但跳过 skip_fd */
static int fds_snapshot(int *out, int only_fd, int skip_fd)
{
    int n = 0;
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_fds[i] > 0 && (only_fd <= 0 || s_fds[i] == only_fd) && s_fds[i] != skip_fd)
            out[n++] = s_fds[i];
    taskEXIT_CRITICAL(&s_mux);
    return n;
}

/* ====== 发送（httpd 线程执行）====== */

typedef struct { size_t len; int only_fd; int skip_fd; char json[]; } ws_tx_work_t;

/* 关掉一条会话并除名。发送失败、表满、收到不支持的帧时用：只除名不关的话，这条连接还能
 * 发命令并被执行，却收不到任何回执（独立复核 P1-1）。*/
static void ws_drop(int fd)
{
    fds_del(fd);
    if (s_hd) httpd_sess_trigger_close(s_hd, fd);
}

static void ws_tx_work_fn(void *arg)
{
    ws_tx_work_t *w = arg;
    httpd_ws_frame_t pkt = {
        .final = true, .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)w->json, .len = w->len,
    };
    int fds[WS_MAX_CLIENTS];
    int n = fds_snapshot(fds, w->only_fd, w->skip_fd);
    for (int i = 0; i < n; i++) {
        int fd = fds[i];
        if (!fd_is_ws(fd)) { fds_del(fd); continue; }   /* 已经不是 WS 会话：除名，不往里写 */
        if (httpd_ws_send_frame_async(s_hd, fd, &pkt) != ESP_OK) {
            ESP_LOGW(TAG, "tx fail fd=%d, session closed", fd);
            ws_drop(fd);
            continue;
        }
        /* WS 会话隔 5s 才 ping 一次，是 LRU 表里最"久未使用"的——连接数吃紧时会被最先挤掉。
         * 每发一帧就刷新它的 LRU 计数，让半死的普通 HTTP 连接先让位。*/
        (void)httpd_sess_update_lru_counter(s_hd, fd);
    }
    free(w);
}

/* 投递一帧。only_fd>0 只发给该客户端；skip_fd>0 广播时跳过它（用于"带 ref 的只给请求者，
 * 其余人拿 ref=0 的副本"）。返回 1=已入队，0=没人收/放不下/内存不够/队列满。
 * 调用方据返回值决定要不要推进游标——没发出去的事件不能当发过。*/
static int ws_send_json(const char *json, size_t len, int only_fd, int skip_fd)
{
    if (!s_hd || !json || !len || len > WS_TX_MAX) return 0;
    int fds[WS_MAX_CLIENTS];
    if (fds_snapshot(fds, only_fd, skip_fd) == 0) return 0;
    ws_tx_work_t *w = malloc(sizeof *w + len);
    if (!w) { ESP_LOGW(TAG, "tx drop: oom (%u bytes)", (unsigned)len); return 0; }
    memcpy(w->json, json, len);
    w->len = len;
    w->only_fd = only_fd;
    w->skip_fd = skip_fd;
    if (httpd_queue_work(s_hd, ws_tx_work_fn, w) != ESP_OK) {
        /* 投递队列满（默认深度 6）：这一帧丢了。回执类帧丢了页面会等 1.5s 超时，所以必须留痕 */
        ESP_LOGW(TAG, "tx drop: httpd work queue full (%u bytes)", (unsigned)len);
        free(w);
        return 0;
    }
    return 1;
}

/* 定长字符串字面量：长度由 strlen 求，避免手数字节数写错 */
static int ws_send_str(const char *json, int only_fd)
{
    return json ? ws_send_json(json, strlen(json), only_fd, -1) : 0;
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

/* 注册表 JSON 直接出自本端参数表（权威）。序列化与数值格式在 web_registry_json（纯逻辑，
 * 主机有单测：数值用最短且能还原的写法，否则网页参数页的小数位会变成 17 位）。
 * R3-A-3：50 条约 7KB 不能开在 httpd 任务栈上，也不能用 static（WS 推送任务与
 * /api/registry 处理任务可能并发重建）→ 一次性堆分配。*/
#define WS_REG_MAX_N 64          /* 参数表上限（现 51 条）；指针数组放在栈上，别开太大 */

size_t ws_registry_json(char *buf, size_t cap)
{
    taskENTER_CRITICAL(&s_reg_mux);
    const char *hit = (s_reg_cache && s_reg_built_gen == s_reg_gen) ? s_reg_cache : NULL;
    size_t hit_len = s_reg_cache_len;
    if (hit && hit_len < cap) memcpy(buf, hit, hit_len + 1);   /* 连结尾 NUL */
    taskEXIT_CRITICAL(&s_reg_mux);
    if (hit) return hit_len < cap ? hit_len : 0;

    uint32_t g0 = s_reg_gen;      /* 数据快照前代：构建窗口内到达的事件必须使缓存失配 */
    int n = param_count();
    if (n > WS_REG_MAX_N) n = WS_REG_MAX_N;
    const param_desc_t *ps[WS_REG_MAX_N];
    for (int i = 0; i < n; i++) ps[i] = param_at(i);

    char *j = malloc(WS_REG_BUF);
    if (!j) return 0;
    size_t len = web_registry_json(ps, n, j, WS_REG_BUF);
    if (!len) { free(j); return 0; }
    taskENTER_CRITICAL(&s_reg_mux);
    char *old = s_reg_cache;
    s_reg_cache = j;
    s_reg_cache_len = len;
    s_reg_built_gen = g0;
    taskEXIT_CRITICAL(&s_reg_mux);
    free(old);                    /* 旧缓存也是 malloc 出来的 */
    if (len >= cap) return 0;
    memcpy(buf, j, len + 1);
    return len;
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

/* pv 的字段都是数字与固定的来源标签（没有自由文本），手拼安全。
 * to_fd>0 只发给它；skip_fd>0 广播时跳过它。*/
static void push_pv(uint16_t id, float v, const char *src, int clamped,
                    uint32_t ref, int ok, int err, int to_fd, int skip_fd)
{
    char buf[160];
    int n = snprintf(buf, sizeof buf,
                     "{\"t\":\"pv\",\"id\":%u,\"v\":%.3f,\"src\":\"%s\",\"clamped\":%d,"
                     "\"ref\":%lu,\"ok\":%d,\"err\":%d}",
                     (unsigned)id, (double)(isfinite(v) ? v : 0.0f), src ? src : "system",
                     clamped ? 1 : 0, (unsigned long)ref, ok, err);
    if (n > 0 && (size_t)n < sizeof buf) ws_send_json(buf, (size_t)n, to_fd, skip_fd);
}

static void push_ack(int cmd, int rv, uint32_t ref, int only_fd)
{
    char buf[96];
    int n = snprintf(buf, sizeof buf, "{\"t\":\"ack\",\"c\":%d,\"rv\":%d,\"ref\":%lu}",
                     cmd, rv, (unsigned long)ref);
    if (n > 0 && (size_t)n < sizeof buf) ws_send_json(buf, (size_t)n, only_fd, -1);
}

/* ====== 参数变更（任一来源；main 的 on_broadcast 钩子调用）====== */

/* 网页 set 的关联信息：只在 httpd 任务里写、并在同一次 param_set() 的同步回调里读。*/
static uint32_t s_web_ref;
static float    s_web_want;
static uint8_t  s_web_seen;
static int      s_web_fd;        /* 发这条 set 的客户端：带 ref 的 pv 只回给它 */

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
    const char *tag = src_name(src, 0);
    int from_web = (src == PARAM_SRC_WEB && s_web_ref);
    if (from_web) {
        /* 请求者：带它自己的 ref（页面靠 ref 认领回执）。
         * 其它页面：ref=0 的副本——两台手机各自的 ref 都从 1 数起，带 ref 群发的话
         * 一方会把另一方的回执当成自己的（独立复核 P2-6）。*/
        s_web_seen = 1;
        /* X02-F-05：回执依据 param_set 的结果——值进了注册表但温控模块没受理（离线 / 忙）时
         * apply_err=2：ok=0、err=2，不再显示成功（改动只在这两处应答上，鉴权 / 连接管理归 e3b）。*/
        push_pv(p->id, p->value, tag, (p->value != s_web_want), s_web_ref,
                param_apply_err(p->id) == 0, param_apply_err(p->id), s_web_fd, -1);
        push_pv(p->id, p->value, tag, 0, 0u, 1, 0, -1, s_web_fd);
    } else {
        push_pv(p->id, p->value, tag, 0, 0u, 1, 0, -1, -1);
    }
    char val[32];
    fmt_param(val, sizeof val, p);
    web_log_addf(now_ms(), param_apply_err(p->id) ? 'w' : 'i', "param", param_apply_err(p->id)
                 ? "%s：%s 改为 %s（已记录，温控模块未受理：离线或忙）" : "%s：%s 改为 %s",
                 src_name(src, 1), p->name ? p->name : p->key, val);
    ws_registry_invalidate();       /* reg 的 def=当前值变了，缓存失配 */
}

/* ====== WS 推送任务 ====== */

/* 日志增量游标：所有在线客户端都收到过 [0, s_log_sent) 的事件。
 * 握手处理函数（httpd 任务）推完全量后会把它抬到当时的总数，推送任务读它决定从哪续。
 * 只增、单个 32 位量：两边各自只读一次/只写一次。*/
static volatile uint32_t s_log_sent;

/* 日志增量：一条最坏情况的 JSON 比 WEB_LOG_ENTRY_MAX（176，按现有文案估的）长得多——
 * 全引号/全控制字符的消息要 6 倍转义。先按估算开，装不下就放大重试；还没发出去就不推进游标。*/
#define WS_LOG_RETRY_MAX 6u
#define WS_OPEN_LOG_MIN_GAP_MS 30000u   /* "网页已连接"两条之间的最小间隔（链路抖动时不刷屏）*/

static void log_push(void)
{
    uint32_t log_sent = s_log_sent, total = web_log_total();
    if (total <= log_sent) return;
    uint32_t cnt = total - log_sent;
    for (uint32_t k = 1; k <= WS_LOG_RETRY_MAX; k++) {
        size_t need = 64u + (size_t)cnt * WEB_LOG_ENTRY_MAX * (size_t)(1u << k);
        char *jb = malloc(need);
        if (!jb) break;
        size_t n = web_log_json(log_sent, total, jb, need);
        int sent = (n && ws_send_json(jb, n, -1, -1)) ? 1 : 0;
        free(jb);
        if (n == 0) continue;         /* 装不下或区间不完整：放大重来 */
        if (sent) s_log_sent = total; /* 没人收/没发出去：游标不动，下一拍再来 */
        return;
    }
}

static void ws_push_task(void *arg)
{
    (void)arg;
    static char buf[768];              /* st 最坏约 450B；任务栈 4096 放不下，改 static */
    web_status_t prev, cur, last_pushed;
    uint8_t have_prev = 0, have_pushed = 0;
    uint32_t last_push_ms = 0;
    esp_task_wdt_add(NULL);            /* R1 P1-07：关键任务纳入 TWDT */
    for (;;) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(WS_TICK_MS));
        gather(&cur);

        /* ① 状态差分 → 事件日志（其它任务写入的条目也在这里一并增量推送）*/
        web_status_diff_log(have_prev ? &prev : NULL, &cur);
        prev = cur;
        have_prev = 1;
        log_push();

        /* ② st：业务字段变了立刻推，否则每 WS_PUSH_MS 一帧 */
        if (!have_pushed || web_status_changed(&last_pushed, &cur) ||
            (uint32_t)(cur.ts_ms - last_push_ms) >= WS_PUSH_MS) {
            size_t n = status_json(buf, sizeof buf, ++s_seq, &cur);
            if (n) ws_send_json(buf, n, -1, -1);
            last_pushed = cur;
            have_pushed = 1;
            last_push_ms = cur.ts_ms;
        }
    }
}

esp_err_t ws_init(httpd_handle_t hd)
{
    s_hd = hd;
    memset(s_fds, 0xFF, sizeof(s_fds));   /* -1 */
    snprintf(s_fw, sizeof s_fw, "%s", esp_app_get_description()->version);
    web_log_reset();
    web_log_addf(now_ms(), 'i', "boot", "主控启动：固件 %s", s_fw);
    s_log_sent = 0;
    if (xTaskCreate(ws_push_task, "tcs_ws", 4096, NULL, 4, NULL) != pdPASS) {
        ESP_LOGE(TAG, "push task not created: pages will get the first push only");
        return ESP_ERR_NO_MEM;             /* P1-12 的另一半（i3）：结果带出去，启动汇总里看得见 */
    }
    return ESP_OK;
}

/* ====== 握手 / 接收 ====== */

/* 在 httpd 处理函数里直接发给刚握手的 fd（官方口径：httpd 上下文可直调）*/
static void send_now(int fd, const char *json, size_t len)
{
    if (!json || !len || !fd_is_ws(fd)) return;
    httpd_ws_frame_t pkt = {
        .final = true, .type = HTTPD_WS_TYPE_TEXT,
        .payload = (uint8_t *)json, .len = len,
    };
    if (httpd_ws_send_frame_async(s_hd, fd, &pkt) != ESP_OK) ws_drop(fd);
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

/* 首推：注册表 → 日志全量 → 一帧状态，最后记"网页已连接"。调用方已经把 fd 登记进表。 */
static void ws_welcome(int fd)
{
    send_big(fd, ws_registry_json, WS_REG_BUF);            /* 注册表（本端权威）*/
    send_big(fd, ws_log_json, WEB_LOG_JSON_CAP);           /* 事件日志全量：[0, 此刻的总数) */
    /* 全量已经覆盖了游标之前积压的事件（没客户端在线时攒下的），把游标抬到此刻的总数——
     * 不抬的话下一拍的增量会把全量里已有的事件再发一遍（2026-11-24 真实浏览器实测：
     * 一条"网页已连接"出现三遍）。在线的老客户端本来就已收到旧游标处，不会漏。
     * 与推送任务最多重叠 100ms 内的一小段增量：宁可重复一条，不可漏掉一条。*/
    s_log_sent = web_log_total();
    {   /* 立刻给一帧状态，页面不必等下一个推送节拍 */
        char buf[768];
        size_t n = ws_status_json(buf, sizeof buf);
        send_now(fd, buf, n);
    }
    if (!fd_is_ws(fd)) return;                             /* 上面某一步发送失败，连接已关 */
    /* "网页已连接"记在全量推完之后，由下一拍的增量带给所有在线页面（各一遍）。
     * 链路抖动时每次重连都会记一条，所以 30s 内连着重连只记第一条；断开不记。*/
    {
        web_log_t last;
        uint32_t now = now_ms();
        if (web_log_last(&last) != 0 || strcmp(last.ev, "ws_open") != 0 ||
            (uint32_t)(now - last.ts_ms) > WS_OPEN_LOG_MIN_GAP_MS)
            web_log_addf(now, 'i', "ws_open", "网页已连接");
    }
}

/* 登记一个新客户端。返回 0=已登记并首推；-1=被拒（连接已关）。 */
static int ws_register(int fd)
{
    if (fds_add(fd) == 0) {
        ESP_LOGI(TAG, "ws client fd=%d", fd);
        ws_welcome(fd);
        return 0;
    }
    /* 表满了（走不到"不是 WebSocket"那一支：本函数只在握手成功之后被调）。回一帧错误再关——
     * 挂着不收推送的连接没有意义，还占着 httpd 的 socket 名额。*/
    static const char busy[] =
        "{\"t\":\"err\",\"code\":\"server_busy\",\"msg\":\"连接数已满，请关掉其它页面再试\"}";
    send_now(fd, busy, sizeof busy - 1);
    ws_drop(fd);
    ESP_LOGW(TAG, "ws open refused fd=%d (table full)", fd);
    return -1;
}

/* 握手完成后的回调（httpd_uri_t.ws_post_handshake_cb）。注意 httpd 握手成功后**不调用**
 * URI 的 handler，只调用这个回调——客户端的登记与首推只能放在这里（见 http_server.c）。*/
esp_err_t ws_on_open(httpd_req_t *req)
{
    int fd = httpd_req_to_sockfd(req);
    if (!fd_is_ws(fd)) {                    /* 理论上到不了：握手成功就会话已是 WS */
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "websocket endpoint: Upgrade required");
        return ESP_FAIL;
    }
    (void)ws_register(fd);
    return ESP_OK;
}

/* 取一个必须是整数、且落在 [lo, hi] 里的字段。非数、带小数、超范围 → 返回 -1。*/
static int json_int_range(const cJSON *item, double lo, double hi, long *out)
{
    if (!cJSON_IsNumber(item)) return -1;
    double d = item->valuedouble;
    if (!(d == (double)(long)d) || d < lo || d > hi) return -1;   /* NaN 也过不了 d==… */
    *out = (long)d;
    return 0;
}

static void handle_set(const cJSON *root, int fd)
{
    const cJSON *id = cJSON_GetObjectItem(root, "id");
    const cJSON *v  = cJSON_GetObjectItem(root, "v");
    const cJSON *r  = cJSON_GetObjectItem(root, "ref");
    long sid = 0, ref = 0;
    /* id 与 ref 必须是整数：原先 (uint16_t)id->valueint 会把 65537 截成 1 去改 tc1_set */
    if (json_int_range(id, 1.0, 65535.0, &sid) != 0 || !cJSON_IsNumber(v)) {
        ws_send_str("{\"t\":\"err\",\"code\":\"bad_set\",\"msg\":\"set 需要整数 id 与数值 v\"}", fd);
        return;
    }
    (void)json_int_range(r, 0.0, 4294967295.0, &ref);      /* ref 可缺省；不合法按 0 */
    float want = (float)v->valuedouble;

    /* 关联信息在 param_set() 的同步回调（ws_param_changed）里消费。
     * 只要值真的变了，回调就会给请求者回一帧带 ref 的 pv（s_web_seen=1）。*/
    s_web_ref = (uint32_t)ref;
    s_web_want = want;
    s_web_seen = 0;
    s_web_fd = fd;
    int prv = param_set((uint16_t)sid, want, PARAM_SRC_WEB);
    s_web_ref = 0;
    s_web_fd = -1;
    if (!s_web_seen) {
        /* 值没有广播出来——可能是"没变"，也可能是被拒绝（只读/越界限幅后等于当前值/
         * 约束/安全校验）。契约要求 set 必有回执，这里补上，并如实说明结果。*/
        int ok = (prv == 0 || prv == 1);
        push_pv((uint16_t)sid, param_get((uint16_t)sid), "web", prv == 1, (uint32_t)ref,
                ok, ok ? 0 : prv, fd, -1);
    }
    ESP_LOGI(TAG, "ws set id=0x%02x v=%.3f rv=%d -> %.3f", (unsigned)sid, (double)want, prv,
             (double)param_get((uint16_t)sid));
}

static void handle_cmd(const cJSON *root, int fd)
{
    const cJSON *c = cJSON_GetObjectItem(root, "c");
    const cJSON *r = cJSON_GetObjectItem(root, "ref");
    long cv = 0, ref = 0;
    /* 网页只放行 1..10。11 是屏侧观测到超温后的停机请求，属于屏链路的内部协议，
     * 不是给使用者点的——原先没有白名单，网页发 c=11 会锁 OVERTEMP 且不留日志。*/
    if (json_int_range(c, 1.0, 10.0, &cv) != 0) {
        ws_send_str("{\"t\":\"err\",\"code\":\"bad_cmd\",\"msg\":\"cmd 需要是 1..10 的整数 c\"}", fd);
        return;
    }
    (void)json_int_range(r, 0.0, 4294967295.0, &ref);
    int rv;
    if (cv == TCS_CMD_M207_SAVE) {
        rv = m207_save_all();                          /* 10：返回下发条数 */
        web_log_addf(now_ms(), rv < 0 ? 'w' : 'i', "m207_save",
                     rv < 0 ? "保存到模块失败（模块离线）" : "高级参数已下发模块保存：%d 条", rv);
    } else {
        rv = control_apply_cmd_src((uint8_t)cv, PARAM_SRC_WEB);   /* 与屏幕 0x15 同一份语义 */
        if (cv == TCS_CMD_ESTOP || cv == TCS_CMD_ALARM_ACK) {
            web_log_addf(now_ms(), 'i', "web_cmd", "%s", cv == TCS_CMD_ESTOP
                         ? "网页：急停" : "网页：确认恢复");
        }
    }
    ESP_LOGI(TAG, "ws cmd=%ld rv=%d -> state=%d alarm=0x%04X",
             cv, rv, (int)control_state(), (unsigned)g_tcs.alarm_bits);
    push_ack((int)cv, rv, (uint32_t)ref, fd);     /* 只回给发起者：别的页面不该收到不属于自己的 ref */
}

/* 探长之后、取载荷之前先看帧类型。返回 1=这一帧到这里就处理完了（PONG），
 * 0=是完整的文本帧、继续取载荷，-1=不支持（二进制/分片/continuation），调用方关连接。*/
static int frame_kind(const httpd_ws_frame_t *pkt)
{
    if (pkt->type == HTTPD_WS_TYPE_TEXT && pkt->final) return 0;
    if (pkt->type == HTTPD_WS_TYPE_PONG) return 1;       /* 我们不发 PING，收到 PONG 丢掉即可 */
    return -1;
}

esp_err_t ws_on_recv(httpd_req_t *req)
{
    httpd_ws_frame_t pkt = { 0 };
    uint8_t tmp[128];
    pkt.payload = tmp;
    int fd = httpd_req_to_sockfd(req);
    if (!fd_is_ws(fd)) {                              /* 不带 Upgrade 头的普通 GET /ws 才会走到这里：回 400 */
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "websocket endpoint: Upgrade required");
        return ESP_FAIL;
    }
    /* 兜底：握手回调没把它登记进表（例如日后 Kconfig 选项被关掉）时，它发来的第一帧
     * 仍会把它登记并首推。命令照常执行，回执也送得出去。*/
    if (!fds_has(fd)) (void)ws_register(fd);
    esp_err_t rv = httpd_ws_recv_frame(req, &pkt, 0);   /* 先探长：pkt.len */
    if (rv != ESP_OK) {
        fds_del(fd);
        return rv;
    }
    /* PING 与 CLOSE 由 httpd 自己应答（URI 没有要 control frames）：PING 回 PONG，
     * CLOSE 回 CLOSE 并在处理函数返回后关连接——那时 fd 已不是 WS 会话，下次发送前会被除名。
     * 这里只会见到文本帧、PONG，以及二进制/分片这类我们不支持的帧。*/
    int kind = frame_kind(&pkt);
    if (kind < 0 || pkt.len >= sizeof(tmp)) {
        /* R1 修复 M14-F-02：超长帧不能只 return——载荷还在 socket 里，
         * 下一轮会把载荷首字节当帧头解析（流错位 + 越界读）。直接关连接最安全。*/
        ws_drop(fd);
        ESP_LOGW(TAG, "ws frame dropped (type=%d final=%d len=%u), session closed",
                 (int)pkt.type, (int)pkt.final, (unsigned)pkt.len);
        return ESP_FAIL;
    }
    if (pkt.len == 0) return ESP_OK;   /* 空帧：不二次调用 recv（否则 IDF 会吞掉下一帧头）*/
    pkt.payload = tmp;
    rv = httpd_ws_recv_frame(req, &pkt, pkt.len);
    if (rv != ESP_OK) return rv;
    if (kind > 0) return ESP_OK;       /* PONG：载荷已取走，无事可做 */

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
        if (n > 0 && (size_t)n < sizeof buf) ws_send_json(buf, (size_t)n, fd, -1);
    }
#if CONFIG_TCS_DIAG
    else if (strcmp(ts, "ota") == 0) {
        /* WP-F.4：m=0 自拷贝+切换；m=1 回滚（boot 指回对槽）。实现在 main.c（组件不能依赖 main）。
         * X03-F-12：已有一次 OTA 在进行时如实回绝；这里原先不看返回值（也没有返回值）。 */
        extern esp_err_t app_ota_trigger(int rollback);
        const cJSON *m = cJSON_GetObjectItem(root, "m");
        /* S 线 i3（条目 10，M14-F-13 的 WS 侧同类问题）：只认精确的整数 0 / 1——原先 m->valueint 把
         * 1.5 截成 1 当回滚，缺字段 / 字符串 "1" / true 则是"不是 1 就当 0"（拷贝 + 重启）。*/
        int rb = web_ota_mode_json(m);
        if (rb < 0) {
            ws_send_str("{\"t\":\"err\",\"code\":\"bad_m\",\"msg\":\"m 只能是 0（拷贝并切换）或 1（回滚）\"}", fd);
        } else {
            ESP_LOGI(TAG, "ws ota trigger rollback=%d", rb);
            if (app_ota_trigger(rb) == ESP_ERR_INVALID_STATE)
                ws_send_str("{\"t\":\"err\",\"code\":\"server_busy\",\"msg\":\"OTA 进行中，请稍后再试\"}", fd);
        }
    }
#endif /* CONFIG_TCS_DIAG: ws ota */
    else {
        ws_send_str("{\"t\":\"err\",\"code\":\"unknown_type\",\"msg\":\"未知消息类型\"}", fd);
    }
    cJSON_Delete(root);
    return ESP_OK;
}
