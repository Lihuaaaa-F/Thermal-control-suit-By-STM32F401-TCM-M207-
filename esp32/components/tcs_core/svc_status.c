/**
 * svc_status.c — 服务启动结果汇总的纯逻辑（P1-12；说明见 svc_status.h）
 */
#include "svc_status.h"
#include <stdio.h>
#include <string.h>

static const struct { uint32_t bit; const char *name; } k_svc[] = {
    { SVC_BIT_SOFTAP,  "softap"  },
    { SVC_BIT_LINK,    "link"    },
    { SVC_BIT_WEB,     "web"     },
    { SVC_BIT_M207,    "m207"    },
    { SVC_BIT_TMP117,  "tmp117"  },
    { SVC_BIT_CTRL,    "ctrl"    },
    { SVC_BIT_CONSOLE, "console" },
    { SVC_BIT_SNAP,    "snap"    },
    { SVC_BIT_BOOTREC, "bootrec" },
};
#define SVC_N ((int)(sizeof(k_svc) / sizeof(k_svc[0])))

_Static_assert((1u << SVC_N) - 1u == SVC_BIT_ALL, "SVC_BIT_* and the name table must cover the same bits");

void svc_init(svc_status_t *s)
{
    if (!s) return;
    s->noted = 0;
    s->failed = 0;
    s->unverified = 0;
}

int svc_note(svc_status_t *s, uint32_t bit, int rv)
{
    if (!s || !bit || (bit & (bit - 1u)) || (bit & ~SVC_BIT_ALL)) return -1;
    s->noted |= bit;
    if (rv != 0) s->failed |= bit;
    else         s->failed &= ~bit;
    return 0;
}

void svc_unverified(svc_status_t *s, uint32_t bit)
{
    if (!s || !bit || (bit & ~SVC_BIT_ALL)) return;
    s->noted |= bit;
    s->unverified |= bit;
}

int svc_all_up(const svc_status_t *s)
{
    return (s && s->failed == 0) ? 1 : 0;
}

size_t svc_names(uint32_t mask, char *out, size_t cap)
{
    size_t n = 0;
    if (!out || cap == 0) return 0;
    out[0] = '\0';
    for (int i = 0; i < SVC_N; i++) {
        size_t len;
        if (!(mask & k_svc[i].bit)) continue;
        len = strlen(k_svc[i].name);
        if (n + (n ? 1u : 0u) + len + 1u > cap) break;     /* 放不下就停：保证 NUL 结尾、不越界 */
        if (n) out[n++] = ' ';
        memcpy(out + n, k_svc[i].name, len + 1u);
        n += len;
    }
    return n;
}

uint32_t svc_report(const svc_status_t *s, svc_log_fn log, void *arg)
{
    char line[160];
    if (!s) return 0;
    if (log) {
        char names[96];
        uint32_t up = s->noted & ~(s->failed | s->unverified);
        svc_names(up, names, sizeof names);
        if (s->failed == 0 && s->unverified == 0)
            snprintf(line, sizeof line, "services up: %s (all up)", names);
        else
            snprintf(line, sizeof line, "services up: %s", names);
        log(arg, SVC_LOG_I, line);
        if (s->failed != 0) {
            svc_names(s->failed, names, sizeof names);
            snprintf(line, sizeof line, "services FAILED to start: %s", names);
            log(arg, SVC_LOG_E, line);
        }
    }
    for (int i = 0; i < SVC_N && log; i++) {
        if (s->unverified & k_svc[i].bit) {
            snprintf(line, sizeof line, "service start result not verifiable (void API): %s", k_svc[i].name);
            log(arg, SVC_LOG_W, line);
        }
    }
    return s->failed;
}
