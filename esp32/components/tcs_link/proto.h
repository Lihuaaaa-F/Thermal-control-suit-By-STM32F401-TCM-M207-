/**
 * proto.h — H 题上下行通信协议 STM32 端参考实现（与 Python comm_protocol.py 字节级一致）
 *
 * 配套文档：01_协议规范/通信协议.md
 * 配套 Python 参考：02_Python参考实现/comm_protocol.py
 *
 * 设计目标：
 *   - 零外部依赖，仅 <stdint.h> <string.h>，裸机/RTOS/PC 均可编译
 *   - 大端字节序手动处理（STM32 小端，不能直接 memcpy 多字节字段）
 *   - 不用 malloc（接收状态机用定长缓冲）
 *   - 提供 self_test()，PC 上 `gcc proto.c proto_test.c -o pt && ./pt` 先验证字节级一致再移植
 *
 * 帧格式（与通信协议.md §1 一致）：
 *   | 0xAA | 0x55 | Length(1B) | Payload(Length B) | CRC8(1B) |
 *   Length = Payload 字节数（不含起始符/Length/CRC），0-250
 *   CRC8 多项式 0x07（CRC-8/CCITT），初始 0x00，覆盖 Length + Payload
 *
 * Payload 首字节 = MsgType，第二字节 = SeqNo(0-255 循环)。
 */
#ifndef GROUND_STATION_PROTO_H
#define GROUND_STATION_PROTO_H

#include <stdint.h>
#include <stddef.h>

/* ====== 帧定界常量 ====== */
#define START_BYTE0     0xAA
#define START_BYTE1     0x55
#define MAX_PAYLOAD     250     /* Payload 上限（整帧 ≤253B） */
#define MAX_FRAME       (2 + 1 + MAX_PAYLOAD + 1)  /* 起始符2+Len1+Payload+CRC1 = 253 */

/* ====== CRC8 多项式（CRC-8/CCITT）====== */
#define CRC8_POLY       0x07
#define CRC8_INIT       0x00

/* ====== 帧类型（Payload 首字节 = MsgType）====== */
typedef enum {
    MT_UPLINK_STATUS       = 0x01,  /* 机→站 状态心跳 */
    MT_UPLINK_DETECTION    = 0x02,  /* 机→站 单次识别 */
    MT_UPLINK_STATS        = 0x03,  /* 机→站 累计统计 */
    MT_UPLINK_ACK          = 0x04,  /* 机→站 确认下行 */
    MT_DOWNLINK_FORBIDDEN  = 0x81,  /* 站→机 禁飞格 */
    MT_DOWNLINK_COMMAND    = 0x82,  /* 站→机 命令 */
    MT_DOWNLINK_TARGET_GRID= 0x83,  /* 站→机 目标格 */
    MT_DOWNLINK_ACK        = 0x84,  /* 站→机 确认上行 */
    MT_DOWNLINK_ADDPOINT   = 0x85,  /* 站→机 临场加航点 */
} msg_type_t;

/* ====== 任务命令 ID（DOWNLINK_COMMAND 的 CmdID 字节）====== */
/* 与 comm_protocol.py CMD_ID_TO_STR、task_state_machine.command_callback 一致 */
typedef enum {
    CMD_START     = 1,
    CMD_PAUSE     = 2,
    CMD_RESUME    = 3,
    CMD_STOP      = 4,
    CMD_ABORT     = 5,
    CMD_RESET     = 6,
    CMD_SURVEY    = 7,
    CMD_DETECT    = 8,
    CMD_LASER     = 9,
    CMD_PITCH_LAND= 10,
    CMD_RETURN    = 11,
    CMD_KILL      = 12,
} cmd_id_t;

/* ====== 状态机状态 ID（UPLINK_STATUS 的 StateID 字节）====== */
/* 与 comm_protocol.py STATE_NAME_TO_ID、task_state_machine.TaskState 一致 */
typedef enum {
    STATE_IDLE       = 0,
    STATE_TAKEOFF    = 1,
    STATE_HOVER      = 2,
    STATE_TRANSIT    = 3,
    STATE_DELIVER    = 4,
    STATE_PATROL     = 5,
    STATE_RETURN     = 6,
    STATE_LAND       = 7,
    STATE_EMERGENCY  = 8,
    STATE_SURVEY     = 9,
    STATE_DETECT     = 10,
    STATE_LASER_POINT= 11,
    STATE_PITCH_LAND = 12,
} state_id_t;

