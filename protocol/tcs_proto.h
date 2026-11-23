/* ===================================================================
 * protocol/tcs_proto.h — 协议**唯一源**（R1 AD-6，2026-11-15）
 *
 * 两端 project_tcs/App/tcs/tcs_proto.h 与 esp32/components/tcs_link/tcs_proto.h
 * 都只是 3 行包装（#include "../../../protocol/tcs_proto.h"）。
 * 禁止在包装文件里定义任何协议内容；改协议只改本文件。
 * 校验：python tools/proto_sync.py --check（两端包装一致 + 无重复定义）
 * =================================================================== */
/**
 * tcs_proto.h — 调温服 H743↔ESP32-S3 链路协议（双端共用一份，任务书 §4.2）
 *
 * 跑在 proto.c 帧层（AA 55|Len|Payload|CRC8）+ comm.c ARQ（500ms 超时×3 重传）之上。
 * 本头文件只定义消息号、载荷布局与 编码/解码 静态内联函数：
 *   - 零 HAL 依赖，仅 <stdint.h>/<string.h>，PC/H743/ESP32 三端同编译（§5.1 tcs_proto 跨端单测）
 *   - 多字节字段一律小端（任务书 §4.2 "字节级，小端"）
 *
 * 消息号段 0x10-0x16 / 0x90-0x91（与 GS 无人机协议 0x01-0x05/0x81-0x85 无冲突）。
 *
 * ⚠️ 长度核对记录（2026-10-18）：STATUS 逐字段相加 = 1+4+2+2+2+1+1+1+1+2+1+1 = 19B。
 *    任务书 §4.2 表尾标注 "17B" 为算术口径（漏加 online+rfu 两字节）；字段清单为准，
 *    双端同源本头文件 + proto_ref.py 同布局，字节级一致由单测保证。
 */
#ifndef TCS_PROTO_H
#define TCS_PROTO_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ====== 协议版本（STATUS.ver；不匹配→网页黄条不拒绝服务）====== */
#define TCS_PROTO_VER       1U

/* ====== 消息号 ====== */
#define MT_TCS_STATUS          0x10  /* H743→ESP 500ms 周期 */
#define MT_TCS_PARAM           0x11  /* 双向参数帧：H743→ESP32=屏改参/握手全量；ESP32→H743=权威镜像（v2 补口）*/
#define MT_TCS_SET_PARAM       0x12  /* ESP→H743 ACK 必达 */
#define MT_TCS_GET_REGISTRY    0x13  /* ESP→H743 无载荷 */
#define MT_TCS_REGISTRY_ENTRY  0x14  /* H743→ESP ×N 条 */
#define MT_TCS_CMD             0x15  /* ESP→H743 ACK 必达 */
#define MT_TCS_ACK             0x90  /* 双向 */

/* TCS_CMD 命令码（§4.2）*/
#define TCS_CMD_TEC1_ON        1
#define TCS_CMD_TEC1_OFF       2
#define TCS_CMD_TEC2_ON        3
#define TCS_CMD_TEC2_OFF       4
#define TCS_CMD_FAN_TOGGLE     5
#define TCS_CMD_MODE_AUTO      6
#define TCS_CMD_MODE_MANUAL    7
#define TCS_CMD_ESTOP          8
#define TCS_CMD_ALARM_ACK      9
#define TCS_CMD_M207_SAVE     10   /* P3：保存到模块（R5 接 M207 `!` 传输）*/
#define TCS_CMD_OVERTEMP_STOP 11   /* R2-N-01：屏侧观测到超温 → 请求对端停机（**不是**急停：
                                    * 不置 ESTOP 锁存、不改手动模式，只锁 OVERTEMP 并关 TCSW）*/

/* TCS_ACK status */
#define TCS_ACK_OK             0
#define TCS_ACK_CLAMP          1
#define TCS_ACK_BAD_ID         2
#define TCS_ACK_BUSY           3

/* 0x10 online 位定义 */
#define TCS_ONLINE_M207        0x01
#define TCS_ONLINE_BODY_SENSOR 0x02
#define TCS_ONLINE_ESP_LINK    0x04

