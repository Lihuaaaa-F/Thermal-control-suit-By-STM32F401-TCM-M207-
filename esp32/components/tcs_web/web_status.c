/**
 * web_status.c — 见 web_status.h。
 * 纯逻辑：除 cJSON/协议头外不 include 任何 ESP 头文件（临界区宏在主机上退化为空）。
 */
#include "web_status.h"
#include "control.h"       /* SAFETY_BIT_*、CTRL_SAFE（HAL 无关的枚举/宏）*/
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
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
    /* 水温/体温按发出去的两位小数比：原始 float 的末位抖动不算变化。
     * age_ms 与 up_s 不参与：它们本来就在走，算进去等于每拍都"变了"。*/
    return (r2(a->tc1_act) != r2(b->tc1_act) || r2(a->tc2_act) != r2(b->tc2_act) ||
            r2(a->body) != r2(b->body) ||
            a->tc1_set != b->tc1_set || a->tc2_set != b->tc2_set ||
            a->tec1 != b->tec1 || a->tec2 != b->tec2 || a->fan != b->fan ||
            a->mode != b->mode || a->state != b->state ||
            a->alarm != b->alarm || a->latch != b->latch || a->armed != b->armed ||
            a->m207 != b->m207 || a->body_ok != b->body_ok ||
            a->sens_open != b->sens_open || a->screen != b->screen);
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

int web_log_last(web_log_t *out)
{
    WEB_LOCK();
    uint32_t total = s_total;
    WEB_UNLOCK();
    return total ? web_log_get(total - 1u, out) : -1;
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
            /* 极小概率：构建期间被新事件挤出环。区间不完整就不能当成功发出去——
             * 调用方据此不推进游标，下一拍重来（独立复核 P2-4）。*/
            if (web_log_get(k, &e) != 0) break;
            cJSON *it = cJSON_CreateObject();
            /* 任何一步分配失败都整体作废：缺了字段的半条、少了几条的半包都不能发 */
            if (!it || !cJSON_AddNumberToObject(it, "ts", e.ts_ms) ||
                !cJSON_AddStringToObject(it, "lvl", lvl_name(e.lvl)) ||
                !cJSON_AddStringToObject(it, "ev", e.ev) ||
                !cJSON_AddStringToObject(it, "msg", e.msg) ||
                !cJSON_AddItemToArray(arr, it)) {
                cJSON_Delete(it);
                break;
            }
        }
        if (cJSON_GetArraySize(arr) != (int)(to - from)) break;   /* 上面任何一处 break 都走到这 */
        char *j = cJSON_PrintBuffered(root, 64u + (size_t)(to - from) * WEB_LOG_ENTRY_MAX, 0);
        if (!j) break;
        size_t len = strlen(j);
        if (len < cap) { memcpy(buf, j, len + 1); out = len; }   /* 连结尾 NUL 一起拷 */
        cJSON_free(j);
    } while (0);
    cJSON_Delete(root);
    return out;
}

/* ---- 屏链路统计 + 热点凭据 → JSON ---- */

const char *const web_link_keys[WEB_LINK_N] = {
    "rx_bytes", "rx_frames", "crc_err", "len_err", "gap_resets", "dup",
    "retry", "txq_drop", "uart_txdrop", "echo_drop", "pending_evict", "proto_mismatch",
};

size_t web_link_json(const uint32_t cnt[WEB_LINK_N], const char *ssid, const char *pass,
                     const char *url, char *buf, size_t cap)
{
    if (!buf || cap == 0 || !cnt || !ssid || !pass || !url) return 0;
    cJSON *root = cJSON_CreateObject();
    if (!root) return 0;
    size_t out = 0;
    do {
        if (!cJSON_AddStringToObject(root, "t", "link")) break;
        size_t k;
        for (k = 0; k < WEB_LINK_N; k++)
            if (!cJSON_AddNumberToObject(root, web_link_keys[k], (double)cnt[k])) break;
        if (k != WEB_LINK_N) break;
        if (!cJSON_AddStringToObject(root, "ssid", ssid) ||
            !cJSON_AddStringToObject(root, "pass", pass) ||
            !cJSON_AddStringToObject(root, "url", url)) break;
        char *j = cJSON_PrintBuffered(root, (int)cap, 0);
        if (!j) break;
        size_t len = strlen(j);
        if (len < cap) { memcpy(buf, j, len + 1); out = len; }   /* 装不下（含结尾 NUL）就回 0，不回半包 */
        cJSON_free(j);
    } while (0);
    cJSON_Delete(root);
    return out;
}

/* ---- 参数表 → reg 消息 ---- */

double web_num_short(float v)
{
    /* 依次试 1..9 位有效数字，取第一个能还原成同一个 float 的写法：
     *   0.1f（作为 double 是 0.100000001490116…）→ "0.1"；25.0f → "25"；1234567.0f → "1234567"。
     * 固定位数不行：%.6g 会把 1234567 写成 1.23457e+06，%g 不限位又会带出 float32 的尾数。*/
    if (!isfinite((double)v)) return 0.0;          /* 与 st 的口径一致：非有限数一律 0 */
    char b[32];
    for (int p = 1; p <= 9; p++) {
        snprintf(b, sizeof b, "%.*g", p, (double)v);
        if ((float)strtod(b, NULL) == v) break;
    }
    return strtod(b, NULL);
}

