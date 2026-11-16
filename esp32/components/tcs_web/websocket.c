/**
 * websocket.c — /ws 客户端管理 + 500ms 状态推送 + set/cmd 下行（任务书 §4.2/4.3）
 *
 * W2（v13.4 §3）：上行 JSON 统一 cJSON（F6 双实现漂移消除；PrintBuffered 堆策略+
 * reg 代缓存）；发送经 httpd_queue_work 官方线程路径（W10#18 上下文违约修复）。
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
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "cJSON.h"
#include <math.h>
#include "link_task.h"
#include "param_registry.h"
#include "control.h"
#include "m207_service.h"

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

/* W2/W10#18：httpd_ws_send_frame_async 官方口径=仅 httpd 上下文直调
 * （esp_http_server.h 头注 "rarely called directly, exception … httpd_queue_work"）。
 * 推送任务（tcs_ws）与 link 事件（tcs_link 任务）均非 httpd 线程——经
 * httpd_queue_work 官方线程安全路径投递到 httpd 线程执行发送。*/
typedef struct {
    size_t len;
    char json[576];          /* st 最坏 239B（红队 R4 实测：float→double 长尾）/pv≈90B；
                              * 超 576 即丢该帧——50 参形态 reg 不走此通道（走 16384 路径）*/
} ws_tx_work_t;

static void ws_tx_work_fn(void *arg)
{
    ws_tx_work_t *w = arg;
    httpd_ws_frame_t pkt = {
        .final = true, .fragmented = false,
        .type = HTTPD_WS_TYPE_TEXT, .payload = (uint8_t *)w->json, .len = w->len,
    };
    /* 快照后发送：临界区只做拷贝（旧版循环内 EXIT/ENTER/continue 会打穿
     * 自旋锁配对——空槽触发 assert，演示模式 500ms 推送首次踩中）*/
    int fds[WS_MAX_CLIENTS];
    int n = 0;
    taskENTER_CRITICAL(&s_mux);
    for (int i = 0; i < WS_MAX_CLIENTS; i++)
        if (s_fds[i] > 0) fds[n++] = s_fds[i];
    taskEXIT_CRITICAL(&s_mux);
    for (int i = 0; i < n; i++)
        if (httpd_ws_send_frame_async(s_hd, fds[i], &pkt) != ESP_OK)
            fds_del(fds[i]);          /* 死连接除名（重连握手重新入册）*/
    free(w);
}

static void fds_send(const char *json, size_t len)
{
    if (!s_hd || !len || len > sizeof(((ws_tx_work_t *)0)->json)) return;
    /* P0（2026-10-30 真机归因）：无客户端时不投递——空闲 500ms 推送不再每拍
     * malloc+queue_work（新构建曾因此 3.6s 触发 lwip panic，旧构建无此路径）*/
    {
        bool any = false;
        taskENTER_CRITICAL(&s_mux);
        for (int i = 0; i < WS_MAX_CLIENTS; i++)
            if (s_fds[i] > 0) { any = true; break; }
        taskEXIT_CRITICAL(&s_mux);
        if (!any) return;
    }
    ws_tx_work_t *w = malloc(sizeof *w);   /* 定长小块，500ms 周期有界（§3.2 堆策略）*/
    if (!w) return;
    memcpy(w->json, json, len);
    w->len = len;
    if (httpd_queue_work(s_hd, ws_tx_work_fn, w) != ESP_OK) free(w);
}

/* ====== JSON 构造（W2：上行统一 cJSON，消除手拼/库双实现漂移 F6）======
 * 堆策略（§3.2）：PrintBuffered 预检缓冲（st 256/pv 128/reg 8192，按实测帧宽），
 * 树用毕必 Delete、串用毕必 cJSON_free——500ms 周期路径无无界分配；
 * 各调用栈自建自毁（树/串不跨线程共享），唯一跨线程共享=reg 缓存（带锁，见下）。
 * 数字格式注记：cJSON 输出 37 而非旧 %.1f 的 "37.0"——JSON 数值语义等价，
 * 前端 Number()/Chart.js 解析无行为差异（等价性 diff 语义口径，非逐字节）。*/
