/**
 * link_send.h — 屏（H743）→ 主控（ESP32）发送面里带策略的那一部分：参数写入（R5-10，2026-11-25）。
 *
 * 背景：屏上改非 M207 参数（模式 / 阈值 / 布防）原先走 0x11 "变更即推"，不等 ACK——
 * 丢了没人知道；ESP32 侧 2026-11-23 加的"发送窗内等长帧即回声"过滤还把它误杀了约 1%
 * （R5 复核 R5-10）。现在屏写任何参数都是一帧 0x12、登记等待确认（500ms 超时 ×3 重传）。
 * 组帧 + 入队 + 登记放在这里、Core/Src/main.c 的钩子只做转发，是为了"走不走 ARQ"这件事
 * 能在主机上测（tests/test_link_send.c）。
 *
 * 方向位不用管：comm 层出队时统一打（R5-01）。消息号与方向的唯一说明在 protocol/tcs_proto.h。
 */
#ifndef TCS_LINK_SEND_H
#define TCS_LINK_SEND_H

#include <stdint.h>
#include "comm.h"

/**
 * 屏写参数：一帧 0x12，等 ACK。
 * 返回 0 = 已入队并登记等待确认；-1 = 组帧失败（载荷超长，正常不会发生）。
 */
int link_send_set_param(comm_link_t *L, uint16_t id, uint8_t type, float value);

/**
 * param_registry 的 on_broadcast 钩子在链路上的实体。
 * flags 取 param_desc_t 的 flags（PF_M207 见 param_registry.h）。
 *
 * PF_M207 参数（tc1_set / tc2_set / 0x100 段映射参数）已由 on_m207_set / on_m207_map_set 钩子
 * 经 link_send_set_param 发出——param_set 先调 side_effects 再调 broadcast——这里不重复发
 * （重复发会占两个等待确认的槽，改一次设定值要两帧）。其余参数在这里走同一条等 ACK 的路。
 * 返回值同 link_send_set_param；PF_M207 短路返回 0。
 */
int link_send_on_broadcast(comm_link_t *L, uint16_t id, uint8_t type, uint8_t flags, float value);

/* ====== X01-F-03（2026-11-26）：参数写入的应答结果 ======
 * 0x90 的 status/ret 原来两端都不解析，BAD_ID / CLAMP / BUSY 一律当成功——被拒绝的写入在屏上
 * 显示成功，等下一帧 0x20 才悄悄改回旧值。现在 comm 层把结果交给这里：本模块记住"这一帧 0x12
 * 写的是哪个参数、写的是多少"，应答回来时按 (seq, mt) 找回那条记录。*/

/* 一次参数写入的最终结果。
 *   id        = 参数 id
 *   status    = TCS_ACK_OK / CLAMP / BAD_ID / BUSY（已按 TCS_ACK_STATUS_MASK 掩过）
 *   ret       = 应答里的 ret（CLAMP 时是主控实际写入的值；其余情况一般为 0）
 *   requested = 屏当时要写的值
 * 上层（main.c）据此提示使用者、并在 CLAMP 时采用 ret / 在拒绝时回退到权威快照值。 */
typedef void (*link_send_result_cb_t)(uint16_t id, uint8_t status, float ret, float requested);
void link_send_on_result(link_send_result_cb_t cb);

/* 注册到 comm 层的应答回调（comm_set_on_ack_result）。main.c 在 comm_init 之后调一次。
 * 不是参数写入的应答（0x13 请求注册表 / 0x15 命令）在这里不处理。 */
void link_send_on_ack_result(uint8_t acked_seq, uint8_t acked_mt, uint8_t status, float ret);

/* 结果计数（非 OK 的写入按 status 分桶）：`stat` 打印用。任一指针可为 NULL。 */
void link_send_ack_stats(uint32_t *out_bad, uint32_t *out_clamp, uint32_t *out_busy);

/* 当前还在等应答的参数写入条数（主机用例用来确认表项不会泄漏）。 */
int link_send_inflight(void);

#endif /* TCS_LINK_SEND_H */
