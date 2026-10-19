/**
 * m207.h — M207 温控模块串口驱动（USART3 PB10/PB11 @57600，任务书 §3.4）
 *
 * 指令集（自 F401 V4.1 源码核实；高级指令等文档）：
 *   查：`TCx:TCACTTEMP?\r`   回：`TCx:TCACTTEMP=<f>\r`（实际水温）
 *   设：`TCx:TCADJTEMP=<f>\r`（下发后模块回显 `TCx:TCADJTEMP=<f>` 即回读确认）
 *   启停：`TCx:TCSW=<0/1>\r`
 *
 * 轮询状态机（替代 F401 阻塞 HAL_Delay 版本）：IDLE→WAIT_REPLY→(RECOVER)。
 * 写路径确认：下发后回读 |回读值−下发值|<0.05，不一致自动重发 1 次，
 * 仍不一致 [WARN] m207 set NAK（仅日志+网页事件，不报警横幅）。
 *
 * PC 单测（§5.1）：m207_parse_line / m207_line_feed 为纯函数，本头文件声明，
 * m207.c 在 TCS_UNIT_TEST 下只编译纯逻辑部分。
 */
#ifndef TCS_M207_H
#define TCS_M207_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 解析结果 ====== */
#define M207_MOD_NONE  0
#define M207_MOD_TC1   1
#define M207_MOD_TC2   2

typedef enum {
    M207_KIND_NONE = 0,
    M207_KIND_POLL,        /* '？' 查询（本机下发方向）*/
    M207_KIND_REPLY,       /* '=' 回复/回显 */
    M207_KIND_ERRCODE,     /* CMD:REPLY=N 错误/执行应答（现行M207协议_v1.md §2/§3）*/
} m207_kind_t;

typedef struct {
    uint8_t module;              /* M207_MOD_TC1/TC2，0=未知模块 */
    char    param[16];           /* "TCACTTEMP"/"TCADJTEMP"/"TCSW"（大写）*/
    m207_kind_t kind;
    float   value;               /* '=' 后的浮点值（缺等号时 0）*/
    uint8_t errcode;             /* KIND_ERRCODE：ERRORCODE 0-8 */
} m207_reply_t;

/* 行长上限（任务书：行长上限 64）*/
#define M207_LINE_MAX 64

/* ====== 纯逻辑 1：行拼装（\r 结尾；超长/溢出安全）======
 * 喂一字节；凑成完整一行（不含 \r）返回 1 且 line 填出（带 '\0'）。*/
typedef struct {
    uint8_t len;
    char    buf[M207_LINE_MAX + 1];
} m207_lineasm_t;

void m207_lineasm_reset(m207_lineasm_t *la);
int  m207_lineasm_feed(m207_lineasm_t *la, uint8_t b, char *line, size_t cap);

/* ====== 纯逻辑 2：协议行解析（自 F401 ProtocolAnalysis 移植改造）======
 * 小写自动大写；`模块:参数=值` → M207_KIND_REPLY；缺冒号/缺等号/空串/超长 → 0。
 * 返回 1=解析成功（out 填出），0=忽略。 */
int m207_parse_line(const char *line, m207_reply_t *out);

/* ====== H10 映射表（纯逻辑，PC 可测）====== */
/* 参数 id → M207 指令模板。文档到达后填表即可接入真参数（扩展成本=一行）。
 * 指令名全大写；ch=1/2（TC1/TC2 通道）。 */
typedef struct {
    uint16_t pid;          /* 注册表参数 id（0x100+）*/
    uint8_t  ch;           /* 下发通道 1/2 */
    char     cmd[14];      /* M207 参数名（如 "TCADJTEMP"/占位 "PIDKP"）*/
    uint8_t  decimals;     /* 下发小数位（回读容差 0.5×10^-decimals）*/
} m207_map_entry_t;

/* 模板格式化（纯函数）：`TC{ch}:{cmd}=%.{}f\r`。返回 0=ok，-1=cap 不足/表项非法 */
int m207_map_format(const m207_map_entry_t *e, float v, char *out, size_t cap);

/* ====== 目标机驱动（TCS_UNIT_TEST 下不编译）====== */
#ifndef TCS_UNIT_TEST

#include "stm32h7xx_hal.h"

typedef enum { M207_ST_IDLE = 0, M207_ST_WAIT_REPLY, M207_ST_RECOVER } m207_state_t;

typedef struct {
    float   value;       /* 待确认下发值 */
    uint8_t ch;          /* 1/2 */
    uint8_t retries;     /* 已自动重发次数（0/1）*/
    uint8_t active;
    char    param[16];   /* 回读确认的参数名（H10 泛化：TCADJTEMP/PIDKP/…）*/
    uint8_t decimals;    /* 容差位数 */
} m207_setpend_t;

void  m207_init(UART_HandleTypeDef *huart);   /* 挂 USART3 @57600 */
void  m207_poll(uint32_t now_ms);             /* 主循环节拍调用，内部状态机 */
int   m207_set_temp(uint8_t ch, float v);     /* 入下发队列，异步；0=ok -1=队列满/参数错 */
int   m207_tec(uint8_t ch, uint8_t on);       /* TCx:TCSW=0/1，异步 */
int   m207_send_raw(const char *cmd);         /* 原始指令透传（console 调试期专用）*/
int   m207_map_set(uint16_t pid, float v);    /* H10：按映射表下发（回读确认+NAK 重发）*/
float m207_last_temp(uint8_t ch);
uint8_t m207_online(void);                    /* 0/1（g_tcs.m207_online 同步）*/

/* ISR 侧：USART3 RX 单字节入口（由 HAL 回调转发）。只入环形缓冲，SPSC。 */
void m207_isr_push(uint8_t b);

/* WP-G 板内仿真（TCS_SIM 构建才实现）：模拟应答器控制 */
void m207_sim_fault(void);        /* 应答静默（拔线仿真）*/
void m207_sim_recover(void);      /* 应答恢复 */
void m207_sim_wrong_echo(void);   /* 下一次设定回读故意错值（重发路径）*/
void m207_sim_drop(uint8_t pct);  /* H11：回包丢弃率注入（0=关闭）*/
/* RX 完成重启 + 错误恢复（main.c 回调转发）*/
void m207_rx_complete(void);
void m207_uart_error_recover(void);

#endif /* !TCS_UNIT_TEST */

#ifdef __cplusplus
}
#endif
#endif /* TCS_M207_H */