size_t ws_status_json(char *buf, size_t cap)
{
    link_status_t snap;                 /* M11-F-12：整块拷贝，避免锁外逐字段读撕裂 */
    link_status_copy(&snap);
    const link_status_t *s = &snap;
    size_t r = 0;
    cJSON *root = cJSON_CreateObject();
    if (!root) return 0;
    do {
        if (!cJSON_AddStringToObject(root, "t", "st")) break;
        if (!s->valid) {
            if (!cJSON_AddBoolToObject(root, "valid", false)) break;
        } else {
            bool sensor_ok = (s->online & TCS_ONLINE_BODY_SENSOR) != 0;
            if (!cJSON_AddBoolToObject(root, "valid", true) ||
                !cJSON_AddNumberToObject(root, "ver", s->ver) ||
                !cJSON_AddNumberToObject(root, "seq", s->seq) ||
                !cJSON_AddNumberToObject(root, "tc1_act", s->tc1_act) ||
                !cJSON_AddNumberToObject(root, "tc2_act", s->tc2_act) ||
                !cJSON_AddNumberToObject(root, "body", s->body) ||
                !cJSON_AddBoolToObject(root, "body_sensor_ok", sensor_ok) ||
                !cJSON_AddNumberToObject(root, "tec1", s->tec1) ||
                !cJSON_AddNumberToObject(root, "tec2", s->tec2) ||
                !cJSON_AddNumberToObject(root, "fan", s->fan) ||
                !cJSON_AddNumberToObject(root, "mode", s->mode) ||
                !cJSON_AddNumberToObject(root, "alarm", s->alarm) ||
                !cJSON_AddNumberToObject(root, "online", s->online) ||
                !cJSON_AddNumberToObject(root, "boot_s",
                                         (double)(esp_timer_get_time() / 1000000LL)))
                break;
        }
        char *j = cJSON_PrintBuffered(root, 256, 0);
        if (!j) break;
        size_t len = strlen(j);
        if (len < cap) { memcpy(buf, j, len); r = len; }
        cJSON_free(j);
    } while (0);
    cJSON_Delete(root);
    return r;
}

/* reg 帧缓存（§3.2 深水区一：50 参≈6-8KB 大对象，官方"immutable 缓存"模式）：
 * def=当前值随任何 link 事件可能变化 → on_link_event 递增代数；
 * 代未变直接复用缓存串，变了才重建（重建在锁外，换入在锁内）。*/
static char *s_reg_cache;
static size_t s_reg_cache_len;
static volatile uint32_t s_reg_gen;          /* 事件代（on_link_event 递增） */
static uint32_t s_reg_built_gen;             /* 缓存构建时的事件代 */
static portMUX_TYPE s_reg_mux = portMUX_INITIALIZER_UNLOCKED;

size_t ws_registry_json(char *buf, size_t cap)
{
    /* 缓存命中：代未变 → 锁内拷贝即走 */
    taskENTER_CRITICAL(&s_reg_mux);
    const char *hit = (s_reg_cache && s_reg_built_gen == s_reg_gen) ? s_reg_cache : NULL;
    size_t hit_len = s_reg_cache_len;
    if (hit && hit_len < cap) memcpy(buf, hit, hit_len);
    taskEXIT_CRITICAL(&s_reg_mux);
    if (hit) return hit_len < cap ? hit_len : 0;

    /* R3-A-3：64×~104B≈6.6KB，原来开在 httpd 任务栈（8KB）上。本函数单实例，
     * 与 s_push_buf 同策略移 static；并发保护见 s_reg_mux（构建期同一时刻只有一个）。*/
    static link_reg_t regs[LINK_REGISTRY_MAX];
    uint32_t g0 = s_reg_gen;      /* 数据快照前代（换入时盖 g0 非 current：构建窗口
                                     * 内到达的事件须使缓存失配——盖 current 会把陈
                                     * 帧虚标成新鲜，红队 W2-R2 反例坐实）*/
    int n = link_get_registry(regs, LINK_REGISTRY_MAX);
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) { cJSON_Delete(root); cJSON_Delete(arr); return 0; }
    /* root 两 Add 失败=残树不得入缓存；arr 未挂入 root 时须显式回收（W2-R3）*/
    if (!cJSON_AddStringToObject(root, "t", "reg") ||
        !cJSON_AddItemToObject(root, "list", arr)) {
        cJSON_Delete(root);
        cJSON_Delete(arr);       /* AddItem 失败时 arr 不为 root 所有，双删安全 */
        return 0;
    }
    for (int i = 0; i < n; i++) {
        const tcs_reg_entry_t *e = &regs[i].e;
        float def = link_param_value(e->id);
        if (isnan(def)) def = e->min;   /* 无缓存（未收到 0x11）→ 滑条置 min */
        cJSON *it = cJSON_CreateObject();
        if (!it) break;
        cJSON_AddNumberToObject(it, "id", e->id);
        cJSON_AddNumberToObject(it, "type", e->type);
        cJSON_AddNumberToObject(it, "flags", e->flags);
        cJSON_AddNumberToObject(it, "min", e->min);
        cJSON_AddNumberToObject(it, "max", e->max);
        cJSON_AddNumberToObject(it, "step", e->step);
        cJSON_AddNumberToObject(it, "def", def);
        cJSON_AddStringToObject(it, "key", e->key);
        cJSON_AddStringToObject(it, "name", e->name);
        cJSON_AddStringToObject(it, "unit", e->unit);
        cJSON_AddItemToArray(arr, it);
    }
    char *j = cJSON_PrintBuffered(root, 16384, 0);   /* R1 P1-01：64 槽注册表 JSON 约 10KB */
    cJSON_Delete(root);
    if (!j) return 0;
    size_t len = strlen(j);
    /* 换入缓存：旧串延后释放（读侧持锁拷贝，不会拿到悬垂指针）；
     * built_gen 盖 g0（数据快照代）——见函数头 g0 注释 */
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
}

