/**
 * audit_ring.h — 关键控制台命令的审计留痕环（纯逻辑，零 HAL，主机可测）
 *
 * M07-F-08（2026-11-25，S-G l1）：改参数 / 改模式 / 驱动引脚 / 急停这类命令原先执行完只在串口回一行
 * 结果，RTT 上没有命令原文和时间戳，复位后更是什么都不剩——现场误操作事后无法归因。
 * 这里是一块**独立的** 512 字节环形缓冲（不走控制台 TX 环，也不走 RTT 缓冲）：每执行一条关键命令记
 * "时间戳 + 命令原文 + 返回值"，覆盖最旧的。console.c 在记一条的同时往 RTT 打一行 [AUDIT]。
 * 三档都保留（交付档的排障价值最大），回放命令只读。
 */
#ifndef TCS_AUDIT_RING_H
#define TCS_AUDIT_RING_H

#include <stdint.h>
#include <stddef.h>
#include "tcs_profile.h"     /* TCS_PROFILE：台架及以上才有的命令，在交付档不进审计表（见 audit_ring.c）*/

#define AUDIT_SLOT_BYTES  32u                 /* 一条记录：时间戳 4 + 返回值 1 + 命令原文 27 */
#define AUDIT_SLOT_COUNT  16u                 /* 16 × 32 = 512 字节 */
#define AUDIT_CMD_MAX     26u                 /* 命令原文最多 26 个字符（不含 NUL）*/

typedef struct {
    uint32_t ts;                              /* 执行时的毫秒戳 */
    int8_t   rv;                              /* 返回值（超出 -128..127 的按边界截）*/
    char     cmd[AUDIT_CMD_MAX + 1u];
} audit_slot_t;

typedef struct {
    audit_slot_t slot[AUDIT_SLOT_COUNT];
    uint8_t      head;                        /* 下一条写这里 */
    uint8_t      count;                       /* 环里现有的条数（≤ AUDIT_SLOT_COUNT）*/
    uint32_t     total;                       /* 开机以来一共记了多少条（含被覆盖的）*/
} audit_ring_t;

/* 清空（total 也清零）*/
void audit_ring_init(audit_ring_t *r);

/* 记一条。返回 1 = 记了；0 = 参数非法（r / cmd 为空）。环满时覆盖最旧的一条，total 照常 +1。*/
int audit_ring_add(audit_ring_t *r, uint32_t ts_ms, const char *cmd, int rv);

/* 把命令原文整理成一条记录用的文本：空白折叠成单个空格、去掉首尾空白、超过 AUDIT_CMD_MAX 个字符的
 * 截断并在末尾加 '~'。放进 out（含 NUL）。line 为空时 out 为空串。*/
void audit_cmd_text(char *out, size_t cap, const char *line);

/* 这条命令要不要留痕：改参数 / 改模式 / 驱动引脚 / 急停与确认 / 仿真注入 / 复位与看门狗测试。
 * cmd = 第一个词，sub = 第二个词（没有就传 NULL 或空串）。只读类命令（ver / stat / perf / tbl …）不留。
 * 台架及以上才有的命令（mode / tec / fan / pindrv / pinrel / m207 send / sim / wdttest）在交付档
 * （TCS_PROFILE == 0）不进表：它们在交付档根本不编译，命令名也不该出现在交付档的产物里
 * （tools/site/verify_prod_clean.py 按产物查台架命令名）。交付档上试敲这些命令不留痕。*/
int audit_is_critical(const char *cmd, const char *sub);

/* 回放：从最旧到最新逐条写进 out，每行 "<ms> <cmd> rv=<rv>\n"。最多写 cap-1 个字符并补 NUL。
 * 返回写进去的条数（cap 不够时后面的不写，返回值 < 环里的条数）。out / cap 非法返回 0 并把 out[0] 置 NUL。*/
int audit_ring_format(const audit_ring_t *r, char *out, size_t cap);

#endif /* TCS_AUDIT_RING_H */
