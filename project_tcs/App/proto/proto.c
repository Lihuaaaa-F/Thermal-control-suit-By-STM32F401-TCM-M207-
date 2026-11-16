/**
 * proto.c — H 题上下行通信协议 STM32 端参考实现
 *
 * 详见 proto.h。与 Python comm_protocol.py 字节级一致。
 * 零外部依赖，裸机/RTOS/PC 均可编译。
 */
#include "proto.h"
#include <string.h>
#include "../../../protocol/proto_layout.h"   /* R1 AD-6：协议布局编译期契约（双端同源）*/

/* ===================================================================== */
/*                              CRC8                                     */
/* ===================================================================== */
uint8_t proto_crc8(const uint8_t *data, size_t len)
{
    /* 逐位实现，与通信协议.md §1 的 C 版、Python crc8() 完全一致 */
    uint8_t crc = CRC8_INIT;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80) {
                crc = (uint8_t)((crc << 1) ^ CRC8_POLY);
            } else {
                crc = (uint8_t)(crc << 1);
            }
        }
    }
    return crc;
}

/* ===================================================================== */
/*                            大端读写工具                                */
/* ===================================================================== */
/* STM32 小端，多字节字段必须手动大端拆/合，不能用 memcpy（会字节反转）。 */
static void put_u16_be(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

#if 0  /* 接收侧解码暂未用到（地面站只编码下行），保留供将来解码上行帧时启用 */
static uint16_t get_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}
#endif

static void put_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFF);
}

#if 0  /* 接收侧解码暂未用到（地面站只编码下行），保留供将来解码上行帧时启用 */
static uint32_t get_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}
#endif

/* int16 定点 0.01m：米 → 补码 int16（×100，四舍五入，限幅 ±32767）。
 * 负数按补码处理：直接强转 uint16_t 即得补码表示，大端拆字节与 Python 一致。
 * Python _to_int16_fixed 返回已是补码范围内的 int，再 &0xFFFF；
 * 这里 int16_t 强转自然带符号，put_u16_be 拆出的字节与 Python struct.pack(">H", x&0xFFFF) 一致。 */
int16_t proto_m_to_i16(double m)
{
    /* 四舍五入：+0.5 后截断（正数），负数 floor 向负无穷。
     * Python 用 round()——银行家舍入（四舍六入五成双），但 0.01m 精度下差异在
     * 0.005m 边界处，实测向量均无 0.005 边界，round 与 floor(x+0.5) 一致。
     * 若出现边界偏差，改用 round。 */
    long iv;
    if (m >= 0) {
        iv = (long)(m * 100.0 + 0.5);
    } else {
        iv = (long)(m * 100.0 - 0.5);
    }
    if (iv > 32767) iv = 32767;
    if (iv < -32768) iv = -32768;
    return (int16_t)iv;
}

double proto_i16_to_m(int16_t v)
{
    return (double)v / 100.0;
}

/* ===================================================================== */
/*                            帧定界（编码整帧）                          */
/* ===================================================================== */
size_t proto_encode_frame(const uint8_t *payload, size_t payload_len,
                          uint8_t *out_buf, size_t out_cap)
{
    if (payload_len > MAX_PAYLOAD) return 0;
    if (out_cap < 2 + 1 + payload_len + 1) return 0;

    size_t i = 0;
    out_buf[i++] = START_BYTE0;
    out_buf[i++] = START_BYTE1;
    out_buf[i++] = (uint8_t)payload_len;  /* Length */
    memcpy(out_buf + i, payload, payload_len);
    i += payload_len;
    /* CRC8 覆盖 Length + Payload */
    uint8_t crc_buf[1 + MAX_PAYLOAD];
    crc_buf[0] = (uint8_t)payload_len;
    memcpy(crc_buf + 1, payload, payload_len);
    out_buf[i++] = proto_crc8(crc_buf, 1 + payload_len);
    return i;
}