static void push_pv(uint16_t id, float v, const char *src, int clamped)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return;
    char *j = NULL;
    do {
        if (!cJSON_AddStringToObject(root, "t", "pv")) break;
        if (!cJSON_AddNumberToObject(root, "id", id)) break;
        if (!cJSON_AddNumberToObject(root, "v", v)) break;
        if (!cJSON_AddStringToObject(root, "src", src)) break;
        if (clamped && !cJSON_AddBoolToObject(root, "clamped", true)) break;
        j = cJSON_PrintBuffered(root, 128, 0);
    } while (0);
    cJSON_Delete(root);
    if (!j) return;
    fds_send(j, strlen(j));
    cJSON_free(j);
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

    /* reg 缓存代数递增：def=当前值随本事件已变，缓存即失效（§3.2）*/
    taskENTER_CRITICAL(&s_reg_mux);
    s_reg_gen++;
    taskEXIT_CRITICAL(&s_reg_mux);

    const char *src = "h743";
    if (s_expect_id && p->id == s_expect_id
        && esp_timer_get_time() - s_expect_us < 3000000LL
        && p->us >= s_expect_us) {
        src = "web_ack";
        s_expect_id = 0;
    }
    push_pv(p->id, p->v, src, 0);   /* R3-E-1：clamp 由 set 的 param_set 返回值直接回执 */
}

/* 500ms 推送任务
 * P0-07：原为 `char buf[4096]` 开在 4096B 任务栈上（反汇编入口帧 4128B，每 500ms 写穿栈块）。
 * 该任务单实例，缓冲移为 static；容量按 fds_send 上限 576B 取（状态 JSON 实测 233B）。
 * 栈保持 4096 不动——靠编译期 -Wstack-usage 防同类回归，而不是把栈调大掩盖问题。 */
static char s_push_buf[576];

