/**
 * post_pure.c — post.c 纯逻辑区（ESP32 侧复用件）
 *
 * 逐字提取自 project_tcs/App/tcs/post.c 纯逻辑区（合理性滤波两函数），Q5 单向同步：
 * 改逻辑只许改 project_tcs 源侧再复制过来。R1b 需 post_m207_sanity_ok（H6.7 滤波）；
 * R1c 续用 post_body_sanity_ok（TMP117 体温滤波）；R1e 续用布防三态机（届时补齐提取）。
 * 声明见同组件 post.h（md5 基线管理，本文件不改动它）。
 */
#include "post.h"

#define POST_BODY_MIN_C    25.0f
#define POST_BODY_MAX_C    45.0f
#define POST_BODY_MAX_RATE 2.0f    /* °C/s */
#define POST_M207_MAX_RATE 10.0f   /* °C/s */

uint8_t post_body_sanity_ok(float prev, float cur, float dt_s)
{
    if (cur < POST_BODY_MIN_C || cur > POST_BODY_MAX_C) return 0;
    if (dt_s <= 0.0f) return 1;                       /* 首样本只查范围 */
    float d = cur - prev;
    if (d < 0) d = -d;
    return (d <= POST_BODY_MAX_RATE * dt_s) ? 1u : 0u;
}

uint8_t post_m207_sanity_ok(float prev, float cur, float dt_s)
{
    if (dt_s <= 0.0f) return 1;
    float d = cur - prev;
    if (d < 0) d = -d;
    return (d <= POST_M207_MAX_RATE * dt_s) ? 1u : 0u;
}

/* ====== H6 布防三态机（R1e 提取；project_tcs post.c 纯逻辑区 37-85/120-125 行逐字）====== */
void post_arm_init(post_arm_ctx_t *c)
{
    c->state = (uint8_t)POST_ARM_UNARMED_ABSENT;      /* 上电默认不布防，auto 由 FSM 升入 */
    c->healthy_run = 0;
}

post_arm_t post_arm_step(post_arm_ctx_t *c, uint8_t mode08, uint8_t post_pass,
                         uint8_t healthy, uint8_t acked)
{
    if (mode08 == 2u) {                               /* 强制不布防（安静）*/
        c->state = (uint8_t)POST_ARM_UNARMED_ABSENT;
        c->healthy_run = 0;
        return (post_arm_t)c->state;
    }

    switch (c->state) {
    case (uint8_t)POST_ARM_UNARMED_ABSENT:
        if (mode08 == 1u || (post_pass && healthy)) { /* 强制布防 / auto 首次升入 */
            c->state = (uint8_t)POST_ARM_ARMED_ONLINE;
            c->healthy_run = 0;
        }
        break;

    case (uint8_t)POST_ARM_ARMED_ONLINE:
        if (!healthy) {                               /* 失联沿 → LOST_ALARM */
            c->state = (uint8_t)POST_ARM_LOST_ALARM;
            c->healthy_run = 0;
        }
        break;

    case (uint8_t)POST_ARM_LOST_ALARM:
        if (healthy) {
            if (c->healthy_run < 255u) c->healthy_run++;
        } else {
            c->healthy_run = 0;
        }
        /* 上线补入：人工清报警 且 连续 10 次健康采样 */
        if (acked && c->healthy_run >= POST_ARM_RECOVER_RUN) {
            c->state = (uint8_t)POST_ARM_ARMED_ONLINE;
            c->healthy_run = 0;
        }
        break;

    default:
        c->state = (uint8_t)POST_ARM_UNARMED_ABSENT;
        break;
    }
    return (post_arm_t)c->state;
}

/* ====== 布防状态（单一存储；目标机 poll 步进后 commit，测试直写）====== */
static post_arm_t s_cur_state = POST_ARM_UNARMED_ABSENT;

void post_arm_commit(post_arm_t st) { s_cur_state = st; }
post_arm_t post_arm_state(void)     { return s_cur_state; }
uint8_t    post_armed(void)         { return (s_cur_state != POST_ARM_UNARMED_ABSENT); }
