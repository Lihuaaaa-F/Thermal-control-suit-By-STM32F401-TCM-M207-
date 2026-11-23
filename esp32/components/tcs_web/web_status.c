/**
 * web_status.c — 见 web_status.h。
 * 纯逻辑：除 cJSON/协议头外不 include 任何 ESP 头文件（临界区宏在主机上退化为空）。
 */
#include "web_status.h"
#include "control.h"       /* SAFETY_BIT_*、CTRL_SAFE（HAL 无关的枚举/宏）*/
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;
#define WEB_LOCK()   taskENTER_CRITICAL(&s_log_mux)
#define WEB_UNLOCK() taskEXIT_CRITICAL(&s_log_mux)
#else
#define WEB_LOCK()   ((void)0)
#define WEB_UNLOCK() ((void)0)
#endif

/* ============ 状态快照 ============ */

/* 浮点 → 两位小数的有理数：18.47f 在 JSON 里就是 18.47，而不是 18.4699993133545 */
static double r2(float v)
{
    if (!isfinite(v)) return 0.0;
    double x = (double)v;
    return (double)(int64_t)(x >= 0.0 ? x * 100.0 + 0.5 : x * 100.0 - 0.5) / 100.0;
}

#define ADDN(field, val) do { \
    if (!cJSON_AddNumberToObject(root, (field), (double)(val))) return -1; \
} while (0)

int web_status_fill_json(const web_status_t *s, cJSON *root)
{
    if (!s || !root) return -1;
    ADDN("ts",        s->ts_ms);
    ADDN("tc1",       r2(s->tc1_act));
    ADDN("tc2",       r2(s->tc2_act));
    ADDN("body",      r2(s->body));
    ADDN("age",       s->age_ms);
    ADDN("tc1_set",   r2(s->tc1_set));
    ADDN("tc2_set",   r2(s->tc2_set));
    ADDN("tec1",      s->tec1 ? 1 : 0);
    ADDN("tec2",      s->tec2 ? 1 : 0);
    ADDN("fan",       s->fan ? 1 : 0);
    ADDN("mode",      s->mode ? 1 : 0);
    ADDN("state",     s->state);
    ADDN("alarm",     s->alarm);
    ADDN("latch",     s->latch);
    ADDN("armed",     s->armed);
    ADDN("m207",      s->m207 ? 1 : 0);
    ADDN("body_ok",   s->body_ok ? 1 : 0);
    ADDN("sens_open", s->sens_open ? 1 : 0);
    ADDN("screen",    s->screen ? 1 : 0);
    ADDN("up_s",      s->up_s);
    ADDN("reset",     s->reset);
    ADDN("heap",      s->heap);
    return 0;
}

int web_status_changed(const web_status_t *a, const web_status_t *b)
{
    if (!a || !b) return 1;
    return (a->tc1_act != b->tc1_act || a->tc2_act != b->tc2_act || a->body != b->body ||
            a->age_ms != b->age_ms ||
            a->tc1_set != b->tc1_set || a->tc2_set != b->tc2_set ||
            a->tec1 != b->tec1 || a->tec2 != b->tec2 || a->fan != b->fan ||
            a->mode != b->mode || a->state != b->state ||
            a->alarm != b->alarm || a->latch != b->latch || a->armed != b->armed ||
            a->m207 != b->m207 || a->body_ok != b->body_ok ||
            a->sens_open != b->sens_open || a->screen != b->screen ||
            a->up_s != b->up_s);
}

/* ============ 事件日志 ============ */

static web_log_t       s_ring[WEB_LOG_N];
static uint32_t        s_total;     /* 已写入总条数；环内下标 = 绝对序号 % WEB_LOG_N */

void web_log_reset(void)
{
    WEB_LOCK();
    memset(s_ring, 0, sizeof(s_ring));
    s_total = 0;
    WEB_UNLOCK();
}

uint32_t web_log_total(void)
{
    WEB_LOCK();
    uint32_t t = s_total;
    WEB_UNLOCK();
    return t;
}

/* 把 src 拷进 cap 字节的目标，最多 cap-1 字节，且不劈开 UTF-8 字符（按起始字节定长）*/
static void utf8_copy(char *dst, size_t cap, const char *src)
{
    size_t n = 0;
    if (!dst || cap == 0) return;
    dst[0] = '\0';
    if (!src) return;
    while (src[n]) {
        unsigned char c = (unsigned char)src[n];
        size_t len = (c < 0x80) ? 1u : ((c & 0xE0) == 0xC0) ? 2u
                   : ((c & 0xF0) == 0xE0) ? 3u : ((c & 0xF8) == 0xF0) ? 4u : 1u;
        if (n + len >= cap) break;              /* 放不下整个字符：停在上一个字符边界 */
        size_t copied = 0;
        while (copied < len && src[n + copied]) { dst[n + copied] = src[n + copied]; copied++; }
        if (copied < len) break;                /* 源串在字符中间结束（残缺尾部）：整字符丢弃 */
        n += copied;
    }
    dst[n] = '\0';
}

void web_log_add(uint32_t ts_ms, char lvl, const char *ev, const char *msg)
{
    web_log_t e;
    memset(&e, 0, sizeof(e));
    e.ts_ms = ts_ms;
    e.lvl = (uint8_t)(lvl ? lvl : 'i');
    utf8_copy(e.ev,  sizeof(e.ev),  ev);
    utf8_copy(e.msg, sizeof(e.msg), msg);
    WEB_LOCK();
    s_ring[s_total % WEB_LOG_N] = e;
    s_total++;
    WEB_UNLOCK();
}

