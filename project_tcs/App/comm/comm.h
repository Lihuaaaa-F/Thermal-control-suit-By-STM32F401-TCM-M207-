/**
 * comm.h — 可靠链路层（序号 + ACK 重传 + 去重），裸机/RTOS/PC 均可编译。
 *
 * 板无关：发送/取时/临界区通过函数指针注入（send_fn / now_ms /
 * enter_critical / exit_critical）。板相关（on_frame 分发 → 刷屏/蜂鸣）由上层
 * 实现（本端是 Core/Src/main.c），本层只管链路。
 *
 * 与 comm_protocol.py ReliableLink 行为对齐：
 *   - pending 按 (SeqNo, MsgType) 复合 key 管理（#54 修问题 1，多帧在途各自独立 ACK）
 *   - 超时 500ms 重传同帧（同 SeqNo），最多 3 次；3 次失败 link_ok=False
 *   - 接收侧 CRC 错丢弃（不回 ACK，让对端超时重传）
 *   - 接收侧重复帧（同 MsgType 同 SeqNo）回 ACK 但不交上层（去重）
 *   - 收到对端 ACK → 清对应 pending，link_ok=True
 *   - 判活单一口径（M04-F-06）：对端发来的任何一帧合法帧都把 link_ok 置真，
 *     由假转真时调一次 on_link_up
 *
 * ====== 待发队列的生产模型（M04-F-07 定论，2026-11-25）======
 * H743 这一份：**仅主循环生产，没有 ISR**。
 *   - 生产：comm_send / comm_txq_push / comm_tick（重传入队）——只在主循环里调用；
 *   - 消费：comm_poll_tx——只在主循环里调用（唯一一处 HAL_UART_Transmit）。
 *   - 中断服务程序不碰 comm 层、不碰待发队列：USART2 的 ISR 只把收到的单字节推进
 *     main.c 的 RX 环形缓冲（link_rx_isr_byte），主循环再从那里取字节喂 comm_rx_feed_byte。
 * 所以 tx_head / tx_tail / tx_count 的读改写只在一个上下文里发生，enter/exit_critical
 * 在本端只是防御性的。
 * ⚠ 不要把 comm_tick 挪进定时器中断。那样队列就变成两个上下文生产，而 comm_poll_tx 是
 *   "先取队头、发完再前移"：ISR 里的重传入队在队满时会把 head 前移并覆盖正在发送的槽，
 *   主循环回到 comm_poll_tx 后再前移一次 head、再减一次 tx_count——那一帧从没发出就被跳过
 *   （审查 M04-F-07 的交错推演）。真要接 ISR，必须先把队列改成单生产者 / 单消费者的索引
 *   协议（或两个索引都用原子操作），再改这段说明。
 * 急停 = 常规尾插（队头插队已删，W3-A / 终审 MF-8）。
 *
 * 依赖：proto.h（帧定界 / CRC）、tcs_proto.h（方向闸门的判定）。不依赖 HAL、不依赖 malloc。
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
/* X01-F-05（2026-11-26）：待发队列深度进协议契约——值只在 protocol/tcs_proto.h 里写一次
 * （TCS_MAX_TXQ），两端 comm.h 都从它取，下面两条断言把"两端一致"和"装得下整批重传"钉住。
 * 容量模型（16 = 8 条等 ACK 在途 + 8 条免 ARQ 周期帧与应答）见协议头的 TCS_MAX_TXQ 注释。*/
#include "tcs_proto.h"       /* TCS_MAX_TXQ（协议唯一源）*/
#define MAX_TXQ               TCS_MAX_TXQ
_Static_assert(MAX_TXQ == TCS_MAX_TXQ, "MAX_TXQ must follow protocol/tcs_proto.h TCS_MAX_TXQ");
_Static_assert(MAX_TXQ >= COMM_MAX_PENDING + 8, "MAX_TXQ must hold a full retransmission burst + 8 frames");
#define COMM_LINK_OK_TRUE     1
#define COMM_LINK_OK_FALSE    0
/* R5-01：回声按消息号分计的桶数 = 消息号去掉方向位（bit6）后的取值范围 0x00..0xBF */
#define COMM_MT_BUCKETS       192
/* X03-F-09：队头那一帧连续多少次没能上线就丢弃（免得一个卡死的发送端把整条队列堵死）*/
#define COMM_TX_MAX_FAIL      3

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
    /* X01-F-03（2026-11-26）：应答的业务结果。原来 0x90 的 status/ret 在 comm 层就被丢掉了，
     * BAD_ID / CLAMP / BUSY 一律被当成功。现在清 pending 之前先落到这里（in_use=0 之后仍可读，
     * 直到该槽被下一帧复用），并通过 on_ack_result 回调交给上层。*/
    uint8_t  ack_status;      /* 最近一次清掉本槽的应答 status（已按 TCS_ACK_STATUS_MASK 取值）*/
    uint8_t  ack_have;        /* 1 = 本槽自登记以来收到过匹配的应答 */
    float    ack_ret;         /* 应答里的 ret（SET_PARAM 回实际写入值）*/
} comm_pending_t;

