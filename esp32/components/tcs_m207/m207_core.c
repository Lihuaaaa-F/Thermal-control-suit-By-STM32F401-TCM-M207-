/**
 * m207_core.c — M207 串口服务的决策逻辑（S-G e2：自主机不可编译的 m207_service.c 抽取）
 *
 * 这里只有状态机与仲裁，没有 RTOS / IDF 调用（见 m207_core.h）。行为与抽取前的
 * m207_service.c 逐拍一致——抽取当次用"旧 m207_service.c 打桩上主机 vs 本文件"做过随机差分
 * （TX 字节流、计数、g_tcs 逐步比对），证据见 docs/results/S_G_e2_结果.md。
 *
 * 架构（第八章预算，与抽取前相同）：
 *   - 服务任务每拍（50ms）调一次 m207_core_tick：先清 RX（m207_core_rx），再在途事务超时检查，
 *     最后 TX 仲裁，每拍至多 1 帧（重发 > 队列帧 > 轮询帧）→ 50ms 铁律结构性满足（正典 §4）。
 *   - 写路径：设 → CMD:REPLY=1/8 → 等下一拍出队 → 查 → 回显比对（容差 0.5×10^-decimals）
 *     → 不一致/超时重发 1 次（重试预算=1/事务）→ NAK 记录（正典 §2/§4）。
 *   - 轮询：500ms 交替 TC1/TC2 TCACTTEMP?；应答窗 6 拍；5 连败离线（F5：ALM_M207 置位+停收新写+
 *     清队列/挂起）；恢复：1s 探测，复联后重同步（TCSW?/TCADJTEMP?+按需 TCSW=0）。
 *   - H6.7 合理性滤波：|Δ|>10°C/s 判不可信→按失败计数（post_pure.c 纯函数）。
 *
 * 官方查证：本文件只有状态机与仲裁，不调用任何外设或库（主机可编译）；串口通道、时钟、
 * 日志由 m207_service.c 用官方 driver/uart、esp_timer、esp_log 提供。M207 为厂商私有协议，
 * 组件库无官方件（查证记录见 m207_service.c 文件头）。
 */
#include "m207_core.h"
#include "m207_service.h"
#include "m207_proto.h"
#include "param_registry.h"
#include "control.h"
#include "post.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>          /* m1（模式页）：isfinite（电学量应答纯化）*/

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
static const char *TAG = "M207";
#define M207_LOGE(...) ESP_LOGE(TAG, __VA_ARGS__)
#define M207_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define M207_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#define M207_LOGD(...) ESP_LOGD(TAG, __VA_ARGS__)
/* 下行队列的生产者在任意任务，消费者是服务任务：入队/出队用自旋锁临界区（两核互斥）。*/
static portMUX_TYPE s_qmux = portMUX_INITIALIZER_UNLOCKED;
#define M207_Q_LOCK()   taskENTER_CRITICAL(&s_qmux)
#define M207_Q_UNLOCK() taskEXIT_CRITICAL(&s_qmux)
#else
/* 主机单测：单线程驱动，无日志 */
#define M207_LOGE(...) do {} while (0)
#define M207_LOGW(...) do {} while (0)
#define M207_LOGI(...) do {} while (0)
#define M207_LOGD(...) do {} while (0)
#define M207_Q_LOCK()   do {} while (0)
#define M207_Q_UNLOCK() do {} while (0)
#endif

/* ====== 节律常量（第八章 / 正典）====== */
#define M207_TICK_MS     50u     /* 队列服务节拍 = 铁律间隔 */
#define M207_POLL_MS     500u    /* 轮询交替周期 */
#define M207_REPLY_TICKS 6u      /* 应答窗 300ms（6 拍）。原 100ms 对 57600 波特+模块行
                                  * 组装过紧：偶发超时累计 5 次即误判离线→SAFE，实测表现
                                  * 为「拔插一次线就停机、要人工确认」。（2026-11-19）*/
#define M207_FAIL_LIMIT  5       /* 5 连败离线（F5）*/
#define M207_PROBE_TICKS 20u     /* 离线探测 1s */
#define M207_Q_LEN       16
#define M207_CMD_MAX     40
#define M207_OPEN_DEBOUNCE 3   /* 连续 =2 次数达标才判断线（防交错误归属）*/
#define M207_PEND_MAX    4       /* 在途写确认事务（FIFO）*/
#define M207_PERF_WIN    200u    /* perf 汇报窗 = 200 拍 = 10s */
#define M207_SAVE_N      32u     /* "保存到模块"的帧数：TC1/TC2 各 16 个参数（映射表前 32 项）*/
#define M207_STATUS_POLL_TICKS 1200u   /* 模块自带保护状态轮询：60s（X04-F-05 ①）*/
#define M207_LINE_IDLE_MS 500u   /* M12-F-10：半行空闲超过这么久就丢弃（一行在 57600 下最长 11ms 到齐）*/

/* ====== 队列消息（生产者：任意任务 API/内部；消费者：服务任务）====== */
typedef struct {
    uint8_t  verify;                 /* 1=需写后读回确认 */
    uint8_t  dead;                   /* X03-F-07：1=已被更新的写取代，出队时跳过（不上线路）*/
    uint8_t  ch;                     /* 1/2 */
    uint8_t  decimals;
    float    value;                  /* 期望值（确认比对）*/
    char     cmd[M207_CMD_MAX];      /* 下行帧（含 \r）*/
    char     qry[M207_CMD_MAX];      /* 确认查询帧 */
    char     param[16];              /* 确认参数名 */
} m207_msg_t;

/* ====== 在途写确认事务（FIFO，串行化：模块应答与下行同序）====== */
typedef struct {
    uint8_t  in_use, phase;          /* phase: 1=等 CMD:REPLY 2=等查询回显 */
    uint8_t  ch, decimals, retries;
    uint8_t  resend_due;             /* 本拍仲裁须重发（set_cmd/qry_cmd 按 phase）*/
    float    value;
    char     param[16];
    char     set_cmd[M207_CMD_MAX];
    char     qry_cmd[M207_CMD_MAX];
    uint32_t deadline;               /* 绝对拍号 */
} m207_pend_t;

/* ====== 状态 ====== */
static m207_core_io_t s_io;
static m207_msg_t s_ring[M207_Q_LEN];   /* 下行命令队列（环形，容量 M207_Q_LEN）*/
static uint8_t  s_q_head, s_q_tail;     /* head 仅生产者推进，tail 仅消费者推进 */
static uint8_t  s_q_count;              /* 当前条数（判满用）*/
static uint8_t  s_started;
static uint8_t  s_online;
static uint8_t  s_probing;           /* 0=常规轮询 FSM（含上电首联）1=离线探测 */
static uint8_t  s_ever_online;
static uint8_t  s_fail;
static uint8_t  s_fail_ch[3];     /* M06-F-05：每通道连续超时（原来共享 s_fail，TC2 一成功就清零）*/
static uint8_t  s_probe_ch = 2;    /* M12-F-03：离线探测通道（2→1 起始，与原 TC1 首问一致）*/
static uint8_t  s_waiting;           /* 1=轮询已发在等应答 */
static uint8_t  s_wait_ch;           /* 轮询在等的通道（轴分离：errcode 归属）*/
static uint8_t  s_ch_open[M207_MOD_MAX];   /* 1/2=该通道传感器断线态（F6，errcode=2 参数族隐藏）；
                                            * M12-F-09 起 module 可为 PCB/SYSTEM/PCRS232 */
static uint8_t  s_open_cnt[M207_MOD_MAX];  /* 断线去抖：连续 =2 计数（≥3 才置态，防交错误归属）*/
static uint8_t  s_poll_ch = 1;       /* 1↔2 交替 */
static uint32_t s_tick;              /* 50ms 拍号 */
static uint32_t s_now_ms;            /* 本拍的真实毫秒时刻（服务给）*/
static uint32_t s_next_poll;         /* 下一轮询拍号 */
static uint32_t s_poll_deadline;     /* R1 M12-F-06：本拍询问的应答截止（发送拍+M207_REPLY_TICKS）*/
static uint32_t s_next_probe;        /* 离线探测拍号 */
static m207_lineasm_t s_asm;
static m207_pend_t s_pend[M207_PEND_MAX];

static float    s_temp[M207_MOD_MAX];      /* [1]=tc1 [2]=tc2 TCACTTEMP */
static uint32_t s_temp_ms[M207_MOD_MAX];    /* 各通道上个样本的真实毫秒时刻（温度龄、合理性 dt 用）*/
static uint32_t s_asm_ms;                   /* 行拼装器最近一个字节的时刻（空闲超时用）*/
static uint8_t  s_have_temp[M207_MOD_MAX];

static struct {
    uint16_t nak, verify_ok, resend, offline, wdrop, qdrop, unexpected;
    uint16_t superseded;                /* X03-F-07：被更新的写取代、没上线路的帧 */
    uint16_t save_ok, save_rej;         /* M12-F-08：保存帧被确认 / 被拒或无应答 */
    uint16_t linedrop;                  /* M12-F-10：没进解析器的行（超长整行 / 空闲半行 / 溢出清缓冲）*/
    uint32_t tx, rx_lines, parse_err;
    uint32_t tick_max_us, fsm_max_us;    /* perf 窗口内最大值 */
} s_stat;
static uint32_t s_txdrop;                /* 发送环满丢帧（服务出口报 -1 的次数）*/
static uint8_t  s_perf_due;

/* ====== 模块自带保护状态（X04-F-05 ①）======
 * 在线时每 60s 读一次 TCx:TCERRORSTATUS? 与 TCx:TCOTPSTATUS?（只读查询，排普通队列），
 * 原值入缓存、经 m207_module_status() 取值。只做"如实取值"：不报警、不进 SAFE。
 * TCERRORSTATUS 含上电锁存位（要模块断电重启才清），真机实录里 TC1 恒回 239 / 253；
 * 把它直接接成 SAFE，设备一上电就永久停机——接不接、按哪些位，由 control 决定（批次 e3a）。 */