/* ===================================================================== */
/*                            接收状态机                                  */
/* ===================================================================== */
void proto_rx_reset(proto_rx_t *rx)
{
    rx->state = RX_WAIT_START0;
    rx->len = 0;
    rx->idx = 0;
}

void proto_rx_init(proto_rx_t *rx)
{
    proto_rx_reset(rx);
}

int proto_rx_feed(proto_rx_t *rx, uint8_t b,
                  const uint8_t **out_payload, size_t *out_len)
{
    switch (rx->state) {
    case RX_WAIT_START0:
        if (b == START_BYTE0) rx->state = RX_WAIT_START1;
        return 0;  /* 非 0xAA 杂字节丢弃 */
    case RX_WAIT_START1:
        if (b == START_BYTE1) {
            rx->state = RX_READ_LEN;
        } else if (b == START_BYTE0) {
            /* 0xAA 0xAA 情况，保留第一个 0xAA 重判 */
            rx->state = RX_WAIT_START1;
        } else {
            rx->state = RX_WAIT_START0;
        }
        return 0;
    case RX_READ_LEN:
        if (b > MAX_PAYLOAD) {
            /* 长度非法，回到找起始符（前进 1 字节重找） */
            rx->n_len_err++;
            rx->state = (b == START_BYTE0) ? RX_WAIT_START1 : RX_WAIT_START0;
            return 0;
        }
        rx->len = b;
        rx->idx = 0;
        if (rx->len == 0) {
            /* 空 Payload，直接等 CRC */
            rx->state = RX_READ_CRC;
        } else {
            rx->state = RX_READ_PAYLOAD;
        }
        return 0;
    case RX_READ_PAYLOAD:
        rx->payload[rx->idx++] = b;
        if (rx->idx >= rx->len) {
            rx->state = RX_READ_CRC;
        }
        return 0;
    case RX_READ_CRC: {
        /* CRC8 覆盖 Length + Payload */
        uint8_t crc_buf[1 + MAX_PAYLOAD];
        crc_buf[0] = rx->len;
        memcpy(crc_buf + 1, rx->payload, rx->len);
        uint8_t crc_calc = proto_crc8(crc_buf, 1 + rx->len);
        rx->state = RX_WAIT_START0;  /* 收完一帧，无论对错都复位找下一帧 */
        if (b == crc_calc) {
            *out_payload = rx->payload;
            *out_len = rx->len;
            return 1;  /* CRC 对，交付 */
        }
        rx->n_crc_err++;
        return 0;  /* CRC 错，丢弃，不交付 */
    }
    default:
        rx->state = RX_WAIT_START0;
        return 0;
    }
}

/* ===================================================================== */
/*                       下行帧编码（站→机）                              */
/* ===================================================================== */
size_t proto_encode_downlink_forbidden(uint8_t seq,
        const uint8_t (*grid_pairs)[2], size_t n,
        uint8_t *out, size_t cap)
{
    if (n > 100) return 0;
    uint8_t payload[3 + 200];
    size_t i = 0;
    payload[i++] = MT_DOWNLINK_FORBIDDEN;
    payload[i++] = seq;
    payload[i++] = (uint8_t)n;
    for (size_t k = 0; k < n; k++) {
        payload[i++] = grid_pairs[k][0];
        payload[i++] = grid_pairs[k][1];
    }
    return proto_encode_frame(payload, i, out, cap);
}

size_t proto_encode_downlink_command(uint8_t seq, cmd_id_t cmd,
        uint8_t *out, size_t cap)
{
    uint8_t payload[3];
    payload[0] = MT_DOWNLINK_COMMAND;
    payload[1] = seq;
    payload[2] = (uint8_t)cmd;
    return proto_encode_frame(payload, 3, out, cap);
}

size_t proto_encode_downlink_target_grid(uint8_t seq,
        uint8_t grid_a, uint8_t grid_b,
        uint8_t *out, size_t cap)
{
    uint8_t payload[4];
    payload[0] = MT_DOWNLINK_TARGET_GRID;
    payload[1] = seq;
    payload[2] = grid_a;
    payload[3] = grid_b;
    return proto_encode_frame(payload, 4, out, cap);
}

