/* test_web_status.c — 网页控制台状态/事件日志的契约测试（WS_PROTOCOL.md v2）
 * 用**真实 cJSON**（与固件同库）编译 web_status.c：固件发出来的 st / log 消息
 * 必须与前端、tools/web_mock.py 期望的字段名、类型、取值逐项一致。
 * 判别性：删掉 web_status_fill_json 里任何一个字段、改错事件码或级别、
 * 把环回绕的边界写错，这里都会红（见 tests/mutation_web.py 的变异清单）。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "web_status.h"
#include "control.h"       /* SAFETY_BIT_*、ctrl_state_t（事件用例）*/

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* 取数值字段并断言其类型与取值（容差 1e-9）。字段缺失 / 非数值 / 取值不符都算失败。*/
static int num_is(cJSON *o, const char *key, double want)
{
    cJSON *it = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(it) && fabs(it->valuedouble - want) < 1e-9;
}

static cJSON *parse(const char *s)
{
    cJSON *o = cJSON_Parse(s);
    if (!o) printf("FAIL: JSON 解析失败: %.120s\n", s);
    return o;
}

/* 检查一段字节串是合法且完整的 UTF-8（没有半个字符）。返回 0=合法。*/
static int utf8_valid(const char *s)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned char c = *p;
        size_t len = (c < 0x80) ? 1u : ((c & 0xE0) == 0xC0) ? 2u
                   : ((c & 0xF0) == 0xE0) ? 3u : ((c & 0xF8) == 0xF0) ? 4u : 0u;
        if (len == 0) return -1;
        for (size_t k = 1; k < len; k++)
            if ((p[k] & 0xC0) != 0x80) return -1;      /* 续字节缺失 = 被劈开 */
        p += len;
    }
    return 0;
}

static web_status_t demo_status(uint32_t ts)
{
    web_status_t s;
    memset(&s, 0, sizeof(s));
    s.ts_ms = ts;
    s.tc1_act = 18.47f; s.tc2_act = 21.51f; s.body = 37.40f;
    s.age_ms = 1000;
    s.tc1_set = 5.0f;   s.tc2_set = 26.0f;
    s.tec1 = 0; s.tec2 = 0; s.fan = 0; s.mode = 0;
    s.state = 0; s.alarm = 0; s.latch = 0; s.armed = 1;
    s.m207 = 1; s.body_ok = 1; s.sens_open = 0; s.screen = 1;
    s.up_s = 14815; s.reset = 11; s.heap = 8519084u;
    return s;
}

static void set_ring(void)
{
    web_log_reset();
    web_log_add(1000, 'i', "boot", "开机");
    web_log_add(2000, 'w', "m207_offline", "温控模块通信中断");
    web_log_add(3000, 'a', "estop_on", "急停触发：TEC 全部停止，风扇全速");
}