/* alarm_bits（§6.2）*/
#define ALM_BODY_SENSOR        0x0001
#define ALM_M207               0x0002
#define ALM_ESTOP              0x0004
#define ALM_OVERTEMP           0x0008
#define ALM_EEPROM             0x0010
#define ALM_SENSOR_OPEN        0x0020  /* 通道探头开路（M207 在线但该通道温度不可用；与 ALM_M207 通信失联分离）*/

/* ====== 0x10 TCS_STATUS（19B 小端）====== */
typedef struct {
    uint8_t  ver;        /* TCS_PROTO_VER */
    uint32_t seq;        /* 发送序号 */
    float    tc1_act;    /* 编解码按 i16÷10 传输（°C）*/
    float    tc2_act;
    float    body;
    uint8_t  tec1, tec2, fan, mode;
    uint16_t alarm;
    uint8_t  online;     /* TCS_ONLINE_* 位或 */
    uint8_t  rfu;
} tcs_status_pl_t;

#define TCS_STATUS_PL_LEN   19

/* ====== 0x11/0x12 PARAM（7B 小端）====== */
typedef struct {
    uint16_t id;
    uint8_t  type;       /* 0=f32 1=u8 2=u16（与 param_registry 一致）*/
    uint32_t value;      /* f32 按 IEEE754 位型小端存 */
} tcs_param_pl_t;

#define TCS_PARAM_PL_LEN    7

/* ====== 0x15 CMD（1B）====== */
#define TCS_CMD_PL_LEN      1

/* ====== 0x90 TCS_ACK（7B 小端）====== */
typedef struct {
    uint8_t  acked_seq;
    uint8_t  acked_mt;
    uint8_t  status;
    float    ret;        /* SET_PARAM 回实际写入值 */
} tcs_ack_pl_t;

#define TCS_ACK_PL_LEN      7

/* ====== 0x14 REGISTRY_ENTRY（变长：count+idx 头 + 条目）====== */
typedef struct {
    uint8_t  count;      /* 本批总条数 */
    uint8_t  idx;        /* 本帧条目序号 0..count-1 */
    uint16_t id;
    uint8_t  type;
    uint8_t  flags;
    float    min, max, step;
    char     key[24];
    char     name[48];   /* UTF-8 */
    char     unit[8];
} tcs_reg_entry_t;

/* 编一条 0x14 载荷。返回载荷字节数；cap 不够返回 0（上层应拆帧）。 */
static inline size_t tcs_pl_registry_entry(uint8_t *out, size_t cap,
                                           const tcs_reg_entry_t *e)
{
    size_t kl = strlen(e->key), nl = strlen(e->name), ul = strlen(e->unit);
    size_t need = 2 + 2 + 1 + 1 + 12 + 1 + kl + 1 + nl + 1 + ul;
    if (cap < need || kl > 200 || nl > 200 || ul > 200) return 0;
    size_t o = 0;
    out[o++] = e->count;
    out[o++] = e->idx;
    out[o++] = (uint8_t)(e->id & 0xFF);
    out[o++] = (uint8_t)(e->id >> 8);
    out[o++] = e->type;
    out[o++] = e->flags;
    memcpy(&out[o], &e->min, 4); o += 4;   /* float 位型小端（ARM/PC 均小端）*/
    memcpy(&out[o], &e->max, 4); o += 4;
    memcpy(&out[o], &e->step, 4); o += 4;
    out[o++] = (uint8_t)kl;
    memcpy(&out[o], e->key, kl); o += kl;
    out[o++] = (uint8_t)nl;
    memcpy(&out[o], e->name, nl); o += nl;
    out[o++] = (uint8_t)ul;
    memcpy(&out[o], e->unit, ul); o += ul;
    return o;
}