/* ====== UPLINK_STATUS Flags 位定义 ====== */
#define FLAG_SURVEY_COMPLETED  0x01
#define FLAG_DETECT_DONE       0x02
#define FLAG_LASER_ALIGNED     0x04
#define FLAG_EMERGENCY         0x08

/* ====== 动物类别 ID（UPLINK_DETECTION 的 AnimalClass 字节）====== */
/* 与 comm_protocol.py ANIMAL_ID_TO_STR、match.yaml animal_classes 顺序一致 */
typedef enum {
    ANIMAL_ELEPHANT = 1,
    ANIMAL_TIGER    = 2,
    ANIMAL_WOLF     = 3,
    ANIMAL_MONKEY   = 4,
    ANIMAL_PEACOCK  = 5,
} animal_id_t;


/* ===================================================================== */
/*                              CRC8 函数                                 */
/* ===================================================================== */
/**
 * CRC-8/CCITT（多项式 0x07，初始 0x00），覆盖 data[0..len-1]。
 * 与通信协议.md §1 的 C 版逐位实现、Python comm_protocol.crc8 完全一致。
 * 已知向量：crc8("123456789")=0xF4，crc8("")=0x00，crc8(0x00)=0x00。
 */
uint8_t proto_crc8(const uint8_t *data, size_t len);


/* ===================================================================== */
/*                            帧定界层（编/解码整帧）                      */
/* ===================================================================== */
/**
 * 编一整帧：起始符 + Length + Payload + CRC8。
 * payload_len 必须 0..MAX_PAYLOAD。返回整帧字节数（=3+payload_len+1）。
 * out_buf 至少 MAX_FRAME 字节。
 * CRC8 覆盖 Length + Payload。
 */
size_t proto_encode_frame(const uint8_t *payload, size_t payload_len,
                          uint8_t *out_buf, size_t out_cap);


/* ===================================================================== */
/*                          接收状态机（帧定界）                          */
/* ===================================================================== */
/**
 * 裸机串口收字节状态机：找起始符 → 读 Length → 收 Payload → 校验 CRC → 交付。
 *
 * 用法（串口中断或轮询逐字节喂）：
 *   proto_rx_t rx;
 *   proto_rx_init(&rx);
 *   for (...) {
 *       uint8_t b = uart_rx_byte();
 *       const uint8_t *payload; size_t len;
 *       if (proto_rx_feed(&rx, b, &payload, &len)) {
 *           // 收到一帧，payload[0]=MsgType，len=Payload 长度
 *           proto_handle_payload(payload, len);
 *       }
 *   }
 *
 * 状态机内自动跳过杂字节、丢弃 CRC 错帧（不交付），与 Python decode_frame 一致。
 */
typedef enum {
    RX_WAIT_START0,     /* 找 0xAA */
    RX_WAIT_START1,     /* 找 0x55 */
    RX_READ_LEN,         /* 读 Length */
    RX_READ_PAYLOAD,     /* 收 Payload */
    RX_READ_CRC,         /* 读 CRC */
} proto_rx_state_t;

typedef struct {
    proto_rx_state_t state;
    uint8_t  len;             /* 本帧 Payload 长度 */
    uint8_t  payload[MAX_PAYLOAD];
    size_t   idx;             /* 当前写入位置 */
    /* M11-F-10：接收侧故障分桶（只观测，不改变状态机行为）*/
    uint32_t n_crc_err;       /* CRC 不符 */
    uint32_t n_len_err;       /* Length 字段非法（>MAX_PAYLOAD）*/
} proto_rx_t;

void proto_rx_init(proto_rx_t *rx);

/**
 * 喂一个字节。若凑成完整且 CRC 正确的帧，返回 1 并把 payload/len 填出；
 * 否则返回 0（还在收/跳杂字节/CRC 错丢弃）。
 */
int proto_rx_feed(proto_rx_t *rx, uint8_t b,
                  const uint8_t **out_payload, size_t *out_len);

