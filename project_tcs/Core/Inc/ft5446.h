/**
 * ft5446.h — FT5446/FT5206 电容触摸驱动（M3；传输层 = ST HAL I2C4，PD12/PD13）
 *
 * 抄官方 LVGL 例程 ctiic.c + ft5206.c（FT5206 与 FT5446 同系列，协议一致）：
 *   - 7bit 地址 0x38（写 0x70 / 读 0x71）
 *   - TP_INT=PH7 输入上拉（轮询）、TP_RST=PH6 推挽（低电平有效复位）
 *   - 传输层：W1a 起缺省走硬件 I2C4；定义 FT5446_BITBANG 保留旧的软件 I2C
 *     （SCL=PD12 推挽、SDA=PD13 开漏+上拉）
 *
 * WP-M2（v10.11）坐标映射定版【T-13】：
 *   本工程首版映射（标准字节序+X 镜像）与 GS 验证版差一个 90° 旋转，为 D1 头号
 *   嫌疑（总师预对拍 2026-10-19：GS 版 2026-07-28 真手指实测"raw 直接当屏幕像素，
 *   无需仿射"=交换 X/Y 不镜像）。本版恢复 GS 验证映射，变换收敛到唯一纯函数
 *   ft5446_map_raw()（PC 单测逐点断言 + raw 级注入走全链，封死注入绕过变换层盲区）。
 *
 * 帧级幽灵过滤已移除：H1 跳距过滤由触摸会话状态机（touch_session.c）按"整会话"
 * 判定取代，跨会话绝不惩罚。本驱动只负责：raw 读取 + 坏包防御 + 映射 + raw 日志。
 *
 * S-G h1a（2026-11-25，审查 M02-F-01/02/04）：
 *   - 读失败就是读失败：ft5446_read() 返回 -1、样本无效，不再当成"合法无触摸"，
 *     也不再用没初始化的缓冲去解析坐标；
 *   - 在线 = 4 个配置寄存器写成功且回读一致（ft5446_probe_online）。版本寄存器只记录，
 *     不再当在线凭据（旧判定把读回 0x0000 也算在线）；
 *   - 触摸链路掉线后自动恢复：判定在 touch_session.c（纯逻辑、PC 单测），
 *     动作在本驱动，由 ft5446_service() 每拍推进一小步，不阻塞主循环。
 */
#ifndef FT5446_H
#define FT5446_H

#include <stdint.h>
#include "touch_session.h"    /* tp_link_t：链路健康/恢复的状态与判定 */
#ifndef TCS_UNIT_TEST
#include "stm32h7xx_hal.h"
#endif

/* ====== FT5446 参数（抄 ft5206.h）====== */
#define FT5446_CMD_WR           0x70    /* 写命令（0x38<<1，末位0）*/
#define FT5446_CMD_RD           0x71    /* 读命令（末位1）*/
#define FT5446_DEVIDE_MODE      0x00    /* 模式控制 */
#define FT5446_REG_NUM_FINGER   0x02    /* 触点状态寄存器 */
#define FT5446_TP1_REG          0x03    /* 第1触点数据地址（6B：XY+weight+area）*/
#define FT5446_ID_G_MODE        0xA4    /* 中断模式控制（0=轮询）*/
#define FT5446_ID_G_THGROUP     0x80    /* 触摸有效值 */
#define FT5446_ID_G_PERIODACTIVE 0x88   /* 激活状态周期 */
#define FT5446_ID_G_LIB_VERSION 0xA1    /* 版本号（T-11：入日志头）*/

/* 上电写入、恢复时重写的 4 个配置寄存器的值（抄 ft5206_init 序列）。
 * 后两个非 0，回读一致即可证明"写进去的读得回来"——在线判定靠它们。*/
#define FT5446_THGROUP_VAL      22u
#define FT5446_PERIOD_VAL       12u

/* ====== 触摸样本（驱动输出 = 会话机输入）====== */
typedef struct {
    uint8_t touched;    /* 1=有触摸（n>0 且首触点 contact 事件）*/
    uint8_t valid;      /* 0=坏包/越界（驱动已计数，会话机防御性忽略）*/
    uint8_t injected;   /* 1=注入样本（T-13 raw 级注入 / 屏坐标注入）*/
    uint8_t contacts;   /* 原始触点数 n（reg 0x02 低 4 位）*/
    uint8_t touch_id;   /* 触点 id（buf[2] 高 4 位）*/
    uint8_t weight;     /* 触点重量（reg 0x07）*/
    uint8_t area;       /* 触点面积（reg 0x08，幽灵判定面积恒定项）*/
    int     x;          /* 屏幕像素 X（ft5446_map_raw 输出，0..LCD_WIDTH-1）*/
    int     y;          /* 屏幕像素 Y（0..LCD_HEIGHT-1）*/
} touch_event_t;

/* ====== 纯逻辑段（PC 单测直接调用；下面的函数不碰任何寄存器）====== */

/* 坐标映射（唯一出口，PC 单测逐点断言）
 * GS 验证版（2026-07-28 本屏真手指实测）：交换 X/Y，不镜像。
 * 面板原生报点轴与横屏显示差 90°：screen_x = raw_y，screen_y = raw_x。
 * 之后走仿射校准（默认恒等）并 clamp 到屏内。*/
void ft5446_map_raw(uint16_t xr, uint16_t yr, int *sx, int *sy);

/* 样本清零（touched/valid/injected/contacts/touch_id/weight/area；x/y 不动）*/
void ft5446_ev_clear(touch_event_t *ev);