typedef struct {
    uint16_t errstat;                     /* 原值；0xFFFF=尚未取到 */
    uint16_t otpstat;                     /* 原值；0xFFFF=尚未取到 */
    uint32_t ms;                          /* 最近一次取到的真实毫秒 */
    uint8_t  valid;
} m207_modst_t;
static m207_modst_t s_modst[3];           /* [1]=TC1 [2]=TC2 */
static uint32_t s_next_status_tick;       /* 下一次状态轮询的拍号；0=一上线就轮询 */

/* ====== 电学量轮询（m1"模式"页，用户 2026-11-26 定的新功能）======
 * 每秒把 TC1/TC2 的 TCACTVOL?（实际输出电压，V，制热为负）与 TCACTCUR?（实际输出电流，A）
 * 各问一遍，供"模式"页显示功率（= |电压| × 电流）。厂商参数字典里的只读状态；真机 2026-11-26
 * 实测模块认这两条（TC1:TCACTVOL=0.023 / TC1:TCACTCUR=0.000 / TC2:TCACTVOL=0.020 /
 * TC2:TCACTCUR=-0.000——电流会回负零，入缓存前吃掉）。
 * 只借温度轮询的空档：温度查询的应答到了之后、下一次温度轮询之前才问，而且应答窗必须在下一次
 * 温度轮询之前结束——温度轮询（500ms 交替）与应答截止不被拖慢，回码也不会被误归给温度查询
 * （errcode=2 同时是"传感器开路"的码）。电学查询的超时 / 被拒不计链路失败（轴分离：
 * comm fail 只由温度轮询的应答窗超时判定）。连续 3 次没拿到就判"模块不支持"、停止轮询，
 * 只留一条日志；复联后清零重试。取值经 m207_elec_get()，带数据龄（太旧按未知，见 M207_ELEC_STALE_MS）。*/
#define M207_ELEC_FAIL_LIMIT   3     /* 连续无应答 / 被拒这么多次 → 模块不支持，停发 */
typedef struct {
    float    volt, cur;                /* 最近值；V（制热为负）/ A（非负）*/
    uint32_t vms, cms;                 /* 各自最近一次取到的真实毫秒 */
    uint8_t  vhave, chave;             /* 1 = 取到过 */
} m207_elec_st_t;
static m207_elec_st_t s_elec[3];       /* [1]=TC1 [2]=TC2 */
static uint8_t  s_elec_idx;            /* 刚问过的序号（0..3，见 s_elec_q）*/
static uint8_t  s_elec_wait;           /* 1 = 电学查询在等应答 */
static uint32_t s_elec_deadline;       /* 应答截止拍号 */
static uint32_t s_elec_next_tick;      /* 最早可发下一条的拍号 */
static uint32_t s_elec_issued_tick;    /* 在途那一条发出的拍号 */
static uint8_t  s_elec_fail;           /* 连续失败次数 */
static uint8_t  s_elec_unsupported;    /* 1 = 模块不认这两条，已停发（复联后清零重试）*/
static const char s_elec_q[4][18] = {
    "TC1:TCACTVOL?\r", "TC1:TCACTCUR?\r", "TC2:TCACTVOL?\r", "TC2:TCACTCUR?\r",
};

/* ====== 安全关断旁路（X03-F-07）======
 * TCSW=0 不进普通队列：每通道一个单槽，最新值覆盖，下一拍优先上线路（排在重发与队列帧之前），
 * 所以不会被 16 条普通写挤掉。上线路时作废该通道队列里还没发的 TCSW 写、以及在途未确认的
 * TCSW 事务——后者若按重试预算重发 TCSW=1，会把刚发出去的关断盖掉（TEC 重新打开而系统以为已关）。
 *
 * i3（P1-09 的另一半，2026-11-26）：槽多一个"保持"位 hold。m207_tec() 第一行就是"离线 → 回 -2"，
 * 拔掉 M207 之后的安全关断连槽都进不了；槽里的帧离线期间发出去也没人听，发完即清，复联时无从补发。
 * 所以安全关断另走 m207_tec_force()：离线也受理，只记 hold；复联第一拍由 m207_go_online() 放回
 * pending，先于队列帧 / 重发 / 轮询上线路。hold 只在两处清：回读到与请求一致（TCSW? 应答为 0），
 * 或者该通道来了更新的"开"。离线沿的清队列只清普通队列，碰不到它。*/
typedef struct { uint8_t pending; uint8_t on; uint8_t hold; } m207_lane_t;
static m207_lane_t s_lane[M207_MOD_MAX];

/* ====== "保存到模块"序列（M12-F-08）======
 * 32 条 `!` 帧不再一次入队（队列只有 16 深，TC2 的 16 条会静默丢掉），
 * 改为由服务任务按游标每拍发一条；掉线即中止。*/
static uint8_t  s_save_active;
static uint8_t  s_save_idx;              /* 下一条要发的保存帧在 s_map 里的下标 */
static uint8_t  s_save_summary_due;      /* 末帧应答窗结束后打一次汇总 */
static uint32_t s_save_last_tick;        /* 末帧上线路的拍号 */
static uint16_t s_save_ack_ok, s_save_ack_other;   /* 本序列内收到的 CMD:REPLY=8 / 其它回码 */

/* ====== H10 映射表（逐字复制自 project_tcs m207.c s_map；R5 接注册表镜像）====== */
static const m207_map_entry_t s_map[] = {
    { 0x100, 1, "TCPIDP",       3, 0 },   /* mod=0：TC 前缀 */
    { 0x101, 1, "TCPIDTI",      2, 0 },   /* mod=0：TC 前缀 */
    { 0x102, 1, "TCPIDTD",      3, 0 },   /* mod=0：TC 前缀 */
    { 0x103, 1, "TCOTPHT",      1, 0 },   /* mod=0：TC 前缀 */
    { 0x104, 1, "TCOTPLT",      1, 0 },   /* mod=0：TC 前缀 */
    { 0x105, 1, "TCOTPHHYS",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x106, 1, "TCOTPMODE",    0, 0 },   /* mod=0：TC 前缀 */
    { 0x107, 1, "TCOCPADJTHR",  2, 0 },   /* mod=0：TC 前缀 */
    { 0x108, 1, "TCOVPADJTHR",  2, 0 },   /* mod=0：TC 前缀 */
    { 0x109, 1, "TCRTOCPHT",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x10A, 1, "TCRTOCPLT",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x10B, 1, "TCMINTEMP",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x10C, 1, "TCMAXTEMP",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x10D, 1, "TCMAXV",       2, 0 },   /* mod=0：TC 前缀 */
    { 0x10E, 1, "TCCOOLVRATIO", 2, 0 },   /* mod=0：TC 前缀 */
    { 0x10F, 1, "TCSTEPTEMP",   3, 0 },   /* mod=0：TC 前缀 */
    { 0x110, 2, "TCPIDP",       3, 0 },   /* mod=0：TC 前缀 */
    { 0x111, 2, "TCPIDTI",      2, 0 },   /* mod=0：TC 前缀 */
    { 0x112, 2, "TCPIDTD",      3, 0 },   /* mod=0：TC 前缀 */
    { 0x113, 2, "TCOTPHT",      1, 0 },   /* mod=0：TC 前缀 */
    { 0x114, 2, "TCOTPLT",      1, 0 },   /* mod=0：TC 前缀 */
    { 0x115, 2, "TCOTPHHYS",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x116, 2, "TCOTPMODE",    0, 0 },   /* mod=0：TC 前缀 */
    { 0x117, 2, "TCOCPADJTHR",  2, 0 },   /* mod=0：TC 前缀 */
    { 0x118, 2, "TCOVPADJTHR",  2, 0 },   /* mod=0：TC 前缀 */
    { 0x119, 2, "TCRTOCPHT",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x11A, 2, "TCRTOCPLT",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x11B, 2, "TCMINTEMP",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x11C, 2, "TCMAXTEMP",    1, 0 },   /* mod=0：TC 前缀 */
    { 0x11D, 2, "TCMAXV",       2, 0 },   /* mod=0：TC 前缀 */
    { 0x11E, 2, "TCCOOLVRATIO", 2, 0 },   /* mod=0：TC 前缀 */
    { 0x11F, 2, "TCSTEPTEMP",   3, 0 },   /* mod=0：TC 前缀 */
    /* 系统/PCB 模块无通道概念；PCB:* 下发由 R5 按 module 字段扩展 */
    /* M12-F-05：正典 §2.5 的模块前缀是 PCB:/PCRS232:，参数名照抄正典
     * （TMOTPHT/TMOTPLT/TMOTPHHYS/COMADDR）。*/
    { 0x120, 0, "TMOTPHT",      1, M207_MOD_PCB     },
    { 0x121, 0, "TMOTPLT",      1, M207_MOD_PCB     },
    { 0x122, 0, "TMOTPHHYS",    1, M207_MOD_PCB     },
    { 0x126, 0, "COMADDR",      0, M207_MOD_PCRS232 },
};
#define M207_MAP_N  (sizeof(s_map) / sizeof(s_map[0]))

/* ====== 发送出口 ====== */
static int64_t m207_now_us(void)
{
    return s_io.now_us ? s_io.now_us(s_io.time_ctx) : 0;
}

/* 返回 0=已交给发送出口（计 tx）；-1=发送环满被丢（计 txdrop；调用方决定是否重发）*/
static int m207_core_tx(const char *cmd, size_t len)
{
    if (!s_io.tx || s_io.tx(s_io.tx_ctx, cmd, len) != 0) {
        s_txdrop++;
        if ((s_txdrop % 50u) == 1u)
            M207_LOGW("tx ring full, dropped %lu frames", (unsigned long)s_txdrop);
        return -1;
    }
    s_stat.tx++;
    return 0;
}

/* ====== 下行队列（环形；生产者任意任务）======
 * 容量与抽取前的 FreeRTOS 队列一致：恰好 M207_Q_LEN 条。用计数判满——
 * "留空一格判满"的环形只能放 N-1 条（第一版就错在这里，主机差分的队列桩同错，没照出来）。*/
/* 调用方已持锁：入队。0=成功，-1=满 */
static int m207_ring_push(const m207_msg_t *msg)
{
    if (s_q_count >= M207_Q_LEN) return -1;
    s_ring[s_q_head] = *msg;
    s_q_head = (uint8_t)((s_q_head + 1u) % M207_Q_LEN);
    s_q_count++;
    return 0;
}

