/**
 * comm.h — 可靠链路层（序号 + ACK 重传 + 去重），裸机/RTOS/PC 均可编译。
 *
 * 板无关：发送/取时/临界区通过函数指针注入（uart_send_bytes / now_ms /
 * enter_critical / exit_critical）。板相关（on_frame 分发 → 刷屏/蜂鸣）由上层
 * main.c 实现，本层只管链路。
 *
 * 与 comm_protocol.py ReliableLink 行为对齐：
 *   - pending 按 (SeqNo, MsgType) 复合 key 管理（#54 修问题 1，多帧在途各自独立 ACK）
 *   - 超时 500ms 重传同帧（同 SeqNo），最多 3 次；3 次失败 link_ok=False
 *   - 接收侧 CRC 错丢弃（不回 ACK，让对端超时重传）
 *   - 接收侧重复帧（同 MsgType 同 SeqNo）回 ACK 但不交上层（去重）
 *   - 收到对端 ACK → 清对应 pending，link_ok=True
 *
 * §0E 中断重入修复（M1.5）：所有 TX 经环形待发队列，TX 只在主循环 comm_poll_tx
 * 串行消费（一处调 HAL_UART_Transmit）。中断里 comm_tick 只判超时+重传入队，不 send_fn。
 * 急停=常规尾插（队头插队已删，W3-A/终审 MF-8）。
 *
 * 依赖：proto.h（帧定界/CRC）。不依赖 HAL、不依赖 malloc。
 */
#ifndef GROUND_STATION_COMM_H
#define GROUND_STATION_COMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "proto.h"

/* ====== 链路参数（与 Python ReliableLink 默认一致）====== */
#define COMM_ACK_TIMEOUT_MS   500    /* 等 ACK 超时（ms） */
#define COMM_MAX_RETRIES      3      /* 最大重传次数（不含首传） */
#define COMM_MAX_PENDING      8      /* 同时等 ACK 的帧上限（下行少，8 够） */
#define COMM_RX_GAP_MS        50     /* M04-F-03：帧内静默 > 此值即丢弃半帧（见 comm.c 说明）*/
#define MAX_TXQ               16     /* 待发队列深度（协议契约：与 H743 一致；注册表突发 50 帧）*/
#define COMM_LINK_OK_TRUE     1
#define COMM_LINK_OK_FALSE    0

/* pending 表项：登记一帧等 ACK。key = (seq, msg_type) 复合。 */
typedef struct {
    uint8_t  in_use;          /* 0=空槽 1=占用 */
    uint8_t  frame[MAX_FRAME];/* 整帧字节（重传用） */
    size_t   frame_len;
    uint8_t  seq;
    uint8_t  msg_type;
    uint8_t  retries;         /* 已重传次数（不含首传） */
    uint32_t deadline_ms;     /* 下次重传/判失败时刻（ms） */
    uint32_t enq_ms;          /* M04-F-05：登记时刻（pending 满时逐最老用）*/
} comm_pending_t;

/* 链路实例 */
typedef struct {
    /* 发送/取时/临界区钩子（上层注入） */
    void   (*send_fn)(const uint8_t *data, size_t len);
    uint32_t (*now_ms_fn)(void);
    void   (*enter_critical)(void);   /* NULL=不保护（PC 单线程测试）*/
    void   (*exit_critical)(void);
    /* 状态 */
    comm_pending_t pending[COMM_MAX_PENDING];
    uint8_t last_seen_seq[256];  /* 按 msg_type 索引上次 seq（配合 last_seen_ok 位图去重）*/
    uint8_t last_seen_ok[32];    /* 位图：该 msg_type 是否已见过（R1 P1：原 int8_t 存 0..255 会溢出）*/
    int     link_ok;             /* 1=通 0=断 */
    /* 断链回调（上层实现：LED/蜂鸣/屏显 EMERGENCY）*/
    void   (*on_link_down)(void);
    uint8_t next_seq;            /* 本端发送序号计数器（0-255 循环） */
    /* proto 接收状态机（逐字节喂） */
    proto_rx_t rx;
    /* M11-F-08：最近一次收到的 ACK（上层 clamp/回执观测；不改 ARQ 行为）*/
    uint8_t  ack_valid;
    uint8_t  ack_seq, ack_mt, ack_status;
    float    ack_ret;
    /* M04-F-03：帧内静默时间戳（上层检测到半帧+静默时丢弃半帧）*/
    uint32_t rx_last_ms;
    uint8_t  rx_gap_seen;
    uint32_t rx_gap_resets;      /* 半帧丢弃次数（linkstat/P6 统计用）*/
    uint32_t pending_evict;      /* M04-F-05：pending 满被逐出的在途帧数（可观测）*/
    uint32_t dup_cnt;            /* M11-F-10：重复帧（幂等重传）计数 */
    uint32_t retry_cnt;          /* M11-F-10：累计重传次数 */
    uint32_t txq_drop;           /* M11-F-10：tx 环满被挤出丢弃的帧数 */
    /* —— §0E 待发队列（环形，TX 串行化）—— */
    uint8_t  tx_queue[MAX_TXQ][MAX_FRAME];
    uint16_t tx_len[MAX_TXQ];
    volatile uint8_t tx_head;   /* 消费索引（主循环 comm_poll_tx / push_front）*/
    volatile uint8_t tx_tail;   /* 生产索引（中断 comm_tick / 主循环 comm_send）*/
    volatile uint8_t tx_count;  /* 队列内帧数 */
} comm_link_t;