void web_log_addf(uint32_t ts_ms, char lvl, const char *ev, const char *fmt, ...)
{
    char msg[WEB_LOG_MSG_MAX];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    /* vsnprintf 按字节截断，可能留下半个汉字：交给 utf8_copy 收尾到字符边界 */
    char clean[WEB_LOG_MSG_MAX];
    utf8_copy(clean, sizeof(clean), msg);
    web_log_add(ts_ms, lvl, ev, clean);
}

int web_log_get(uint32_t abs_idx, web_log_t *out)
{
    if (!out) return -1;
    int rv = -1;
    WEB_LOCK();
    if (abs_idx < s_total && s_total - abs_idx <= WEB_LOG_N) {
        *out = s_ring[abs_idx % WEB_LOG_N];
        rv = 0;
    }
    WEB_UNLOCK();
    return rv;
}

/* ---- 差分 → 事件 ---- */

/* 0→1 记一条 lvl 级事件，1→0 记一条 info 级"恢复"事件。返回新增条数。*/
static unsigned edge(uint32_t ts, char lvl, const char *ev_on, const char *msg_on,
                     const char *ev_off, const char *msg_off, int was, int now)
{
    if (!was && now) { web_log_add(ts, lvl, ev_on,  msg_on);  return 1; }
    if (was && !now) { web_log_add(ts, 'i', ev_off, msg_off); return 1; }
    return 0;
}

unsigned web_status_diff_log(const web_status_t *prev, const web_status_t *now)
{
    if (!prev || !now || prev->ts_ms == 0) return 0;
    uint32_t ts = now->ts_ms;
    unsigned n = 0;
    /* 按对使用者的重要程度排序：同一拍里急停/超温排最前 */
    n += edge(ts, 'a', "estop_on",     "急停触发：TEC 全部停止，风扇全速",
                   "estop_off",    "急停已解除",
              prev->latch & SAFETY_BIT_ESTOP, now->latch & SAFETY_BIT_ESTOP);
    n += edge(ts, 'a', "overtemp_on",  "水温过高：已停止加热",
                   "overtemp_off", "水温回落，超温已解除",
              prev->alarm & ALM_OVERTEMP, now->alarm & ALM_OVERTEMP);
    n += edge(ts, 'a', "safe_in",      "进入安全态",
                   "safe_out",     "退出安全态",
              prev->state == CTRL_SAFE, now->state == CTRL_SAFE);
    n += edge(ts, 'w', "body_lost",    "体温传感器失联",
                   "body_ok",      "体温传感器恢复",
              !prev->body_ok, !now->body_ok);
    n += edge(ts, 'w', "m207_offline", "温控模块通信中断",
                   "m207_online",  "温控模块回线",
              !prev->m207, !now->m207);
    n += edge(ts, 'w', "sens_open",    "水温探头断线",
                   "sens_closed",  "水温探头恢复",
              prev->sens_open, now->sens_open);
    n += edge(ts, 'w', "screen_down",  "屏幕链路断开（主控继续独立运行）",
                   "screen_up",    "屏幕已连接",
              !prev->screen, !now->screen);     /* 1→0 记"断开"：入参取反 */
    if (prev->mode != now->mode) {
        web_log_add(ts, 'i', "mode", now->mode ? "切换到自动模式" : "切换到手动模式");
        n++;
    }
    n += edge(ts, 'i', "tec1_on", "贴身水箱 TEC 开", "tec1_off", "贴身水箱 TEC 关",
              prev->tec1, now->tec1);
    n += edge(ts, 'i', "tec2_on", "环境水箱 TEC 开", "tec2_off", "环境水箱 TEC 关",
              prev->tec2, now->tec2);
    n += edge(ts, 'i', "fan_on",  "风扇 开",          "fan_off",  "风扇 关",
              prev->fan, now->fan);
    return n;
}

/* ---- 日志 → JSON ---- */

static const char *lvl_name(uint8_t lvl)
{
    return (lvl == 'a') ? "alarm" : (lvl == 'w') ? "warn" : "info";
}

size_t web_log_json(uint32_t from, uint32_t to, char *buf, size_t cap)
{
    if (!buf || cap == 0 || to <= from) return 0;
    uint32_t total;
    WEB_LOCK();
    total = s_total;
    WEB_UNLOCK();
    if (to > total) to = total;
    if (from + WEB_LOG_N < total) from = total - WEB_LOG_N;   /* 早于环里最旧的：抬到最旧 */
    if (to <= from) return 0;

    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_CreateArray();
    if (!root || !arr) { cJSON_Delete(root); cJSON_Delete(arr); return 0; }
    size_t out = 0;
    do {
        if (!cJSON_AddStringToObject(root, "t", "log") ||
            !cJSON_AddItemToObject(root, "list", arr)) break;
        for (uint32_t k = from; k < to; k++) {
            web_log_t e;
            if (web_log_get(k, &e) != 0) break;     /* 极小概率：构建期间被新事件挤出环 */
            cJSON *it = cJSON_CreateObject();
            if (!it) { out = 0; break; }
            cJSON_AddNumberToObject(it, "ts", e.ts_ms);
            cJSON_AddStringToObject(it, "lvl", lvl_name(e.lvl));
            cJSON_AddStringToObject(it, "ev", e.ev);
            cJSON_AddStringToObject(it, "msg", e.msg);
            cJSON_AddItemToArray(arr, it);
        }
        char *j = cJSON_PrintBuffered(root, 64u + (size_t)(to - from) * WEB_LOG_ENTRY_MAX, 0);
        if (!j) break;
        size_t len = strlen(j);
        if (len < cap) { memcpy(buf, j, len); out = len; }
        cJSON_free(j);
    } while (0);
    cJSON_Delete(root);
    return out;
}