/* 读一拍的寄存器读取回调：目标机 = HAL，PC 单测 = 桩。0=成功，非 0=失败（且不写 buf）。*/
typedef int (*ft5446_rd_fn)(void *ctx, uint8_t reg, uint8_t *buf, uint8_t len);

/* 读一拍：触点状态 → （有触点时）第 1 触点 6 字节 → 解析 → 映射 → raw 日志。
 * 返回 0 = 两次寄存器读都成功（样本本身仍可能因事件标志不是 contact 被丢弃，看 ev->valid）；
 * 返回 -1 = 有一次读失败：ev 的 touched/valid 等字段全 0，**绝不出样本**（M02-F-02）。*/
int ft5446_read_ops(touch_event_t *ev, uint32_t now_ms, ft5446_rd_fn rd, void *ctx);

/* 在线判定的输入（纯函数 ft5446_probe_online 用）*/
typedef struct {
    uint8_t wr_ok;      /* bit0..3：DEVIDE_MODE / ID_G_MODE / THGROUP / PERIODACTIVE 写成功 */
    uint8_t rd_ok;      /* bit0：THGROUP 回读成功；bit1：PERIODACTIVE 回读成功 */
    uint8_t thgroup;    /* THGROUP 回读值 */
    uint8_t period;     /* PERIODACTIVE 回读值 */
    uint8_t ver_ok;     /* 1 = 版本寄存器读成功（不参与判定，只作记录）*/
    uint8_t ver[2];     /* 版本寄存器值 */
} ft5446_probe_t;

/* 在线判定：4 个配置写都成功 + 两个非 0 寄存器回读一致。版本号不设门。1=在线。*/
uint8_t ft5446_probe_online(const ft5446_probe_t *p);

/* ====== raw 日志（M0：时间戳/触点数/raw xy/事件/id/重量/面积，RAM 环形）====== */
#define FTLOG_RING_N  4096u

typedef struct {
    uint32_t t_ms;      /* millis() */
    int16_t  xr, yr;    /* 原始报点 */
    uint8_t  n;         /* 触点数（0=抬起标记）*/
    uint8_t  ev;        /* 事件旗标（buf[0] 高 4 位）*/
    uint8_t  id;        /* 触点 id */
    uint8_t  src;       /* bit0=注入样本 */
} ftlog_ent_t;          /* 12B × 4096 = 48KB */

void ftlog_clear(void);
void ftlog_enable(uint8_t on);
uint32_t ftlog_count(void);          /* 累计入环条数（环形覆盖不减少）*/
const ftlog_ent_t *ftlog_at(uint32_t idx);   /* idx < ftlog_count()，老→新 */
uint16_t ft5446_fw_version(void);    /* T-11：IC 固件版本寄存器值（最近一次核对时锁存）*/

/* 驱动层计数（只读遥测，三档都有）*/
extern volatile uint32_t g_ft5446_nak_cnt;        /* 读事务 NAK 次数（HAL_I2C_ERROR_AF）*/
extern volatile uint32_t g_ft5446_read_cnt;       /* 读事务总数（NAK 率分母）*/
extern volatile uint32_t g_ft5446_buserr_cnt;     /* 读事务里 NAK 以外的失败：超时/总线错/两线被拉死 */
extern volatile uint32_t g_ft5446_badsample_drops;/* 坏包（事件标志非 contact）丢弃数 */

/* ====== raw 级注入（TCS_SIM 构建，T-13：携带 raw 坐标走完整变换链）====== */
void ft5446_raw_inject(int down, uint16_t xr, uint16_t yr);

/* ====== API ====== */

/* 初始化：I2C + 复位 FT5446 + 配寄存器 + 回读核对。返回 0=成功；
 * -2=器件不应答/回读不一致；-3=I2C4 时钟或引脚初始化失败；-4=配置寄存器写失败。
 * 失败时触摸离线，主循环里的 ft5446_service() 会接着试（上电时触摸没起来也能自己恢复）。*/
int ft5446_init(void);

/* 触摸是否在线（链路状态 == 在线）。掉线后为 0，恢复成功回到 1。*/
int touch_ready(void);

/* 读当前触摸样本（轮询）。只读+映射+日志，不做会话级过滤（会话机负责）。
 * 返回 0=这一拍总线读成功（无触摸时 ev->touched=0、ev->valid=1，供会话机结算抬起）；
 * -1=读失败或触摸离线：ev->valid=0，没有任何样本。*/
int ft5446_read(touch_event_t *ev);

/* 每拍调用一次（主循环，放在第一次 ft5446_read() 之前）：触摸链路掉线后的恢复。
 * 每拍最多做一小步（清总线 / 外设重配 / 复位 IC / 写配置 / 回读核对各算一步），
 * 步与步之间的延时靠"等时间到"，不阻塞。在线时立即返回。*/
void ft5446_service(void);

/* 链路状态的只读快照：在线/离线、恢复轮数、恢复成功次数、连续失败轮询数、离线时长。
 * 界面横幅、控制台、探针取数都从这里拿。*/
void ft5446_link_status(tp_link_t *out);

/* 触摸校准：仿射参数 x_s=a*x+b, y_s=c*y+d（映射后微调，默认恒等）。*/
void ft5446_set_calibration(float a, float b, float c, float d);

/* ====== 台架钩子（只在台架/仿真档；交付档不编译，符号表里查不到）======
 * 写 1 → 下一拍 ft5446_service() 把在线的触摸链路强制打掉，走一遍完整的恢复流程。
 * 用途：不用拔排线就能在真机上反复验证恢复流程（拔插只在验证"掉线判定"时才需要）。*/
#if TCS_PROFILE >= 1
#include "tcs_profile.h"
extern volatile uint8_t g_tp_force_recover;
#endif

#endif /* FT5446_H */