/* 链路实例 */
typedef struct {
    /* 发送/取时/临界区钩子（上层注入）。
     * send_fn 返回 0 = 这一帧已上线；非 0 = 没有上线（HAL 返回 BUSY / ERROR / TIMEOUT）。
     * X03-F-09（2026-11-25）：原来是 void——comm_poll_tx 发完就前移队头，HAL_UART_Transmit
     * 返回 HAL_BUSY 的那一帧（例如句柄正被诊断用的中断发送占着）被消费掉了却从没上过线。 */
    int    (*send_fn)(const uint8_t *data, size_t len);
    uint32_t (*now_ms_fn)(void);
    void   (*enter_critical)(void);   /* NULL=不保护（PC 单线程用；本端只有主循环动队列，见文件头）*/
    void   (*exit_critical)(void);
    /* 状态 */
    comm_pending_t pending[COMM_MAX_PENDING];
    uint8_t last_seen_seq[256];  /* 按 msg_type 索引上次 seq（配合 last_seen_ok 位图去重）*/
    uint8_t last_seen_ok[32];    /* 位图：该 msg_type 是否已见过（R1 P1：原 int8_t 存 0..255 会溢出）*/
    int     link_ok;             /* 1=通 0=断（ARQ 判死 + 任何对端合法帧复位，见 comm.c 的 comm_note_peer）*/
    /* 断链 / 恢复回调（上层实现：LED/蜂鸣/屏显/日志）*/
    void   (*on_link_down)(void);
    void   (*on_link_up)(void);  /* M04-F-06：link_ok 由假转真时调一次；comm_set_on_link_up 设定，可空 */
    uint8_t next_seq;            /* 本端发送序号计数器（0-255 循环） */
    /* proto 接收状态机（逐字节喂） */
    proto_rx_t rx;
    /* M04-F-03：帧内静默时间戳（上层检测到半帧+静默时丢弃半帧）*/
    uint32_t rx_last_ms;
    uint8_t  rx_gap_seen;
    uint32_t rx_gap_resets;      /* 半帧丢弃次数（linkstat/P6 统计用）*/
    uint32_t pending_evict;      /* M04-F-05：pending 满被逐出的在途帧数（可观测）*/
    uint32_t dup_cnt;            /* M11-F-10：重复帧（幂等重传）计数 */
    uint32_t retry_cnt;          /* M11-F-10：累计重传次数 */
    uint32_t txq_drop;           /* M11-F-10：tx 环满被挤出丢弃的帧数（X03-F-09：也计连续发送失败被丢弃的帧）*/
    uint32_t tx_fail_cnt;        /* X03-F-09：send_fn 报"没有上线"的次数 */
    uint8_t  tx_fail_streak;     /* X03-F-09：队头这一帧已经连续失败几次 */
    /* —— X01-F-03：应答的业务结果（status/ret 不再被丢弃）—— */
    uint32_t ack_nonok_cnt;      /* status != TCS_ACK_OK 的应答数（含未匹配到 pending 的）*/
    uint8_t  last_ack_mt;        /* 最近一条应答确认的消息号（0 = 还没收到过应答）*/
    uint8_t  last_ack_status;
    float    last_ack_ret;
    uint8_t  last_ack_have;
    /* 匹配到 pending 时调用（在临界区之外，回调里可以安全再调 comm_*）。
     * 参数 = 被确认的 (seq, mt) + 应答的业务结果。NULL = 不通知。*/
    void   (*on_ack_result)(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret);
    /* —— §0E 待发队列（环形，TX 串行化；生产模型见文件头）—— */
    uint8_t  tx_queue[MAX_TXQ][MAX_FRAME];
    uint16_t tx_len[MAX_TXQ];
    volatile uint8_t tx_head;   /* 消费索引（只有主循环的 comm_poll_tx 改）*/
    volatile uint8_t tx_tail;   /* 生产索引（只有主循环的 comm_send / comm_txq_push / comm_tick 改）*/
    volatile uint8_t tx_count;  /* 队列内帧数 */
    /* —— R5-01 方向闸门（2026-11-25）——判定是 protocol/tcs_proto.h 的纯函数 tcs_rx_verdict，
     * 两端 comm.c 同用一份逻辑；这里只是它的输入与计数。见 comm.c 的 comm_rx_gate。*/
    uint8_t  self_side;         /* 本端身份：TCS_SIDE_H743 / TCS_SIDE_ESP32（comm_init 设定）*/
    uint32_t echo_cnt;          /* 本端发出的帧被回送、按方向位丢弃的总数（stat echo=）*/
    uint32_t wrongdir_cnt;      /* 方向位说是对端、但方向表不允许的消息号（含 ACK 确认了本端不会发的消息号）*/
    uint32_t tx_wrongdir_cnt;   /* 自检：本端发出了方向表不允许自己发的消息号（编程错误，应为 0）*/
    uint16_t echo_by_mt[COMM_MT_BUCKETS];   /* 回声按消息号分计（下标 = 消息号去掉方向位）*/
} comm_link_t;