/* 解 0x14 载荷 → 条目。返回 0=ok，-1=长度不合法。 */
static inline int tcs_scan_registry_entry(const uint8_t *pl, size_t len,
                                          tcs_reg_entry_t *e)
{
    if (len < 22) return -1;
    size_t o = 0;
    e->count = pl[o++];
    e->idx   = pl[o++];
    e->id    = (uint16_t)(pl[o] | ((uint16_t)pl[o + 1] << 8)); o += 2;
    e->type  = pl[o++];
    e->flags = pl[o++];
    memcpy(&e->min,  &pl[o], 4); o += 4;
    memcpy(&e->max,  &pl[o], 4); o += 4;
    memcpy(&e->step, &pl[o], 4); o += 4;
    uint8_t kl = pl[o++];
    if (o + kl > len) return -1;
    if (kl >= sizeof(e->key)) return -1;
    memcpy(e->key, &pl[o], kl); e->key[kl] = 0; o += kl;
    uint8_t nl = pl[o++];
    if (o + nl > len) return -1;
    if (nl >= sizeof(e->name)) return -1;
    memcpy(e->name, &pl[o], nl); e->name[nl] = 0; o += nl;
    uint8_t ul = pl[o++];
    if (o + ul > len) return -1;
    if (ul >= sizeof(e->unit)) return -1;
    memcpy(e->unit, &pl[o], ul); e->unit[ul] = 0; o += ul;
    return 0;
}

/* ====== 通用小端打包/解包 ====== */
static inline void tcs_put_u16(uint8_t *p, uint16_t v)
{ p[0] = (uint8_t)(v & 0xFF); p[1] = (uint8_t)(v >> 8); }

static inline uint16_t tcs_get_u16(const uint8_t *p)
{ return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

static inline void tcs_put_u32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }

static inline uint32_t tcs_get_u32(const uint8_t *p)
{ return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static inline void tcs_put_i16(uint8_t *p, int16_t v)
{ tcs_put_u16(p, (uint16_t)v); }

static inline int16_t tcs_get_i16(const uint8_t *p)
{ return (int16_t)tcs_get_u16(p); }

/* float ↔ 位型（无硬件浮点依赖的纯位操作）*/
static inline uint32_t tcs_f2b(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static inline float    tcs_b2f(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

/* ====== 0x10 STATUS 编解码 ====== */
static inline size_t tcs_pl_status(uint8_t *out, size_t cap, const tcs_status_pl_t *s)
{
    if (cap < TCS_STATUS_PL_LEN) return 0;
    size_t o = 0;
    out[o++] = s->ver;
    tcs_put_u32(&out[o], s->seq); o += 4;
    tcs_put_i16(&out[o], (int16_t)(s->tc1_act * 10.0f + ((s->tc1_act < 0) ? -0.5f : 0.5f))); o += 2;
    tcs_put_i16(&out[o], (int16_t)(s->tc2_act * 10.0f + ((s->tc2_act < 0) ? -0.5f : 0.5f))); o += 2;
    tcs_put_i16(&out[o], (int16_t)(s->body     * 10.0f + ((s->body     < 0) ? -0.5f : 0.5f))); o += 2;
    out[o++] = s->tec1;
    out[o++] = s->tec2;
    out[o++] = s->fan;
    out[o++] = s->mode;
    tcs_put_u16(&out[o], s->alarm); o += 2;
    out[o++] = s->online;
    out[o++] = s->rfu;
    return o;
}

static inline int tcs_scan_status(const uint8_t *pl, size_t len, tcs_status_pl_t *s)
{
    if (len < TCS_STATUS_PL_LEN) return -1;
    size_t o = 0;
    s->ver = pl[o++];
    s->seq = tcs_get_u32(&pl[o]); o += 4;
    s->tc1_act = (float)tcs_get_i16(&pl[o]) / 10.0f; o += 2;
    s->tc2_act = (float)tcs_get_i16(&pl[o]) / 10.0f; o += 2;
    s->body    = (float)tcs_get_i16(&pl[o]) / 10.0f; o += 2;
    s->tec1 = pl[o++];
    s->tec2 = pl[o++];
    s->fan  = pl[o++];
    s->mode = pl[o++];
    s->alarm = tcs_get_u16(&pl[o]); o += 2;
    s->online = pl[o++];
    s->rfu = pl[o++];
    return 0;
}

/* ====== 0x11/0x12 PARAM 编解码 ====== */
static inline size_t tcs_pl_param(uint8_t *out, size_t cap, const tcs_param_pl_t *p)
{
    if (cap < TCS_PARAM_PL_LEN) return 0;
    tcs_put_u16(&out[0], p->id);
    out[2] = p->type;
    tcs_put_u32(&out[3], p->value);
    return TCS_PARAM_PL_LEN;
}

static inline int tcs_scan_param(const uint8_t *pl, size_t len, tcs_param_pl_t *p)
{
    if (len < TCS_PARAM_PL_LEN) return -1;
    p->id   = tcs_get_u16(&pl[0]);
    p->type = pl[2];
    p->value = tcs_get_u32(&pl[3]);
    return 0;
}

/* ====== 0x15 CMD / 0x90 ACK ====== */
static inline size_t tcs_pl_cmd(uint8_t *out, size_t cap, uint8_t cmd)
{
    if (cap < 1) return 0;
    out[0] = cmd;
    return 1;
}

static inline size_t tcs_pl_ack(uint8_t *out, size_t cap, const tcs_ack_pl_t *a)
{
    if (cap < TCS_ACK_PL_LEN) return 0;
    out[0] = a->acked_seq;
    out[1] = a->acked_mt;
    out[2] = a->status;
    tcs_put_u32(&out[3], tcs_f2b(a->ret));
    return TCS_ACK_PL_LEN;
}

static inline int tcs_scan_ack(const uint8_t *pl, size_t len, tcs_ack_pl_t *a)
{
    if (len < TCS_ACK_PL_LEN) return -1;
    a->acked_seq = pl[0];
    a->acked_mt  = pl[1];
    a->status    = pl[2];
    a->ret       = tcs_b2f(tcs_get_u32(&pl[3]));
    return 0;
}

/* ====== 0x20 TCS_SNAPSHOT / 0x21 TCS_DELTA（R2 架构 v2：ESP→H743 200ms 全量，
 * fire-and-forget；本段与 esp32/components/tcs_link/tcs_proto.h 逐字同源——
 * 字节格式=唯一真相，改动必须双端同步并经 proto_ref.py 字节对照）====== */
#define MT_TCS_SNAPSHOT       0x20
#define MT_TCS_DELTA          0x21

typedef struct {
    uint8_t  ver;        /* TCS_PROTO_VER */
    uint32_t seq;        /* 帧序号（发送侧自增；收端做单调性检查 F15）*/
    uint32_t ms;         /* ESP boot 毫秒戳 */
    float    tc1_act, tc2_act, body;
    float    tc1_set, tc2_set;
    uint8_t  tec1, tec2, fan, mode;
    uint16_t alarm;
    uint8_t  online;     /* TCS_ONLINE_* 位或 */
    uint16_t fresh_ms;   /* M207 温度缓存龄 ms（0xFFFF=未知/离线）*/
    uint8_t  badge;      /* 布防角标 0/1/2（post_arm_t）*/
    uint8_t  rfu;
} tcs_snap_pl_t;

#define TCS_SNAP_PL_LEN   40   /* 1+4+4+20+4+2+1+2+1+1 */

#define TCS_D_TC1_SET  (1u << 0)
#define TCS_D_TC2_SET  (1u << 1)
#define TCS_D_TEC      (1u << 2)   /* 1B：bit0=tec1 bit1=tec2 */
#define TCS_D_FAN      (1u << 3)
#define TCS_D_MODE     (1u << 4)
#define TCS_D_ALARM    (1u << 5)
#define TCS_D_BODY     (1u << 6)
#define TCS_D_BADGE    (1u << 7)

typedef struct {             /* 全量源值；编码按 mask 摘取，解码只填 mask 内字段 */
    float    tc1_set, tc2_set, body;
    uint8_t  tec;        /* bit0=tec1 bit1=tec2 */
    uint8_t  fan, mode, badge;
    uint16_t alarm;
} tcs_delta_src_t;

static inline size_t tcs_pl_snapshot(uint8_t *out, size_t cap, const tcs_snap_pl_t *s)
{
    if (cap < TCS_SNAP_PL_LEN) return 0;
    size_t o = 0;
    out[o++] = s->ver;
    tcs_put_u32(&out[o], s->seq); o += 4;
    tcs_put_u32(&out[o], s->ms);  o += 4;
    tcs_put_u32(&out[o], tcs_f2b(s->tc1_act)); o += 4;
    tcs_put_u32(&out[o], tcs_f2b(s->tc2_act)); o += 4;
    tcs_put_u32(&out[o], tcs_f2b(s->body));    o += 4;
    tcs_put_u32(&out[o], tcs_f2b(s->tc1_set)); o += 4;
    tcs_put_u32(&out[o], tcs_f2b(s->tc2_set)); o += 4;
    out[o++] = s->tec1; out[o++] = s->tec2; out[o++] = s->fan; out[o++] = s->mode;
    tcs_put_u16(&out[o], s->alarm); o += 2;
    out[o++] = s->online;
    tcs_put_u16(&out[o], s->fresh_ms); o += 2;
    out[o++] = s->badge;
    out[o++] = s->rfu;
    return o;                              /* == TCS_SNAP_PL_LEN */
}

static inline int tcs_scan_snapshot(const uint8_t *pl, size_t len, tcs_snap_pl_t *s)
{
    if (len < TCS_SNAP_PL_LEN) return -1;
    size_t o = 0;
    s->ver = pl[o++];
    s->seq = tcs_get_u32(&pl[o]); o += 4;
    s->ms  = tcs_get_u32(&pl[o]); o += 4;
    s->tc1_act = tcs_b2f(tcs_get_u32(&pl[o])); o += 4;
    s->tc2_act = tcs_b2f(tcs_get_u32(&pl[o])); o += 4;
    s->body    = tcs_b2f(tcs_get_u32(&pl[o])); o += 4;
    s->tc1_set = tcs_b2f(tcs_get_u32(&pl[o])); o += 4;
    s->tc2_set = tcs_b2f(tcs_get_u32(&pl[o])); o += 4;
    s->tec1 = pl[o++]; s->tec2 = pl[o++]; s->fan = pl[o++]; s->mode = pl[o++];
    s->alarm = tcs_get_u16(&pl[o]); o += 2;
    s->online = pl[o++];
    s->fresh_ms = tcs_get_u16(&pl[o]); o += 2;
    s->badge = pl[o++];
    s->rfu = pl[o++];
    return 0;
}

static inline size_t tcs_pl_delta(uint8_t *out, size_t cap, uint16_t mask,
                                  const tcs_delta_src_t *d)
{
    size_t need = 2;
    if (mask & TCS_D_TC1_SET) need += 4;
    if (mask & TCS_D_TC2_SET) need += 4;
    if (mask & TCS_D_TEC)     need += 1;
    if (mask & TCS_D_FAN)     need += 1;
    if (mask & TCS_D_MODE)    need += 1;
    if (mask & TCS_D_ALARM)   need += 2;
    if (mask & TCS_D_BODY)    need += 4;
    if (mask & TCS_D_BADGE)   need += 1;
    if (cap < need) return 0;
    size_t o = 0;
    tcs_put_u16(&out[o], mask); o += 2;
    if (mask & TCS_D_TC1_SET) { tcs_put_u32(&out[o], tcs_f2b(d->tc1_set)); o += 4; }
    if (mask & TCS_D_TC2_SET) { tcs_put_u32(&out[o], tcs_f2b(d->tc2_set)); o += 4; }
    if (mask & TCS_D_TEC)     { out[o++] = d->tec; }
    if (mask & TCS_D_FAN)     { out[o++] = d->fan; }
    if (mask & TCS_D_MODE)    { out[o++] = d->mode; }
    if (mask & TCS_D_ALARM)   { tcs_put_u16(&out[o], d->alarm); o += 2; }
    if (mask & TCS_D_BODY)    { tcs_put_u32(&out[o], tcs_f2b(d->body)); o += 4; }
    if (mask & TCS_D_BADGE)   { out[o++] = d->badge; }
    return o;
}

static inline int tcs_scan_delta(const uint8_t *pl, size_t len, tcs_delta_src_t *d,
                                 uint16_t *mask_out)
{
    if (len < 2) return -1;
    uint16_t mask = tcs_get_u16(&pl[0]);
    size_t o = 2;
    if (mask & TCS_D_TC1_SET) { if (len < o+4) return -1; d->tc1_set = tcs_b2f(tcs_get_u32(&pl[o])); o += 4; }
    if (mask & TCS_D_TC2_SET) { if (len < o+4) return -1; d->tc2_set = tcs_b2f(tcs_get_u32(&pl[o])); o += 4; }
    if (mask & TCS_D_TEC)     { if (len < o+1) return -1; d->tec = pl[o++]; }
    if (mask & TCS_D_FAN)     { if (len < o+1) return -1; d->fan = pl[o++]; }
    if (mask & TCS_D_MODE)    { if (len < o+1) return -1; d->mode = pl[o++]; }
    if (mask & TCS_D_ALARM)   { if (len < o+2) return -1; d->alarm = tcs_get_u16(&pl[o]); o += 2; }
    if (mask & TCS_D_BODY)    { if (len < o+4) return -1; d->body = tcs_b2f(tcs_get_u32(&pl[o])); o += 4; }
    if (mask & TCS_D_BADGE)   { if (len < o+1) return -1; d->badge = pl[o++]; }
    *mask_out = mask;
    return 0;
}

/* ====== 0x22 TCS_INFO（U 线 W3：ESP32→H743 设备信息，免 ARQ 周期推送）======
 * 载荷 = UTF-8 文本，每行 "key=value"，行间 '\n'，结尾不带 '\n'；总长 ≤ TCS_INFO_MAX_LEN。
 * 现有键：ssid（热点名）/ pass（热点密码）/ url（控制台网址）/ fw（主控固件版本）。
 * 值里的 '\n' 与 '=' 被换成空格（保证一行一键）；键为空或含这两个字符的整对跳过。
 * 旧屏固件对未知消息号静默丢弃（on_frame 的 default 分支）→ 前向兼容。*/
#define MT_TCS_INFO          0x22
#define TCS_INFO_MAX_LEN     120

/* 编 n 对 key=value。返回载荷字节数（不含结尾 NUL）；一对都放不下或参数非法返回 0。*/
static inline size_t tcs_pl_info(char *out, size_t cap, const char *const *keys,
                                 const char *const *vals, int n)
{
    size_t o = 0;
    if (!out || cap < 2 || !keys || !vals || n <= 0) return 0;
    out[0] = '\0';
    for (int i = 0; i < n; i++) {
        const char *k = keys[i] ? keys[i] : "";
        const char *v = vals[i] ? vals[i] : "";
        if (!*k || strchr(k, '=') || strchr(k, '\n')) continue;
        size_t need = strlen(k) + 1u + strlen(v) + (o ? 1u : 0u);
        if (need > cap - 1u - o) return 0;      /* 放不下：整条不发（收端不做半截解析）*/
        if (o) out[o++] = '\n';
        while (*k) out[o++] = *k++;
        out[o++] = '=';
        while (*v) { out[o++] = (*v == '\n' || *v == '=') ? ' ' : *v; v++; }
    }
    out[o] = '\0';
    return o;
}

/* 取 key 对应的值（拷贝到 out，含 NUL）。返回值长度；未找到/参数非法/值放不下返回 -1
 * （热点名与密码不能被截断显示——放不下宁可显示"未知"）。*/
static inline int tcs_info_get(const char *pl, size_t len, const char *key,
                               char *out, size_t cap)
{
    if (!pl || !key || !out || cap == 0) return -1;
    size_t klen = strlen(key);
    if (klen == 0) return -1;
    size_t i = 0;
    while (i <= len) {
        size_t j = i;
        while (j < len && pl[j] != '\n') j++;
        if (j - i > klen && pl[i + klen] == '=' && memcmp(pl + i, key, klen) == 0) {
            size_t vlen = j - i - klen - 1u;
            if (vlen >= cap) return -1;
            memcpy(out, pl + i + klen + 1u, vlen);
            out[vlen] = '\0';
            return (int)vlen;
        }
        if (j >= len) break;
        i = j + 1u;
    }
    return -1;
}

#endif /* TCS_PROTO_H */