/**
 * M04-F-03：丢弃当前半帧，回到"找起始符"。
 * 上层检测到帧内长时间静默（噪声/截断后线路恢复）时调用——否则状态机会把
 * 后续有效帧当作本帧 payload 吞掉（Len 被噪声放大时最多吞 250 字节）。
 */
void proto_rx_reset(proto_rx_t *rx);


/* ===================================================================== */
/*                         int16 定点 0.01m 编解码                        */
/* ===================================================================== */
/* 米 → int16 定点（×100，四舍五入，限幅 ±32767）。
 * 负数补码 int16 编码，返回值可直接大端拆字节存帧。
 * 与 Python _to_int16_fixed / encode_uplink_status.to_int16 一致。 */
int16_t proto_m_to_i16(double m);

/* int16 定点 → 米（/100）。与 Python _s16(v)/100 一致。 */
double proto_i16_to_m(int16_t v);


/* ===================================================================== */
/*                          各帧类型 编码（站→机 下行）                     */
/* ===================================================================== */
/* DOWNLINK_FORBIDDEN (0x81)。grid_pairs = [(a,b),...]，n 个，n≤100。 */
size_t proto_encode_downlink_forbidden(uint8_t seq,
        const uint8_t (*grid_pairs)[2], size_t n,
        uint8_t *out, size_t cap);

/* DOWNLINK_COMMAND (0x82)。cmd 见 CMD_* 枚举。 */
size_t proto_encode_downlink_command(uint8_t seq, cmd_id_t cmd,
        uint8_t *out, size_t cap);

/* DOWNLINK_TARGET_GRID (0x83)。 */
size_t proto_encode_downlink_target_grid(uint8_t seq,
        uint8_t grid_a, uint8_t grid_b,
        uint8_t *out, size_t cap);

/* DOWNLINK_ACK (0x84)。确认收到上行帧。 */
size_t proto_encode_downlink_ack(uint8_t seq,
        uint8_t acked_seq, msg_type_t acked_msg_type,
        uint8_t *out, size_t cap);

/* DOWNLINK_ADDPOINT (0x85)。临场加航点，坐标米。 */
size_t proto_encode_downlink_addpoint(uint8_t seq,
        double x_m, double y_m, double z_m,
        uint8_t *out, size_t cap);


/* ===================================================================== */
/*                          各帧类型 编码（机→站 上行）                     */
/* ===================================================================== */
/* 地面站一般不发上行帧，但互测时可能需要模拟机载端发上行帧验证对端解析，故一并提供。 */

/* UPLINK_STATUS (0x01)。位置米，flags 见 FLAG_* 位。 */
size_t proto_encode_uplink_status(uint8_t seq, state_id_t state,
        double x_m, double y_m, double z_m, uint8_t flags,
        uint8_t *out, size_t cap);

/* UPLINK_DETECTION (0x02)。animal 见 ANIMAL_* 枚举；timestamp 自起飞秒。 */
size_t proto_encode_uplink_detection(uint8_t seq,
        uint8_t grid_a, uint8_t grid_b, animal_id_t animal,
        uint8_t count, uint8_t confidence, uint32_t timestamp_s,
        double animal_x_m, double animal_y_m,
        uint8_t *out, size_t cap);

/* UPLINK_STATS (0x03)。counts[5] 顺序：elephant/tiger/wolf/monkey/peacock。 */
size_t proto_encode_uplink_stats(uint8_t seq,
        uint8_t covered, uint8_t missed,
        const uint8_t counts[5], uint8_t cov_pct,
        uint8_t *out, size_t cap);

/* UPLINK_ACK (0x04)。确认收到下行帧。 */
size_t proto_encode_uplink_ack(uint8_t seq,
        uint8_t acked_seq, msg_type_t acked_msg_type,
        uint8_t *out, size_t cap);


/* ===================================================================== */
/*                            自检（PC 编译验证）                          */
/* ===================================================================== */
/**
 * 用已知向量验证 CRC8 + 各帧编解码字节级与 Python comm_protocol.py 一致。
 * 返回通过用例数（见 proto_test.c 对照表）。
 * 嵌入式同学移植后，先在 PC 上 `gcc proto.c proto_test.c && ./a.out` 跑通再上板。
 */
int proto_self_test(void);

#endif /* GROUND_STATION_PROTO_H */
