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
 * 消息号段 0x10-0x15 / 0x20-0x22 / 0x90（与 GS 无人机协议 0x01-0x05/0x81-0x85 无冲突；
 * 0x16、0x91 未定义）。每个消息号的发送方见下面"方向表"，它是方向信息的唯一说明。
 *
 * ⚠️ 两端固件必须成对更新（2026-11-25，R5-01）：线上帧带方向位，屏写参数只用 0x12、
 *    参数镜像只用 0x11。只换一端时设备的表现在"方向"一节末尾。
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

/* ====== 协议版本（STATUS.ver / SNAPSHOT.ver；不匹配→网页黄条、屏丢弃快照）======
 * 2026-11-25（R5-01）没有递增：屏端 POST 的 VER 自检项写死 TCS_PROTO_VER == 1
 * （project_tcs/App/tcs/post.c），递增会让交付档自检失败；方向位的不兼容是整帧级的
 * （见下），靠"必须成对更新"约束，不靠版本号。要递增须连同 POST 那一项一起改。*/
#define TCS_PROTO_VER       1U

/* ====== 方向：这一帧是谁发的（R5-01，2026-11-25）======
 *
 * 起因：2026-11-23 现场，ESP32 自己发出的参数镜像（0x11）经接线回声回到自己的 RX，被当成
 * "屏发来的改参"写进权威注册表，tc1_set 从 40.1 被改到下限 5.0。帧里原先没有任何字段能
 * 回答"这是谁发的"。
 *
 * 线上消息号字节（Payload[0]，整帧第 4 字节）= 消息号 | 方向位：
 *     bit6 (0x40) = 1 → 发送方是 ESP32（主控）
 *     bit6 (0x40) = 0 → 发送方是 H743（屏）
 * 在用的消息号（TCS 0x10-0x15 / 0x20-0x22 / 0x90，GS 遗留 0x01-0x05 / 0x81-0x85）bit6
 * 都是 0，所以一个机制对所有消息号生效；下方 _Static_assert 把"消息号不得占用 bit6"钉死。
 *
 * 方向位由 comm 层统一处理：发送时在出队处打上本端方向（发送点不需要、也不能自己打）；
 * 接收时在 ACK 处理、去重、交付之前先判方向。判定只看字节——不看长度、不看时刻。
 *
 * 极性为什么是"1 = ESP32"：只换一端时要倒向安全的一侧。旧屏固件发出的帧没有这一位
 * （读作"H743 发的"），新 ESP32 照常认——急停 0x15、写参数 0x12 仍然执行。若定成
 * "1 = H743"，旧屏的急停帧会被新 ESP32 当成"本端回送"整帧丢掉。
 *
 * 只换一端的实际表现（只为排障；两端固件必须成对更新）：
 *   新 ESP32 + 旧屏：屏不出数（旧屏不认带方向位的 0x20）；旧屏用 0x11 写的非 M207 参数被
 *                    按方向表丢弃（屏上改了又跳回）；ESP32 日志反复 link down (ARQ)。
 *   新屏 + 旧 ESP32：屏把旧 ESP32 的帧全当成"本端回送"丢弃——屏不出数；屏的写参数 0x12
 *                    与急停 0x15 仍会到达旧 ESP32 并执行。
 *
 * 方向表（"谁会发"由两端源码的收发分支决定；python tools/proto_sync.py --check 逐条核对
 * 两端的发送点与接收分支，改了代码没改这张表 → 门禁红）：
 *
 *   消息号  名称             谁会发        载荷   可靠性        接收方怎么处理
 *   ------  ---------------  ------------  ----  ------------  ----------------------------
 *   0x10    STATUS          H743          19B   免 ARQ        ESP32：链路判活 + 网页状态
 *   0x11    PARAM           ESP32          7B   免 ARQ        H743：权威镜像，静默落本地表
 *   0x12    SET_PARAM       H743           7B   等 ACK        ESP32：写权威注册表（屏的参数写入）
 *   0x13    GET_REGISTRY    ESP32          0B   等 ACK        H743：开始节拍化下发 0x14
 *   0x14    REGISTRY_ENTRY  H743         变长   免 ARQ        ESP32：注册表条目
 *   0x15    CMD             双向           1B   等 ACK        对端执行命令。ESP32→H743 方向
 *                                                      当前没有调用者（link_send_cmd 零调用）
 *   0x20    SNAPSHOT        ESP32         40B   免 ARQ        H743：全量快照（屏唯一读源）
 *   0x21    DELTA           ESP32        变长   免 ARQ        H743：位图增量
 *   0x22    INFO            ESP32        变长   免 ARQ        H743：热点名/密码/网址/固件版本
 *   0x90    ACK             双向           7B   免 ARQ        comm 层消费（清 pending、判活）
 *
 * 其它消息号：没有登记的方向表项 → 两个方向都放行到收帧分发，由分发的 default 丢弃
 * （前向兼容：0x22 刚加进来时旧屏就是这样处理的）。*/
