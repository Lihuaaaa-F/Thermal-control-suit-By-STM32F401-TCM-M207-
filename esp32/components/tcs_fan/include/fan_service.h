/**
 * fan_service.h — 风扇驱动（ESP32-S3；R1d，任务书 v11.3 第十三章）
 *
 * **GPIO42** → 光耦，**低电平=开**（接线表 F1）。v2.1 换脚：风扇 PWM 原在 GPIO48，与板载 WS2812 RGB
 * 共脚（PWM 被灯珠当噪声数据锁存、随机亮色），改到 42。GPIO48 / GPIO38 现在只用于开机时给板载
 * RGB 灯珠发清灯帧（v1.0 板 = 48，v1.1 板 = 38），与风扇无关。
 * LEDC PWM 25kHz/10bit；占空比 API 0-100（低电平占空比=百分比）；默认二值 0/100。
 * control_ops.fan_set(uint8_t on) 由 R1e 装配到 fan_set_on（0/100 直控）。
 * 硬件上没有转速反馈（无 tach / 电流采样）。板上自测做电平回读软件子集（[FANLB]，L3）；
 * 运行期有"命令-回读"对账（fan_get_act，见下）；风扇本体转不转、线断没断发现不了。
 * P-2 电压两态（万用表）挂账用户。
 */
#ifndef TCS_FAN_SERVICE_H
#define TCS_FAN_SERVICE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FAN_GPIO        42   /* v2.1 换脚：48 与板载 WS2812 RGB 共脚（PWM 被灯珠当噪声），42 空闲无特殊功能 */
#define FAN_FREQ_HZ     25000
#define FAN_DUTY_MAX    100

/* LEDC 10 位占空比满量程：2^10 - 1 */
#define FAN_DUTY_FULL   1023u

/* ====== 既有接口（签名未变）====== */
int fan_service_start(void);          /* LEDC 初始化（duty=0 关）。0=ok */
void fan_set_duty(uint8_t pct);       /* 0-100；0=关（引脚恒高），100=全开（恒低）*/
void fan_set_on(uint8_t on);          /* 二值包装（control ops 用）：0→0 / 非0→100 */
uint8_t fan_get_duty(void);           /* 当前设定值（0-100）*/

void fan_ws2812_black_frame(int gpio_num);  /* 对任意脚发 WS2812 全黑帧（LED 排查/清灯）*/
void fan_ws2812_ledtest(int gpio_num);      /* 红闪 700ms→黑（人眼可视自证帧时序+引脚）*/

/* 上电自测：0/100 两态引脚电平回读（低=开红线）+ 中点不炸；证据=[FANLB] */
void fan_lb_boottest(void);

/* ====== 执行器侧"命令-回读"（S-G e2：X04-F-05 ②、M13-F-06）======
 * fan_get_act() 把"命令值"与"引脚实际在做什么"对账。它能发现的是 ESP32 这一侧的输出通路故障：
 * LEDC 通道被别人改了、引脚被别的功能 / 别的驱动占走（gpio_config、WS2812 借脚后没还）。
 * 它发现不了风扇本体：光耦、扇叶、供电 —— 硬件上没有转速反馈。
 * 服务启动后每秒核对一次；回读与命令不符计一次 mismatch（累计可读）。 */
typedef enum {
    FAN_ACT_UNKNOWN = 0,   /* LEDC 通道尚未配置 */
    FAN_ACT_OFF,           /* 命令 0%：占空比回读=满、引脚恒高 —— 一致 */
    FAN_ACT_ON,            /* 命令 100%：占空比回读=0、引脚恒低 —— 一致 */
    FAN_ACT_PWM,           /* 0<命令<100%：占空比回读与命令一致；引脚在 25kHz 翻转，电平无法逐点核对 */
    FAN_ACT_MISMATCH,      /* 回读与命令不一致：输出通路有问题 */
} fan_act_t;

fan_act_t  fan_get_act(void);              /* 当前引脚实际状态（按上面的对账判定）*/
const char *fan_act_name(fan_act_t a);     /* 判定值的人话名字（日志 / 控制台用）*/
uint32_t   fan_mismatch_count(void);       /* 周期核对发现回读与命令不符的累计次数 */

/* ====== 纯逻辑（主机可测；实现在本头文件里）====== */
/* 命令占空比 → LEDC 占空比（高电平计数）：低=开，所以低电平占空比 = pct% → 高电平 = (100-pct)% */
static inline uint32_t fan_duty_of_pct(uint8_t pct)
{
    if (pct > FAN_DUTY_MAX) pct = FAN_DUTY_MAX;
    return (uint32_t)(FAN_DUTY_MAX - pct) * FAN_DUTY_FULL / FAN_DUTY_MAX;
}

