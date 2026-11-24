/**
 * web_selftest_ws.c — WebSocket 回环自检（只在台架/现场档编译：CONFIG_TCS_DIAG 或 CONFIG_TCS_SIM）
 *
 * 为什么需要（U 线，2026-11-24）：网页的全部实时功能都走 /ws，而开发机连不上设备热点
 * （一连就断网），浏览器到不了设备。这里让固件**自己当一个最小的 WebSocket 客户端连自己**，
 * 把服务端的行为在真机上验一遍：
 *   握手 → 首推顺序（reg、log、st）→ ping/pong 带 ref → set 必回执（含"值没变"与"被拒绝"）
 *   → cmd 白名单 → ack 带 ref → CLOSE 之后会话表清空 → 不带 Upgrade 的 GET /ws 回 400
 *   → 第 5 个客户端被拒（上限 4）→ 全部断开后会话表回到 0。
 * 不改任何参数：set 用的是"与当前值相同"的值和"必然被约束拒绝"的值。
 * 生产档（两个开关都关）整段不编译。
 */
#include "websocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include "lwip/sockets.h"
#include "esp_timer.h"

#if CONFIG_TCS_DIAG || CONFIG_TCS_SIM

#define OP_TEXT  0x1
#define OP_CLOSE 0x8
#define OP_PING  0x9
#define OP_PONG  0xA

static int s_pass, s_total;
static void chk(int ok, const char *what)
{
    s_total++;
    if (ok) s_pass++;
    printf("    %-46s %s\r\n", what, ok ? "OK" : "FAIL");
}

/* ---- 最小 TCP / WS 客户端 ---- */