#define MT_TCS_STATUS          0x10
#define MT_TCS_PARAM           0x11  /* 仅 ESP32→H743：权威参数镜像（屏显示跟随）*/
#define MT_TCS_SET_PARAM       0x12  /* 仅 H743→ESP32：屏写参数，等 ACK（R5-10 起所有参数都走它）*/
#define MT_TCS_GET_REGISTRY    0x13  /* 仅 ESP32→H743，无载荷 */
#define MT_TCS_REGISTRY_ENTRY  0x14  /* 仅 H743→ESP32 ×N 条 */
#define MT_TCS_CMD             0x15  /* 双向（ESP32→H743 当前无调用者）*/
#define MT_TCS_SNAPSHOT        0x20  /* 仅 ESP32→H743：全量快照（载荷见下方 0x20/0x21 段）*/
#define MT_TCS_DELTA           0x21  /* 仅 ESP32→H743：位图增量 */
#define MT_TCS_INFO            0x22  /* 仅 ESP32→H743：设备信息（载荷见下方 0x22 段）*/
#define MT_TCS_ACK             0x90  /* 双向 */

/* 方向位的三个取值：只有谁会发 */
#define TCS_DIR_H743_ONLY      1u    /* bit0：H743（屏）会发 */
#define TCS_DIR_ESP32_ONLY     2u    /* bit1：ESP32（主控）会发 */
#define TCS_DIR_BOTH           3u

/* 每个消息号的"谁会发"。改方向 = 改协议：同步改上面的表与两端代码分支。*/
#define MT_TCS_STATUS_DIR          TCS_DIR_H743_ONLY
#define MT_TCS_PARAM_DIR           TCS_DIR_ESP32_ONLY
#define MT_TCS_SET_PARAM_DIR       TCS_DIR_H743_ONLY
#define MT_TCS_GET_REGISTRY_DIR    TCS_DIR_ESP32_ONLY
#define MT_TCS_REGISTRY_ENTRY_DIR  TCS_DIR_H743_ONLY
#define MT_TCS_CMD_DIR             TCS_DIR_BOTH
#define MT_TCS_ACK_DIR             TCS_DIR_BOTH
#define MT_TCS_SNAPSHOT_DIR        TCS_DIR_ESP32_ONLY
#define MT_TCS_DELTA_DIR           TCS_DIR_ESP32_ONLY
#define MT_TCS_INFO_DIR            TCS_DIR_ESP32_ONLY

/* 本端身份（comm_init 的 self_side）*/
#define TCS_SIDE_H743          0u
#define TCS_SIDE_ESP32         1u

/* 消息号字节里的方向位与掩码 */
#define TCS_MT_DIR_ESP32       0x40u  /* bit6 = 1：发送方是 ESP32 */
#define TCS_MT_NUM_MASK        0xBFu  /* 去掉方向位 */
#define TCS_MT_DIR_MASK        0x40u

/* 在用的消息号都不得占用方向位（占用后"谁发的"就分不清了）*/
#define TCS_ASSERT_NO_DIR_(mt) _Static_assert(((mt) & TCS_MT_DIR_MASK) == 0u, \
    "message number uses the direction bit (0x40): " #mt)
TCS_ASSERT_NO_DIR_(MT_TCS_STATUS);     TCS_ASSERT_NO_DIR_(MT_TCS_PARAM);
TCS_ASSERT_NO_DIR_(MT_TCS_SET_PARAM);  TCS_ASSERT_NO_DIR_(MT_TCS_GET_REGISTRY);
TCS_ASSERT_NO_DIR_(MT_TCS_REGISTRY_ENTRY); TCS_ASSERT_NO_DIR_(MT_TCS_CMD);
TCS_ASSERT_NO_DIR_(MT_TCS_ACK);        TCS_ASSERT_NO_DIR_(MT_TCS_SNAPSHOT);
TCS_ASSERT_NO_DIR_(MT_TCS_DELTA);      TCS_ASSERT_NO_DIR_(MT_TCS_INFO);

/* 线上消息号字节 → 发送方（TCS_SIDE_*）*/
static inline uint8_t tcs_frame_sender(uint8_t mt_wire)
{
    return (mt_wire & TCS_MT_DIR_ESP32) ? TCS_SIDE_ESP32 : TCS_SIDE_H743;
}

/* 线上消息号字节 → 消息号（去掉方向位）*/
static inline uint8_t tcs_frame_mt(uint8_t mt_wire)
{
    return (uint8_t)(mt_wire & TCS_MT_NUM_MASK);
}

