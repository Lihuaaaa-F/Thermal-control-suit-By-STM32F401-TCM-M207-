/**
 * param_registry.c — 参数注册表实现（任务书 §3.3）
 *
 * 参数表 v1（id 永久分配）：tc1_set/tc2_set/body_hot/body_cold/hyst/mode/
 * tc_set_confirm/commissioned(0x08 布防模式：0=auto POST 过且源健康→布防，
 * 1=强制布防，2=强制不布防；H6 起 max=2，旧 0/1 值语义兼容)。
 * M207 底层参数（0x100+，PF_WEB 无 PF_SCREEN，D6）待文档后在此表追加——注册表驱动
 * 一切界面，届时网页自动出现新控件（M5 演示），屏端调参页自动不出现（无 PF_SCREEN）。
 *
 * 约束实现：param_set 内强制 body_cold < body_hot（写任一侧校验另一侧，违反拒写 -2）。
 */
#include "param_registry.h"
#include "proto.h"        /* proto_crc8 复用（EE CRC8）*/
#include <string.h>

/* ====== 状态快照实例 ====== */
tcs_state_t g_tcs = {
    .tc1_act = 0.0f, .tc2_act = 0.0f, .body_temp = 0.0f,
    .tec1_on = 0, .tec2_on = 0, .fan_on = 0, .mode = 0,
    .alarm_bits = 0, .m207_online = 0, .body_sensor_ok = 0,
    .esp_link_ok = 0, .fw_ver = 0x010200u,   /* v1.2.0 BCD（第六阶段：触摸会话重写+UI 工艺）*/
};