int web_registry_fill_json(const param_desc_t *const *ps, int n, cJSON *root)
{
    if (!ps || n < 0 || !root) return -1;
    cJSON *arr = cJSON_CreateArray();
    if (!arr) return -1;
    /* 挂到 root 上之后 arr 归 root 所有（调用方删 root 时一起释放）；没挂上去才由这里释放 */
    if (cJSON_AddStringToObject(root, "t", "reg") == NULL ||
        cJSON_AddItemToObject(root, "list", arr) == 0) {
        cJSON_Delete(arr);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        const param_desc_t *p = ps[i];
        if (!p) continue;                           /* 调用方给了空位：跳过，不出空对象 */
        cJSON *it = cJSON_CreateObject();
        if (!it) return -1;
        int ok = cJSON_AddNumberToObject(it, "id",    p->id) != NULL &&
                 cJSON_AddNumberToObject(it, "type",  p->type) != NULL &&
                 cJSON_AddNumberToObject(it, "flags", p->flags) != NULL &&
                 cJSON_AddNumberToObject(it, "min",   web_num_short(p->min)) != NULL &&
                 cJSON_AddNumberToObject(it, "max",   web_num_short(p->max)) != NULL &&
                 cJSON_AddNumberToObject(it, "step",  web_num_short(p->step)) != NULL &&
                 cJSON_AddNumberToObject(it, "def",   web_num_short(p->value)) != NULL &&   /* 当前值 */
                 cJSON_AddStringToObject(it, "key",  p->key  ? p->key  : "") != NULL &&
                 cJSON_AddStringToObject(it, "name", p->name ? p->name : "") != NULL &&
                 cJSON_AddStringToObject(it, "unit", p->unit ? p->unit : "") != NULL;
        if (ok) ok = (cJSON_AddItemToArray(arr, it) != 0);   /* 进了 arr 之后 it 归 arr 所有 */
        if (!ok) { cJSON_Delete(it); return -1; }            /* 没进 arr：由这里释放 */
    }
    return 0;
}

size_t web_registry_json(const param_desc_t *const *ps, int n, char *buf, size_t cap)
{
    if (!buf || cap == 0) return 0;
    cJSON *root = cJSON_CreateObject();
    if (!root) return 0;
    size_t out = 0;
    if (web_registry_fill_json(ps, n, root) == 0) {
        char *j = cJSON_PrintBuffered(root, (int)(cap < 16384u ? cap : 16384u), 0);
        if (j) {
            size_t len = strlen(j);
            if (len < cap) { memcpy(buf, j, len + 1); out = len; }
            cJSON_free(j);
        }
    }
    cJSON_Delete(root);
    return out;
}

/* ====== /api/ota 的 m 取值（M14-F-13）====== */
int web_ota_mode_value(const char *v)
{
    if (!v) return -1;
    if (strcmp(v, "0") == 0) return 0;
    if (strcmp(v, "1") == 0) return 1;
    return -1;
}

/* S 线 i3（条目 10；M14-F-13 在 WS 侧的同类问题）：浏览器发 {"t":"ota","m":1.5} 时，cJSON 的 valueint
 * 把 1.5 截成 1 → 当成回滚执行；m 缺失 / 字符串 "1" / true / false 也都落到"不是 1 就当 0"（拷贝 + 重启）。
 * 这里与 HTTP 侧同一口径：只认精确的整数 0（拷贝 + 切换）/ 1（回滚），其余一律 -1——
 * 调用方回 {"t":"err","code":"bad_m",…}，两种模式都不触发。 */
int web_ota_mode_json(const cJSON *m)
{
    double d;
    if (!m || !cJSON_IsNumber(m)) return -1;   /* 缺字段 / 字符串 / true / null / 数组 / 对象 */
    d = m->valuedouble;
    if (d == 0.0) return 0;
    if (d == 1.0) return 1;
    return -1;                                  /* 1.5 / 2 / -1 / 0.001 … */
}

/* r7f（独立复核 R7-07，2026-11-26）：OTA / 回滚触发前要求输出已停（两路 TEC 的命令意图都是关）。
 * 拷贝与回滚都会重启设备：重启期间模块保持断电前的状态，重启后自动模式按体温自己恢复——
 * 加热途中升级等于把 TEC 留在一个没人看的闭环里。两道入口（/api/ota、WS 的 ota 帧）都先问这一句，
 * 不满足就如实回 outputs_on、不触发、不重启；也不"先自动急停"（那会在升级后留一个要人工确认的急停）。*/
int web_ota_outputs_stopped(int tec1_cmd_on, int tec2_cmd_on)
{
    return (!tec1_cmd_on && !tec2_cmd_on) ? 1 : 0;
}
