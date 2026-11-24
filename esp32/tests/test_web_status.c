/* test_web_status.c — 网页控制台状态/事件日志/link 消息的契约测试（WS_PROTOCOL.md v2）
 * 用**真实 cJSON**（与固件同库）编译 web_status.c：固件发出来的 st / log / link 消息
 * 必须与前端、tools/web_mock.py 期望的字段名、类型、取值逐项一致。
 * 判别性（tests/mutation_web.py 逐条验证）：删掉 web_status_fill_json 里任何一个字段、
 * 改错事件码或级别、把环回绕的边界写错、两位小数舍入改截断、link 消息的热点名不转义，
 * 这里都会红。
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "web_status.h"
#include "control.h"       /* SAFETY_BIT_*、ctrl_state_t（事件用例）*/

static int g_pass, g_fail;
/* 失败行立刻落盘：后面某个断言把进程带崩时，前面的失败清单也不丢（输出重定向时 stdout 是全缓冲的）*/
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); fflush(stdout); } \
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
    if (!o) { printf("FAIL: JSON 解析失败: %.120s\n", s); fflush(stdout); }
    return o;
}

/* 分配失败注入（cJSON 钩子）：第 g_alloc_fail_at 次分配返回 NULL（1 起数；0 = 不注入）。
 * 用来证明"任何一次分配失败都不会发出残缺的 JSON"。*/
