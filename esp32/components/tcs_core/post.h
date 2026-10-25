/**
 * post.h — 上电自检(POST 六项) + 动态布防三态机 + 合理性滤波（H6，任务书 v7.3 §2）
 *
 * 六项自检：TMP117 ID=0x0117 / M207 一拍(首次在线) / EEPROM CRC / 参数表 /
 *           屏触(lcd+touch ready) / 版本(协议+固件+注册表非空)。
 * 布防三态机：UNARMED_ABSENT ⇄ ARMED_ONLINE ⇄ LOST_ALARM
 *   - 0x08 commissioned 语义升级（id 不变义）：0=auto(POST 过且源健康→布防)
 *     1=强制布防(FMEA 恒开) 2=强制不布防(安静,演示/未接线)
 *   - ARMED→LOST：源失联沿（报警由 control FMEA 置位，本机记状态）
 *   - LOST→ARMED：人工清报警(acked) 且 连续 10 次健康采样(上线补入)
 *   - 任意态→UNARMED：mode08==2
 * 合理性滤波（H6.7）：体温 25~45°C 且 |Δ|≤2°C/s；M207 |Δ|>10°C/s → 按失联路径。
 *
 * 纯逻辑（零 HAL）PC 单测：post_arm_step/post_*_sanity_ok/post_set_result 聚合；
 * 目标机侧 post_init/post_poll 在 main 装配（TCS_UNIT_TEST 下不编译）。
 */
#ifndef TCS_POST_H
#define TCS_POST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====== 六项自检 id ====== */
typedef enum {
    POST_ITEM_TMP117 = 0,    /* 读 Device ID == 0x0117 */
    POST_ITEM_M207,          /* 一拍内首次在线（宽限窗内）*/
    POST_ITEM_EEPROM,        /* 镜像 magic/ver/CRC 有效（离线=FAIL 不阻塞）*/
    POST_ITEM_TABLE,         /* 参数表序列化 CRC+范围自洽 */
    POST_ITEM_SCRTOUCH,      /* lcd_ready && touch_ready */
    POST_ITEM_VER,           /* 协议版本+固件 BCD+注册表非空 */
    POST_ITEM_N              /* =6 */
} post_item_t;

/* ====== 布防三态 ====== */
typedef enum {
    POST_ARM_UNARMED_ABSENT = 0,
    POST_ARM_ARMED_ONLINE   = 1,
    POST_ARM_LOST_ALARM     = 2,
} post_arm_t;

#define POST_ARM_RECOVER_RUN  10u   /* LOST→ARMED 上线补入：连续 10 次健康采样 */
#define POST_M207_WAIT_MS     2500u /* M207 一拍宽限（≤ boot→UI 3s 预算内不阻塞）*/

/* ====== 纯逻辑 1：合理性滤波（H6.7）====== */
/* 体温：25~45°C 且 |Δ| ≤ 2°C/s（dt_s 为距上一有效样本的秒数）。首样本（dt=0）只查范围。 */
uint8_t post_body_sanity_ok(float prev, float cur, float dt_s);
/* M207 水温：|Δ| > 10°C/s 判不可信（调用方走失联路径）；首样本放行。 */
uint8_t post_m207_sanity_ok(float prev, float cur, float dt_s);

/* ====== 纯逻辑 2：布防三态机 ====== */
typedef struct {
    uint8_t state;        /* post_arm_t */
    uint8_t healthy_run;  /* LOST 期连续健康采样计数 */
} post_arm_ctx_t;

void post_arm_init(post_arm_ctx_t *c);
/**
 * 每次健康采样节拍（100ms）喂一步，返回新状态。
 * @param mode08     参数 0x08 当前值（0 auto / 1 强制布防 / 2 强制不布防）
 * @param post_pass  POST 六项全过（done 且全绿）
 * @param healthy    体温+M207 数据源当前均健康
 * @param acked      失联类报警位均已被人工清除
 */
post_arm_t post_arm_step(post_arm_ctx_t *c, uint8_t mode08, uint8_t post_pass,
                         uint8_t healthy, uint8_t acked);

/* ====== 纯逻辑 3：结果聚合 ====== */
void    post_reset_results(void);
void    post_set_result(uint8_t item, uint8_t ok);   /* 测试/目标机共写入口 */
uint8_t post_item_ok(uint8_t item);
uint8_t post_item_decided(uint8_t item);             /* 该项是否已裁决 */
uint8_t post_all_pass(void);                         /* 六项全绿 */

/* ====== 纯逻辑 4：布防状态（control FMEA 判据由此取，PC 测试可直写）====== */
void       post_arm_commit(post_arm_t st);   /* 目标机 poll 步进后 / 测试直写 */
post_arm_t post_arm_state(void);
uint8_t    post_armed(void);                 /* state != UNARMED_ABSENT */

/* ====== 目标机接口（TCS_UNIT_TEST 下不编译）====== */
#ifndef TCS_UNIT_TEST

void post_init(int eeprom_cfg_invalid);   /* 立即项打分（EEPROM 结果由 main 传入）*/
void post_poll(uint32_t now_ms);          /* 主循环节拍：M207 宽限 + 100ms 布防步进 */

uint8_t    post_done(void);               /* 六项全部已裁决 */
uint32_t   post_result_mask(void);        /* bit i = item i pass（console/sim 取证）*/
uint32_t   post_done_mask(void);          /* bit i = item i 已裁决 */
/* 角标显示控制（tcs_ui 读）：0=未开始 1=显示中 2=已淡出 */
uint8_t    post_badge_state(void);
void       post_badge_reshow(uint32_t ms);     /* 重显 ms 毫秒（console post badge [ms]，默认/0=3s；取证/彩排用）*/
void       post_sim_fail_item(uint8_t item);   /* TCS_SIM：强制单项 FAIL（演示/取证）*/

#endif /* !TCS_UNIT_TEST */

#ifdef __cplusplus
}
#endif
#endif /* TCS_POST_H */