/* ====== 参数表 v1（初值/[min,max]/step/flags 严格按任务书 §3.3 表）====== */
static param_desc_t s_params[PARAM_CAPACITY] = {
    /* id    key            name             unit   type            flags                                              min   max   step  value */
    { 0x01, "tc1_set",      "贴身水箱设定温度", "°C", PARAM_TYPE_F32,
      PF_RW|PF_SCREEN|PF_WEB|PF_PERSIST|PF_M207,              5.0f, 45.0f, 0.1f, 25.0f, 0, 0 },
    { 0x02, "tc2_set",      "环境水箱设定温度", "°C", PARAM_TYPE_F32,
      PF_RW|PF_SCREEN|PF_WEB|PF_PERSIST|PF_M207,              5.0f, 45.0f, 0.1f, 25.0f, 0, 0 },
    { 0x03, "body_hot",     "体温过热阈值",     "°C", PARAM_TYPE_F32,
      PF_RW|PF_SCREEN|PF_WEB|PF_PERSIST,                      36.0f, 40.0f, 0.1f, 37.5f, 0, 0 },
    { 0x04, "body_cold",    "体温过冷阈值",     "°C", PARAM_TYPE_F32,
      PF_RW|PF_SCREEN|PF_WEB|PF_PERSIST,                      35.0f, 39.0f, 0.1f, 36.5f, 0, 0 },
    { 0x05, "hyst",         "滞回回差",         "°C", PARAM_TYPE_F32,
      PF_RW|PF_SCREEN|PF_WEB|PF_PERSIST,                      0.1f,  1.0f,  0.1f, 0.2f,  0, 0 },
    { 0x06, "mode",         "控制模式",         "",   PARAM_TYPE_U8,
      PF_RW|PF_SCREEN|PF_WEB|PF_PERSIST,                      0.0f,  1.0f,  1.0f, 0.0f,  0, 0 },
    { 0x07, "tc_set_confirm", "高温二次确认阈值", "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_PERSIST,                                38.0f, 45.0f, 0.5f, 40.0f, 0, 0 },
    { 0x08, "commissioned", "布防模式",     "",   PARAM_TYPE_U8,
      PF_RW|PF_SCREEN|PF_WEB|PF_PERSIST,                      0.0f,  2.0f,  1.0f, 0.0f,  0, 0 },
    /* ====== H10 映射全表（v1.2 §2.5；所有权矩阵=模块 EEPROM 唯一真值，本地不持久）======
     * flags：PF_RW|PF_WEB|PF_M207（无 PF_SCREEN=屏不显红线；无 PF_PERSIST=不落本地 EEPROM）。
     * 值域/默认按《现行M207协议_v1.md》字典；危险参数加 PF_DANGER（网页二次确认）。
     * R5（ESP32）接真实传输：写=透传+50ms 读回校验，上电=模块读回镜像。 */
    /* --- TC1（0x100-0x10F）--- */
    { 0x100, "m207_kp",        "TC1 PID 比例",     "",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.0f, 100.0f, 0.1f, 0.4f,  0, 0 },
    { 0x101, "m207_ki",        "TC1 PID 积分",     "",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.0f, 9999.0f, 1.0f, 40.0f, 0, 0 },
    { 0x102, "m207_kd",        "TC1 PID 微分",     "",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.0f, 9999.0f, 0.01f, 0.1f, 0, 0 },
    { 0x103, "m207_t1_otp_ht", "TC1 过温高阈值",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 150.0f, 1.0f, 30.0f, 0, 0 },
    { 0x104, "m207_t1_otp_lt", "TC1 过温低阈值",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 150.0f, 1.0f, 15.0f, 0, 0 },
    { 0x105, "m207_t1_otp_hy", "TC1 过温滞回",     "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 50.0f, 1.0f, 1.0f,  0, 0 },
    { 0x106, "m207_t1_otpm",   "TC1 过温保护模式", "",  PARAM_TYPE_U8,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 2.0f, 1.0f, 0.0f,  0, 0 },
    { 0x107, "m207_t1_ocp",    "TC1 过流保护阈值", "A",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 10.0f, 0.1f, 5.0f,  0, 0 },
    { 0x108, "m207_t1_ovp",    "TC1 过压保护阈值", "V",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 24.0f, 0.1f, 9.0f,  0, 0 },
    { 0x109, "m207_t1_rtoch",  "TC1 开路保护高",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 200.0f, 1.0f, 100.0f, 0, 0 },
    { 0x10A, "m207_t1_rtocl",  "TC1 开路保护低",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 200.0f, 1.0f, -20.0f, 0, 0 },
    { 0x10B, "m207_t1_mint",   "TC1 最低设定温度", "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   -50.0f, 150.0f, 1.0f, -10.0f, 0, 0 },
    { 0x10C, "m207_t1_maxt",   "TC1 最高设定温度", "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   -50.0f, 200.0f, 1.0f, 120.0f, 0, 0 },
    { 0x10D, "m207_t1_maxv",   "TC1 最大输出电压", "V",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 12.0f, 0.1f, 8.0f,  0, 0 },
    { 0x10E, "m207_t1_vratio", "TC1 冷热电压比",   "",   PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.1f, 10.0f, 0.1f, 1.0f,  0, 0 },
    { 0x10F, "m207_t1_step",   "TC1 温度步进",     "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.001f, 10.0f, 0.001f, 0.01f, 0, 0 },
    /* --- TC2（0x110-0x11F，与 TC1 逐项镜像）--- */
    { 0x110, "m207_t2_kp",     "TC2 PID 比例",     "",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.0f, 100.0f, 0.1f, 0.4f,  0, 0 },
    { 0x111, "m207_t2_ki",     "TC2 PID 积分",     "",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.0f, 9999.0f, 1.0f, 40.0f, 0, 0 },
    { 0x112, "m207_t2_kd",     "TC2 PID 微分",     "",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.0f, 9999.0f, 0.01f, 0.1f, 0, 0 },
    { 0x113, "m207_t2_otp_ht", "TC2 过温高阈值",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 150.0f, 1.0f, 30.0f, 0, 0 },
    { 0x114, "m207_t2_otp_lt", "TC2 过温低阈值",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 150.0f, 1.0f, 15.0f, 0, 0 },
    { 0x115, "m207_t2_otp_hy", "TC2 过温滞回",     "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 50.0f, 1.0f, 1.0f,  0, 0 },
    { 0x116, "m207_t2_otpm",   "TC2 过温保护模式", "",  PARAM_TYPE_U8,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 2.0f, 1.0f, 0.0f,  0, 0 },
    { 0x117, "m207_t2_ocp",    "TC2 过流保护阈值", "A",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 10.0f, 0.1f, 5.0f,  0, 0 },
    { 0x118, "m207_t2_ovp",    "TC2 过压保护阈值", "V",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 24.0f, 0.1f, 9.0f,  0, 0 },
    { 0x119, "m207_t2_rtoch",  "TC2 开路保护高",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 200.0f, 1.0f, 100.0f, 0, 0 },
    { 0x11A, "m207_t2_rtocl",  "TC2 开路保护低",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 200.0f, 1.0f, -20.0f, 0, 0 },
    { 0x11B, "m207_t2_mint",   "TC2 最低设定温度", "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   -50.0f, 150.0f, 1.0f, -10.0f, 0, 0 },
    { 0x11C, "m207_t2_maxt",   "TC2 最高设定温度", "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   -50.0f, 200.0f, 1.0f, 120.0f, 0, 0 },
    { 0x11D, "m207_t2_maxv",   "TC2 最大输出电压", "V",  PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 12.0f, 0.1f, 8.0f,  0, 0 },
    { 0x11E, "m207_t2_vratio", "TC2 冷热电压比",   "",   PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.1f, 10.0f, 0.1f, 1.0f,  0, 0 },
    { 0x11F, "m207_t2_step",   "TC2 温度步进",     "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207,                                   0.001f, 10.0f, 0.001f, 0.01f, 0, 0 },
    /* --- 系统/PCB（0x120-0x126）--- */
    { 0x120, "m207_pcb_otph",  "PCB 过温高阈值",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 150.0f, 1.0f, 60.0f, 0, 0 },
    { 0x121, "m207_pcb_otpl",  "PCB 过温低阈值",   "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         -50.0f, 150.0f, 1.0f, -10.0f, 0, 0 },
    { 0x122, "m207_pcb_otph2", "PCB 过温滞回",     "°C", PARAM_TYPE_F32,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 50.0f, 1.0f, 10.0f, 0, 0 },
    { 0x126, "m207_com_addr",  "M207 串口地址",    "",   PARAM_TYPE_U8,
      PF_RW|PF_WEB|PF_M207|PF_DANGER,                         0.0f, 254.0f, 1.0f, 0.0f, 0, 0 },
    /* --- 只读遥测段（0x130+，无 PF_RW；网页只显不出控件）--- */
    { 0x130, "m207_t1_otp_st", "TC1 过温保护结果", "",  PARAM_TYPE_U8,
      PF_WEB|PF_M207,                                         0.0f, 1.0f, 1.0f, 0.0f,  0, 0 },
    { 0x131, "m207_t1_ocp_st", "TC1 过流保护结果", "",  PARAM_TYPE_U8,
      PF_WEB|PF_M207,                                         0.0f, 1.0f, 1.0f, 0.0f,  0, 0 },
    { 0x132, "m207_t1_ovp_st", "TC1 过压保护结果", "",  PARAM_TYPE_U8,
      PF_WEB|PF_M207,                                         0.0f, 1.0f, 1.0f, 0.0f,  0, 0 },
    { 0x133, "m207_t2_otp_st", "TC2 过温保护结果", "",  PARAM_TYPE_U8,
      PF_WEB|PF_M207,                                         0.0f, 1.0f, 1.0f, 0.0f,  0, 0 },
    /* 只读身份量（U8 原始值）：区间 0..255——min=max=0 会触发 POST TABLE
     * 的 min<max 断言而永久 FAIL（2026-10-30 真机实证）。*/
    { 0x134, "m207_sys_model", "M207 型号",        "",   PARAM_TYPE_U8,
      PF_WEB|PF_M207,                                         0.0f, 255.0f, 1.0f, 0.0f,  0, 0 },
    { 0x135, "m207_sys_ver",   "M207 固件版本",    "",   PARAM_TYPE_U8,
      PF_WEB|PF_M207,                                         0.0f, 255.0f, 1.0f, 0.0f,  0, 0 },
};
#define PARAM_N_REGISTERED  50

/* 默认初值（与表 value 列同序；param_load_defaults 用）*/
static const float s_defaults[PARAM_N_REGISTERED] = {
    25.0f, 25.0f, 37.5f, 36.5f, 0.2f, 0.0f, 40.0f, 0.0f,
    0.4f, 40.0f, 0.1f, 30.0f, 15.0f, 1.0f, 0.0f, 5.0f, 9.0f, 100.0f, -20.0f,
    -10.0f, 120.0f, 8.0f, 1.0f, 0.01f,
    0.4f, 40.0f, 0.1f, 30.0f, 15.0f, 1.0f, 0.0f, 5.0f, 9.0f, 100.0f, -20.0f,
    -10.0f, 120.0f, 8.0f, 1.0f, 0.01f,
    60.0f, -10.0f, 10.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
};

static param_hooks_t s_hooks;
static uint32_t s_armed_at_ms;    /* 首个脏参出现后由 flush 起算的 2s 防抖窗口 */
static uint8_t  s_flush_armed;

/* ====== 基础 ====== */
void param_registry_init(const param_hooks_t *h)
{
    if (h) s_hooks = *h;
    param_load_defaults();
    s_armed_at_ms = 0;
    s_flush_armed = 0;
}

int param_count(void) { return PARAM_N_REGISTERED; }

const param_desc_t *param_at(int idx)
{
    if (idx < 0 || idx >= PARAM_N_REGISTERED) return 0;
    return &s_params[idx];
}

param_desc_t *param_find(uint16_t id)
{
    for (int i = 0; i < PARAM_N_REGISTERED; i++)
        if (s_params[i].id == id) return &s_params[i];
    return 0;
}

const param_desc_t *param_find_by_key(const char *key)
{
    for (int i = 0; i < PARAM_N_REGISTERED; i++)
        if (strcmp(s_params[i].key, key) == 0) return &s_params[i];
    return 0;
}

float param_get(uint16_t id)
{
    param_desc_t *p = param_find(id);
    return p ? p->value : 0.0f;
}

void param_broadcast(param_desc_t *p, uint8_t src)
{
    (void)src;
    if (!p) return;
    if (s_hooks.on_broadcast) s_hooks.on_broadcast(p, src);
    p->dirty_ui = 0;
}

/* 类型写回：u8/u16 整数量化 */
static float type_quantize(const param_desc_t *p, float v)
{
    if (p->type == PARAM_TYPE_U8)  return (float)(uint8_t)v;
    if (p->type == PARAM_TYPE_U16) return (float)(uint16_t)v;
    return v;
}

static void side_effects(param_desc_t *p)
{
    if (p->flags & PF_M207) {
        if (p->id >= PARAM_ID_M207_BASE) {
            /* H10：映射表参数（0x100+）走通用模板通道 */
            if (s_hooks.on_m207_map_set) s_hooks.on_m207_map_set(p->id, p->value);
        } else {
            uint8_t ch = (p->id == PARAM_ID_TC1_SET) ? 1u : 2u;
            if (s_hooks.on_m207_set) s_hooks.on_m207_set(ch, p->value);
        }
    }
    if (p->id == PARAM_ID_MODE) {
        g_tcs.mode = (uint8_t)p->value;
        if (s_hooks.on_mode_change) s_hooks.on_mode_change((uint8_t)p->value);
    }
    if (p->flags & PF_PERSIST) {
        p->dirty_persist = 1;
        s_flush_armed = 1;    /* 防抖窗口起点在首次 flush 调用时锁定 */
    }
}

int param_set(uint16_t id, float v, uint8_t src)
{
    param_desc_t *p = param_find(id);
    if (!p) return -1;
    if (!(p->flags & PF_RW)) return -1;

    /* clamp（任务书：越界返回 clamp 后值经 src 通道回显 → 返回 1 由调用方回显）*/
    float c = v;
    if (c < p->min) c = p->min;
    if (c > p->max) c = p->max;
    c = type_quantize(p, c);
    int clamped = (c != v);

    /* 相等短路（仍回显，ACK ret 带当前值）*/
    if (c == p->value) {
        param_broadcast(p, src);
        return clamped ? 1 : 0;
    }

    /* 跨参数约束：body_cold < body_hot（写任一侧校验另一侧，违反拒写）*/
    if (p->id == PARAM_ID_BODY_HOT && c <= param_get(PARAM_ID_BODY_COLD)) return -2;
    if (p->id == PARAM_ID_BODY_COLD && c >= param_get(PARAM_ID_BODY_HOT)) return -2;

    p->value = c;
    side_effects(p);
    param_broadcast(p, src);
    return clamped ? 1 : 0;
}

/* R2：链路快照回灌——把 ESP32 权威值静默写入本地注册表（屏显示源）。
 * 不走 param_set：其副作用会回推 0x11/0x12 成环；不置 persist、不广播。
 * 仅 clamp+量化后写 value（脏 UI 由 500ms UI 扫描自然发现）。*/
void param_link_sync(uint16_t id, float v)
{
    param_desc_t *p = param_find(id);
    if (!p) return;
    float c = v;
    if (c < p->min) c = p->min;
    if (c > p->max) c = p->max;
    c = type_quantize(p, c);
    if (c != p->value) {
        p->value = c;
        p->dirty_ui = 1;
    }
}

/* ====== EEPROM v2 序列化（纯函数，PC 单测 §5.1）====== */
/* 物理布局：记录区 0x04..0xF7 共 244B = 30 条 8B 记录；CRC 在 0xF8。
 * 任务书 count 字段口径 ≤31；30 < 31，取更严者 → >30 条返回 -1（防记录覆盖 CRC 区）。 */
int param_serialize(uint8_t img[EE_IMG_SIZE])
{
    int n = 0;
    memset(img, 0, EE_IMG_SIZE);
    img[0] = EE_MAGIC_V2;
    img[1] = EE_LAYOUT_VER;
    for (int i = 0; i < PARAM_N_REGISTERED; i++)
        if (s_params[i].flags & PF_PERSIST) n++;
    if (n > 30) return -1;
    img[2] = (uint8_t)n;
    img[3] = 0;               /* rfu */
    int o = 4;
    for (int i = 0; i < PARAM_N_REGISTERED; i++) {
        const param_desc_t *p = &s_params[i];
        if (!(p->flags & PF_PERSIST)) continue;
        uint32_t v32;
        if (p->type == PARAM_TYPE_F32) {
            float f = p->value;
            memcpy(&v32, &f, 4);
        } else {
            v32 = (uint32_t)p->value;
        }
        img[o]   = (uint8_t)(p->id & 0xFF);
        img[o+1] = (uint8_t)(p->id >> 8);
        img[o+2] = p->type;
        img[o+3] = 0;                    /* rfu */
        img[o+4] = (uint8_t)(v32);
        img[o+5] = (uint8_t)(v32 >> 8);
        img[o+6] = (uint8_t)(v32 >> 16);
        img[o+7] = (uint8_t)(v32 >> 24);
        o += 8;
    }
    img[EE_CRC_ADDR] = proto_crc8(img, EE_CRC_ADDR);
    return 0;
}

int param_deserialize(const uint8_t *img)
{
    if (img[0] != EE_MAGIC_V2 || img[1] != EE_LAYOUT_VER) return -1;
    if (proto_crc8(img, EE_CRC_ADDR) != img[EE_CRC_ADDR]) return -1;
    int n = img[2];
    if (n > 30) return -1;
    param_load_defaults();               /* 先全默认，再用记录覆盖（缺记录=默认）*/
    const uint8_t *r = &img[4];
    for (int i = 0; i < n; i++, r += 8) {
        uint16_t id = (uint16_t)(r[0] | ((uint16_t)r[1] << 8));
        uint8_t  ty = r[2];
        uint32_t v32 = (uint32_t)r[4] | ((uint32_t)r[5] << 8)
                     | ((uint32_t)r[6] << 16) | ((uint32_t)r[7] << 24);
        param_desc_t *p = param_find(id);
        if (!p) continue;                /* 未知 id（旧固件残留）→ 跳过 */
        float v;
        if (ty == PARAM_TYPE_F32) memcpy(&v, &v32, 4);
        else v = (float)v32;
        if (v < p->min) v = p->min;      /* 防御：EEPROM 里被改坏的越界值 */
        if (v > p->max) v = p->max;
        p->value = v;
        if (p->id == PARAM_ID_MODE) g_tcs.mode = (uint8_t)v;
    }
    return 0;
}

void param_load_defaults(void)
{
    for (int i = 0; i < PARAM_N_REGISTERED; i++) {
        s_params[i].value = s_defaults[i];
        s_params[i].dirty_persist = 0;
        s_params[i].dirty_ui = 0;
    }
    g_tcs.mode = 0;                       /* F9：上电默认手动 */
}

void param_load_eeprom(void)
{
    /* 目标板入口：读 256B 镜像 → deserialize。eeprom.c 在目标工程提供，
     * PC 单测提供同名桩（不调本函数即可）。离线/读失败由 main 置 ALM_EEPROM。 */
    extern int eeprom_ready(void);
    extern int eeprom_read_image(uint8_t img[EE_IMG_SIZE]);
    uint8_t img[EE_IMG_SIZE];
    if (eeprom_ready() && eeprom_read_image(img) == 0)
        (void)param_deserialize(img);
}

int param_persist_now(void)
{
    uint8_t img[EE_IMG_SIZE];
    int dirty = 0;
    for (int i = 0; i < PARAM_N_REGISTERED; i++)
        if (s_params[i].dirty_persist) { dirty = 1; break; }
    if (!dirty) return 1;
    if (param_serialize(img) != 0) return -1;
    if (s_hooks.eeprom_commit && s_hooks.eeprom_commit(img) != 0) return -1;
    for (int i = 0; i < PARAM_N_REGISTERED; i++) s_params[i].dirty_persist = 0;
    s_flush_armed = 0;
    s_armed_at_ms = 0;
    return 0;
}

void param_persist_flush(uint32_t now_ms)
{
    if (!s_flush_armed) return;
    if (s_armed_at_ms == 0) {             /* 窗口起点 = 置脏后首个节拍 */
        s_armed_at_ms = now_ms ? now_ms : 1u;
        return;
    }
    if (now_ms - s_armed_at_ms < PARAM_PERSIST_DEBOUNCE_MS) return;
    param_persist_now();
}
