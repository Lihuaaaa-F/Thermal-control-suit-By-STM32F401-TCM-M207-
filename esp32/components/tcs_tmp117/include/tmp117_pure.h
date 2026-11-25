/**
 * tmp117_pure.h — TMP117 纯逻辑（ESP32 侧复用件；R1c，任务书 v11.3 第十一章）
 *
 * 健康机/换算逐字提取自 project_tcs/App/tcs/tmp117.c+h 纯函数区——Q5 单向同步：
 * 改健康机逻辑只许改 project_tcs 源侧再复制过来；R4 H743 瘦身后源侧删除即终局。
 * 提取时刻源 md5（2026-10-26）：tmp117.c `0018aa234ee6d75b36b206d4094253a5`
 * / tmp117.h `1c1b5ad62b54e59042ac1c5cc0b97a75`（docs/results/R1.md R1c 节）。
 * 滑动均值窗口为本侧新增纯函数（H743 为 poll 内联逻辑，语义一致：FIFO 窗口 8）。
 */
#ifndef TCS_TMP117_PURE_H
#define TCS_TMP117_PURE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TMP117_LSB_C        0.0078125f
#define TMP117_AVG_WIN      8
#define TMP117_ERR_TRIP     10     /* 连续 10 错 → 掉线 */
#define TMP117_OK_RUN       3      /* 连续 3 成功 → 恢复数据 */

/* 寄存器地址（SNOSD2D Table 7-3）：温度结果 0x00、配置 0x03、器件 ID 0x0F（只读，期望 0x0117）。
 * 内核与服务共用一份定义（M13-F-02 / M13-F-08）。 */
#define TMP117_REG_TEMP     0x00
#define TMP117_REG_CONFIG   0x03
#define TMP117_REG_DEVID    0x0F
#define TMP117_DEVID_EXPECT 0x0117u

/* ====== 纯逻辑 1：raw→°C（project_tcs tmp117.c 逐字）====== */
float tmp117_raw_to_c(int16_t raw);

/* ====== 配置寄存器比对（M13-F-02；S-G e2 新增；TMP117 数据手册 SNOSD2D）======
 * 寄存器 0x03，Table 7-3 给出的出厂复位值 0x0220 = 连续转换 + CONV=100 + AVG=01。
 * 按 Table 7-7（连续转换模式下的转换周期），CONV=100 在任何 AVG 下都是 1 s——
 * 审查原文写的"默认 250 ms"有误。也就是说 100 ms 采样下每个转换结果被读约 10 次。
 * 位域（Table 7-6）：[15] HIGH_Alert(R) [14] LOW_Alert(R) [13] Data_Ready(R) [12] EEPROM_Busy(R)
 *   [11:10] MOD[1:0]（00 连续转换 / 01 关断 / 11 单次）
 *   [9:7]   CONV[2:0]（AVG=01 时：000 与 001 → 125 ms，010 → 250 ms，011 → 500 ms，100 → 1 s）
 *   [6:5]   AVG[1:0]（00 不平均 / 01 8 次 / 10 32 次 / 11 64 次）
 *   [4] T/nA（0 报警模式）[3] POL [2] DR/Alert（0 = ALERT 引脚反映报警）[1] Soft_Reset（回读恒 0）[0] — */
#define TMP117_CFG_RW_MASK   0x0FFCu   /* 参与比对的字段：MOD..DR/Alert（bit 11..2）；状态位与 Soft_Reset 不比 */
#define TMP117_CFG_WORD      0x00A0u   /* MOD=00 连续转换；CONV=001（AVG=8 时 125 ms）；AVG=01（8 次平均）；其余 0。
                                         * 即出厂复位值只把 CONV 从 100（1 s）改成 001（125 ms），与 100 ms 采样基本对齐 */
uint8_t tmp117_cfg_matches(uint16_t cfg);   /* 1=配置寄存器回读值与期望一致（只比可读写字段）*/

/* 健康计数器（每次采样结果喂一步；返回 body_sensor_ok 当前值 0/1）
 * alarm_set/alarm_clr_req 由 control 侧实现注入：掉线沿置 ALM_BODY_SENSOR；恢复沿只清 ok 不清报警位 */
typedef struct {
    uint8_t ok;             /* 当前 body_sensor_ok */
    uint8_t err_run;        /* 连续错误计数 */
    uint8_t ok_run;         /* 连续成功计数 */
} tmp117_health_t;

void tmp117_health_init(tmp117_health_t *h);
/* step=1 成功 / 0 错误。alarm_raise 在掉线沿被调一次。 */
void tmp117_health_step(tmp117_health_t *h, int step, void (*alarm_raise)(void));

/* ====== 纯逻辑 2：滑动均值窗口（本侧新增；语义=H743 poll 内联逻辑）====== */
typedef struct {
    float win[TMP117_AVG_WIN];
    int   idx;
    int   fill;
} tmp117_avg_t;

void  tmp117_avg_init(tmp117_avg_t *a);
void  tmp117_avg_push(tmp117_avg_t *a, float v);
float tmp117_avg_mean(const tmp117_avg_t *a);

#ifdef __cplusplus
}
#endif
#endif /* TCS_TMP117_PURE_H */