/* 消息号 + 发送方 → 线上消息号字节（comm 层出队时调用；已带方向位的输入先去掉再打）*/
static inline uint8_t tcs_frame_mark(uint8_t mt, uint8_t side)
{
    return (uint8_t)(tcs_frame_mt(mt) | (side == TCS_SIDE_ESP32 ? TCS_MT_DIR_ESP32 : 0u));
}

/* 消息号的"谁会发"（方向表）。未登记的消息号返回 0（两个方向都放行，见上）。*/
static inline uint8_t tcs_mt_dir(uint8_t mt)
{
    switch (mt) {
    case MT_TCS_STATUS:         return MT_TCS_STATUS_DIR;
    case MT_TCS_PARAM:          return MT_TCS_PARAM_DIR;
    case MT_TCS_SET_PARAM:      return MT_TCS_SET_PARAM_DIR;
    case MT_TCS_GET_REGISTRY:   return MT_TCS_GET_REGISTRY_DIR;
    case MT_TCS_REGISTRY_ENTRY: return MT_TCS_REGISTRY_ENTRY_DIR;
    case MT_TCS_CMD:            return MT_TCS_CMD_DIR;
    case MT_TCS_ACK:            return MT_TCS_ACK_DIR;
    case MT_TCS_SNAPSHOT:       return MT_TCS_SNAPSHOT_DIR;
    case MT_TCS_DELTA:          return MT_TCS_DELTA_DIR;
    case MT_TCS_INFO:           return MT_TCS_INFO_DIR;
    default:                    return 0u;
    }
}

/* 发送方 side 是否可能发出消息号 mt。未登记的消息号对两个方向都算"可能"。*/
static inline int tcs_mt_sent_by(uint8_t mt, uint8_t side)
{
    uint8_t d = tcs_mt_dir(mt);
    if (d == 0u) return 1;
    return (d & (1u << (side ? 1u : 0u))) != 0u;
}

/* 收帧判定的三种结果 */
#define TCS_RX_ACCEPT        0   /* 对端发来的、方向表允许的帧：照常处理 */
#define TCS_RX_ECHO          1   /* 本端发出的帧被回送：丢弃，不计入任何处理 */
#define TCS_RX_WRONG_DIR     2   /* 方向位说是对端，但对端不会发这个消息号（或 ACK 确认了一个
                                  * 本端不会发的消息号）：丢弃并单独计数——它不是回声，是坏帧或
                                  * 版本不匹配的对端 */

/* 收帧方向闸门（纯函数：两端 comm.c 同用，不依赖 HAL/RTOS）。
 *   mt_wire  = 帧载荷第 0 字节（消息号 | 方向位）
 *   self     = 本端身份（TCS_SIDE_*）
 *   payload  = 帧载荷（ACK 帧用它核对"被确认的消息号"），plen = 载荷长度（≥2）
 * 判定顺序不能换：先看方向位（回声一律不碰，包括 ACK），再看方向表。*/
static inline int tcs_rx_verdict(uint8_t mt_wire, uint8_t self,
                                 const uint8_t *payload, size_t plen)
{
    uint8_t sender = tcs_frame_sender(mt_wire);
    if (sender == self) return TCS_RX_ECHO;
    uint8_t mt = tcs_frame_mt(mt_wire);
    if (!tcs_mt_sent_by(mt, sender)) return TCS_RX_WRONG_DIR;
    /* ACK（0x90，及 GS 遗留的 0x04/0x84，布局相同：载荷 [2]=被确认 seq、[3]=被确认消息号）
     * 确认的必须是本端会发给对端的帧。本端发出的 ACK 回声即使方向位被改坏，它确认的也
     * 是对端发来的消息号——按方向表过不了这一关。*/
    if (mt == MT_TCS_ACK || mt == 0x04u || mt == 0x84u) {
        if (plen >= 4u && !tcs_mt_sent_by(tcs_frame_mt(payload[3]), self))
            return TCS_RX_WRONG_DIR;
    }
    return TCS_RX_ACCEPT;
}

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
#define ALM_SERVICE            0x0040  /* 有后台服务没能启动（P1-12，S 线 e3a）：相应功能不可用，重启前不清。屏端与网页还没有这一位的文案 */

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

/* ====== 0x20 TCS_SNAPSHOT / 0x21 TCS_DELTA（R2 架构 v2：ESP32→H743 200ms 全量，
 * fire-and-forget）。本段与本文件其余部分一样只有一份（R1 AD-6：两端包装文件只是
 * include 本文件，不存在"两份逐字同源"的文本）；字节格式以本文件为准，
 * 两端各自的单测（project_tcs/tests、esp32/tests）与 project_tcs/tools/proto_ref.py
 * 对同一组字节向量。====== */
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