/* 由"命令占空比、LEDC 占空比回读、引脚电平采样"判定引脚实际在做什么。
 * lvl_lo / lvl_hi = 连续采样引脚电平得到的最小 / 最大值（恒定电平 → lo == hi）。
 * duty_full = LEDC 占空比满量程（本服务为 10 位 = 1023）。 */
static inline fan_act_t fan_act_classify(uint8_t cmd_pct, uint32_t duty_rb, uint32_t duty_full,
                                         int lvl_lo, int lvl_hi)
{
    if (duty_full == 0u || duty_rb > duty_full) return FAN_ACT_MISMATCH;
    if (cmd_pct > FAN_DUTY_MAX) cmd_pct = FAN_DUTY_MAX;
    if (duty_rb != fan_duty_of_pct(cmd_pct)) return FAN_ACT_MISMATCH;
    if (cmd_pct == 0u)
        return (lvl_lo == 1 && lvl_hi == 1) ? FAN_ACT_OFF : FAN_ACT_MISMATCH;   /* 关：引脚应恒高 */
    if (cmd_pct >= FAN_DUTY_MAX)
        return (lvl_lo == 0 && lvl_hi == 0) ? FAN_ACT_ON : FAN_ACT_MISMATCH;    /* 全开：引脚应恒低 */
    return FAN_ACT_PWM;   /* 中间占空比：占空比回读一致；引脚在 25kHz 翻转，电平不逐点核对 */
}

#define FAN_ACT_SAMPLES 16    /* ×3µs ≈ 48µs：覆盖 25kHz 的一个整周期（40µs）*/

/* ====== 弱校验（M13-F-06 方案②；只给判定，不报警、不进 SAFE——接线归 control，阈值未在真机标定）======
 * 思路：TEC 持续输出（加热或制冷）满 min_on_ms，水温却几乎不动 → 执行器侧（风扇 / TEC）
 * 或温度来源可疑。这是间接证据：热负载、环境温度、传感器滞后都会让它误判，
 * 所以只能当弱线索，阈值必须上机标定后再决定怎么用。 */
typedef struct {
    uint32_t min_on_ms;    /* TEC 连续输出多久才下结论（默认 5 min）*/
    float    min_delta_c;  /* 这段时间里水温至少应变化多少（默认 0.5°C）*/
} fan_weak_cfg_t;

typedef struct {
    uint8_t  tec_on_prev;  /* 上一拍 TEC 是否在输出 */
    uint32_t on_since_ms;  /* 本轮 TEC 连续输出的起点；0=当前没有输出 */
    float    temp_at_on_c; /* 本轮开始时的水温 */
} fan_weak_ctx_t;

/* 返回 1=可疑：TEC 连续输出 ≥ min_on_ms 而 |水温变化| < min_delta_c。
 * now_ms=0 表示时间基准未建立，不下结论。cfg 可为 NULL（用默认阈值）。 */
static inline int fan_weak_check(uint8_t tec_on, float temp_c, uint32_t now_ms,
                                 const fan_weak_cfg_t *cfg, fan_weak_ctx_t *c)
{
    uint32_t min_on = cfg ? cfg->min_on_ms : 300000u;
    float min_d = cfg ? cfg->min_delta_c : 0.5f;
    float d;
    if (c == 0) return 0;                           /* 本头文件只含 <stdint.h>：不用 NULL（目标构建里未定义）*/
    if (!tec_on) {                                    /* TEC 停了：本轮清零 */
        c->tec_on_prev = 0u;
        c->on_since_ms = 0u;
        return 0;
    }
    if (!c->tec_on_prev || c->on_since_ms == 0u) {    /* 新一轮连续输出：记起点 */
        c->tec_on_prev = 1u;
        if (now_ms == 0u) return 0;
        c->on_since_ms = now_ms;
        c->temp_at_on_c = temp_c;
        return 0;
    }
    if (now_ms == 0u) return 0;                       /* 时间基准未建立：不下结论 */
    if (now_ms - c->on_since_ms < min_on) return 0;
    d = temp_c - c->temp_at_on_c;
    if (d < 0.0f) d = -d;
    return (d < min_d) ? 1 : 0;
}

#ifdef __cplusplus
}
#endif
#endif /* TCS_FAN_SERVICE_H */
