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
