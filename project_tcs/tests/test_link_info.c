/* test_link_info.c — 主控设备信息（0x22 INFO）在屏侧的保存与"扫码连热点"二维码内容（U 线 W3）
 * 判别性：转义写错、半截更新、放不下还硬写都会红。 */
#include <stdio.h>
#include <string.h>
#include "link_info.h"

static int g_pass, g_fail;
#define CHECK(cond, name) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("FAIL: %s (line %d)\n", name, __LINE__); } \
} while (0)

/* 字符串不等时把两边按十六进制打出来：终端里"看着一样"的字符串可能差一个不可见字节 */
static int same(const char *got, const char *want)
{
    if (strcmp(got, want) == 0) return 1;
    printf("  got : len=%u %s\n", (unsigned)strlen(got), got);
    printf("  want: len=%u %s\n", (unsigned)strlen(want), want);
    printf("  hex got : ");
    for (size_t i = 0; i <= strlen(got); i++) printf("%02x", (unsigned char)got[i]);
    printf("\n  hex want: ");
    for (size_t i = 0; i <= strlen(want); i++) printf("%02x", (unsigned char)want[i]);
    printf("\n");
    return 0;
}
#define STREQ(a, b, name) CHECK(same((a), (b)), name)

int main(void)
{
    const tcs_link_info_t *li = link_info();
    char qr[200];
    size_t n;

    CHECK(li->valid == 0, "boot: no INFO yet");
    n = link_info_wifi_qr(qr, sizeof qr);
    CHECK(n > 0 && n == strlen(qr), "qr: length matches return value");
    STREQ(qr, "http://192.168.4.1", "qr without INFO falls back to default url");

    /* ---- 合法载荷：四个键齐全 ---- */
    const char *pl = "ssid=TCS-9A3C\npass=12345678\nurl=http://192.168.4.1\nfw=1.2.0";
    uint32_t g0 = link_info_gen();
    CHECK(link_info_store(pl, strlen(pl)) == 0, "store valid INFO");
    CHECK(li->valid == 1, "valid=1 after store");
    STREQ(li->ssid, "TCS-9A3C", "ssid");
    STREQ(li->pass, "12345678", "pass");
    STREQ(li->url, "http://192.168.4.1", "url");
    STREQ(li->fw, "1.2.0", "fw");
    CHECK(link_info_gen() == g0 + 1u, "generation +1 when content changes");
    /* 主控每 3.2s 重发一次内容相同的 INFO：不能帧帧换号，否则连接页会每 3.2s 整页重建一次
     * （重编码二维码、重画位图，重建瞬间的"返回"点击还会丢；独立复核 P2-1）。*/
    CHECK(link_info_store(pl, strlen(pl)) == 0 && link_info_gen() == g0 + 1u,
          "same INFO again: generation unchanged");

    n = link_info_wifi_qr(qr, sizeof qr);
    CHECK(n == strlen(qr), "wifi qr: length matches return value");
    STREQ(qr, "WIFI:T:WPA;S:TCS-9A3C;P=12345678;;", "wifi qr: WIFI credentials format");

    /* ---- 热点名/密码含规范里的特殊字符：逐个加反斜杠转义 ---- */
    pl = "ssid=a;b:c,d\"e\\f\npass=p;ss\nurl=\nfw=";
    CHECK(link_info_store(pl, strlen(pl)) == 0, "store INFO with special characters");
    STREQ(li->ssid, "a;b:c,d\"e\\f", "ssid stored verbatim (escaping only when building QR)");
    n = link_info_wifi_qr(qr, sizeof qr);
    CHECK(n == strlen(qr), "escaped qr: length matches return value");
    STREQ(qr, "WIFI:T:WPA;S:a\\;b\\:c\\,d\\\"e\\\\f;P=p\\;ss;;", "escaped qr: backslash ; , : quote");
    CHECK(li->url[0] == '\0' && li->fw[0] == '\0', "url/fw empty when absent");

    /* ---- 放不下：返回 0，不写半截 ---- */
    CHECK(link_info_wifi_qr(qr, 20) == 0, "qr: too small returns 0");
    CHECK(link_info_wifi_qr(qr, 0) == 0 && link_info_wifi_qr(NULL, 100) == 0,
          "qr: null/zero capacity returns 0");

    /* ---- 非法载荷：拒绝且保留旧值（不能把已经显示着的热点名冲掉）---- */
    uint32_t g1 = link_info_gen();
    CHECK(link_info_store("ssid=ONLY", 9) == -1, "INFO without pass rejected");
    STREQ(li->ssid, "a;b:c,d\"e\\f", "rejected frame keeps old value");
    CHECK(link_info_store(NULL, 0) == -1 && link_info_store(pl, 0) == -1, "empty payload rejected");
    {
        char big[200];
        memset(big, 'x', sizeof big);
        CHECK(link_info_store(big, sizeof big) == -1, "payload over TCS_INFO_MAX_LEN rejected");
    }
    {
        char name[64], frame[128];
        memset(name, 's', 40);
        name[40] = '\0';
        snprintf(frame, sizeof frame, "ssid=%s\npass=x", name);
        CHECK(link_info_store(frame, strlen(frame)) == -1, "overlong ssid: whole frame rejected");
        STREQ(li->pass, "p;ss", "pass also unchanged (no partial update)");
    }
    CHECK(link_info_gen() == g1, "rejected frames do not bump generation");

    /* ---- 没有热点名但有网址：二维码退回网址 ---- */
    pl = "ssid=\npass=x\nurl=http://10.0.0.1\nfw=9.9.9";
    CHECK(link_info_store(pl, strlen(pl)) == 0, "empty ssid is still a valid frame");
    n = link_info_wifi_qr(qr, sizeof qr);
    CHECK(n > 0, "qr fallback to url still yields content");
    STREQ(qr, "http://10.0.0.1", "no ssid: QR encodes the url");

    printf("test_link_info: PASS=%d FAIL=%d\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
