/**
 * boot_record.c — 上次运行记录的纯逻辑（X04-F-03 / R5-13；说明见 boot_record.h）
 *
 * 判无效的四种情况，任何一种都当作"没有上一轮的记录"，绝不把随机内容当成证据：
 *   魔数不对 / 版本不对 / 长度不对 / CRC 不对。
 * 判别性用例与变异：tests/test_tcs_core.c（B 系列）、tests/mutation_core.py。
 */
#include "boot_record.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(boot_record_t) == 28, "boot_record_t must stay packed: layout is persisted across resets");
_Static_assert(offsetof(boot_record_t, crc) == 24, "CRC covers the first 24 bytes");

uint32_t bootrec_crc32(const uint8_t *p, size_t n)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc ^ 0xFFFFFFFFu;
}

static uint32_t rec_crc(const boot_record_t *rec)
{
    return bootrec_crc32((const uint8_t *)rec, offsetof(boot_record_t, crc));
}

int bootrec_valid(const boot_record_t *rec)
{
    if (!rec) return 0;
    if (rec->magic != BOOTREC_MAGIC) return 0;
    if (rec->ver != BOOTREC_VER) return 0;
    if (rec->size != (uint16_t)sizeof(boot_record_t)) return 0;
    return rec->crc == rec_crc(rec);
}

uint32_t bootrec_start(boot_record_t *rec, uint8_t reset_reason, bootrec_prev_t *prev)
{
    boot_record_t old = *rec;              /* 一次整块拷贝：下面写新记录不碰旧值 */
    uint32_t seq = 1;
    memset(prev, 0, sizeof(*prev));
    if (bootrec_valid(&old)) {
        prev->valid       = 1;
        prev->reset_reason = old.reset_reason;
        prev->uptime_s    = old.uptime_s;
        prev->alarm       = old.alarm;
        prev->latch       = old.latch;
        prev->ctrl_state  = old.ctrl_state;
        prev->seq         = old.seq;
        seq = old.seq + 1u;
    }
    memset(rec, 0, sizeof(*rec));
    rec->magic        = BOOTREC_MAGIC;
    rec->ver          = BOOTREC_VER;
    rec->size         = (uint16_t)sizeof(*rec);
    rec->seq          = seq;
    rec->reset_reason = reset_reason;
    rec->flags        = prev->valid ? 0u : 1u;
    rec->crc          = rec_crc(rec);
    return seq;
}

void bootrec_refresh(boot_record_t *rec, uint32_t uptime_s, uint16_t alarm,
                     uint8_t latch, uint8_t ctrl_state)
{
    if (rec->magic != BOOTREC_MAGIC || rec->ver != BOOTREC_VER) return;   /* 没 start 过 */
    rec->uptime_s   = uptime_s;
    rec->alarm      = alarm;
    rec->latch      = latch;
    rec->ctrl_state = ctrl_state;
    rec->crc        = rec_crc(rec);
}

int bootrec_is_crash(int reset_reason)
{
    switch (reset_reason) {
    case BOOTREC_RST_POWERON:
    case BOOTREC_RST_EXT:
    case BOOTREC_RST_SW:
    case BOOTREC_RST_DEEPSLEEP:
    case BOOTREC_RST_USB:
    case BOOTREC_RST_JTAG:
        return 0;                          /* 人或程序有意为之的重启 */
    default:
        return 1;                          /* PANIC / INT_WDT / TASK_WDT / WDT / BROWNOUT / CPU_LOCKUP /
                                            * PWR_GLITCH / UNKNOWN，以及表外的任何值 */
    }
}

const char *bootrec_reset_name(int reset_reason)
{
    static const char *const names[BOOTREC_RST_N] = {
        "UNKNOWN", "POWERON", "EXT", "SW", "PANIC", "INT_WDT", "TASK_WDT", "WDT",
        "DEEPSLEEP", "BROWNOUT", "SDIO", "USB", "JTAG", "EFUSE", "PWR_GLITCH", "CPU_LOCKUP",
    };
    if (reset_reason < 0 || reset_reason >= BOOTREC_RST_N) return "?";
    return names[reset_reason];
}

size_t bootrec_format_prev(const bootrec_prev_t *prev, uint16_t nvalm, uint8_t core_dump,
                           char *out, size_t cap)
{
    int n;
    if (!out || cap == 0) return 0;
    if (prev && prev->valid) {
        n = snprintf(out, cap, " prst=%d pup=%" PRIu32 "s palm=0x%04X plat=0x%02X pseq=%" PRIu32
                           " nvalm=0x%04X cd=%u",
                     (int)prev->reset_reason, prev->uptime_s, (unsigned)prev->alarm,
                     (unsigned)prev->latch, prev->seq, (unsigned)nvalm, (unsigned)core_dump);
    } else {
        n = snprintf(out, cap, " prst=-1 pup=0s palm=0x0000 plat=0x00 pseq=0 nvalm=0x%04X cd=%u",
                     (unsigned)nvalm, (unsigned)core_dump);
    }
    return (n < 0) ? 0u : (size_t)n;
}