/* 服务内部帧（复联查询等）：满了计 qdrop 并留日志——不静默丢 */
static int m207_enqueue(const m207_msg_t *msg)
{
    int rc;
    M207_Q_LOCK();
    rc = s_started ? m207_ring_push(msg) : -1;
    M207_Q_UNLOCK();
    if (rc != 0) {
        s_stat.qdrop++;
        M207_LOGW("tx queue full: internal frame dropped (%s)", msg->cmd);
    }
    return rc;
}

/* API 写入的统一出口（M12-F-08）：在线判定与入队在同一临界区——离线清队与入队不交错。
 * 返回 0=入队；-1=队列满（计 qdrop，限频日志）；-2=未启动或离线（计 wdrop）。
 * 队列满不再无声：每一次被拒都有计数，调用方拿到的是 -1 而不是 0。*/
static int m207_submit(const m207_msg_t *msg)
{
    int rc;
    M207_Q_LOCK();
    if (!s_started || !s_online) { s_stat.wdrop++; rc = -2; }
    else if (m207_ring_push(msg) == 0) rc = 0;
    else { s_stat.qdrop++; rc = -1; }
    M207_Q_UNLOCK();
    if (rc == -1 && (s_stat.qdrop % 8u) == 1u)
        M207_LOGW("tx queue full: write dropped, qdrop=%u (last '%s')",
                  (unsigned)s_stat.qdrop, msg->cmd);
    return rc;
}

/* 出队：跳过已被取代的消息（它们计入 superseded）。返回 1=取到一条要发的 */
static int m207_dequeue(m207_msg_t *msg)
{
    int rc = 0;
    M207_Q_LOCK();
    while (s_q_count > 0) {
        *msg = s_ring[s_q_tail];
        s_q_tail = (uint8_t)((s_q_tail + 1u) % M207_Q_LEN);
        s_q_count--;
        if (!msg->dead) { rc = 1; break; }
    }
    M207_Q_UNLOCK();
    return rc;
}

/* 旁路槽置位（任意任务）：该通道下一条 TCSW 写下一拍优先上线路；槽里已有未发出的写则被最新值覆盖 */
static void m207_lane_set(uint8_t ch, uint8_t on)
{
    M207_Q_LOCK();
    s_lane[ch].pending = 1;
    s_lane[ch].on = on ? 1u : 0u;
    M207_Q_UNLOCK();
}