static int tcp_open(const char *ip, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return -1;
    struct timeval tv = { .tv_sec = 3, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = inet_addr(ip);
    if (connect(fd, (struct sockaddr *)&a, sizeof a) != 0) { close(fd); return -1; }
    return fd;
}

static int rx_exact(int fd, uint8_t *b, size_t n)
{
    size_t got = 0;
    while (got < n) {
        int r = recv(fd, b + got, n - got, 0);
        if (r <= 0) return -1;
        got += (size_t)r;
    }
    return 0;
}

/* 读 HTTP 响应头直到空行，返回状态码；失败 -1 */
static int http_status(int fd)
{
    char head[600];
    size_t n = 0;
    while (n + 4 < sizeof head) {
        if (rx_exact(fd, (uint8_t *)head + n, 1) != 0) return -1;
        if (n >= 3 && memcmp(head + n - 3, "\r\n\r\n", 4) == 0) { head[n + 1] = 0; break; }
        n++;
    }
    int code = 0;
    if (sscanf(head, "HTTP/1.1 %d", &code) != 1) return -1;
    return code;
}

static int tx_all(int fd, const void *b, size_t n)
{
    const uint8_t *p = b;
    while (n) {
        int r = send(fd, p, n, 0);
        if (r <= 0) return -1;
        p += r; n -= (size_t)r;
    }
    return 0;
}

/* 发 WebSocket 握手（upgrade=1）或普通 GET（upgrade=0），返回响应状态码 */
static int ws_handshake(int fd, const char *ip, int port, int upgrade)
{
    char req[260];
    int n = upgrade
        ? snprintf(req, sizeof req,
                   "GET /ws HTTP/1.1\r\nHost: %s:%d\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                   "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n", ip, port)
        : snprintf(req, sizeof req, "GET /ws HTTP/1.1\r\nHost: %s:%d\r\n\r\n", ip, port);
    if (n <= 0 || tx_all(fd, req, (size_t)n) != 0) return -1;
    return http_status(fd);
}

/* 读一个服务端帧。载荷的前 cap 字节留在 pl（更长的部分读走丢弃）。返回 opcode，失败 -1 */
static int ws_frame(int fd, uint8_t *pl, size_t cap, size_t *len)
{
    uint8_t h[2];
    if (rx_exact(fd, h, 2) != 0) return -1;
    int op = h[0] & 0x0F;
    uint64_t n = h[1] & 0x7Fu;
    if (n == 126u) {
        uint8_t e[2];
        if (rx_exact(fd, e, 2) != 0) return -1;
        n = ((uint64_t)e[0] << 8) | e[1];
    } else if (n == 127u) {
        uint8_t e[8];
        if (rx_exact(fd, e, 8) != 0) return -1;
        n = 0;
        for (int i = 0; i < 8; i++) n = (n << 8) | e[i];
    }
    uint8_t mask[4] = { 0 };
    if (h[1] & 0x80u && rx_exact(fd, mask, 4) != 0) return -1;    /* RFC 6455：服务端帧不得掩码；容错 */
    *len = 0;
    while (n) {
        uint8_t tmp[256];
        size_t k = n < sizeof tmp ? (size_t)n : sizeof tmp;
        if (rx_exact(fd, tmp, k) != 0) return -1;
        if (mask[0] | mask[1] | mask[2] | mask[3])
            for (size_t i = 0; i < k; i++) tmp[i] ^= mask[(*len + i) & 3u];
        if (*len + k <= cap) { memcpy(pl + *len, tmp, k); *len += k; }
        n -= k;
    }
    return op;
}

/* 发一个带掩码的客户端帧（载荷 < 126 字节） */
static int ws_send_masked(int fd, int op, const char *pl)
{
    size_t n = strlen(pl);
    if (n >= 126u) return -1;
    uint8_t buf[140], mask[4] = { 0x37, 0xFA, 0x21, 0x3D };
    size_t h = 0;
    buf[h++] = (uint8_t)(0x80u | op);
    buf[h++] = (uint8_t)(0x80u | n);
    memcpy(buf + h, mask, 4);
    h += 4;
    for (size_t i = 0; i < n; i++) buf[h + i] = (uint8_t)(pl[i] ^ mask[i & 3u]);
    return tx_all(fd, buf, h + n);
}

/* 读下一帧文本到 buf（结尾补 NUL），返回 opcode，失败 -1 */
static int ws_text(int fd, char *buf, size_t cap)
{
    size_t n = 0;
    int op = ws_frame(fd, (uint8_t *)buf, cap - 1, &n);
    buf[n] = 0;
    return op;
}

/* 依次读到 type 为 want 的帧为止（最多 20 帧）；找到返回 0 */
static int ws_expect(int fd, const char *want, char *buf, size_t cap)
{
    char key[16];
    snprintf(key, sizeof key, "\"t\":\"%s\"", want);
    for (int i = 0; i < 20; i++) {
        int op = ws_text(fd, buf, cap);
        if (op != OP_TEXT) { printf("    (want %s: got op=%d errno=%d)\r\n", want, op, errno); return -1; }
        if (strstr(buf, key)) return 0;
    }
    printf("    (want %s: 20 frames without it)\r\n", want);
    return -1;
}

static int ws_client_count_after_ms(int ms)
{
    int64_t t0 = esp_timer_get_time();
    while (esp_timer_get_time() - t0 < (int64_t)ms * 1000) {
        if (ws_client_count() == 0) return 0;
        usleep(50 * 1000);
    }
    return -1;
}

int ws_selftest_ws(const char *ip, int port, int *pass, int *total)
{
    s_pass = s_total = 0;
    char b[320];
    printf("  ws loopback %s:%d\r\n", ip, port);

    /* ---- A：握手 + 首推顺序 + 注册表数值不带 float32 尾数 ---- */
    char hyst[16] = "", cold[16] = "";        /* 当前值（照抄 reg 里的写法，下面 set 回同一个值）*/
    int fd = tcp_open(ip, port);
    int code = (fd >= 0) ? ws_handshake(fd, ip, port, 1) : -1;
    chk(code == 101, "handshake: 101 Switching Protocols");
    if (code != 101) { if (fd >= 0) close(fd); goto tail; }
    {
        int ok = 1;
        /* 第一帧是注册表（约 7KB）：整帧读进堆缓冲，查数值写法、取两个参数的当前值 */
        char *reg = malloc(WS_REG_BUF);
        size_t n = 0;
        int op = reg ? ws_frame(fd, (uint8_t *)reg, WS_REG_BUF - 1, &n) : -1;
        if (!reg || op != OP_TEXT) {
            ok = 0;
            printf("    (first frame: op=%d len=%u errno=%d head=%02x %02x %02x %02x)\r\n", op, (unsigned)n, errno,
                   reg ? reg[0] : 0, n > 1 ? reg[1] : 0, n > 2 ? reg[2] : 0, n > 3 ? reg[3] : 0);
        } else {
            reg[n] = 0;
            if (!strstr(reg, "\"t\":\"reg\"")) ok = 0;
            /* 小数点后面跟 10 位以上数字 = float32 尾数（0.10000000149011612）*/
            int longdec = 0;
            for (const char *p = reg; (p = strchr(p, '.')) != NULL; p++) {
                int d = 0;
                while (p[1 + d] >= '0' && p[1 + d] <= '9') d++;
                if (d >= 10) { longdec = 1; break; }
            }
            chk(!longdec, "reg numbers carry no float32 tail");
            /* "def" 在 "key" 之前：从 key 往回找最近的 "def": */
            const char *cur = NULL;
            if ((cur = strstr(reg, "\"key\":\"hyst\"")) != NULL) {
                const char *d = strstr(reg, "\"def\":");
                const char *best = NULL;
                while (d && d < cur) { best = d; d = strstr(d + 6, "\"def\":"); }
                if (best) sscanf(best + 6, "%15[^,]", hyst);
            }
            if ((cur = strstr(reg, "\"key\":\"body_cold\"")) != NULL) {
                const char *d = strstr(reg, "\"def\":");
                const char *best = NULL;
                while (d && d < cur) { best = d; d = strstr(d + 6, "\"def\":"); }
                if (best) sscanf(best + 6, "%15[^,]", cold);
            }
        }
        free(reg);
        static const char *const order[3] = { "reg", "log", "st" };
        for (int i = 1; ok && i < 3; i++) {
            char key[16];
            snprintf(key, sizeof key, "\"t\":\"%s\"", order[i]);
            if (ws_text(fd, b, sizeof b) != OP_TEXT || !strstr(b, key)) ok = 0;
        }
        chk(ok, "first frames in order: reg, log, st");
        chk(hyst[0] && cold[0], "current hyst / body_cold read back from reg");
    }

    /* ---- B：ping → pong 带 ref ---- */
    ws_send_masked(fd, OP_TEXT, "{\"t\":\"ping\",\"ref\":7}");
    chk(ws_expect(fd, "pong", b, sizeof b) == 0 && strstr(b, "\"ref\":7") != NULL, "ping -> pong carries ref");

    /* ---- C：set 必回执。两个 set 都不改任何参数：
     *         第一个写回 hyst 的当前值（"值没变"也要回执）；第二个把 body_hot 写成 body_cold 的当前值
     *         （必然被"过冷阈值必须小于过热阈值"拒绝）。---- */
    if (hyst[0] && cold[0]) {
        char f[80];
        snprintf(f, sizeof f, "{\"t\":\"set\",\"id\":5,\"v\":%s,\"ref\":9}", hyst);
        ws_send_masked(fd, OP_TEXT, f);
        chk(ws_expect(fd, "pv", b, sizeof b) == 0 && strstr(b, "\"id\":5") != NULL &&
            strstr(b, "\"ref\":9") != NULL && strstr(b, "\"ok\":1") != NULL,
            "set (same value) -> pv ok=1 with my ref");
        snprintf(f, sizeof f, "{\"t\":\"set\",\"id\":3,\"v\":%s,\"ref\":10}", cold);
        ws_send_masked(fd, OP_TEXT, f);
        chk(ws_expect(fd, "pv", b, sizeof b) == 0 && strstr(b, "\"id\":3") != NULL &&
            strstr(b, "\"ref\":10") != NULL && strstr(b, "\"ok\":0") != NULL && strstr(b, "\"err\":-2") != NULL,
            "set (rejected by constraint) -> pv ok=0 err=-2");
    }
    ws_send_masked(fd, OP_TEXT, "{\"t\":\"set\",\"id\":65537,\"v\":1,\"ref\":11}");
    chk(ws_expect(fd, "err", b, sizeof b) == 0 && strstr(b, "bad_set") != NULL, "set id=65537 -> err bad_set (not truncated)");

    /* ---- D：命令白名单与 ack 带 ref。命令 9 是"确认"：关断方向，没有锁存时什么都不做 ---- */
    ws_send_masked(fd, OP_TEXT, "{\"t\":\"cmd\",\"c\":11,\"ref\":5}");
    chk(ws_expect(fd, "err", b, sizeof b) == 0 && strstr(b, "bad_cmd") != NULL, "cmd 11 (screen-only) -> err bad_cmd");
    ws_send_masked(fd, OP_TEXT, "{\"t\":\"cmd\",\"c\":9,\"ref\":6}");
    chk(ws_expect(fd, "ack", b, sizeof b) == 0 && strstr(b, "\"ref\":6") != NULL, "cmd 9 (ack, nothing latched) -> ack with ref");

    /* ---- E：CLOSE → 服务端回 CLOSE 并关会话；会话表随后清空 ----
     * 应答之前可能还排着几帧在途的推送（它们在服务端处理 CLOSE 之前就发出了）：跳过文本帧。---- */
    {
        uint8_t cl[2] = { 0x03, 0xE8 };
        uint8_t buf[8], mask[4] = { 0x37, 0xFA, 0x21, 0x3D };
        buf[0] = 0x88; buf[1] = 0x82;
        memcpy(buf + 2, mask, 4);
        buf[6] = cl[0] ^ mask[0]; buf[7] = cl[1] ^ mask[1];
        tx_all(fd, buf, 8);
        int op = -1;
        for (int i = 0; i < 40; i++) {
            size_t n = 0;
            op = ws_frame(fd, buf, sizeof buf, &n);
            if (op != OP_TEXT) break;                /* CLOSE 应答（8），或连接已断（-1）*/
        }
        chk(op == OP_CLOSE, "CLOSE -> server answers CLOSE");
        close(fd);
    }
    chk(ws_client_count_after_ms(1500) == 0, "client table empty after CLOSE");

    /* ---- F：不带 Upgrade 头的普通 GET /ws ---- */
    fd = tcp_open(ip, port);
    code = (fd >= 0) ? ws_handshake(fd, ip, port, 0) : -1;
    chk(code == 400, "plain GET /ws -> 400 (not registered as a WS client)");
    if (fd >= 0) close(fd);
    (void)ws_client_count_after_ms(800);

    /* ---- G：多客户端 + 不打招呼就断开（半死连接）----
     * 回环里每个客户端要占两个 socket（自己这头 + 服务端那头），lwip 的 socket 总数有限
     * （CONFIG_LWIP_MAX_SOCKETS=10，还有热点 DHCP 等在用），所以这里只开 3 个；
     * "第 5 个被拒"在回环里造不出来（要 11 个 socket），由代码审读覆盖。---- */
    {
        enum { NCLI = 3 };
        int fds[NCLI], n = 0;
        for (int i = 0; i < NCLI; i++) {
            fds[i] = tcp_open(ip, port);
            if (fds[i] < 0) break;
            if (ws_handshake(fds[i], ip, port, 1) != 101) { close(fds[i]); fds[i] = -1; break; }
            n++;
        }
        chk(n == NCLI, "clients 1..3 handshakes accepted");
        {
            int64_t t0 = esp_timer_get_time();
            while (esp_timer_get_time() - t0 < 1500000 && ws_client_count() != n) usleep(20 * 1000);
            chk(ws_client_count() == n, "client table lists all three");
        }
        for (int i = 0; i < n; i++) close(fds[i]);          /* 不发 CLOSE：模拟手机断电/断网 */
        chk(ws_client_count_after_ms(2500) == 0, "client table empty after abrupt disconnects");
    }
tail:
    printf("  ws loopback: %d/%d PASS\r\n", s_pass, s_total);
    *pass = s_pass;
    *total = s_total;
    return (s_total && s_pass == s_total) ? 0 : -1;
}

#else /* 生产档 */

int ws_selftest_ws(const char *ip, int port, int *pass, int *total)
{
    (void)ip; (void)port; (void)pass; (void)total;
    return -1;
}

#endif /* CONFIG_TCS_DIAG || CONFIG_TCS_SIM */