/* ====== API ====== */

/**
 * 初始化链路实例。send_fn/now_ms_fn 必须非空。
 * on_link_down 可空（NULL=断链不通知）；恢复回调用 comm_set_on_link_up 单独设。
 * enter_critical/exit_critical 可空（NULL=不保护，PC 单线程用）。本端只有主循环动队列，
 * 这两个钩子只是防御性的；要给就用"保存-恢复 PRIMASK"的那一对（X03-F-10），不要裸
 * __disable_irq / __enable_irq。
 * self_side = 本端身份（TCS_SIDE_H743 / TCS_SIDE_ESP32，R5-01）：发出的帧在出队时打上
 * 这一侧的方向位；收到的帧里方向位等于本端的，是本端回送，任何处理之前丢弃。
 * 其它取值一律按 TCS_SIDE_H743 处理。
 */
void comm_init(comm_link_t *L,
               int (*send_fn)(const uint8_t *data, size_t len),
               uint32_t (*now_ms_fn)(void),
               void (*on_link_down)(void),
               void (*enter_critical)(void),
               void (*exit_critical)(void),
               uint8_t self_side);

/**
 * M04-F-06：注册"链路恢复"回调。link_ok 由假转真时调用一次。
 * NULL = 不通知。
 */
void comm_set_on_link_up(comm_link_t *L, void (*cb)(void));

/**
 * 取下一个本端序号（0-255 循环）。给 proto_encode_* 的 seq 参数用。
 */
uint8_t comm_next_seq(comm_link_t *L);

/**
 * 发送一整帧并登记 pending 等 ACK。
 * 返回 0 = 已登记并入队；-1 = 长度不合法（< 5 或 > MAX_FRAME），什么都没做。
 * out_seq 可为 NULL；不为 NULL 时写入本帧的序号（= frame[4]）。
 * M04-F-04（2026-11-25）：原先返回序号、失败返回 0xFF——序号 255 也是合法值，
 * 第 256 帧会被调用方当成"失败"（屏端 m207_redirect_send 因此回落到本地 M207 写）。
 * 调用方按返回值是否为 0 判成败，不要看序号。
 * frame_bytes 必须已是完整帧（proto_encode_* 产出），frame[4]=SeqNo。
 * msg_type = 该帧类型（MT_*），用于 pending 复合 key。
 * §0E：首传经 txq 入队（不直接 send_fn），由 comm_poll_tx 串行消费。
 */
int comm_send(comm_link_t *L, const uint8_t *frame_bytes, size_t len, uint8_t msg_type,
              uint8_t *out_seq);

/**
 * §0E：帧入待发队列尾部（不登记 pending）。应答帧、快照类周期帧用。队列满则丢最老。
 */
void comm_txq_push(comm_link_t *L, const uint8_t *frame, size_t len);

/**
 * 唯一 TX 消费点：队列非空则取队头发一帧（阻塞的 HAL_UART_Transmit 只在此处发生）。
 * X03-F-09：send_fn 返回非 0（这一帧没有上线）时**不前移队头**，下一拍重试；
 * 队头同一帧连续 COMM_TX_MAX_FAIL 次失败才丢弃它（计 txq_drop）并取下一帧。
 * 每次失败都计 tx_fail_cnt。本端的发送是阻塞调用，失败（HAL_BUSY / 超时）是暂态的，
 * 所以"留在队里重试"是对的；ESP32 那一份是非阻塞语义，做法不同（见那边的 comm.h）。
 */