/* ====== API ====== */

/**
 * 初始化链路实例。send_fn/now_ms_fn 必须非空。
 * on_link_down 可空（NULL=断链不通知）。
 * enter_critical/exit_critical 可空（NULL=不保护，PC 单线程用；MCU 必须给
 * __disable_irq/__enable_irq 以保护 tx 队列索引的读改写竞态）。
 */
void comm_init(comm_link_t *L,
               void (*send_fn)(const uint8_t *data, size_t len),
               uint32_t (*now_ms_fn)(void),
               void (*on_link_down)(void),
               void (*enter_critical)(void),
               void (*exit_critical)(void));

/**
 * 取下一个本端序号（0-255 循环）。给 proto_encode_* 的 seq 参数用。
 */
uint8_t comm_next_seq(comm_link_t *L);

/**
 * 发送一整帧并登记 pending 等 ACK。返回 SeqNo。
 * frame_bytes 必须已是完整帧（proto_encode_* 产出），frame[4]=SeqNo。
 * msg_type = 该帧类型（MT_*），用于 pending 复合 key。
 * §0E：首传经 txq 入队（不直接 send_fn），主循环 comm_poll_tx 串行消费。
 */
uint8_t comm_send(comm_link_t *L, const uint8_t *frame_bytes, size_t len, uint8_t msg_type);
/* R1 P1-02：免 ARQ 发送（只入 txq，不登记 pending）——用于 0x20/0x21 快照 */
uint8_t comm_send_noarq(comm_link_t *L, const uint8_t *frame_bytes, size_t len);

/**
 * §0E：帧入待发队列尾部（不登记 pending）。主循环 STATUS/命令首发、ISR 回 ACK 用。
 * 队列满则丢最老。
 */
void comm_txq_push(comm_link_t *L, const uint8_t *frame, size_t len);

/**
 * §0E.2：帧插到待发队列头部（急停优先，不登记 pending，不等 ACK）。
 * 队列满也丢最老腾位。急停直发专用。
 */

/**
 * §0E：主循环唯一 TX 消费点。队列非空则取队头发一帧（阻塞 send_fn）。
 * HAL_UART_Transmit 只在此处发生，不与中断并发。
 */
void comm_poll_tx(comm_link_t *L);

/**
 * 定时器 tick（裸机 100ms TIM 中断或主循环调）。遍历 pending：
 *   超时未达 max_retries → 重传入队（§0E，不 send_fn）
 *   已达 max_retries → 清该 pending；若清完 link_ok=False + 调 on_link_down
 */
void comm_tick(comm_link_t *L);

/**
 * 收到对端 ACK 帧时调。按 (acked_seq, acked_mt) 清对应 pending，link_ok=True。
 * 返回 1=匹配到并清掉，0=没匹配。
 */
int comm_on_ack(comm_link_t *L, uint8_t acked_seq, uint8_t acked_mt);
/* M11-F-08：最近一次收到的 ACK（只读观测，不改 ARQ 行为）。返回 1=有新 ACK 可读；
 * 0x90 的 ret 字段可解析，老式 3 字节 ACK 的 ret=0。*/
int comm_last_ack(const comm_link_t *L, uint8_t *seq, uint8_t *mt,
                  uint8_t *status, float *ret);

/**
 * 接收侧：喂一个字节。若凑成完整且 CRC 正确的帧：
 *   - 是 ACK 帧 → 调 comm_on_ack，不交上层，返回 0
 *   - 是重复帧（同 msg_type 同 seq）→ 标记 is_dup=1（上层应回 ACK 但不处理），返回 0
 *   - 是新帧 → 填 out_payload/out_len，返回 1（上层处理 + 回 ACK）
 *   - 还在收/CRC 错/杂字节 → 返回 0，不填
 *
 * 上层用法：
 *   const uint8_t *pl; size_t plen; int dup;
 *   if (comm_rx_feed_byte(&L, b, &pl, &plen, &dup)) on_frame(pl, plen);
 *   else if (dup) send_ack(pl[1], pl[0]);  // 重复帧也回 ACK（pl 指向上次的 payload）
 */
int comm_rx_feed_byte(comm_link_t *L, uint8_t b,
                      const uint8_t **out_payload, size_t *out_len, int *out_is_dup);

/**
 * 取最近一次"重复帧"的 payload 指针（上层回 ACK 用）。去重命中时返回 1 并填 seq/mt。
 */
int comm_last_dup_info(comm_link_t *L, uint8_t *out_seq, uint8_t *out_mt);

/**
 * 当前 pending 数（调试用）。
 */
int comm_pending_count(const comm_link_t *L);

/**
 * 当前待发队列帧数（§0E 压测验收用，probe-rs 读 tx_count 判 TX 是否卡死）。
 */
int comm_txq_count(const comm_link_t *L);

#endif /* GROUND_STATION_COMM_H */
