/**
 * link_info.c — 见 link_info.h。纯逻辑：只依赖 protocol/tcs_proto.h 的取值函数。
 */
#include "link_info.h"
#include "tcs_proto.h"
#include <stdio.h>
#include <string.h>

static tcs_link_info_t s_info;
static uint32_t        s_gen;

const tcs_link_info_t *link_info(void) { return &s_info; }
uint32_t link_info_gen(void) { return s_gen; }

/* WIFI 二维码里这五个字符有语法含义：前面加反斜杠 */
static int needs_escape(char c)
{
    return c == '\\' || c == ';' || c == ',' || c == ':' || c == '"';
}

/* 把 s 转义后追加到 out[o..]，返回新的 o；放不下返回 0 */
static size_t append_escaped(char *out, size_t cap, size_t o, const char *s)
{
    for (; *s; s++) {
        size_t need = needs_escape(*s) ? 2u : 1u;
        if (o + need >= cap) return 0;      /* 放不下：整条不出（半个热点名扫不出来）*/
        if (need == 2u) out[o++] = '\\';
        out[o++] = *s;
    }
    return o;
}

size_t link_info_wifi_qr(char *out, size_t cap)
{
    if (!out || cap < 2) return 0;
    out[0] = '\0';
    if (!s_info.valid || !s_info.ssid[0]) {
        /* 没有热点名：退回网址（旧主控固件不发 INFO，或热点被关掉）*/
        const char *fallback = s_info.valid && s_info.url[0] ? s_info.url : "http://192.168.4.1";
        if (strlen(fallback) + 1u > cap) return 0;
        strcpy(out, fallback);
        return strlen(out);
    }
    static const char head[] = "WIFI:T:WPA;S:";
    size_t o;
    if (sizeof head > cap) return 0;
    strcpy(out, head);
    o = sizeof head - 1u;
    if ((o = append_escaped(out, cap, o, s_info.ssid)) == 0) return 0;
    if (o + 4u >= cap) return 0;            /* ";P=" + 结尾留位 */
    strcpy(out + o, ";P=");
    o += 3u;
    if ((o = append_escaped(out, cap, o, s_info.pass)) == 0) return 0;
    if (o + 4u >= cap) return 0;            /* ";;" + 结尾留位 */
    strcpy(out + o, ";;");
    o += 2u;
    return o;
}

int link_info_store(const char *pl, size_t len)
{
    if (!pl || len == 0 || len > TCS_INFO_MAX_LEN) return -1;
    /* 先全部取到临时缓冲：热点名/密码缺了就不改动旧值（宁可不更新，不留半截）*/
    char ssid[sizeof s_info.ssid], pass[sizeof s_info.pass];
    char url[sizeof s_info.url], fw[sizeof s_info.fw];
    if (tcs_info_get(pl, len, "ssid", ssid, sizeof ssid) < 0) return -1;
    if (tcs_info_get(pl, len, "pass", pass, sizeof pass) < 0) return -1;
    /* 网址与固件版本可以缺省（旧主控固件）：缺了就显示空，不影响热点名/密码 */
    url[0] = 0;
    fw[0] = 0;
    (void)tcs_info_get(pl, len, "url", url, sizeof url);
    (void)tcs_info_get(pl, len, "fw",  fw,  sizeof fw);
    strcpy(s_info.ssid, ssid);
    strcpy(s_info.pass, pass);
    strcpy(s_info.url, url);
    strcpy(s_info.fw, fw);
    s_info.valid = 1;
    s_gen++;
    return 0;
}