void comm_poll_tx(comm_link_t *L);

/**
 * 定时 tick（本端由主循环每拍调）。遍历 pending：
 *   超时未达 max_retries → 重传入队（§0E，不 send_fn）
 *   已达 max_retries → 清该 pending；全部失败 → link_ok=False + 调 on_link_down
 */
void comm_tick(comm_link_t *L);

/**
 * 收到对端 ACK 帧时调。按 (acked_seq, acked_mt) 清对应 pending，link_ok=True。
 * 返回 1=匹配到并清掉，0=没匹配。
 *
 * X01-F-03：本函数是"只有链路保活信息"的旧入口，等价于 status = TCS_ACK_OK、ret = 0。
 * 真实应答请用 comm_on_ack_ex（comm.c 收帧路径已改用它），业务结果由它交给上层。
 */
int comm_on_ack(comm_link_t *L, uint8_t acked_seq, uint8_t acked_mt);

/**
 * X01-F-03：带业务结果的应答入口（comm.c 的收帧路径调用）。
 *   - status 已按 TCS_ACK_STATUS_MASK 掩过；非 TCS_ACK_OK 计入 ack_nonok_cnt（无论是否匹配）；
 *   - 匹配到 pending 时：结果写进该条目（ack_status / ack_ret / ack_have）再清 in_use，
 *     link_ok 置真，并在临界区之外调用 on_ack_result（若已注册）；
 *   - 返回 1 = 匹配到并清掉，0 = 没匹配（仍然记了 last_ack_* 与非 OK 计数）。
 */
int comm_on_ack_ex(comm_link_t *L, uint8_t acked_seq, uint8_t acked_mt,
                   uint8_t status, float ret);

/**
 * X01-F-03：注册"应答结果"回调（匹配到 pending 时调用，临界区之外）。
 * NULL = 不通知。callback 里可以再调 comm_*。
 */
void comm_set_on_ack_result(comm_link_t *L,
                            void (*cb)(uint8_t acked_seq, uint8_t acked_mt,
                                       uint8_t status, float ret));

/**
 * X01-F-03：取最近一条应答的业务结果（排障/上报用）。
 * 返回 1 = 自上一次调用以来收到过应答（读后清标记），0 = 没有新的。
 * out_* 都可以为 NULL。
 */
int comm_last_ack(comm_link_t *L, uint8_t *out_mt, uint8_t *out_status, float *out_ret);

/**
 * 接收侧：喂一个字节。若凑成完整且 CRC 正确的帧：
 *   - 方向位说是本端（回送）或方向表不允许 → 丢弃，返回 0，什么都不碰（R5-01）
 *   - 是 ACK 帧 → 调 comm_on_ack，不交上层，返回 0
 *   - 是重复帧（同 msg_type 同 seq）→ 标记 is_dup=1（上层应回 ACK 但不处理），返回 0
 *   - 是新帧 → 填 out_payload/out_len，返回 1（上层处理 + 回 ACK）；payload[0] 是去掉方向位的消息号
 *   - 还在收/CRC 错/杂字节 → 返回 0，不填
 * 任何放行的帧（ACK / 重复 / 新帧）都会把 link_ok 置真（M04-F-06）。
 *
 * 上层用法：
 *   const uint8_t *pl; size_t plen; int dup;
 *   if (comm_rx_feed_byte(&L, b, &pl, &plen, &dup)) on_frame(pl, plen);
 *   else if (dup) send_ack(pl[1], pl[0]);  // 重复帧也回 ACK
 */
int comm_rx_feed_byte(comm_link_t *L, uint8_t b,
                      const uint8_t **out_payload, size_t *out_len, int *out_is_dup);

/**
 * 取最近一次"重复帧"的 seq / 消息号（上层回 ACK 用）。去重命中时返回 1 并填 seq/mt。
 */
int comm_last_dup_info(comm_link_t *L, uint8_t *out_seq, uint8_t *out_mt);

/**
 * 当前 pending 数（调试用）。
 */
int comm_pending_count(const comm_link_t *L);

/**
 * 当前待发队列帧数（§0E 压测验收用，读 tx_count 判 TX 是否卡死）。
 */
int comm_txq_count(const comm_link_t *L);

#endif /* GROUND_STATION_COMM_H */
