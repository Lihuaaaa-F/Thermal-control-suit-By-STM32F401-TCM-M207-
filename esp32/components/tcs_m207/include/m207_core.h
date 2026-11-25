/**
 * m207_core.h — M207 服务的决策逻辑内核（主机可编译；S-G e2 自 m207_service.c 抽取）
 *
 * 原来写在 m207_service.c 里的全部状态机——下行队列、在途写确认事务、轮询/探测 FSM、
 * 应答行处置、统计——都在这里。本文件与 m207_core.c 不含任何 RTOS / IDF 调用：时间与发送
 * 经 m207_core_io_t 注入，所以可以在主机上逐拍驱动（esp32/tests/test_m207_core.c），
 * 也可以让 Python 对端模型（project_tcs/tools/m207_sim.py）直接对着固件实现跑注入矩阵
 * （esp32/tests/test_m207_service_adv.py）。m207_service.c 只剩 IDF 外壳：UART 驱动、
 * 服务任务、发送出口（含取证构建的应答器注入）、上电回环自测。
 *
 * 对外 API（m207_set_temp / m207_tec / …）仍声明在 m207_service.h，实现在 m207_core.c。
 */
#ifndef TCS_M207_CORE_H
#define TCS_M207_CORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 发送出口（服务任务提供）：0=已交给 UART 发送环；-1=发送环满被丢（核心计 txdrop）。
 * 每拍至多调用一次（50ms 铁律由核心的仲裁保证）。*/
typedef int (*m207_tx_fn)(void *ctx, const char *frame, size_t len);
/* 单调微秒时钟（perf 统计用；主机测试给虚拟时钟）。*/
typedef int64_t (*m207_now_us_fn)(void *ctx);

typedef struct {
    m207_tx_fn      tx;
    void           *tx_ctx;
    m207_now_us_fn  now_us;       /* 可为 NULL（perf 统计恒 0）*/
    void           *time_ctx;
} m207_core_io_t;

/* 上电初始化：清全部状态、排入双通道安全默认帧（TC1/TC2:TCSW=0）。io 须常驻。*/
void m207_core_init(const m207_core_io_t *io);

/* 一个服务拍（50ms）分两步，中间是服务清 RX（与抽取前的拍内顺序逐项一致）：
 *   m207_core_tick_begin(now_ms)   拍号+1（本拍内收到的应答行按新拍号记时戳）
 *   m207_core_rx(...) × n           清 UART 环形缓冲、逐行处置
 *   m207_core_tick_end(t0_us)      在途事务超时检查 → TX 仲裁（每拍至多 1 帧）→ perf 统计
 * now_ms=真实毫秒时基；t0_us=本拍开始处理的时刻（服务在清 RX 之前取），只用于 tick_max 统计。*/
void m207_core_tick_begin(uint32_t now_ms);
void m207_core_tick_end(int64_t t0_us);

/* 喂 RX 字节（服务每拍清空 UART 环形缓冲后调用，可多次）。now_ms=读到这批字节的时刻。*/
void m207_core_rx(const uint8_t *buf, size_t n, uint32_t now_ms);

/* UART 溢出、驱动清了接收缓冲之后调用：行拼装器里的半行已不完整，整段丢弃（计 linedrop）。*/
void m207_core_rx_flush(void);

typedef struct {
    uint32_t tick_max_us;      /* 窗口内最长的拍（队列服务+节拍，预算 1000µs）*/
    uint32_t fsm_max_us;       /* 窗口内最长的轮询状态机调用（预算 300µs）*/
    /* 累计计数快照（[M207P] 汇报行用）*/
    uint32_t tx, rx_lines, parse_err;
    uint16_t nak, verify_ok, resend, offline, qdrop, wdrop;
    uint16_t superseded;      /* 被更新的同类写取代、没上线路的帧（X03-F-07）*/
    uint16_t unexpected;      /* 无事务、非等待窗的 CMD:REPLY（M12-F-14）*/
    uint16_t linedrop;        /* 没进解析器的行（M12-F-10）*/
    uint32_t txdrop;          /* 发送环满被丢的帧 */
} m207_core_perf_t;
/* perf 汇报窗（200 拍）满：返回 1 并把窗口峰值填入 out、清零；否则返回 0。*/
int m207_core_perf_take(m207_core_perf_t *out);

/* ====== 上电回环自检用的帧表与判定（M12-F-13；自 m207_service.c 抽出，主机可测）======
 * 过线帧（m207_lb_wire_*）只有 ? 查询：自检时模块若接着，不会被改任何参数。
 * 解析样本（m207_lb_parse_*）不过线，只在板上逐字节喂进行拼装器再解析。 */
size_t      m207_lb_wire_n(void);
const char *m207_lb_wire_frame(size_t i);
size_t      m207_lb_parse_n(void);
const char *m207_lb_parse_frame(size_t i);
int  m207_lb_echo_ok(const char *wire, const char *echo);   /* 过线帧是合法查询且回显逐字节相同 */
int  m207_lb_parse_ok(const char *sample);                  /* 解析样本的语义正确 */
uint32_t m207_open_debounce_ms(void);   /* M12-F-15：同通道从第一次 =2 到坐实传感器开路的最短时延 */

#if CONFIG_TCS_M207_SELFLOOP
/* 取证构建的 selfloop 断言用：该通道是否已收到过温度。*/
uint8_t m207_core_have_temp(uint8_t ch);
#endif

#ifdef __cplusplus
}
#endif
#endif /* TCS_M207_CORE_H */