static int g_alloc_fail_at, g_alloc_n;
static void *hook_malloc(size_t sz)
{
    if (g_alloc_fail_at && ++g_alloc_n == g_alloc_fail_at) return NULL;
    return malloc(sz);
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
            { "tc1_set", offsetof(web_status_t, tc1_set) },
            { "tec1", offsetof(web_status_t, tec1) },
            { "tec2", offsetof(web_status_t, tec2) },       { "fan", offsetof(web_status_t, fan) },
            { "mode", offsetof(web_status_t, mode) },       { "state", offsetof(web_status_t, state) },
            { "alarm", offsetof(web_status_t, alarm) },     { "latch", offsetof(web_status_t, latch) },
            { "armed", offsetof(web_status_t, armed) },     { "m207", offsetof(web_status_t, m207) },
            { "body_ok", offsetof(web_status_t, body_ok) }, { "sens_open", offsetof(web_status_t, sens_open) },
            { "screen", offsetof(web_status_t, screen) },
        };
        for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
            b = a;
            ((unsigned char *)&b)[f[i].off] ^= 1;
            CHECK(web_status_changed(&a, &b) == 1, f[i].name);   /* 失败时打印字段名 */
        }
        /* 水温/体温：按发出去的两位小数比，变了 0.01 才算 */
        b = a; b.tc1_act = a.tc1_act + 0.01f; CHECK(web_status_changed(&a, &b) == 1, "tc1_act 变了 0.01");
        b = a; b.tc2_act = a.tc2_act - 0.01f; CHECK(web_status_changed(&a, &b) == 1, "tc2_act 变了 0.01");
        b = a; b.body    = a.body    + 0.01f; CHECK(web_status_changed(&a, &b) == 1, "body 变了 0.01");
        b = a; b.tc2_set = a.tc2_set + 0.1f;  CHECK(web_status_changed(&a, &b) == 1, "tc2_set");
        /* 不算变化的：age 每 50ms 量化一跳、运行时长每秒走一格——算进去等于每拍都"变了"，
         * 500ms 心跳永远走不到（独立复核 P2-2：真机上 st 实际约 10Hz）；以及水温 float 末位抖动 */
        b = a; b.age_ms = (uint16_t)(a.age_ms ^ 1u); b.up_s = a.up_s ^ 1u;
        CHECK(web_status_changed(&a, &b) == 0, "B age/运行时长起伏不算");
        b = a; b.tc1_act = a.tc1_act + 0.0001f; b.tc2_act = a.tc2_act + 0.0001f; b.body = a.body + 0.0001f;
        CHECK(web_status_changed(&a, &b) == 0, "B 水温 float 末位抖动不算（发出去都是两位小数）");
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
        /* 首份即使带着故障（例如上电时急停锁存还在）也只建基线：不能当成"刚刚触发"记一条 */
        web_status_t f0 = demo_status(0);
        web_status_t f1 = demo_status(1000);
        f1.latch |= SAFETY_BIT_ESTOP; f1.state = (uint8_t)CTRL_SAFE; f1.fan = 1;
        CHECK(web_status_diff_log(&f0, &f1) == 0 && web_log_total() == 0,
              "C 首份带着故障也只建基线");

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
        /* from 落在"已被覆盖"的范围内（最旧一条的前一个）：同样抬到最旧，不能回空包 */
        size_t n9 = web_log_json(9, 60, buf, sizeof(buf));
        cJSON *o9 = parse(buf);
        CHECK(n9 > 0 && o9 && cJSON_GetArraySize(cJSON_GetObjectItem(o9, "list")) == 50,
              "D from=9（早于最旧一条）也抬到最旧");
        cJSON_Delete(o9);
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
        web_log_add(6, 0, "lvl0", "x");                  /* 级别传 0：按 info */
        CHECK(web_log_get(2, &e) == 0 && e.lvl == 'i', "E 级别传 0 时按 info");
    }

    /* ====== F. log 消息 JSON ====== */
    {
        set_ring();                                    /* 3 条：info / warn / alarm */
        memset(buf, 'X', 512);                          /* 整段 JSON 都盖到：NUL 检查才不靠栈上残留 */
        size_t n = web_log_json(0, 3, buf, sizeof(buf));
        CHECK(n > 0 && n < sizeof(buf), "F log JSON 生成");
        CHECK(buf[n] == '\0', "F 结尾带 NUL");
        cJSON *o = parse(buf);
        if (o) {
            cJSON *tj = cJSON_GetObjectItem(o, "t");
            CHECK(cJSON_IsString(tj) && strcmp(tj->valuestring, "log") == 0, "F t=log");
            cJSON *list = cJSON_GetObjectItem(o, "list");
            CHECK(cJSON_IsArray(list) && cJSON_GetArraySize(list) == 3, "F list 3 条，旧→新");
            static const char *want_lvl[] = { "info", "warn", "alarm" };
            static const double want_ts[] = { 1000, 2000, 3000 };
            for (int i = 0; i < 3; i++) {
                cJSON *it = cJSON_GetArrayItem(list, i);       /* 条数不对时为 NULL：下面逐层判空 */
                cJSON *lv = it ? cJSON_GetObjectItem(it, "lvl") : NULL;
                cJSON *ts = it ? cJSON_GetObjectItem(it, "ts") : NULL;
                int ok = cJSON_IsString(lv) && cJSON_IsNumber(ts) &&
                         strcmp(lv->valuestring, want_lvl[i]) == 0 && ts->valuedouble == want_ts[i];
                CHECK(ok, "F 每条的级别/时间戳");
                CHECK(it && cJSON_GetObjectItem(it, "ev") && cJSON_GetObjectItem(it, "msg"),
                      "F 每条都有 ev 与 msg");
            }
            cJSON *last = cJSON_GetArrayItem(list, 2);
            CHECK(last && cJSON_GetObjectItem(last, "ev") &&
                  strcmp(cJSON_GetObjectItem(last, "ev")->valuestring, "estop_on") == 0,
                  "F 最新一条在最末");
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

    /* ====== G. link 消息（GET /api/linkstats）：12 个计数 + 热点名/密码/网址 ======
     * 热点名/密码是使用者可改的自由文本：含引号/反斜杠/控制字符/汉字时也必须是合法 JSON，
     * 且原样还原（手拼 "%s" 遇到第一个引号就废）。*/
    {
        uint32_t cnt[WEB_LINK_N];
        for (uint32_t i = 0; i < WEB_LINK_N; i++) cnt[i] = 1000u + i;
        cnt[0] = 3000000000u;                      /* 超过 INT_MAX：仍要是精确整数 */
        size_t n = web_link_json(cnt, "TCS-A8A1", "12345678", "http://192.168.4.1",
                                 buf, sizeof(buf));
        CHECK(n > 0, "G link JSON 生成");
        cJSON *o = parse(buf);
        if (o) {
            CHECK(strcmp(cJSON_GetObjectItem(o, "t")->valuestring, "link") == 0, "G t=link");
            CHECK(cJSON_GetArraySize(o) == (int)WEB_LINK_N + 4, "G 12 个计数 + t/ssid/pass/url");
            for (uint32_t i = 0; i < WEB_LINK_N; i++)
                CHECK(num_is(o, web_link_keys[i], (double)cnt[i]), web_link_keys[i]);
            CHECK(num_is(o, "rx_bytes", 3000000000.0), "G 超过 INT_MAX 的计数仍精确");
            cJSON *s = cJSON_GetObjectItem(o, "ssid");
            cJSON *p = cJSON_GetObjectItem(o, "pass");
            cJSON *u = cJSON_GetObjectItem(o, "url");
            CHECK(cJSON_IsString(s) && strcmp(s->valuestring, "TCS-A8A1") == 0, "G ssid");
            CHECK(cJSON_IsString(p) && strcmp(p->valuestring, "12345678") == 0, "G pass");
            CHECK(cJSON_IsString(u) && strcmp(u->valuestring, "http://192.168.4.1") == 0, "G url");
        } else {
            g_fail++; printf("FAIL: G link JSON 解析失败\n");
        }
        cJSON_Delete(o);

        const char *ssid = "调温服\"A\"\\B";
        const char *pass = "p\"a\\b\x01\x1f\x7f\n\t";
        n = web_link_json(cnt, ssid, pass, "http://192.168.4.1", buf, sizeof(buf));
        CHECK(n > 0, "G 含特殊字符也能生成");
        o = parse(buf);
        if (o) {
            cJSON *s = cJSON_GetObjectItem(o, "ssid");
            cJSON *p = cJSON_GetObjectItem(o, "pass");
            CHECK(cJSON_IsString(s) && strcmp(s->valuestring, ssid) == 0,
                  "G 引号/反斜杠/汉字原样还原");
            CHECK(cJSON_IsString(p) && strcmp(p->valuestring, pass) == 0,
                  "G 控制字符原样还原");
            CHECK(cJSON_GetArraySize(o) == (int)WEB_LINK_N + 4,
                  "G 特殊字符没有多出/少掉字段（不能借引号注入）");
        } else {
            g_fail++; printf("FAIL: G 特殊字符的 link JSON 解析失败: %.160s\n", buf);
        }
        cJSON_Delete(o);

        /* 容量边界：cap 要放下"内容 + 结尾 NUL"——恰好多 1 → 成功；正好等于内容长度 → 0 */
        n = web_link_json(cnt, "TCS-A8A1", "12345678", "http://192.168.4.1", buf, sizeof(buf));
        CHECK(n > 0 && web_link_json(cnt, "TCS-A8A1", "12345678", "http://192.168.4.1",
                                     buf, n + 1) == n, "G 恰好放得下内容+NUL → 成功");
        CHECK(n > 0 && web_link_json(cnt, "TCS-A8A1", "12345678", "http://192.168.4.1",
                                     buf, n) == 0, "G 只放得下内容、放不下 NUL → 0");
        /* 写出来的是结尾带 NUL 的 C 串（测试与日志都能直接当字符串用）*/
        memset(buf, 'X', sizeof buf);
        n = web_link_json(cnt, "TCS-A8A1", "12345678", "http://192.168.4.1", buf, sizeof(buf));
        CHECK(n > 0 && buf[n] == '\0', "G 结尾带 NUL");
        CHECK(n > 0 && buf[n + 1] == 'X', "G 没有多写一个字节");

        CHECK(web_link_json(cnt, "a", "b", "c", NULL, 100) == 0, "G 空缓冲返回 0");
        CHECK(web_link_json(NULL, "a", "b", "c", buf, sizeof(buf)) == 0, "G 计数空指针返回 0");
        CHECK(web_link_json(cnt, NULL, "b", "c", buf, sizeof(buf)) == 0, "G ssid 空指针返回 0");
    }

    /* ====== H. reg 消息：参数表的 float 必须按 6 位有效数字发出去 ======
     * 参数表里是 float：step=0.1f 作为 double 打印是 0.10000000149011612，网页按它数小数位，
     * 参数页就会变成 17 位小数（独立复核 P0：真机上 51 个参数里 19 个中招，含两个水箱设定）。*/
    {
        CHECK(web_num_short(0.1f) == 0.1, "H 0.1f 归一成 0.1");
        CHECK(web_num_short(0.2f) == 0.2 && web_num_short(0.01f) == 0.01 && web_num_short(0.05f) == 0.05, "H 常见步进");
        CHECK(web_num_short(25.0f) == 25.0 && web_num_short(123456.0f) == 123456.0 && web_num_short(1234567.0f) == 1234567.0,
              "H 整数原样");
        CHECK(web_num_short(37.4f) == 37.4 && web_num_short(-0.3f) == -0.3, "H 负数与一位小数");
        CHECK(web_num_short(NAN) == 0.0 && web_num_short(INFINITY) == 0.0, "H 非有限数归 0（与 st 同口径）");

        param_desc_t p1 = { .id = 0x0001, .key = "tc1_set", .name = "贴身水箱设定温度", .unit = "°C",
                            .type = 0, .flags = 0x1F, .min = 5.0f, .max = 45.0f, .step = 0.1f, .value = 25.0f };
        param_desc_t p2 = { .id = 0x0004, .key = "body_cold", .name = "体温过冷阈值", .unit = "°C",
                            .type = 0, .flags = 0x1F, .min = 35.0f, .max = 39.0f, .step = 0.1f, .value = 36.5f };
        param_desc_t p3 = { .id = 0x0103, .key = NULL, .name = NULL, .unit = NULL,
                            .type = 0, .flags = 0x07, .min = 0.0f, .max = 200.0f, .step = 0.5f, .value = 0.5f };
        const param_desc_t *ps[] = { &p1, &p2, NULL, &p3, &p1 };   /* 含一个空位 */
        size_t n = web_registry_json(ps, 5, buf, sizeof(buf));
        CHECK(n > 0 && buf[n] == '\0', "H reg JSON 生成且带 NUL");
        CHECK(strstr(buf, "0.100000001") == NULL && strstr(buf, "e-") == NULL && strstr(buf, "e+") == NULL,
              "H 不出现 float32 尾数");
        CHECK(strstr(buf, "\"step\":0.1,") != NULL && strstr(buf, "\"step\":0.5,") != NULL, "H 步进是短小数");
        cJSON *o = parse(buf);
        if (o) {
            cJSON *list = cJSON_GetObjectItem(o, "list");
            CHECK(strcmp(cJSON_GetObjectItem(o, "t")->valuestring, "reg") == 0, "H t=reg");
            CHECK(cJSON_IsArray(list) && cJSON_GetArraySize(list) == 4, "H 5 个位置里 1 个空位被跳过 → 4 条");
            cJSON *it = cJSON_GetArrayItem(list, 0);
            CHECK(it && num_is(it, "id", 1) && num_is(it, "min", 5) && num_is(it, "max", 45) &&
                  num_is(it, "step", 0.1) && num_is(it, "def", 25), "H 第 1 条各字段");
            CHECK(it && cJSON_GetArraySize(it) == 10, "H 每条 10 个字段");
            CHECK(strcmp(cJSON_GetObjectItem(it, "key")->valuestring, "tc1_set") == 0, "H key");
            CHECK(strcmp(cJSON_GetObjectItem(it, "name")->valuestring, "贴身水箱设定温度") == 0, "H name（UTF-8 原样）");
            cJSON *it3 = cJSON_GetArrayItem(list, 2);
            CHECK(it3 && num_is(it3, "step", 0.5) && strcmp(cJSON_GetObjectItem(it3, "key")->valuestring, "") == 0,
                  "H 字符串字段为空的参数写成空串");
            CHECK(cJSON_GetArrayItem(list, 3) && cJSON_GetArrayItem(list, 3) != it &&
                  cJSON_GetArraySize(cJSON_GetArrayItem(list, 3)) == 10, "H 同一个描述符出现两次各出一条");
        } else {
            g_fail++; printf("FAIL: H reg JSON 解析失败\n");
        }
        cJSON_Delete(o);
        CHECK(web_registry_json(ps, 5, buf, 16) == 0, "H 容量不够返回 0");
        CHECK(web_registry_json(NULL, 0, buf, sizeof(buf)) == 0, "H 空指针返回 0");
        n = web_registry_json(ps, 0, buf, sizeof(buf));
        o = parse(buf);
        CHECK(o && cJSON_IsArray(cJSON_GetObjectItem(o, "list")) &&
              cJSON_GetArraySize(cJSON_GetObjectItem(o, "list")) == 0, "H 0 条参数 = 空列表");
        cJSON_Delete(o);
    }

    /* ====== I. 分配失败时不能发出半包 ======
     * 独立复核 P2-4：第 7 次分配失败时 web_log_json 返回 21 字节的空列表、第 8~16 次失败
     * 返回缺字段的条目——调用方会把它们当成功发出去并推进游标，这些事件就永久丢了。*/
    {
        set_ring();
        int partial = 0, succeeded = 0;
        cJSON_Hooks hooks = { hook_malloc, free };
        cJSON_InitHooks(&hooks);
        for (int k = 1; k <= 60; k++) {              /* 让第 k 次分配失败（1 起数）*/
            g_alloc_fail_at = k;
            g_alloc_n = 0;
            memset(buf, 0, 64);
            size_t n = web_log_json(0, 3, buf, sizeof(buf));
            g_alloc_fail_at = 0;                     /* 测试自己的 parse 不受注入影响 */
            if (n == 0) continue;                    /* 整体失败：正是期望的行为 */
            succeeded++;
            cJSON *o = cJSON_Parse(buf);
            cJSON *list = o ? cJSON_GetObjectItem(o, "list") : NULL;
            cJSON *last = list ? cJSON_GetArrayItem(list, 2) : NULL;
            int whole = list && cJSON_GetArraySize(list) == 3 &&      /* 三条都在 */
                        last && cJSON_GetObjectItem(last, "msg") &&   /* 末条字段齐 */
                        cJSON_GetObjectItem(last, "ev") && cJSON_GetObjectItem(last, "ts");
            cJSON_Delete(o);
            if (!whole) {
                partial++;
                printf("FAIL: I 第 %d 次分配失败时发出了残缺的包: %.160s\n", k, buf);
            }
        }
        cJSON_InitHooks(NULL);                       /* 恢复默认分配器 */
        CHECK(partial == 0, "I 任何一次分配失败都不产生残缺的包");
        CHECK(succeeded > 0, "I 注入点之前的分配不影响结果（有成功的轮次）");
        size_t n = web_log_json(0, 3, buf, sizeof(buf));
        cJSON *o = parse(buf);
        CHECK(o && cJSON_GetArraySize(cJSON_GetObjectItem(o, "list")) == 3, "I 恢复分配器后正常");
        cJSON_Delete(o);
    }

    printf("test_web_status: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