static void ws_push_task(void *arg)
{
    esp_task_wdt_add(NULL);   /* R1 P1-07：关键任务纳入 TWDT（原来只有 ctrl_svc）*/
    while (1) {
        esp_task_wdt_reset();
        vTaskDelay(pdMS_TO_TICKS(WS_PUSH_MS));
        size_t n = ws_status_json(s_push_buf, sizeof(s_push_buf));
        if (n) fds_send(s_push_buf, n);
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
    char *buf = malloc(16384);   /* 与 ws_registry_json 的 cap 同宽（64 槽）*/
    if (buf) {
        size_t n = ws_registry_json(buf, 16384);
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
    if (pkt.len >= sizeof(tmp)) {
        /* R1 修复 M14-F-02：超长帧不能只 return——载荷还在 socket 里，
         * 下一轮会把载荷首字节当帧头解析（流错位 + 越界读）。
         * 直接关掉这条连接最安全（写通道不做流式丢弃）。*/
        int fd = httpd_req_to_sockfd(req);
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
    if (!root) return ESP_OK;
    cJSON *t = cJSON_GetObjectItem(root, "t");
    if (t && cJSON_IsString(t)) {
        if (strcmp(t->valuestring, "set") == 0) {
            cJSON *id = cJSON_GetObjectItem(root, "id");
            cJSON *v  = cJSON_GetObjectItem(root, "v");
            if (cJSON_IsNumber(id) && cJSON_IsNumber(v)) {
                s_expect_id = (uint16_t)id->valueint;
                s_expect_us = esp_timer_get_time();
                /* v2 权威在 ESP32：网页写必须落本端注册表（→M207 verify），
                 * 再由 0x20/0x21 回传屏幕。此前误走 link_set_param（反向 0x12），
                 * 会让网页值只到屏幕、不改 M207，两侧长期分叉。*/
                int prv = param_set((uint16_t)id->valueint,
                                    (float)v->valuedouble, PARAM_SRC_WEB);
                ESP_LOGI(TAG, "ws set id=%u v=%.2f rv=%d -> %.2f",
                         (unsigned)id->valueint, v->valuedouble, prv,
                         (double)param_get((uint16_t)id->valueint));
                /* R3-E-1：clamp/失败回执以本次 param_set 返回值为准（权威在本端）。
                 * 原来靠 0x90 ACK + link_set_param 的 id 关联，而 link_set_param 已无
                 * 调用者 → 链路恒为 0，clamped 判定永不成立（死链）。*/
                if (prv == 1) {                 /* 被限幅：回显实际值 + clamped */
                    s_expect_id = 0;            /* 本帧已由这里回显，不再等事件回显 */
                    push_pv((uint16_t)id->valueint,
                            param_get((uint16_t)id->valueint), "web_ack", 1);
                } else if (prv < 0) {           /* 只读/非法值/NaN */
                    char e[64];
                    snprintf(e, sizeof e, "{\"t\":\"perr\",\"id\":%u,\"rv\":%d}",
                             (unsigned)id->valueint, prv);
                    fds_send(e, strlen(e));
                }
            }
        } else if (strcmp(t->valuestring, "cmd") == 0) {
            cJSON *c = cJSON_GetObjectItem(root, "c");
            if (cJSON_IsNumber(c)) {
                /* v2 权威在 ESP32：急停/确认在本端直接执行（TEC/风扇在这里）。
                 * 原来转发 0x15 给屏幕只会在 H743 的本地控制里空转。*/
                int cv = c->valueint;
                int rv = 0;
                /* R1 P1-03：补齐网页 cmd 1–7/10 的执行端（原来只实现 8/9，其余静默丢弃、
                 * 前端却弹"成功"）。1-4=TEC1/2 开/关，5=风扇切换，6/7=自动/手动，
                 * 10=保存到模块（逐参数 `!`）。安全门控仍在 out_tec_sw（锁存期禁开）。*/
                switch (cv) {
                case 1: control_manual_tec(1, 1); break;
                case 2: control_manual_tec(1, 0); break;
                case 3: control_manual_tec(2, 1); break;
                case 4: control_manual_tec(2, 0); break;
                case 5: control_manual_fan(g_tcs.fan_on ? 0 : 1); break;
                case 6: rv = param_set(PARAM_ID_MODE, 1.0f, PARAM_SRC_WEB); break;
                case 7: rv = param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_WEB); break;
                case 8: tcs_emergency_stop(); break;
                case 9: tcs_alarm_ack(); break;
                case 10: rv = m207_save_all(); break;
                default: rv = -1; break;
                }
                ESP_LOGI(TAG, "ws cmd=%d rv=%d -> state=%d alarm=0x%04X",
                         cv, rv, (int)control_state(), (unsigned)g_tcs.alarm_bits);
                /* 回执：前端据此显示成功/失败（rv<0=拒绝/未实现）*/
                {
                    char ack[64];
                    int n = snprintf(ack, sizeof(ack),
                                     "{\"t\":\"ack\",\"c\":%d,\"rv\":%d}", cv, rv);
                    if (n > 0 && (size_t)n < sizeof(ack)) fds_send(ack, (size_t)n);
                }
            }
        }
#if CONFIG_TCS_DIAG
        else if (strcmp(t->valuestring, "ota") == 0) {
            /* WP-F.4：m=0 自拷贝+切换；m=1 回滚（boot 指回对槽）*/
            extern void app_ota_trigger(int rollback);
            cJSON *m = cJSON_GetObjectItem(root, "m");
            int rb = (cJSON_IsNumber(m) && m->valueint == 1) ? 1 : 0;
            ESP_LOGI(TAG, "ws ota trigger rollback=%d", rb);
            app_ota_trigger(rb);
        }
#endif /* CONFIG_TCS_DIAG: ws ota */
    }
    cJSON_Delete(root);
    return ESP_OK;
}