int main(void)
{
    char buf[16384];

    /* ====== A. st 消息：22 个业务字段，名称/类型/取值逐项对契约 ====== */
    {
        web_status_t s = demo_status(14815234u);
        cJSON *root = cJSON_CreateObject();
        CHECK(web_status_fill_json(&s, root) == 0, "A fill_json ok");
        char *j = cJSON_PrintUnformatted(root);
        cJSON *o = parse(j);
        CHECK(o != NULL, "A st JSON 可解析");
        if (o) {
            CHECK(cJSON_GetArraySize(o) == 22, "A st 共 22 个业务字段");
            CHECK(num_is(o, "ts", 14815234), "A ts");
            CHECK(num_is(o, "tc1", 18.47), "A tc1 两位小数");
            CHECK(num_is(o, "tc2", 21.51), "A tc2");
            CHECK(num_is(o, "body", 37.40), "A body");
            CHECK(num_is(o, "age", 1000), "A age");
            CHECK(num_is(o, "tc1_set", 5), "A tc1_set");
            CHECK(num_is(o, "tc2_set", 26), "A tc2_set");
            CHECK(num_is(o, "tec1", 0), "A tec1");
            CHECK(num_is(o, "tec2", 0), "A tec2");
            CHECK(num_is(o, "fan", 0), "A fan");
            CHECK(num_is(o, "mode", 0), "A mode");
            CHECK(num_is(o, "state", 0), "A state");
            CHECK(num_is(o, "alarm", 0), "A alarm");
            CHECK(num_is(o, "latch", 0), "A latch");
            CHECK(num_is(o, "armed", 1), "A armed");
            CHECK(num_is(o, "m207", 1), "A m207");
            CHECK(num_is(o, "body_ok", 1), "A body_ok");
            CHECK(num_is(o, "sens_open", 0), "A sens_open");
            CHECK(num_is(o, "screen", 1), "A screen");
            CHECK(num_is(o, "up_s", 14815), "A up_s");
            CHECK(num_is(o, "reset", 11), "A reset");
            CHECK(num_is(o, "heap", 8519084), "A heap");
            CHECK(strstr(j, "18.469") == NULL, "A 不出现浮点尾数");
        }
        cJSON_Delete(o);
        cJSON_free(j);
        cJSON_Delete(root);

        /* 归一化与非法值：标志位只出 0/1；NaN/Inf 写 0；负数四舍五入正确 */
        web_status_t t = demo_status(1);
        t.tec1 = 7; t.fan = 255; t.mode = 3; t.screen = 9;
        t.body = NAN; t.tc2_act = INFINITY; t.tc1_act = -0.014f; t.m207 = 0;
        root = cJSON_CreateObject();
        CHECK(web_status_fill_json(&t, root) == 0, "A fill_json 非法值不崩");
        j = cJSON_PrintUnformatted(root);
        o = parse(j);
        if (o) {
            CHECK(num_is(o, "tec1", 1) && num_is(o, "fan", 1) && num_is(o, "mode", 1) &&
                  num_is(o, "screen", 1) && num_is(o, "m207", 0), "A 标志位归一成 0/1");
            CHECK(num_is(o, "body", 0) && num_is(o, "tc2", 0), "A NaN/Inf 写 0");
            CHECK(num_is(o, "tc1", -0.01), "A 负数四舍五入到 -0.01");
        }
        cJSON_Delete(o);
        cJSON_free(j);
        cJSON_Delete(root);
        CHECK(web_status_fill_json(NULL, NULL) == -1, "A 空指针返回 -1");
    }

    /* ====== B. 变化检测：时间戳/堆的起伏不算变化，业务字段任一不同算变化 ====== */
    {
        web_status_t a = demo_status(1000), b = a;
        CHECK(web_status_changed(&a, &b) == 0, "B 相同快照无变化");
        b.ts_ms = 99999; b.heap = 1;
        CHECK(web_status_changed(&a, &b) == 0, "B ts/heap 变化不算");
        struct { const char *name; size_t off; } f[] = {
            { "tc1_act", offsetof(web_status_t, tc1_act) }, { "tc2_act", offsetof(web_status_t, tc2_act) },
            { "body", offsetof(web_status_t, body) },       { "age_ms", offsetof(web_status_t, age_ms) },
            { "tc1_set", offsetof(web_status_t, tc1_set) }, { "tec1", offsetof(web_status_t, tec1) },
            { "tec2", offsetof(web_status_t, tec2) },       { "fan", offsetof(web_status_t, fan) },
            { "mode", offsetof(web_status_t, mode) },       { "state", offsetof(web_status_t, state) },
            { "alarm", offsetof(web_status_t, alarm) },     { "latch", offsetof(web_status_t, latch) },
            { "armed", offsetof(web_status_t, armed) },     { "m207", offsetof(web_status_t, m207) },
            { "body_ok", offsetof(web_status_t, body_ok) }, { "sens_open", offsetof(web_status_t, sens_open) },
            { "screen", offsetof(web_status_t, screen) },   { "up_s", offsetof(web_status_t, up_s) },
        };
        for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
            b = a;
            ((unsigned char *)&b)[f[i].off] ^= 1;
            CHECK(web_status_changed(&a, &b) == 1, f[i].name);   /* 失败时打印字段名 */
        }
        CHECK(web_status_changed(NULL, &a) == 1 && web_status_changed(&a, NULL) == 1,
              "B 空指针按有变化处理");
    }

    /* ====== C. 差分事件：事件码、级别、顺序 ====== */
    {
        web_log_reset();
        web_status_t a = demo_status(0);
        web_status_t b = a;
        b.ts_ms = 1000;
        CHECK(web_status_diff_log(&a, &b) == 0, "C 首份只建基线");
        CHECK(web_log_total() == 0, "C 首份不产生事件");

        /* 故障沿：急停(+安全态) → 体温失联 → 模块离线 → 探头断线 → 屏幕断 → TEC/风扇/模式 */
        web_status_t n = b;
        n = b; n.ts_ms = 1100; n.latch |= SAFETY_BIT_ESTOP; n.state = (uint8_t)CTRL_SAFE;
        n.tec1 = 1; n.fan = 1; n.mode = 1; n.tec2 = 1;
        CHECK(web_status_diff_log(&b, &n) == 6, "C 一拍内 6 个事件");
        b = n;
        n.ts_ms = 1200; n.body_ok = 0;
        CHECK(web_status_diff_log(&b, &n) == 1, "C 体温失联 1 个事件");
        b = n;
        n.ts_ms = 1300; n.m207 = 0; n.sens_open = 1; n.screen = 0;
        CHECK(web_status_diff_log(&b, &n) == 3, "C 模块/探头/屏幕 3 个事件");
        b = n;
        /* 恢复沿 */
        n.ts_ms = 1400; n.latch = 0; n.state = (uint8_t)CTRL_IDLE; n.body_ok = 1; n.m207 = 1;
        n.sens_open = 0; n.screen = 1; n.tec1 = 0; n.tec2 = 0; n.fan = 0; n.mode = 0;
        CHECK(web_status_diff_log(&b, &n) == 10, "C 恢复沿 10 个事件");

        static const char *want_ev[] = {
            "estop_on", "safe_in", "mode", "tec1_on", "tec2_on", "fan_on",
            "body_lost",
            "m207_offline", "sens_open", "screen_down",
            "estop_off", "safe_out", "body_ok", "m207_online", "sens_closed", "screen_up",
            "mode", "tec1_off", "tec2_off", "fan_off",
        };
        static const char want_lvl[] = {
            'a', 'a', 'i', 'i', 'i', 'i',
            'w',
            'w', 'w', 'w',
            'i', 'i', 'i', 'i', 'i', 'i',
            'i', 'i', 'i', 'i',
        };
        const unsigned N = sizeof(want_ev) / sizeof(want_ev[0]);
        CHECK(web_log_total() == N, "C 日志总条数");
        for (unsigned i = 0; i < N && i < web_log_total(); i++) {
            web_log_t e;
            CHECK(web_log_get(i, &e) == 0, "C web_log_get");
            if (strcmp(e.ev, want_ev[i]) != 0) {
                g_fail++; printf("FAIL: C 事件码 #%d 得到 %s 期望 %s\n", i, e.ev, want_ev[i]);
            } else g_pass++;
            if (e.lvl != want_lvl[i]) {
                g_fail++; printf("FAIL: C 事件级别 #%d %s 得到 %c 期望 %c\n", i, e.ev, e.lvl, want_lvl[i]);
            } else g_pass++;
            if (e.ts_ms == 0) { g_fail++; printf("FAIL: C 事件时间戳 #%d 为 0\n", i); } else g_pass++;
        }
        web_log_t e;
        CHECK(web_log_get(0, &e) == 0 && strcmp(e.msg, "急停触发：TEC 全部停止，风扇全速") == 0,
              "C 急停文案");
        CHECK(web_log_get(6, &e) == 0 && strcmp(e.msg, "体温传感器失联") == 0, "C 体温失联文案");
    }

    /* ====== D. 环回绕：只保留最新 50 条，绝对序号取值，越界返回 -1 ====== */
    {
        web_log_reset();
        for (uint32_t i = 0; i < 60; i++) {
            char m[32];
            snprintf(m, sizeof(m), "m%u", i);
            web_log_add(i, 'i', "seq", m);
        }
        web_log_t e;
        CHECK(web_log_total() == 60, "D total=60");
        CHECK(web_log_get(9, &e) == -1, "D 序号 9 已被覆盖");
        CHECK(web_log_get(10, &e) == 0 && strcmp(e.msg, "m10") == 0, "D 最旧一条是序号 10");
        CHECK(web_log_get(59, &e) == 0 && strcmp(e.msg, "m59") == 0, "D 最新一条是序号 59");
        CHECK(web_log_get(60, &e) == -1, "D 尚未写入的序号取不到");
        size_t n = web_log_json(0, 60, buf, sizeof(buf));
        CHECK(n > 0, "D 全量 JSON 生成");
        cJSON *o = parse(buf);
        if (o) {
            cJSON *list = cJSON_GetObjectItem(o, "list");
            CHECK(cJSON_IsArray(list) && cJSON_GetArraySize(list) == 50, "D 全量 = 环容量 50 条");
            cJSON *first = cJSON_GetArrayItem(list, 0);
            CHECK(strcmp(cJSON_GetObjectItem(first, "msg")->valuestring, "m10") == 0,
                  "D from 自动抬到最旧");
        }
        cJSON_Delete(o);
    }

    /* ====== E. UTF-8 截断：不劈汉字、串合法、长度受控 ====== */
    {
        web_log_reset();
        /* 40 个汉字 = 120 字节 > WEB_LOG_MSG_MAX-1 */
        char longmsg[128];
        memset(longmsg, 0, sizeof(longmsg));
        for (int i = 0; i < 40; i++) memcpy(longmsg + i * 3, "温", 3);
        web_log_add(1, 'i', "evt", longmsg);
        web_log_t e;
        CHECK(web_log_get(0, &e) == 0, "E 长消息已写入");
        size_t len = strlen(e.msg);
        CHECK(len <= WEB_LOG_MSG_MAX - 1, "E 长度不超上限");
        CHECK(len % 3 == 0 && utf8_valid(e.msg) == 0, "E 汉字没被劈开");
        CHECK(len == WEB_LOG_MSG_MAX - 3, "E 恰好留下 23 个整汉字");   /* 71 字节容量 → 69 字节 */

        /* addf：printf 按字节截断后留下的半个汉字要被清掉。
         * "急停触发 " = 4 个汉字 + 1 个 ASCII 空格 = 13 字节；其后 71 字节容量里
         * 只放得下 19 个整汉字（57 字节）→ 结果 70 字节 */
        web_log_reset();
        web_log_addf(2, 'a', "estop_on", "急停触发 %s", longmsg);
        CHECK(web_log_get(0, &e) == 0 && utf8_valid(e.msg) == 0 && strlen(e.msg) == 70,
              "E addf 也落在字符边界");

        /* 事件码超长 */
        web_log_reset();
        web_log_add(3, 'w', "this_event_code_is_way_too_long_for_the_field", "x");
        CHECK(web_log_get(0, &e) == 0 && strlen(e.ev) == WEB_LOG_EV_MAX - 1, "E 事件码截断到上限");
        CHECK(utf8_valid(e.ev) == 0, "E 事件码合法");

        /* 空指针与残缺尾部不崩 */
        web_log_reset();
        web_log_add(4, 'i', NULL, NULL);
        CHECK(web_log_get(0, &e) == 0 && e.ev[0] == '\0' && e.msg[0] == '\0', "E 空指针写空串");
        web_log_add(5, 'i', "k", "\xe6\xb0");      /* "温" 缺最后一个字节 */
        CHECK(web_log_get(1, &e) == 0 && e.msg[0] == '\0', "E 残缺尾部被丢弃");
    }

    /* ====== F. log 消息 JSON ====== */
    {
        set_ring();                                    /* 3 条：info / warn / alarm */
        size_t n = web_log_json(0, 3, buf, sizeof(buf));
        CHECK(n > 0 && n < sizeof(buf), "F log JSON 生成");
        cJSON *o = parse(buf);
        if (o) {
            CHECK(strcmp(cJSON_GetObjectItem(o, "t")->valuestring, "log") == 0, "F t=log");
            cJSON *list = cJSON_GetObjectItem(o, "list");
            CHECK(cJSON_IsArray(list) && cJSON_GetArraySize(list) == 3, "F list 3 条，旧→新");
            static const char *want_lvl[] = { "info", "warn", "alarm" };
            static const double want_ts[] = { 1000, 2000, 3000 };
            for (int i = 0; i < 3; i++) {
                cJSON *it = cJSON_GetArrayItem(list, i);
                if (strcmp(cJSON_GetObjectItem(it, "lvl")->valuestring, want_lvl[i]) != 0 ||
                    cJSON_GetObjectItem(it, "ts")->valuedouble != want_ts[i]) {
                    g_fail++; printf("FAIL: F 第 %d 条级别/时间戳不对\n", i);
                } else g_pass++;
                CHECK(cJSON_GetObjectItem(it, "ev") && cJSON_GetObjectItem(it, "msg"),
                      "F 每条都有 ev 与 msg");
            }
            CHECK(strcmp(cJSON_GetObjectItem(cJSON_GetArrayItem(list, 2), "ev")->valuestring,
                         "estop_on") == 0, "F 最新一条在最末");
        }
        cJSON_Delete(o);

        /* 增量区间 */
        n = web_log_json(2, 3, buf, sizeof(buf));
        o = parse(buf);
        CHECK(o && cJSON_GetArraySize(cJSON_GetObjectItem(o, "list")) == 1, "F 增量只含第 3 条");
        cJSON_Delete(o);

        /* 空区间 / 容量不足 → 0 */
        CHECK(web_log_json(3, 3, buf, sizeof(buf)) == 0, "F 空区间返回 0");
        CHECK(web_log_json(5, 9, buf, sizeof(buf)) == 0, "F 超出已写条数返回 0");
        CHECK(web_log_json(0, 3, buf, 16) == 0, "F 容量不足返回 0");
        CHECK(web_log_json(0, 3, NULL, 100) == 0, "F 空指针返回 0");
    }

    printf("test_web_status: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