/* 等长语义同 strncpy 写入已清零的缓冲：最多 cap-1 字节，尾 NUL */
static void m207_zcopy(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (n > cap - 1) n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static void m207_enqueue_simple(const char *cmd)
{
    m207_msg_t m;
    memset(&m, 0, sizeof(m));
    m207_zcopy(m.cmd, sizeof(m.cmd), cmd);
    (void)m207_enqueue(&m);
}

/* ====== 在途确认事务 ====== */
static m207_pend_t *pend_alloc(void)
{
    for (int i = 0; i < M207_PEND_MAX; i++)
        if (!s_pend[i].in_use) return &s_pend[i];
    return NULL;
}

static m207_pend_t *pend_find(uint8_t phase, uint8_t ch, const char *param)
{
    for (int i = 0; i < M207_PEND_MAX; i++) {
        m207_pend_t *p = &s_pend[i];
        if (p->in_use && p->phase == phase && p->ch == ch &&
            strcmp(p->param, param) == 0) return p;
    }
    return NULL;
}

static m207_pend_t *pend_find_any_phase1(void)
{
    for (int i = 0; i < M207_PEND_MAX; i++)
        if (s_pend[i].in_use && s_pend[i].phase == 1) return &s_pend[i];
    return NULL;
}

static void pend_release(m207_pend_t *p) { memset(p, 0, sizeof(*p)); }

static float pend_tol(uint8_t decimals)
{
    float t = 0.5f;
    while (decimals--) t *= 0.1f;
    return t + 1e-4f;                    /* 0.5×10^-dec + 浮点余量 */
}

static void pend_nak(m207_pend_t *p, const char *why, int code)
{
    (void)why; (void)code;               /* 主机构建无日志，参数只进日志 */
    s_stat.nak++;
    M207_LOGW("set NAK ch=%u %s set=%.3f (%s code=%d) nak_total=%u",
              (unsigned)p->ch, p->param, (double)p->value, why, code,
              (unsigned)s_stat.nak);
    pend_release(p);
}

/* 重试预算=1/事务：标记本拍仲裁重发（不直接 TX——保 50ms 铁律单帧/拍）*/
static void pend_mark_resend(m207_pend_t *p, uint8_t phase, const char *why)
{
    if (p->retries) { pend_nak(p, why, 0); return; }
    p->retries = 1;
    p->phase = phase;                    /* 1=重发设定 2=重发查询 */
    p->resend_due = 1;
    s_stat.resend++;
    M207_LOGW("%s ch=%u %s → resend #1", why, (unsigned)p->ch, p->param);
}

/* ====== 在线/离线沿 ====== */
static void m207_go_online(const char *why)
{
    (void)why;
    s_fail = 0;
    s_fail_ch[1] = s_fail_ch[2] = 0;   /* M06-F-05：链路恢复即重置通道计数 */
    s_probing = 0;
    if (s_online) return;
    s_online = 1;
    g_tcs.m207_online = 1;
    M207_LOGI("online (%s)", why);
    s_next_status_tick = 0;                /* X04-F-05：复联后马上读一次模块保护状态 */
    s_elec_unsupported = 0;                /* m1（模式页）：复联后重新试电学量查询 */
    /* 复联重同步（E-08 预研）：目标温度只读回；TEC 该关的通道经安全关断旁路补发 TCSW=0
     * （X03-F-07：先于只读查询上线路，且自带一次回读确认），该开的通道只读回 TCSW?。
     * 不在这里开环恢复 TEC：模块侧是什么就镜像什么，要不要重开由 control 裁决。
     *
     * "该关"的判据（i3，P1-09 的另一半 / X03-F-08）——三条任一成立就补发，普通队列放行之前先发它：
     *   ① hold：离线期间经 m207_tec_force() 记下的关断还没被读回确认；
     *   ② 命令意图是关（control_tec_cmd）：不读回显 g_tcs.tecN_on——关断之后晚到的 TCSW? 应答会把
     *      回显写成"开"，按回显判就漏发，模块带着输出复联而系统以为已关；
     *   ③ 回显是关：原有判据，保留（命令是开而回显是关的通道照旧补发一次，由 control 的
     *      "模块回读关断"把参数置回暂停，不自动重开）。*/
    m207_enqueue_simple("TC1:TCADJTEMP?\r");
    m207_enqueue_simple("TC2:TCADJTEMP?\r");
    for (int ch = 1; ch <= 2; ch++) {
        uint8_t cmd_on  = control_tec_cmd((uint8_t)ch);
        uint8_t echo_on = (ch == 1) ? (uint8_t)(g_tcs.tec1_on ? 1u : 0u)
                                    : (uint8_t)(g_tcs.tec2_on ? 1u : 0u);
        if (s_lane[ch].hold || !cmd_on || !echo_on) {
            M207_Q_LOCK();
            s_lane[ch].pending = 1;
            s_lane[ch].on = 0u;
            M207_Q_UNLOCK();
        } else {
            m207_enqueue_simple(ch == 1 ? "TC1:TCSW?\r" : "TC2:TCSW?\r");
        }
    }
}

static void m207_go_offline(void)
{
    m207_msg_t m;
    /* 转离线与清队列在同一临界区：与 m207_submit 的"在线判定 + 入队"互斥，
     * 不会有写入在清队之后又落进队列、等到探测期才发出去。*/
    M207_Q_LOCK();
    s_online = 0;
    g_tcs.m207_online = 0;
    while (m207_dequeue(&m)) s_stat.qdrop++;
    M207_Q_UNLOCK();
    s_probing = 1;                       /* 转入 1s 探测（一次性沿；报警粘滞）*/
    s_stat.offline++;
    s_waiting = 0;
    s_elec_wait = 0;                     /* m1（模式页）：在途的电学查询随离线作废（不计失败）*/
    tcs_alarm_raise(ALM_M207);           /* F5：粘滞至人工 ack（tcs_alarm_ack）*/
    M207_LOGW("OFFLINE (%u fails) → ALM_M207, writes halted, SAFE by control",
              (unsigned)s_fail);
    s_fail = 0;
    /* 失败全部在途事务 */
    for (int i = 0; i < M207_PEND_MAX; i++)
        if (s_pend[i].in_use) pend_nak(&s_pend[i], "offline", 0);
    /* 保存序列中止（M12-F-08）：日志说明发到第几一条；已发帧的确认/拒绝计入累计 */
    if (s_save_active) {
        M207_LOGW("save_all aborted at %u/%u (offline)",
                  (unsigned)s_save_idx, (unsigned)M207_SAVE_N);
        s_save_active = 0;
        s_save_summary_due = 0;
        s_stat.save_ok = (uint16_t)(s_stat.save_ok + s_save_ack_ok);
        s_stat.save_rej = (uint16_t)(s_stat.save_rej + s_save_ack_other);
    }
    s_next_probe = s_tick + M207_PROBE_TICKS;
}

/* ====== 模块保护状态：解码与取值（X04-F-05 ①）====== */
uint8_t m207_errstat_fault_mask(uint16_t errstat)
{
    /* 厂商帮助文件（tc_hlp.xml）：8 位，正常=1 故障=0；bit0 外部 / bit1 过压 / bit2 过流 /
     * bit3 过温 / bit4 传感器开路 / bit5 负载短路 / bit6 负载开路；bit7 不用（全正常时回 255）。 */
    return (uint8_t)((~(uint32_t)errstat) & 0x7Fu);
}

const char *m207_fault_bit_name(uint8_t bit)
{
    static const char *const names[7] = {
        "external", "over-voltage", "over-current", "over-temp",
        "sensor-open", "load-short", "load-open",
    };
    return bit < 7 ? names[bit] : "?";
}

/* 把故障位拼成人话（如 "sensor-open|over-temp"）；buf 至少 96 字节 */
static const char *m207_fault_names(uint8_t mask, char *buf, size_t cap)
{
    size_t o = 0;
    buf[0] = '\0';
    for (int b = 0; b < 7 && o + 2 < cap; b++)
        if (mask & (1u << b)) {
            int n = snprintf(buf + o, cap - o, "%s%s", o ? "|" : "", m207_fault_bit_name((uint8_t)b));
            if (n < 0) break;
            o += (size_t)n;
        }
    return buf;
}

int m207_module_status(uint8_t ch, m207_mod_status_t *out)
{
    const m207_modst_t *m;
    if (!out || ch < 1 || ch > 2) return -1;
    m = &s_modst[ch];
    out->errstat = m->valid ? m->errstat : 0xFFFFu;
    out->otpstat = m->valid ? m->otpstat : 0xFFFFu;
    out->fault_mask = m->valid ? m207_errstat_fault_mask(m->errstat) : 0u;
    out->valid = m->valid;
    out->age_ms = m->valid ? (s_now_ms - m->ms) : 0xFFFFFFFFu;   /* 掉线后继续增长：旧值带龄可辨 */
    return 0;
}

/* ====== 电学量：入缓存的纯化、轮询子状态机与取值（m1"模式"页"）====== */
/* 应答数值入缓存前的纯化：负零吃掉（真机电流回 -0.000）；电流恒为正，负值按坏值处理；
 * 非有限值、明显超出量程的值按坏值处理（电压制热为负；量程上限远高于模块的 TCMAXV，
 * 只用来挡住垃圾值——同时也保证 0.01V / 0.01A 的定点打包永远不会溢出）。
 * 返回 1 = 可用（*out 填出），0 = 坏值。*/
#define M207_ELEC_V_MAX   100.0f
#define M207_ELEC_I_MAX    50.0f
static int m207_elec_sanitize(float v, int is_cur, float *out)
{
    if (!isfinite(v)) return 0;
    if (v == 0.0f) v = 0.0f;              /* -0.0 → +0.0 */
    if (is_cur && v < 0.0f) return 0;
    if (v < -M207_ELEC_V_MAX || v > (is_cur ? M207_ELEC_I_MAX : M207_ELEC_V_MAX)) return 0;
    *out = v;
    return 1;
}

/* 模块不认这两条：只留一条日志，停止轮询（复联后重试）*/
static void m207_elec_give_up(const char *why)
{
    s_elec_unsupported = 1;
    M207_LOGW("TCACTVOL/TCACTCUR not supported by this module (%s x%u): "
              "electrical polling stopped, power reads unknown until reconnect",
              why, (unsigned)M207_ELEC_FAIL_LIMIT);
}

static void m207_elec_result(int ok, const char *why)
{
    s_elec_wait = 0;
    if (ok) {
        s_elec_fail = 0;
        return;
    }
    if (s_elec_fail < 255u) s_elec_fail++;
    if (s_elec_fail >= M207_ELEC_FAIL_LIMIT) m207_elec_give_up(why);
}

/* 本拍的发送名额给了别的帧：在途的电学等待作废——那一帧的回码不能被当成电学查询的应答。
 * 电学应答本身按参数名匹配，迟到了照样入缓存。*/
static void m207_elec_yield(void)
{
    s_elec_wait = 0;
    s_elec_next_tick = s_tick + M207_REPLY_TICKS + 1u;
}

static void m207_elec_tx_note(int sent)
{
    if (sent && s_elec_wait && s_elec_issued_tick != s_tick) m207_elec_yield();
}

/* 温度轮询的空档里问一条电学量。返回本拍要发的帧，或 NULL（不发）。
 * 只在在线、非探测、没有温度查询在等应答、且还没到下一次温度轮询时被调用（见 m207_fsm_poll）。*/
static const char *m207_elec_poll(void)
{
    uint8_t q;
    if (s_elec_unsupported || !s_online) return NULL;
    if (s_elec_wait) {
        if ((int32_t)(s_tick - s_elec_deadline) >= 0) m207_elec_result(0, "no reply");
        return NULL;                      /* 等应答（或刚超时）：本拍不发 */
    }
    if ((int32_t)(s_tick - s_elec_next_tick) < 0) return NULL;
    /* 空档要放得下一个应答窗：截止拍必须早于下一次温度轮询（温度轮询永不被它挤后，
     * 它的回码也永远不会落进温度查询的等待窗）*/
    if (s_tick + M207_REPLY_TICKS >= s_next_poll) return NULL;
    q = (uint8_t)((s_elec_idx + 1u) % 4u);
    s_elec_idx = q;
    s_elec_wait = 1;
    s_elec_issued_tick = s_tick;
    s_elec_deadline = s_tick + M207_REPLY_TICKS;
    return s_elec_q[q];
}

int m207_elec_get(uint8_t ch, m207_elec_t *out)
{
    const m207_elec_st_t *e;
    uint32_t av, ac;
    if (!out || ch < 1 || ch > 2) return -1;
    e = &s_elec[ch];
    out->volt = e->volt;
    out->cur  = e->cur;
    out->fresh = (uint8_t)(e->vhave && e->chave);
    /* 数据龄取电压、电流里较旧的那个（真实毫秒；掉线后继续增长）*/
    av = e->vhave ? (s_now_ms - e->vms) : 0xFFFFFFFFu;
    ac = e->chave ? (s_now_ms - e->cms) : 0xFFFFFFFFu;
    out->age_ms = av > ac ? av : ac;
    out->valid = (uint8_t)(out->fresh && out->age_ms <= M207_ELEC_STALE_MS);
    return 0;
}

uint8_t m207_elec_supported(void) { return (uint8_t)(s_elec_unsupported ? 0u : 1u); }

/* ====== 应答行处理 ====== */
static void m207_handle_line(const char *line)
{
    m207_reply_t r;
    if (!m207_parse_line(line, &r)) {
        s_stat.parse_err++;
        M207_LOGI("parse-err line: '%s'", line);   /* 异常路径，非逐拍 */
        return;
    }
    s_stat.rx_lines++;

    if (r.kind == M207_KIND_ERRCODE) {
        m207_pend_t *p = pend_find_any_phase1();
        M207_LOGD("[RXE] errcode=%u pend=%s", (unsigned)r.errcode,
                  p ? (p->phase == 1 ? "phase1" : "phase2") : "none"); /* 拍内不做控制台 IO */
        if (p) {
            if (r.errcode == 1 || r.errcode == 8) {
                p->phase = 2;            /* 设定已受理 */
                p->resend_due = 1;       /* 下一拍仲裁发确认查询（≥50ms 铁律）*/
                p->deadline = s_tick + M207_REPLY_TICKS;
            } else {
                pend_nak(p, "module rejected", (int)r.errcode);  /* 协议拒绝，重发无益 */
            }
        } else {
            /* 没有在途设定事务：这条回码应答的是一条查询（轮询 / 离线探测 / 控制台 raw / 保存帧）。
             * 轴分离（R1b 修复）：CMD:REPLY 应答=链路双向活的证据，永不计 comm fail；
             * errcode=2 等温度查询=参数族被固件隐藏=该通道传感器断线（FMEA F6）。 */
            uint8_t qch = 0;                 /* 被应答的查询通道；0=没有查询在途 */
            if (s_waiting) { qch = s_wait_ch; s_waiting = 0; }
            else if (s_probing) qch = s_wait_ch;   /* 探测期：最近一条探测问的通道 */
            else if (s_elec_wait) {
                /* m1（模式页）：这条回码应答的是电学量查询——模块不认 TCACTVOL / TCACTCUR。
                 * 轴分离：是链路双向活的证据，不算链路失败；也不归到温度查询，
                 * 所以 =2 不会被当成传感器开路。*/
                m207_elec_result(0, "rejected");
                return;
            }

            if (!qch && (s_save_active || s_save_summary_due)) {
                /* 保存序列进行中（或末帧应答窗内）。保存期间不发轮询，无事务的回码只会是
                 * `!` 帧的应答：正典——可设参数保存 → 8；只读 / 不可存 → 3。 */
                if (r.errcode == 8) {
                    s_save_ack_ok++;
                } else {
                    s_save_ack_other++;
                    M207_LOGW("save frame rejected: CMD:REPLY=%u", (unsigned)r.errcode);
                }
                return;
            }
            if (!qch) {
                s_stat.unexpected++;     /* 没有查询在途的回码（注入 / 迟到）→ 留痕可见，不当链路证据 */
                M207_LOGI("unexpected CMD:REPLY=%u (no poll wait)",
                          (unsigned)r.errcode);
                return;
            }

            m207_pollerr_act_t a =
                m207_classify_poll_errcode(r.errcode, 1, "TCACTTEMP");
            if (a == M207_POLLERR_SENSOR_OPEN) {
                uint8_t ch = qch;
                if (ch >= 1 && ch <= 2) {
                    if (!s_online) {
                        /* M12-F-03：链路还没建立（开机首联 / 离线探测），这条 =2 只可能是应答
                         * 我们刚发的那一条查询——归属确定，立即坐实，不去抖。*/
                        s_open_cnt[ch] = M207_OPEN_DEBOUNCE;
                    } else if (s_open_cnt[ch] < 255) {
                        /* 去抖：在线时 console / 探测帧的 =2 可能落进轮询等待窗被误归属，
                         * 连续 ≥3 次才判断线。轮询 500ms 一拍、TC1/TC2 交替，
                         * 同一通道 1s 才被问一次，所以从第一次 =2 到坐实 ≥2s（不是 1.5s）。*/
                        s_open_cnt[ch]++;
                    }
                    if (!s_ch_open[ch] && s_open_cnt[ch] >= M207_OPEN_DEBOUNCE) {
                        s_ch_open[ch] = 1;
                        /* M12-F-01：镜像到 g_tcs，让控制层按「温度源丢失」处理 */
                        g_tcs.m207_sensor_open = (uint8_t)(s_ch_open[1] || s_ch_open[2]);
                        M207_LOGW("ch%u SENSOR-OPEN (errcode=2 ×%u param "
                                  "hidden) — F6 断线态，控制按失联，温度缓存冻结",
                                  (unsigned)ch, (unsigned)s_open_cnt[ch]);
                    }
                }
            } else {
                s_stat.unexpected++;   /* 链路活但应答语义意外 → 留痕取证 */
                M207_LOGI("unexpected CMD:REPLY=%u (poll ch=%u)",
                          (unsigned)r.errcode, (unsigned)qch);
            }
            if (!s_online) {
                /* M12-F-03（后半）：模块回了合法的 CMD:REPLY，就是"它收到了我们的帧并应答"
                 * ——链路双向活。原来只在轮询等待窗里清等待，离线探测期只计 unexpected；
                 * 于是两路传感器同时开路（模块只回 =2）时链路永远回不到 online，
                 * ALM_M207 也就永远确认不掉。传感器开路另走 F6 位，不阻断链路恢复。*/
                char why[48];
                snprintf(why, sizeof why, "reply code=%u ch=%u",
                         (unsigned)r.errcode, (unsigned)qch);
                s_ever_online = 1;
                m207_go_online(why);
            }
        }
        return;
    }
    if (r.module == M207_MOD_NONE) return;

    /* 写路径确认：phase2 事务的查询回显。只认 '=' 应答（KIND_REPLY）——
     * 回环下自家查询行（'?'，KIND_POLL，value=0）不得参与比对。 */
    {
        m207_pend_t *p = (r.kind == M207_KIND_REPLY) ?
                         pend_find(2, r.module, r.param) : NULL;
        if (p) {
            float diff = r.value - p->value;
            if (diff < 0) diff = -diff;
            if (diff <= pend_tol(p->decimals)) {
                s_stat.verify_ok++;
                M207_LOGI("verify OK ch=%s %s=%.3f (want %.3f)",
                          m207_mod_name(r.module), r.param, (double)r.value, (double)p->value);
                pend_release(p);
            } else {
                pend_mark_resend(p, 1, "echo mismatch");
            }
        }
    }

    /* 缓存更新 */
    /* M12-F-09：通道温度只属于 TC1/TC2。PCB/SYSTEM/PCRS232 现在能解析出模块号
     * （用于观测留痕），但它们的 TCACTTEMP 不得写进 tc1/tc2 缓存或覆盖 g_tcs。*/
    if (r.module != M207_MOD_TC1 && r.module != M207_MOD_TC2) {
        if (r.kind == M207_KIND_REPLY)
            M207_LOGI("module %s %s=%.3f (non-channel, log only)",
                      m207_mod_name(r.module), r.param, (double)r.value);
        return;
    }
    if (r.kind == M207_KIND_REPLY && strcmp(r.param, "TCACTTEMP") == 0) {
        if (s_have_temp[r.module]) {
            /* dt 用真实毫秒：服务拍被饿后拍号落后于真实时间（M12-F-07 的追赶拍被吞），
             * 按拍号算会把隔了很久才来的合理样本误判成跳变。*/
            float dt = (float)(s_now_ms - s_temp_ms[r.module]) / 1000.0f;
            if (!post_m207_sanity_ok(s_temp[r.module], r.value, dt)) {
                M207_LOGW("implausible ch=%s %.1f→%.1f (dt=%.2fs) → count fail",
                          m207_mod_name(r.module), (double)s_temp[r.module],
                          (double)r.value, (double)dt);
                s_fail++;
                s_waiting = 0;
                return;                  /* 不采纳（H6.7）*/
            }
        }
        s_temp[r.module] = r.value;
        s_temp_ms[r.module] = s_now_ms;
        s_have_temp[r.module] = 1;
        s_fail_ch[r.module] = 0;                 /* M06-F-05：本通道恢复 */
        if (s_ch_open[r.module]) {                       /* F6 恢复沿（接回传感器）*/
            s_ch_open[r.module] = 0;
            g_tcs.m207_sensor_open = (uint8_t)(s_ch_open[1] || s_ch_open[2]);
            M207_LOGW("ch%s temperature usable again (errcode=2 closed or timeout cleared)",
                      m207_mod_name(r.module));
        }
        s_open_cnt[r.module] = 0;
        if (r.module == 1) {
            g_tcs.tc1_act = r.value;
            /* N-01：超温切断不得只依赖 control 任务的周期采样（现为 100ms）。设备服务层解析到超温
             * 就立刻置锁存（门控 out_tec_sw 的"开"），并直接下发 TCSW=0。
             * 这样即使 control 任务创建失败/卡死，TEC 仍会被关。*/
            if (r.value > ALM_OVERTEMP_HARD_C) {
                control_safety_trip(SAFETY_BIT_OVERTEMP);
                tcs_alarm_raise(ALM_OVERTEMP);   /* 锁存必须伴随可见报警，否则屏/网页只看到"停机"不知原因 */
                /* X03-F-07：安全关断走旁路，不排在普通写后面。i3：改走 m207_tec_force()——
                 * 这一帧发出后链路若随即断开、没等到读回确认，复联时凭保持位照样先补发。*/
                (void)m207_tec_force(1, 0);
            }
        }
        else               g_tcs.tc2_act = r.value;
        m207_go_online(s_ever_online ? "recover" : "first");
        s_ever_online = 1;
        s_waiting = 0;
    } else if (r.kind == M207_KIND_REPLY && strcmp(r.param, "TCSW") == 0) {
        if (r.module == 1) g_tcs.tec1_on = (r.value != 0.0f);
        else               g_tcs.tec2_on = (r.value != 0.0f);
        /* i3（P1-09 的另一半）：读回是"关" = 旁路里保持的关断已经到了执行器，解除保持。
         * 读回仍是"开"则保持不动，下一次复联照样先补发。*/
        if (r.value == 0.0f) {
            M207_Q_LOCK();
            s_lane[r.module].hold = 0;
            M207_Q_UNLOCK();
        }
    } else if (r.kind == M207_KIND_REPLY &&
               (strcmp(r.param, "TCERRORSTATUS") == 0 || strcmp(r.param, "TCOTPSTATUS") == 0)) {
        /* 模块自带保护状态（X04-F-05 ①）：原值入缓存并留痕。不在这里报警、不进 SAFE。 */
        m207_modst_t *m = &s_modst[r.module];
        int is_err = strcmp(r.param, "TCERRORSTATUS") == 0;
        if (!(r.value >= 0.0f && r.value <= 65535.0f)) {
            s_stat.parse_err++;
            M207_LOGW("module ch%u %s out of range (%.3f) — ignored",
                      (unsigned)r.module, r.param, (double)r.value);
        } else {
            uint16_t v = (uint16_t)(r.value + 0.5f);
            uint16_t prev = is_err ? m->errstat : m->otpstat;
            if (is_err) m->errstat = v; else m->otpstat = v;
            m->ms = s_now_ms;
            m->valid = 1;
            if (v != prev) {
                uint8_t f = is_err ? m207_errstat_fault_mask(v) : 0u;
                if (is_err && f) {
                    char nb[96];
                    M207_LOGW("module ch%u TCERRORSTATUS=%u fault-bits=0x%02X (%s) — "
                              "reported only, NOT wired to SAFE",
                              (unsigned)r.module, (unsigned)v, (unsigned)f,
                              m207_fault_names(f, nb, sizeof nb));
                    (void)nb;
                } else {
                    M207_LOGI("module ch%u %s=%u", (unsigned)r.module, r.param, (unsigned)v);
                }
            }
        }
    } else if (r.kind == M207_KIND_REPLY &&
               (strcmp(r.param, "TCACTVOL") == 0 || strcmp(r.param, "TCACTCUR") == 0)) {
        /* m1（模式页）：电学量应答（只读状态）。数值入缓存；真机电流可回 -0.000（负零吃掉）。
         * 不报警、不进 SAFE、不计链路失败。*/
        m207_elec_st_t *e = &s_elec[r.module];
        int is_cur = (strcmp(r.param, "TCACTCUR") == 0);
        float v;
        if (!m207_elec_sanitize(r.value, is_cur, &v)) {
            s_stat.parse_err++;
            M207_LOGW("module ch%u %s out of range (%.3f) — ignored",
                      (unsigned)r.module, r.param, (double)r.value);
        } else {
            if (is_cur) { e->cur = v;   e->cms = s_now_ms; e->chave = 1; }
            else        { e->volt = v;  e->vms = s_now_ms; e->vhave = 1; }
            m207_elec_result(1, "ok");
        }
    } else if (r.kind == M207_KIND_REPLY && strcmp(r.param, "TCADJTEMP") == 0) {
        M207_LOGI("module TCADJTEMP ch=%u = %.2f (cache mirror at R5)",
                  (unsigned)r.module, (double)r.value);
    } else if (r.kind == M207_KIND_REPLY) {
        /* 非缓存参数的应答（TCERRORSTATUS/TCOTPSTATUS/SYSMODEL 类）——
         * 原样留痕（R5 字典镜像接入前的通用观测口）*/
        M207_LOGI("reply ch=%u %s=%.3f", (unsigned)r.module, r.param, (double)r.value);
    }
}

void m207_core_rx(const uint8_t *buf, size_t n, uint32_t now_ms)
{
    char line[M207_LINE_MAX + 1];
    if (n == 0) return;
    s_asm_ms = now_ms;                        /* 最近一个字节到达的时刻（空闲超时用）*/
    for (size_t i = 0; i < n; i++) {
        uint8_t b = buf[i];
        /* M12-F-10：超长行在行尾被行拼装器整行丢弃（截断的前缀不进解析器）——留痕计数 */
        if (s_asm.over && (b == '\r' || b == '\n')) {
            s_stat.linedrop++;
            M207_LOGW("over-long line dropped (more than %d bytes)", M207_LINE_MAX);
        }
        if (m207_lineasm_feed(&s_asm, b, line, sizeof(line)))
            m207_handle_line(line);
    }
}

void m207_core_rx_flush(void)
{
    if (s_asm.len) {
        s_stat.linedrop++;
        M207_LOGW("uart overflow: partial line discarded (%u bytes)", (unsigned)s_asm.len);
    }
    m207_lineasm_reset(&s_asm);
}

/* ====== 轮询/恢复状态机（返回本拍要发的轮询帧或 NULL）======
 * 上电=常规轮询 FSM（H743 同语义）：500ms 交替、应答窗超时计败、5 连败→
 * go_offline（ALM_M207 一次性沿）→转 1s 探测；探测成功→go_online("recover")。 */
static const char *m207_fsm_poll(void)
{
    static char cmd[24];
    if (!s_probing) {
        if (s_waiting) {
            if ((int32_t)(s_tick - s_poll_deadline) >= 0) {
                /* 超时归属本拍询问的通道（s_wait_ch）*/
                uint8_t ch = s_wait_ch;
                if (ch >= 1 && ch <= 2) {
                    if (s_fail_ch[ch] < 255) s_fail_ch[ch]++;
                    /* M06-F-05：单通道连续失败达上限 → 该通道温度视为不可用
                     * （与 errcode=2 的传感器断线同一后果：control 进 SAFE）。
                     * 原来共享一个 s_fail 且"任一成功回复清零"，TC1 单通道失联
                     * 会被 TC2 的成功永久掩盖，tc1_act 冻结而系统判健康。*/
                    if (s_fail_ch[ch] >= M207_FAIL_LIMIT && !s_ch_open[ch]) {
                        s_ch_open[ch] = 1;
                        g_tcs.m207_sensor_open = (uint8_t)(s_ch_open[1] || s_ch_open[2]);
                        M207_LOGW("ch%u TIMEOUT x%u -> temperature unavailable",
                                  (unsigned)ch, (unsigned)s_fail_ch[ch]);
                    }
                }
                s_fail++;                /* 总链路失败计数（全通道静默才判离线）*/
                s_waiting = 0;
                if (s_fail >= M207_FAIL_LIMIT) { m207_go_offline(); return NULL; }
            }
            return NULL;
        }
        if (s_tick >= s_next_poll) {
            s_next_poll = s_tick + M207_POLL_MS / M207_TICK_MS;
            s_wait_ch = s_poll_ch;                      /* 轴分离：errcode 归属通道 */
            s_poll_deadline = s_tick + M207_REPLY_TICKS; /* 应答窗=发送后 300ms（M207_REPLY_TICKS，2026-11-19 从 100ms 放宽）*/
            snprintf(cmd, sizeof(cmd), "TC%u:TCACTTEMP?\r", (unsigned)s_poll_ch);
            s_poll_ch = (s_poll_ch == 1) ? 2u : 1u;
            s_waiting = 1;
            return cmd;
        }
        /* m1（模式页）：温度轮询的空档借给电学量查询——不占温度轮询的位置、不拖后它的应答截止 */
        return m207_elec_poll();
    } else {
        /* 离线/恢复：1s 探测，TC1/TC2 交替（M12-F-03）。
         * 原来只问 TC1：TC1 传感器开路时模块回 ERRCODE=2，链路永远回不到 online，
         * ALM_M207 也就永远 ack 不掉。任一通道能回温度即证明模块/链路活着；
         * 单通道传感器开路由 s_ch_open/m207_sensor_open 单独表达（control 进 SAFE）。
         * 两路都开路时模块只回 CMD:REPLY=2——那也是链路活的证据（M12-F-03 后半，
         * 见 m207_handle_line）：上线，两通道标传感器开路。*/
        if (s_tick >= s_next_probe) {
            s_next_probe = s_tick + M207_PROBE_TICKS;
            s_probe_ch = (s_probe_ch == 1) ? 2u : 1u;
            s_wait_ch = s_probe_ch;            /* M12-F-03：这条探测问的通道（回码归属用）*/
            snprintf(cmd, sizeof(cmd), "TC%u:TCACTTEMP?\r", (unsigned)s_probe_ch);
            return cmd;
        }
    }
    return NULL;
}

static const char *m207_fsm_poll_timed(void)
{
    int32_t t0 = (int32_t)m207_now_us();
    const char *r = m207_fsm_poll();
    int32_t dt = (int32_t)m207_now_us() - t0;
    if (dt > 0 && (uint32_t)dt > s_stat.fsm_max_us) s_stat.fsm_max_us = (uint32_t)dt;
    return r;
}

void m207_core_tick_begin(uint32_t now_ms)
{
    s_now_ms = now_ms;
    s_tick++;
    /* M12-F-10：半行空闲超时。一行在 57600 下最长 11ms 就到齐；超过 500ms 还没有后续字节，
     * 说明模块复位或线上是噪声。残段留着会与下一行拼成一条"看起来合法"的行。*/
    if (s_asm.len && (uint32_t)(now_ms - s_asm_ms) > M207_LINE_IDLE_MS) {
        s_stat.linedrop++;
        M207_LOGW("partial line discarded: idle %ums with %u bytes",
                  (unsigned)(now_ms - s_asm_ms), (unsigned)s_asm.len);
        m207_lineasm_reset(&s_asm);
    }
}

/* 本拍的发送名额先给安全关断旁路（X03-F-07）。返回 1=本拍的名额已用于旁路帧 */
static int m207_lane_step(void)
{
    uint8_t lch = 0;
    for (int ch = 1; ch <= 2 && !lch; ch++)
        if (s_lane[ch].pending) lch = (uint8_t)ch;
    if (!lch) return 0;

    uint8_t on = s_lane[lch].on;
    unsigned nsup = 0, ninfl = 0;
    /* (a) 队列里还没发出去的该通道 TCSW 写：都比这条旁路请求旧，作废（出队时跳过）*/
    M207_Q_LOCK();
    for (uint8_t idx = s_q_tail, k = 0; k < s_q_count;
         k++, idx = (uint8_t)((idx + 1u) % M207_Q_LEN)) {
        m207_msg_t *q = &s_ring[idx];
        if (!q->dead && q->verify && q->ch == lch && strcmp(q->param, "TCSW") == 0) {
            q->dead = 1;
            nsup++;
        }
    }
    M207_Q_UNLOCK();
    s_stat.superseded = (uint16_t)(s_stat.superseded + nsup);
    /* (b) 在途未确认的 TCSW 事务：作废。不计 NAK——它是被更新的命令取代，不是确认失败；
     *     留着的话，它应答超时会按重试预算重发 TCSW=1，把这条关断盖掉。*/
    for (int i = 0; i < M207_PEND_MAX; i++) {
        m207_pend_t *p = &s_pend[i];
        if (p->in_use && p->ch == lch && strcmp(p->param, "TCSW") == 0) {
            pend_release(p);
            ninfl++;
        }
    }
    if (nsup || ninfl)
        M207_LOGI("safe-off ch=%u: superseded %u queued + %u unconfirmed TCSW write(s)",
                  (unsigned)lch, nsup, ninfl);

    /* (c) 上线路 */
    char cmd[24];
    snprintf(cmd, sizeof cmd, "TC%u:TCSW=%u\r", (unsigned)lch, (unsigned)on);
    if (m207_core_tx(cmd, strlen(cmd)) != 0)
        return 1;                         /* 发送环满：槽保持待发，下一拍重来 */
    /* 发出去了才清槽；这期间若有更新的请求改写了槽值，保留新值 */
    M207_Q_LOCK();
    if (s_lane[lch].pending && s_lane[lch].on == on) s_lane[lch].pending = 0;
    M207_Q_UNLOCK();
    if (s_online) {
        /* 在线：和其它写一样读回确认——模块不肯关就重发一次，仍不肯就 NAK，如实可见 */
        m207_pend_t *p = pend_alloc();
        if (p) {
            p->in_use = 1; p->phase = 1;
            p->ch = lch; p->decimals = 0; p->retries = 0;
            p->value = on ? 1.0f : 0.0f;
            strcpy(p->param, "TCSW");
            m207_zcopy(p->set_cmd, sizeof(p->set_cmd), cmd);
            snprintf(p->qry_cmd, sizeof(p->qry_cmd), "TC%u:TCSW?\r", (unsigned)lch);
            p->deadline = s_tick + M207_REPLY_TICKS;
        } else {
            M207_LOGW("safe-off ch=%u sent unverified (no free confirmation slot)",
                      (unsigned)lch);
        }
    }
    return 1;
}

/* 发送名额没被旁路占用时的仲裁：每拍至多 1 帧（重发 > 队列帧 > 保存帧 > 轮询帧）*/
static void m207_arbitrate(void)
{
    m207_pend_t *rp = NULL;
    /* 模块自带保护状态轮询（X04-F-05 ①）：每 60s 一次，在线、此刻没有轮询在等应答、
     * 没有保存在进行时才排。4 条都是只读查询，走普通队列（排在重发之后，每拍一帧）。 */
    if (s_online && !s_waiting && !s_save_active &&
        (s_next_status_tick == 0u ||
         (int32_t)(s_tick - s_next_status_tick) >= 0)) {
        s_next_status_tick = s_tick + M207_STATUS_POLL_TICKS;
        m207_enqueue_simple("TC1:TCERRORSTATUS?\r");
        m207_enqueue_simple("TC2:TCERRORSTATUS?\r");
        m207_enqueue_simple("TC1:TCOTPSTATUS?\r");
        m207_enqueue_simple("TC2:TCOTPSTATUS?\r");
    }
    for (int i = 0; i < M207_PEND_MAX; i++)
        if (s_pend[i].in_use && s_pend[i].resend_due) { rp = &s_pend[i]; break; }
    if (rp) {
        const char *c = (rp->phase == 1) ? rp->set_cmd : rp->qry_cmd;
        m207_core_tx(c, strlen(c));
        rp->resend_due = 0;
        rp->deadline = s_tick + M207_REPLY_TICKS;
    } else {
        m207_msg_t m;
        if (m207_dequeue(&m)) {
            m207_core_tx(m.cmd, strlen(m.cmd));
            if (m.verify) {
                m207_pend_t *p = pend_alloc();
                if (p) {
                    p->in_use = 1; p->phase = 1;
                    p->ch = m.ch; p->decimals = m.decimals; p->retries = 0;
                    p->value = m.value;
                    m207_zcopy(p->param, sizeof(p->param), m.param);
                    m207_zcopy(p->set_cmd, sizeof(p->set_cmd), m.cmd);
                    m207_zcopy(p->qry_cmd, sizeof(p->qry_cmd), m.qry);
                    p->deadline = s_tick + M207_REPLY_TICKS;
                } else {
                    M207_LOGW("pend overflow: %s sent unverified", m.cmd);
                }
            }
        } else if (s_save_active) {
            /* "保存到模块"：按游标每拍一条。发送环满则下一拍重发同一条，不跳号（M12-F-08）*/
            const m207_map_entry_t *e = &s_map[s_save_idx];
            char cmd[M207_CMD_MAX];
            snprintf(cmd, sizeof cmd, "TC%u:%s!\r", (unsigned)e->ch, e->cmd);
            if (m207_core_tx(cmd, strlen(cmd)) == 0) {
                s_save_idx++;
                s_save_last_tick = s_tick;
                if (s_save_idx >= M207_SAVE_N) {
                    s_save_active = 0;
                    s_save_summary_due = 1;
                }
            }
        } else {
            const char *poll = m207_fsm_poll_timed();
            if (poll) m207_core_tx(poll, strlen(poll));
        }
    }
}

void m207_core_tick_end(int64_t t0_us)
{
    /* 1) 在途事务超时检查（重试只标记，TX 交给本拍仲裁）*/
    for (int i = 0; i < M207_PEND_MAX; i++) {
        m207_pend_t *p = &s_pend[i];
        if (!p->in_use || p->resend_due) continue;
        if ((int32_t)(s_tick - p->deadline) >= 0) {
            if (p->phase == 1) pend_mark_resend(p, 1, "ack timeout");
            else               pend_mark_resend(p, 2, "query echo lost");
        }
    }

    /* 2) 发送名额：安全关断旁路优先；否则 重发 > 队列帧 > 保存帧 > 轮询帧。每拍至多 1 帧 */
    {
        uint32_t sent0 = s_stat.tx + s_txdrop;
        if (!m207_lane_step()) m207_arbitrate();
        m207_elec_tx_note(sent0 != s_stat.tx + s_txdrop);   /* m1（模式页）：见函数注释 */
    }

    /* 3) 保存序列结账（M12-F-08）：末帧的应答窗结束后汇总一次，没回码的记作被拒 */
    if (s_save_summary_due &&
        (int32_t)(s_tick - s_save_last_tick) >= (int32_t)M207_REPLY_TICKS) {
        unsigned noreply = (unsigned)M207_SAVE_N - s_save_ack_ok - s_save_ack_other;
        s_save_summary_due = 0;
        s_stat.save_ok = (uint16_t)(s_stat.save_ok + s_save_ack_ok);
        s_stat.save_rej = (uint16_t)(s_stat.save_rej + s_save_ack_other + noreply);
        if (noreply || s_save_ack_other)
            M207_LOGW("save_all done: sent=%u ack_ok=%u rejected=%u no_reply=%u",
                      (unsigned)M207_SAVE_N, (unsigned)s_save_ack_ok,
                      (unsigned)s_save_ack_other, noreply);
        else
            M207_LOGI("save_all done: sent=%u ack_ok=%u rejected=%u no_reply=%u",
                      (unsigned)M207_SAVE_N, (unsigned)s_save_ack_ok,
                      (unsigned)s_save_ack_other, noreply);
    }

    /* 3) perf 统计（第八章：队列服务+节流 ≤1ms/拍，轮询状态机 ≤0.3ms）*/
    if (s_io.now_us) {
        int32_t dt = (int32_t)m207_now_us() - (int32_t)t0_us;
        if (dt > 0 && (uint32_t)dt > s_stat.tick_max_us) s_stat.tick_max_us = (uint32_t)dt;
    }
    if ((s_tick % M207_PERF_WIN) == 0) s_perf_due = 1;
}

int m207_core_perf_take(m207_core_perf_t *out)
{
    if (!s_perf_due) return 0;
    s_perf_due = 0;
    if (out) {
        out->tick_max_us = s_stat.tick_max_us;
        out->fsm_max_us = s_stat.fsm_max_us;
        out->tx = s_stat.tx; out->rx_lines = s_stat.rx_lines; out->parse_err = s_stat.parse_err;
        out->nak = s_stat.nak; out->verify_ok = s_stat.verify_ok; out->resend = s_stat.resend;
        out->offline = s_stat.offline; out->qdrop = s_stat.qdrop; out->wdrop = s_stat.wdrop;
        out->superseded = s_stat.superseded; out->unexpected = s_stat.unexpected;
        out->txdrop = s_txdrop;
        out->linedrop = s_stat.linedrop;
    }
    s_stat.tick_max_us = 0;
    s_stat.fsm_max_us = 0;
    return 1;
}

void m207_core_init(const m207_core_io_t *io)
{
    s_io = io ? *io : (m207_core_io_t){0};
    s_q_head = s_q_tail = 0;
    s_q_count = 0;
    s_online = 0;
    s_probing = 0;
    s_ever_online = 0;
    s_fail = 0;
    s_fail_ch[1] = s_fail_ch[2] = 0;
    s_probe_ch = 2;
    s_waiting = 0;
    s_wait_ch = 0;
    memset(s_ch_open, 0, sizeof(s_ch_open));
    memset(s_open_cnt, 0, sizeof(s_open_cnt));
    s_poll_ch = 1;
    s_tick = 0;
    s_now_ms = 0;
    s_next_poll = 0;
    s_poll_deadline = 0;
    s_next_probe = 0;
    m207_lineasm_reset(&s_asm);
    memset(s_pend, 0, sizeof(s_pend));
    memset(s_lane, 0, sizeof(s_lane));
    s_save_active = 0;
    s_save_idx = 0;
    s_save_summary_due = 0;
    s_save_last_tick = 0;
    s_save_ack_ok = s_save_ack_other = 0;
    memset(s_temp, 0, sizeof(s_temp));
    memset(s_temp_ms, 0, sizeof(s_temp_ms));
    s_asm_ms = 0;
    memset(s_have_temp, 0, sizeof(s_have_temp));
    memset(&s_stat, 0, sizeof(s_stat));
    s_txdrop = 0;
    s_perf_due = 0;
    memset(s_modst, 0, sizeof(s_modst));
    s_next_status_tick = 0;
    memset(s_elec, 0, sizeof(s_elec));    /* m1（模式页）：电学量缓存与轮询子状态机清零 */
    s_elec_idx = 0;
    s_elec_wait = 0;
    s_elec_deadline = 0;
    s_elec_next_tick = 0;
    s_elec_issued_tick = 0;
    s_elec_fail = 0;
    s_elec_unsupported = 0;
    g_tcs.m207_online = 0;               /* 上电默认值 */
    g_tcs.m207_sensor_open = 0;
    s_started = 1;

    /* F9 上电安全默认：双通道 TCSW=0（模块若在场，先钳到安全侧）。
     * 走安全关断旁路：头两拍各发一条，上线路顺序与抽取前相同；此时离线，不做读回确认。*/
    m207_lane_set(1, 0);
    m207_lane_set(2, 0);
}

#if CONFIG_TCS_M207_SELFLOOP
uint8_t m207_core_have_temp(uint8_t ch)
{
    return (ch < M207_MOD_MAX) ? s_have_temp[ch] : 0u;
}
#endif

/* ====== 公共 API（声明见 m207_service.h）====== */
int m207_set_temp(uint8_t ch, float v)
{
    if (ch != 1 && ch != 2) return -1;
    if (v < -20.0f || v > 80.0f) return -1;
    if (!s_started || !s_online) { s_stat.wdrop++; return -2; }
    m207_msg_t m;
    memset(&m, 0, sizeof(m));
    m.verify = 1; m.ch = ch; m.decimals = 1; m.value = v;
    strcpy(m.param, "TCADJTEMP");
    snprintf(m.cmd, sizeof(m.cmd), "TC%u:TCADJTEMP=%.1f\r", (unsigned)ch, (double)v);
    snprintf(m.qry, sizeof(m.qry), "TC%u:TCADJTEMP?\r", (unsigned)ch);
    return m207_submit(&m);
}

int m207_tec(uint8_t ch, uint8_t on)
{
    m207_msg_t m;
    if (ch != 1 && ch != 2) return -1;
    if (!s_started || !s_online) { s_stat.wdrop++; return -2; }
    M207_Q_LOCK();
    if (on) s_lane[ch].hold = 0;         /* "开"是更新的请求：解除旁路里保持的关断（i3）*/
    if (!on || s_lane[ch].pending) {
        /* X03-F-07：关断走旁路单槽，不进普通队列——队列满了也收得下，下一拍优先上线路。
         * "开"照旧走普通队列，保住 control"先设目标、后开"的入队顺序；但旁路槽里若还有
         * 一条没发出去的写（50ms 窗内先关后开），以最新值为准。*/
        s_lane[ch].pending = 1;
        s_lane[ch].on = on ? 1u : 0u;
        M207_Q_UNLOCK();
        return 0;
    }
    M207_Q_UNLOCK();
    memset(&m, 0, sizeof(m));
    m.verify = 1; m.ch = ch; m.decimals = 0; m.value = on ? 1.0f : 0.0f;
    strcpy(m.param, "TCSW");
    snprintf(m.cmd, sizeof(m.cmd), "TC%u:TCSW=%u\r", (unsigned)ch, (unsigned)(on ? 1u : 0u));
    snprintf(m.qry, sizeof(m.qry), "TC%u:TCSW?\r", (unsigned)ch);
    return m207_submit(&m);
}

/* P1-09 的另一半（i3，2026-11-26）：安全关断专用入口（control 的 ops.tec_force 接到这里）。
 * 与 m207_tec() 的差别只有一条——模块离线也受理：
 *   在线：等同 m207_tec(ch, 0)，下一拍优先上线路并读回确认；
 *   离线：只记保持位（不发：模块不在，发了也只是把这拍的名额让给一帧没人听的帧，发完即清，
 *         复联时无从补发），复联第一拍由 m207_go_online() 放回槽里补发，先于普通队列放行。
 * 不占普通队列，离线沿的清队列碰不到它。每 500ms 的重试由 control 的锁存重申驱动
 * （enter_safe → safe_outputs → ops.tec_force），这里不自带定时器。
 * 只有"关"走这里："开"转回普通入口，保住"先设目标、后开"的入队顺序。
 * 返回 0 = 已受理；-1 = 通道非法。*/
int m207_tec_force(uint8_t ch, uint8_t on)
{
    if (ch != 1 && ch != 2) return -1;
    if (on) return m207_tec(ch, 1);
    M207_Q_LOCK();
    s_lane[ch].hold = 1;
    s_lane[ch].on = 0u;
    if (s_online) s_lane[ch].pending = 1;
    M207_Q_UNLOCK();
    return 0;
}

int m207_map_set(uint16_t pid, float v)
{
    if (!s_started || !s_online) { s_stat.wdrop++; return -2; }
    for (size_t i = 0; i < M207_MAP_N; i++) {
        const m207_map_entry_t *e = &s_map[i];
        if (e->pid != pid) continue;
        /* M12-F-05：0x120-0x126 走 PCB:/PCRS232: 前缀（m207_map_entry_t.mod），不再拒绝 */
        m207_msg_t m;
        memset(&m, 0, sizeof(m));
        m.verify = 1; m.ch = e->ch; m.decimals = e->decimals; m.value = v;
        m207_zcopy(m.param, sizeof(m.param), e->cmd);
        if (m207_map_format(e, v, m.cmd, sizeof(m.cmd)) != 0) return -1;
        if (e->mod != 0)
            snprintf(m.qry, sizeof(m.qry), "%s:%s?\r", m207_mod_name(e->mod), e->cmd);
        else
            snprintf(m.qry, sizeof(m.qry), "TC%u:%s?\r", (unsigned)e->ch, e->cmd);
        return m207_submit(&m);
    }
    return -1;                           /* 未映射参数 */
}

int m207_save_all(void)
{
    if (!s_started || !s_online) { s_stat.wdrop++; return -2; }
    if (s_save_active) return -1;              /* 上一轮还在发：调用方应答 BUSY */
    /* M12-F-08：原来一次入队 32 条，队列只有 16 深——TC2 的 16 条被静默丢掉，
     * 返回值却让屏幕收到 OK。现在由服务任务按游标每拍发一条（见 m207_arbitrate），
     * 32 条都会上线路；返回"将要发送的条数"。PCB/PCRS232 组语法未确认，不发（M12-F-05）。*/
    s_save_idx = 0;
    s_save_ack_ok = s_save_ack_other = 0;
    s_save_active = 1;
    M207_LOGI("save_all: %u frames, one per tick", (unsigned)M207_SAVE_N);
    return (int)M207_SAVE_N;
}

uint8_t m207_save_busy(void) { return s_save_active; }

int m207_send_raw(const char *cmd)
{
    if (!cmd || !*cmd) return -1;
    if (!s_started || !s_online) { s_stat.wdrop++; return -2; }
    m207_msg_t m;
    memset(&m, 0, sizeof(m));
    /* API 契约：调用方可不带 \r，此处统一补齐（实测：本固件对无 \r 命令按帧间
     * 隙超时解析并回 CMD:REPLY=2——协议 §1"静默忽略"不成立，见 R1b 试验日志）*/
    size_t n = strlen(cmd);
    if (n >= M207_CMD_MAX - 1) return -1;
    strncpy(m.cmd, cmd, M207_CMD_MAX - 2);
    if (m.cmd[n - 1] != '\r') m.cmd[n] = '\r';
    return m207_submit(&m);
}

uint8_t m207_online(void) { return s_online; }
float m207_last_temp(uint8_t ch) { return (ch == 1 || ch == 2) ? s_temp[ch] : 0.0f; }
uint32_t m207_temp_age_ms(void)
{
    if (!s_ever_online) return 0xFFFFu;
    /* R1 修复 M12-F-11：任一通道从未更新 → 年龄未知，直接 0xFFFF。
     * 原实现跳过该通道，单通道无数据时仍显示"新鲜"。*/
    if (!s_have_temp[1] || !s_have_temp[2]) return 0xFFFFu;
    uint32_t worst = 0;                                /* 取较旧通道=显示新鲜度下界 */
    for (int ch = 1; ch <= 2; ch++) {
        /* 真实毫秒龄：服务拍被饿时拍号会落后于真实时间，按拍号算会把旧数据报成新鲜 */
        uint32_t age = s_now_ms - s_temp_ms[ch];
        if (age > worst) worst = age;
    }
    return (worst > 0xFFFFu) ? 0xFFFFu : worst;
}
uint16_t m207_stats_nak(void) { return s_stat.nak; }
uint16_t m207_stats_verify_ok(void) { return s_stat.verify_ok; }
uint16_t m207_stats_resend(void) { return s_stat.resend; }
uint16_t m207_stats_offline(void) { return s_stat.offline; }
uint16_t m207_stats_wdrop(void) { return s_stat.wdrop; }
uint16_t m207_stats_unexpected(void) { return s_stat.unexpected; }
void m207_stats_queue(m207_qstats_t *out)
{
    if (!out) return;
    out->qdrop = s_stat.qdrop;
    out->superseded = s_stat.superseded;
    out->unexpected = s_stat.unexpected;
    out->save_ok = s_stat.save_ok;
    out->save_rej = s_stat.save_rej;
    out->txdrop = s_txdrop;
    out->linedrop = s_stat.linedrop;
}
uint8_t m207_alarm_active(void) { return tcs_alarm_active(ALM_M207); }

void m207_perf_reset(void)
{
    s_stat.tick_max_us = 0;
    s_stat.fsm_max_us = 0;
}

/* ====== 上电回环自检用的帧表与判定（M12-F-13；自 m207_service.c 抽出，主机可测）======
 * 过线帧只有 ? 查询：内部回环把它们原样送回 RX，证明的是"真 UART 外设 + 真驱动 + 行拼装 + 解析器"
 * 这一条路；回显必须逐字节相同、且解析出正确的模块 / 参数 / 类型。
 * 原来的前三帧是写命令（TCx:TCACTTEMP=…）：模块接着时这些帧经 TX 引脚真实下发——只读参数被拒（=3）
 * 还好，万一模块不按"只读即拒绝"实现，就是一次对温度参数的写。现在过线的全是只读查询；
 * 解析样本（REPLY 行、ERRCODE 全表）不过线，只在板上逐字节喂进行拼装器再解析。 */
static const char *const s_lb_wire[] = {
    "TC1:TCACTTEMP?", "TC2:TCACTTEMP?", "TC1:TCADJTEMP?", "TC2:TCSW?",
};
#define M207_LB_WIRE_N (sizeof(s_lb_wire) / sizeof(s_lb_wire[0]))

static const char *const s_lb_parse[] = {
    "TC1:TCACTTEMP=25.5", "TC2:TCACTTEMP=-10.05", "TC1:TCACTTEMP=1.0",
    "CMD:REPLY=0", "CMD:REPLY=1", "CMD:REPLY=2", "CMD:REPLY=3",
    "CMD:REPLY=4", "CMD:REPLY=5", "CMD:REPLY=6", "CMD:REPLY=7",
    "CMD:REPLY=8",
};
#define M207_LB_PARSE_N (sizeof(s_lb_parse) / sizeof(s_lb_parse[0]))

size_t m207_lb_wire_n(void) { return M207_LB_WIRE_N; }
const char *m207_lb_wire_frame(size_t i) { return i < M207_LB_WIRE_N ? s_lb_wire[i] : ""; }
size_t m207_lb_parse_n(void) { return M207_LB_PARSE_N; }
const char *m207_lb_parse_frame(size_t i) { return i < M207_LB_PARSE_N ? s_lb_parse[i] : ""; }

/* 回显判定（纯函数）：过线帧只能是合法的 ? 查询（不许带 = 或 !），回显与它逐字节相同 */
int m207_lb_echo_ok(const char *wire, const char *echo)
{
    m207_reply_t r;
    if (!wire || !echo) return 0;
    if (strchr(wire, '=') || strchr(wire, '!')) return 0;
    if (!m207_parse_line(wire, &r) || r.kind != M207_KIND_POLL) return 0;
    return strcmp(wire, echo) == 0 ? 1 : 0;
}

/* 解析判定（纯函数）：REPLY 行的模块 / 参数 / 值，ERRCODE 行的全表 0..8 */
int m207_lb_parse_ok(const char *sample)
{
    m207_reply_t r;
    if (!sample || !*sample) return 0;
    if (!m207_parse_line(sample, &r)) return 0;
    if (strncmp(sample, "CMD:REPLY=", 10) == 0)
        return r.kind == M207_KIND_ERRCODE && r.errcode == (uint8_t)atoi(&sample[10]);
    if (r.kind != M207_KIND_REPLY || r.module == M207_MOD_NONE) return 0;
    if (strcmp(sample, "TC1:TCACTTEMP=25.5") == 0)
        return r.module == M207_MOD_TC1 && r.value > 25.49f && r.value < 25.51f;
    if (strcmp(sample, "TC2:TCACTTEMP=-10.05") == 0)
        return r.module == M207_MOD_TC2 && r.value < -10.04f && r.value > -10.06f;
    if (strcmp(sample, "TC1:TCACTTEMP=1.0") == 0)
        return r.module == M207_MOD_TC1 && r.value > 0.99f && r.value < 1.01f;
    return r.param[0] != '\0';           /* 其余：参数名非空即过 */
}

/* 同通道去抖时延的推算（M12-F-15）：轮询 500ms 一拍、通道交替，同一通道 1s 才被问一次；
 * 连续 3 次 =2 才坐实——第一次、第二次、第三次分别落在 0s、1s、2s，所以从第一次 =2 到坐实是 2s
 * （原注释写的"1.5s 内坐实"不对）。 */
uint32_t m207_open_debounce_ms(void)
{
    return (uint32_t)(M207_OPEN_DEBOUNCE - 1u) * 2u * M207_POLL_MS;
}

/* ====== 钩子适配器（param_registry PF_M207 写入口）====== */
void m207_hook_set_temp(uint8_t ch, float v) { (void)m207_set_temp(ch, v); }
void m207_hook_map_set(uint16_t pid, float v) { (void)m207_map_set(pid, v); }
