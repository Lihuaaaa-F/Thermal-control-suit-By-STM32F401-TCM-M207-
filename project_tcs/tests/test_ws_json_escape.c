/* test_ws_json_escape.c — W2 转义向量单测（v13.4 §3.3）
 *
 * 目的：证明 websocket.c W2 化后的 cJSON 构造模式（AddString/AddNumber/AddBool
 * →PrintBuffered→Delete）对敌意字段值转义正确。直接编译 **真实官方 cJSON 源**
 * （IDF v5.5.5 components/json/cJSON，与固件同源），非桩非镜像。
 * 局限声明：本测在 PC 复刻同构的构造序列（websocket.c 依赖 esp 头无法 PC 直编），
 * 转义正确性=cJSON 官方库行为+我们"所有字符串字段必经 AddString"的构造纪律；
 * 固件侧该纪律由门禁保障（websocket.c 内 0 处 snprintf）。
 *
 * 向量（册 §3.3 逐条）：双引号/反斜杠/控制字符/UTF-8 中文/超长数值极值。
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "cJSON.h"

static int fails;
static int checks;  /* X06-F-06：断言数基线门禁的取数口 */

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("    FAIL: %s\n", msg); fails++; } else { checks++; } \
} while (0)

/* 与 websocket.c 同构的 reg item 构造（敌意 name/key/unit 注入点）*/
static char *build_reg_item(const char *key, const char *name, const char *unit)
{
    cJSON *it = cJSON_CreateObject();
    if (!it) return NULL;
    cJSON_AddNumberToObject(it, "id", 1);
    cJSON_AddNumberToObject(it, "type", 2);
    cJSON_AddNumberToObject(it, "flags", 0x55);
    cJSON_AddNumberToObject(it, "min", -10.0);
    cJSON_AddNumberToObject(it, "max", 100.0);
    cJSON_AddNumberToObject(it, "step", 0.5);
    cJSON_AddNumberToObject(it, "def", 37.5);
    cJSON_AddStringToObject(it, "key", key);
    cJSON_AddStringToObject(it, "name", name);
    cJSON_AddStringToObject(it, "unit", unit);
    char *j = cJSON_PrintBuffered(it, 256, 0);
    cJSON_Delete(it);
    return j;
}

static void vec_escape(const char *tag, const char *hostile)
{
    char *j = build_reg_item(hostile, hostile, hostile);
    CHECK(j != NULL, "PrintBuffered 产物为空");
    if (!j) return;
    /* ① 回读解析必须成功 */
    cJSON *back = cJSON_Parse(j);
    CHECK(back != NULL, "回读解析失败");
    if (back) {
        cJSON *f = cJSON_GetObjectItem(back, "name");
        CHECK(f && cJSON_IsString(f), "name 字段缺失/非串");
        CHECK(f && strcmp(f->valuestring, hostile) == 0, "name 回读 != 原值（转义往返失真）");
        cJSON_Delete(back);
    }
    /* ② 产物内不得含未转义控制字符（<0x20 原样出现即坏）*/
    for (const char *p = j; *p; p++)
        /* X06-F-06：原来的 `|| *p == '\0'` 是死项——循环条件就是 *p，进到体内必然非 0。
         * 去掉后断言才是它名字说的那件事：产物里不得出现原始控制字符。*/
        CHECK((unsigned char)*p >= 0x20, "产物含未转义控制字符");
    cJSON_free(j);
    printf("  vec[%s] OK\n", tag);
}

int main(void)
{
    puts("test_ws_json_escape: W2 转义向量（真实 IDF cJSON 源）");

    /* 向量 1-4：敌意字符串字段（册点名四类）*/
    vec_escape("双引号",      "a\"b");
    vec_escape("反斜杠",      "C:\\path\\x");
    vec_escape("控制字符",    "ctl\x01\x1f");
    vec_escape("UTF8中文",    "温度上限-模式二（最长参数名形态）");

    /* 向量 5：极值数值（tc1=-10.0/100.0 边界，浮点经 AddNumber 往返）*/
    {
        cJSON *root = cJSON_CreateObject();
        cJSON_AddNumberToObject(root, "tc1_act", -10.0);
        cJSON_AddNumberToObject(root, "tc2_act", 100.0);
        cJSON_AddNumberToObject(root, "v", 0.001);
        char *j = cJSON_PrintBuffered(root, 128, 0);
        cJSON_Delete(root);
        CHECK(j != NULL, "极值帧打印失败");
        if (j) {
            cJSON *back = cJSON_Parse(j);
            CHECK(back != NULL, "极值帧解析失败");
            if (back) {
                cJSON *a = cJSON_GetObjectItem(back, "tc1_act");
                cJSON *b = cJSON_GetObjectItem(back, "tc2_act");
                CHECK(a && fabs(a->valuedouble + 10.0) < 1e-9, "tc1=-10.0 往返失真");
                CHECK(b && fabs(b->valuedouble - 100.0) < 1e-9, "tc2=100.0 往返失真");
                cJSON_Delete(back);
            }
            cJSON_free(j);
        }
        printf("  vec[极值数值] OK\n");
    }

    /* 向量 6：pv 帧形态（id/v/src/clamped 敌意 src）*/
    vec_escape("pv.src", "web_ack");

    /* 向量 7：中文+引号混合长串（50 参最长名压力形态）*/
    vec_escape("混合长串", "模式\"二\"上限\\温度\"中文\"混\x02合-0123456789");

    printf("test_ws_json_escape: PASS=%d FAIL=%d\n", checks, fails);
    return fails ? 1 : 0;
}