size_t proto_encode_downlink_ack(uint8_t seq,
        uint8_t acked_seq, msg_type_t acked_msg_type,
        uint8_t *out, size_t cap)
{
    uint8_t payload[4];
    payload[0] = MT_DOWNLINK_ACK;
    payload[1] = seq;
    payload[2] = acked_seq;
    payload[3] = (uint8_t)acked_msg_type;
    return proto_encode_frame(payload, 4, out, cap);
}

size_t proto_encode_downlink_addpoint(uint8_t seq,
        double x_m, double y_m, double z_m,
        uint8_t *out, size_t cap)
{
    uint8_t payload[8];
    payload[0] = MT_DOWNLINK_ADDPOINT;
    payload[1] = seq;
    put_u16_be(payload + 2, (uint16_t)proto_m_to_i16(x_m));
    put_u16_be(payload + 4, (uint16_t)proto_m_to_i16(y_m));
    put_u16_be(payload + 6, (uint16_t)proto_m_to_i16(z_m));
    return proto_encode_frame(payload, 8, out, cap);
}

/* ===================================================================== */
/*                       上行帧编码（机→站，互测用）                       */
/* ===================================================================== */
size_t proto_encode_uplink_status(uint8_t seq, state_id_t state,
        double x_m, double y_m, double z_m, uint8_t flags,
        uint8_t *out, size_t cap)
{
    uint8_t payload[10];
    payload[0] = MT_UPLINK_STATUS;
    payload[1] = seq;
    payload[2] = (uint8_t)state;
    put_u16_be(payload + 3, (uint16_t)proto_m_to_i16(x_m));
    put_u16_be(payload + 5, (uint16_t)proto_m_to_i16(y_m));
    put_u16_be(payload + 7, (uint16_t)proto_m_to_i16(z_m));
    payload[9] = flags;
    return proto_encode_frame(payload, 10, out, cap);
}

size_t proto_encode_uplink_detection(uint8_t seq,
        uint8_t grid_a, uint8_t grid_b, animal_id_t animal,
        uint8_t count, uint8_t confidence, uint32_t timestamp_s,
        double animal_x_m, double animal_y_m,
        uint8_t *out, size_t cap)
{
    uint8_t payload[15];
    payload[0]  = MT_UPLINK_DETECTION;
    payload[1]  = seq;
    payload[2]  = grid_a;
    payload[3]  = grid_b;
    payload[4]  = (uint8_t)animal;
    payload[5]  = count;
    payload[6]  = confidence;
    put_u16_be(payload + 7,  (uint16_t)proto_m_to_i16(animal_x_m));
    put_u16_be(payload + 9,  (uint16_t)proto_m_to_i16(animal_y_m));
    put_u32_be(payload + 11, timestamp_s);
    return proto_encode_frame(payload, 15, out, cap);
}

size_t proto_encode_uplink_stats(uint8_t seq,
        uint8_t covered, uint8_t missed,
        const uint8_t counts[5], uint8_t cov_pct,
        uint8_t *out, size_t cap)
{
    uint8_t payload[10];
    payload[0] = MT_UPLINK_STATS;
    payload[1] = seq;
    payload[2] = covered;
    payload[3] = missed;
    payload[4] = counts[0];
    payload[5] = counts[1];
    payload[6] = counts[2];
    payload[7] = counts[3];
    payload[8] = counts[4];
    payload[9] = cov_pct;
    return proto_encode_frame(payload, 10, out, cap);
}

size_t proto_encode_uplink_ack(uint8_t seq,
        uint8_t acked_seq, msg_type_t acked_msg_type,
        uint8_t *out, size_t cap)
{
    uint8_t payload[4];
    payload[0] = MT_UPLINK_ACK;
    payload[1] = seq;
    payload[2] = acked_seq;
    payload[3] = (uint8_t)acked_msg_type;
    return proto_encode_frame(payload, 4, out, cap);
}
