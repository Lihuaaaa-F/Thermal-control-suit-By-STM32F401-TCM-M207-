/*********************************************************************
 * SEGGER_RTT_Conf.h — 工程配置实例（W4.2，v13.4 §5.2）
 *
 * 官方 Config/SEGGER_RTT_Conf.h 空壳的实例化（引用链：SEGGER_RTT.h →
 * vendor/SEGGER_RTT_ConfDefaults.h → 本文件，引号包含解析到 App/rtt/）。
 * 只覆盖与自研版行为的映射差异项，其余用官方默认；差异对表见
 * docs/results/W4_2_完成.md 行为差异表。
 */
#ifndef SEGGER_RTT_CONF_H
#define SEGGER_RTT_CONF_H

/* 缓冲数：对齐自研版 1 上行 + 1 下行（官方默认 3+3 省内存） */
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS    (1)
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS  (1)

/* 上行 1024B=对齐自研现值（官方默认同值，显式写出锚定语义）；
 * 下行 0=无人消费（自研版 16B 下行从未被读，官方语义 size 0=不可用） */
#define BUFFER_SIZE_UP                   (1024)
#define BUFFER_SIZE_DOWN                 (0)

/* 模式：官方默认即 NO_BLOCK_SKIP（满时丢新不阻塞），不自研覆盖；
 * 与自研版"满则环形覆盖最老"的差异=已知行为变化，见行为差异表 */

#endif
