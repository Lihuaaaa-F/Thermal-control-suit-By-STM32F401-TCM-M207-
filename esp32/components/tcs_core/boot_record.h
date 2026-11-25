/**
 * boot_record.h — 上次运行记录（X04-F-03 / R5-13）：复位原因与上次运行时长要落得住
 *
 * 现场：ESP32 运行 1264 s 时自行复位一次，复位原因无法事后取证——`status` 里的 rst= 只有
 * 有人及时敲才看得到；启动日志又在 USB 串口重枚举期间丢失。本模块把两样东西留到下一次启动：
 *   - 本次启动的复位原因（esp_reset_reason()）；
 *   - 本次运行的时长、最后的报警位 / 安全锁存 / 控制状态（周期刷新）。
 * 存放在 RTC 慢速存储的 NOINIT 段：软复位、panic、看门狗复位都保得住，只有冷上电（以及把
 * RTC 域断掉的欠压）才是随机内容。所以记录带魔数 + 版本 + 长度 + CRC32，随机内容一律判无效。
 *
 * 纯逻辑、零 HAL（主机单测 tests/test_tcs_core.c）：存储放在哪里、多久刷新一次由调用方决定
 * （目标板上是 main/tcs_bootrec.c）。
 */
#ifndef TCS_BOOT_RECORD_H
#define TCS_BOOT_RECORD_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BOOTREC_MAGIC   0x54435342u   /* 'TCSB'（小端存放为 42 53 43 54）*/
#define BOOTREC_VER     0x0001u

/* 复位原因的数值：与 esp_reset_reason_t（IDF esp_system.h）逐项对应。
 * 目标板上 main/tcs_bootrec.c 有 _Static_assert 对表，任何一边改了数值都编译不过。*/
#define BOOTREC_RST_UNKNOWN     0
#define BOOTREC_RST_POWERON     1
#define BOOTREC_RST_EXT         2
#define BOOTREC_RST_SW          3
#define BOOTREC_RST_PANIC       4
#define BOOTREC_RST_INT_WDT     5
#define BOOTREC_RST_TASK_WDT    6
#define BOOTREC_RST_WDT         7
#define BOOTREC_RST_DEEPSLEEP   8
#define BOOTREC_RST_BROWNOUT    9
#define BOOTREC_RST_SDIO        10
#define BOOTREC_RST_USB         11
#define BOOTREC_RST_JTAG        12
#define BOOTREC_RST_EFUSE       13
#define BOOTREC_RST_PWR_GLITCH  14
#define BOOTREC_RST_CPU_LOCKUP  15
#define BOOTREC_RST_N           16

/* 28 字节、无内部填充：主机测试断言 sizeof == 28，目标板上 _Static_assert 与 esp_reset_reason_t 对表 */
typedef struct {
    uint32_t magic;         /* BOOTREC_MAGIC */
    uint16_t ver;           /* BOOTREC_VER：布局变了就升，旧记录按无效处理 */
    uint16_t size;          /* sizeof(boot_record_t)：防止不同编译选项下的布局差异被当成有效记录 */
    uint32_t seq;           /* 启动序号：上一条有效记录 +1；没有有效记录 = 1 */
    uint32_t uptime_s;      /* 本次运行时长（周期刷新，最多滞后一个刷新周期）*/
    uint16_t alarm;         /* 最近一次刷新时的报警位（g_tcs.alarm_bits）*/
    uint8_t  latch;         /* 最近一次刷新时的安全锁存（control_safety_latched()）*/
    uint8_t  ctrl_state;    /* 最近一次刷新时的控制状态（ctrl_state_t）*/
    uint8_t  reset_reason;  /* 本次启动的 esp_reset_reason() */
    uint8_t  flags;         /* bit0 = 启动时没有上一轮的有效记录（冷上电 / 首次运行 / 记录被改写）*/
    uint16_t pad;
    uint32_t crc;           /* 前面 24 字节的 CRC32（bootrec_crc32）*/
} boot_record_t;

/* 上一轮运行留下的信息（bootrec_start 抄出来给日志 / status 用）*/
typedef struct {
    uint8_t  valid;         /* 0 = 没有上一轮的记录 */
    uint8_t  reset_reason;  /* 上一轮是被什么复位原因**启动**的；上一轮**怎么结束的**看本轮的复位原因 */
    uint8_t  latch;
    uint8_t  ctrl_state;
    uint16_t alarm;
    uint32_t uptime_s;      /* 上一轮运行了多久（最后一次刷新时的值）*/
    uint32_t seq;           /* 上一轮的启动序号 */
} bootrec_prev_t;

uint32_t    bootrec_crc32(const uint8_t *p, size_t n);       /* 标准 CRC-32（poly 0xEDB88320，反射）*/
int         bootrec_valid(const boot_record_t *rec);         /* 魔数 / 版本 / 长度 / CRC 全对才算 */
/* 上电第一步：校验存储里的旧记录并抄到 *prev；随后写入本轮的新记录。返回本轮启动序号。*/
uint32_t    bootrec_start(boot_record_t *rec, uint8_t reset_reason, bootrec_prev_t *prev);
/* 周期刷新（调用方保证串行）：更新运行时长与当前报警位 / 锁存 / 控制状态。记录没 start 过则不动。*/
void        bootrec_refresh(boot_record_t *rec, uint32_t uptime_s, uint16_t alarm,
                            uint8_t latch, uint8_t ctrl_state);
/* 复位原因归类与名字：数值与 esp_reset_reason_t 逐项对应（目标板上有 _Static_assert 对表）*/
int         bootrec_is_crash(int reset_reason);              /* 1 = 异常结束（panic / 看门狗 / 欠压 / 锁死…）*/
const char *bootrec_reset_name(int reset_reason);

/* status 一行的"上次"字段（R5-13：status 同时打印"本次"和"上次"；"本次"的 rst= / up= 由调用方
 * 照旧打印）。追加到 out：prst=上一轮的启动复位原因、pup=上一轮运行时长、palm= / plat=上一轮
 * 最后一次刷新时的报警位 / 锁存、pseq=启动序号；nvalm=随安全锁存落盘的报警位快照、cd=是否留着
 * core dump。prev 为 NULL 或 valid=0 时 prst=-1（没有上一轮的记录）。返回写入的字符数。*/
size_t bootrec_format_prev(const bootrec_prev_t *prev, uint16_t nvalm, uint8_t core_dump,
                           char *out, size_t cap);

#ifdef __cplusplus
}
#endif
#endif /* TCS_BOOT_RECORD_H */
