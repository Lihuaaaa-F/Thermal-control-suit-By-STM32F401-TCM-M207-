/**
 * ui_model.c — 见 ui_model.h。纯逻辑：不 include 任何 LVGL / HAL 头文件。
 */
#include "ui_model.h"
#include <stdio.h>
#include <string.h>

/* ====== 连接视图 ====== */

ui_link_view_t ui_link_view(const ui_in_t *in)
{
    ui_link_view_t v;
    memset(&v, 0, sizeof v);
    uint8_t ever = (in->snap_age_ms != UI_LINK_NEVER);
    uint8_t fresh = (uint8_t)(ever && in->snap_age_ms <= UI_LINK_STALE_MS);
    v.link = fresh ? UI_LINK_UP : (ever ? UI_LINK_DOWN : UI_LINK_NEVER_SEEN);
    v.stale = (uint8_t)(v.link != UI_LINK_UP);
    /* 主控不在时三个源一律"未知"：最近一帧快照里的值是陈旧数据，不能拿来冒充"在线" */
    if (!fresh) {
        v.module = UI_SRC_UNKNOWN;
        v.body = UI_SRC_UNKNOWN;
        v.down_ms = ever ? in->snap_age_ms : in->now_ms;
        return v;
    }
    v.module = (in->m207_online && !in->sensor_open) ? UI_SRC_OK : UI_SRC_LOST;
    v.body = in->body_ok ? UI_SRC_OK : UI_SRC_LOST;
    v.down_ms = 0;
    return v;
}

/* ====== 状态胶囊 ====== */

/* 主控断开这么久之后，"秒"改说"分钟"（状态胶囊与横幅共用）*/
#define UI_DOWN_MIN_S  100u

void ui_status_ctrl(const ui_link_view_t *lv, char *text, size_t cap, ui_led_t *led)
{
    switch (lv->link) {
        case UI_LINK_UP:
            snprintf(text, cap, "主控已连接");
            *led = UI_LED_GREEN;
            break;
        case UI_LINK_NEVER_SEEN:
            snprintf(text, cap, "主控未连接");
            *led = UI_LED_RED;
            break;
        default: {
            uint32_t s = lv->down_ms / 1000u;
            if (s < UI_DOWN_MIN_S) snprintf(text, cap, "主控断开 %us", (unsigned)s);
            else          snprintf(text, cap, "主控断开 %u分", (unsigned)(s / 60u));
            *led = UI_LED_RED;
            break;
        }
    }
}

void ui_status_module(const ui_link_view_t *lv, char *text, size_t cap, ui_led_t *led)
{
    switch (lv->module) {
        case UI_SRC_OK:
            snprintf(text, cap, "模块在线");
            *led = UI_LED_GREEN;
            break;
        case UI_SRC_UNKNOWN:
            snprintf(text, cap, "模块未知");
            *led = UI_LED_GRAY;
            break;
        default:
            snprintf(text, cap, "模块失联");
            *led = UI_LED_YELLOW;
            break;
    }
}

/* ====== 横幅 ====== */

static uint8_t alarm_count(uint16_t alarm)
{
    uint8_t n = 0;
    for (uint16_t b = alarm; b; b &= (uint16_t)(b - 1u)) n++;
    return n;
}

static void set_banner(ui_banner_t *b, ui_bn_kind_t kind, uint8_t red, ui_bn_tap_t tap,
                       const char *title, const char *detail, uint8_t extra)
{
    memset(b, 0, sizeof *b);
    b->kind = kind;
    b->red = red;
    b->tap = tap;
    snprintf(b->title, sizeof b->title, "%s", title);
    snprintf(b->detail, sizeof b->detail, "%s", detail);
    b->extra = extra;
}

void ui_banner_select(const ui_in_t *in, const ui_link_view_t *lv, ui_banner_t *out)
{
    memset(out, 0, sizeof *out);
    if (lv->link != UI_LINK_UP) {
        char detail[80];
        /* 屏端已经记下急停、却还没送到主控：这是此刻最要紧的一件事，压过"连接中断"本身。
         * 屏端会在链路恢复后自动重发急停意图（main.c），所以文案说"连上后立即送达"。
         * 第二行要放进 390px 宽的横幅里：比这更长会顶到右边的状态胶囊。*/
        if (in->estop_local) {
            if (lv->link == UI_LINK_NEVER_SEEN)
                snprintf(detail, sizeof detail, "连上后立即送达 · 正在等待主控");
            else
                snprintf(detail, sizeof detail, "连上后立即送达 · 数据停在 %us 前",
                         (unsigned)(lv->down_ms / 1000u));
            set_banner(out, UI_BN_ESTOP_UNSENT, 1, UI_BN_TAP_HINT, "急停尚未送达主控", detail, 0);
            return;
        }
        if (lv->link == UI_LINK_NEVER_SEEN) {
            snprintf(detail, sizeof detail, "正在等待主控 · 连上后自动刷新");
        } else {
            /* 满 UI_DOWN_MIN_S 秒改说"分钟"，与状态胶囊（ui_status_ctrl）同一口径 */
            uint32_t s = lv->down_ms / 1000u;
            if (s < UI_DOWN_MIN_S)
                snprintf(detail, sizeof detail, "数据停在 %us 前 · 自动重连中", (unsigned)s);
            else
                snprintf(detail, sizeof detail, "数据停在 %u 分钟前 · 自动重连中", (unsigned)(s / 60u));
        }
        /* 此时屏上的报警位也是陈旧的：不拿它画横幅，等链路回来再按真值显示 */
        set_banner(out, UI_BN_LINK_LOST, 1, UI_BN_TAP_HINT, "主控连接中断", detail, 0);
        return;
    }

    uint16_t a = in->alarm;
    if (a & ALM_ESTOP) {
        /* "TEC 已停 · 风扇全速"是主控快照里的事实，不是屏端按了键就能替它说的话：
         * 只有主控快照带了急停位、且执行器状态确实如此，才这么写。
         * 否则只说"指令已发出，等待确认"（独立复核 P1-1，2026-11-24）。*/
        uint8_t extra = (uint8_t)(alarm_count(a) - 1u);
        if (in->estop_peer && !in->tec1 && !in->tec2 && in->fan)
            set_banner(out, UI_BN_ESTOP, 1, UI_BN_TAP_ACK, "急停已触发",
                       "TEC 已停 · 风扇全速 · 点击解除", extra);
        else if (in->estop_peer)
            set_banner(out, UI_BN_ESTOP, 1, UI_BN_TAP_ACK, "急停已触发",
                       "主控已执行急停 · 点击解除", extra);
        else
            set_banner(out, UI_BN_ESTOP_PENDING, 1, UI_BN_TAP_HINT, "急停指令已发出",
                       "等待主控确认 · 确认后可点此解除", extra);
        return;
    }
    if (a & ALM_OVERTEMP) {
        set_banner(out, UI_BN_OVERTEMP, 1, UI_BN_TAP_ACK, "水温过高",
                   "已停止加热 · 降下来后点击解除", (uint8_t)(alarm_count(a) - 1u));
        return;
    }
    /* 自动恢复类：源没好之前点确认没有意义（主控会拒绝），所以文案只说"接好后自动恢复" */
    const char *auto_title = NULL;
    if (a & ALM_BODY_SENSOR)      auto_title = "体温传感器失联";
    else if (a & ALM_SENSOR_OPEN) auto_title = "水温探头断线";
    else if (a & ALM_M207)        auto_title = "温控模块通信中断";
    if (auto_title) {
        set_banner(out, UI_BN_AUTO, 0, UI_BN_TAP_HINT, auto_title,
                   "温控已暂停 · 接好后自动恢复", (uint8_t)(alarm_count(a) - 1u));
        return;
    }
    if (a & ALM_EEPROM) {
        set_banner(out, UI_BN_INFO, 0, UI_BN_TAP_ACK, "参数校验失败",
                   "已恢复默认参数 · 请重新设定", 0);
        return;
    }
    /* 没有报警位、但源确实不在（未布防时不上报警）：只提示，不说"已停止"——未布防时并不停机 */
    if (lv->body == UI_SRC_LOST) {
        set_banner(out, UI_BN_INFO, 0, UI_BN_TAP_HINT, "体温传感器未连接",
                   in->mode ? "接好后自动开始自动温控" : "体温不显示 · 接好后自动恢复", 0);
    } else if (lv->module == UI_SRC_LOST) {
        set_banner(out, UI_BN_INFO, 0, UI_BN_TAP_HINT,
                   in->sensor_open ? "水温探头未连接" : "温控模块未连接",
                   "水温不显示 · 接好后自动恢复", 0);
    }
}

const char *ui_banner_tap_hint(const ui_banner_t *b)
{
    switch (b->tap) {
        case UI_BN_TAP_ACK:   return NULL;             /* 由确认反馈（ui_ack_*）出提示 */
        case UI_BN_TAP_HINT:
            if (b->kind == UI_BN_LINK_LOST)    return "正在重新连接主控，连上后会自动刷新";
            if (b->kind == UI_BN_ESTOP_UNSENT) return "急停指令已记下，主控连上后立即送达";
            if (b->kind == UI_BN_ESTOP_PENDING) return "主控确认执行后才能解除";
            return "接好后会自动恢复，不需要点确认";
        default:              return NULL;
    }
}

/* ====== 点击"解除 / 恢复"后的反馈 ====== */

void ui_ack_start(ui_ack_t *a, const ui_in_t *in)
{
    a->waiting = 1;
    a->sent_ms = in->now_ms;
    a->seq_at_send = in->snap_seq;
}

void ui_ack_cancel(ui_ack_t *a)
{
    a->waiting = 0;
}

ui_ack_result_t ui_ack_poll(ui_ack_t *a, const ui_in_t *in, uint16_t *still)
{
    if (!a->waiting) return UI_ACK_WAIT;
    if (still) *still = 0;
    /* 先静置一段时间再下结论。点击那一刻已经现读了快照序号（tui.c 在 ui_ack_start 前调
     * input_refresh），所以"点击前到的那一帧"不会被当成回话；静置是为了跳过**点击之后**
     * 才发出、但主控还没处理完确认的那一两帧旧快照（快照 200ms 一帧）。
     * 真机（2026-11-24）：静置 450ms 时，点确认到出提示 530ms。*/
    if ((uint32_t)(in->now_ms - a->sent_ms) < UI_ACK_SETTLE_MS) return UI_ACK_WAIT;
    /* 点击之后链路断了：屏上残留的报警位是断开那一刻的旧值，说明不了"没解除"，
     * 结论只能是"主控没回话"（独立复核 P2-3）。*/
    if (in->snap_age_ms > UI_LINK_STALE_MS) {
        a->waiting = 0;
        return UI_ACK_NO_REPLY;
    }
    uint8_t new_snap = (uint8_t)(in->snap_seq != a->seq_at_send);
    if (!new_snap) {
        if ((uint32_t)(in->now_ms - a->sent_ms) < UI_ACK_TIMEOUT_MS) return UI_ACK_WAIT;
        a->waiting = 0;
        return UI_ACK_NO_REPLY;
    }
    /* 静置之后，按主控当前的报警位说话 */
    a->waiting = 0;
    if (in->alarm == 0) return UI_ACK_ALL_CLEAR;
    if (still) *still = in->alarm;
    return UI_ACK_PARTIAL;
}

const char *ui_ack_reason(uint16_t still, const ui_link_view_t *lv)
{
    if (still & ALM_BODY_SENSOR) return "体温传感器仍失联";
    if (still & ALM_SENSOR_OPEN) return "水温探头仍断线";
    if (still & ALM_M207)        return "温控模块仍失联";
    if (still & ALM_OVERTEMP)    return "水温仍高于 48°C";
    if (still & ALM_ESTOP) {
        /* 急停只在其余源都健康时才解除：残留原因就是同帧里的别的东西 */
        if (lv->body == UI_SRC_LOST)   return "急停未解除：体温传感器仍失联";
        if (lv->module == UI_SRC_LOST) return "急停未解除：温控模块仍失联";
        return "急停仍未解除，请再点一次";
    }
    if (still & ALM_EEPROM)       return "参数校验仍失败，请重新设定";
    return NULL;
}

/* ====== 主页差分 ====== */

void ui_home_state(const ui_in_t *in, const ui_link_view_t *lv, ui_home_t *out)
{
    memset(out, 0, sizeof *out);
    out->link = lv->link;
    out->module = lv->module;
    out->body = lv->body;
    /* 急停键：只要屏端记下了急停（哪怕主控断了、还没送到）就是灰的——再点也不会有别的
     * 动作（意图已在，链路恢复后自动重发），红键反而像"还没急停"。*/
    out->estop = in->estop_local ? 1u : 0u;
    /* 主控不在时，执行器/模式都是陈旧值：保持 0，屏幕按"未知"画，不做乐观显示 */
    if (lv->link != UI_LINK_UP) return;
    out->mode = in->mode ? 1u : 0u;
    out->fan = in->fan ? 1u : 0u;
    out->tec1 = in->tec1 ? 1u : 0u;
    out->tec2 = in->tec2 ? 1u : 0u;
}

uint32_t ui_home_diff(const ui_home_t *was, const ui_home_t *now)
{
    uint32_t m = 0;
    if (was->link != now->link)   m |= UI_H_LINK;
    if (was->module != now->module) m |= UI_H_MODULE | UI_H_LINK;
    if (was->body != now->body)   m |= UI_H_BODY;
    if (was->mode != now->mode)   m |= UI_H_MODE;
    if (was->fan != now->fan)     m |= UI_H_FAN;
    if (was->tec1 != now->tec1)   m |= UI_H_TEC1;
    if (was->tec2 != now->tec2)   m |= UI_H_TEC2;
    if (was->estop != now->estop) m |= UI_H_ESTOP;
    return m;
}

/* ====== m1"模式"页（用户 2026-11-26 定的新功能）====== */

void ui_mode_key(uint8_t online, uint8_t mode, char *text, size_t cap, uint8_t *enabled)
{
    snprintf(text, cap, "%s", mode ? "自动" : "手动");
    *enabled = online ? 1u : 0u;
}

ui_tec_dir_t ui_mode_dir_of(uint8_t d)
{
    switch (d) {
        case TCS_TEC_DIR_PAUSE: return UI_TD_PAUSE;
        case TCS_TEC_DIR_COOL:  return UI_TD_COOL;
        case TCS_TEC_DIR_HEAT:  return UI_TD_HEAT;
        default:                return UI_TD_EXT;   /* 3 以及任何没见过的码：开着，但不是模式页设的 */
    }
}

/* 主控在安全态（或急停 / 超温 / 模块、探头报警未解除）：主控会拒绝手动输出（control_param_check）*/
#define UI_SAFE_ALARMS  (ALM_ESTOP | ALM_OVERTEMP | ALM_M207 | ALM_SENSOR_OPEN | ALM_BODY_SENSOR)

/* 点击之后链路断了没有（与 ui_ack_poll 同一口径；写成函数是为了不重复 mutation_ui_model.py 的锚点原文）*/
static int link_is_stale(const ui_in_t *in) { return in->snap_age_ms > UI_LINK_STALE_MS; }

static uint8_t tec_running(const ui_mode_state_t *s)
{
    return (uint8_t)(s->dir[0] == UI_TD_COOL || s->dir[0] == UI_TD_HEAT ||
                     s->dir[1] == UI_TD_COOL || s->dir[1] == UI_TD_HEAT);
}

void ui_mode_state(const ui_in_t *in, const ui_link_view_t *lv, ui_mode_state_t *out)
{
    memset(out, 0, sizeof *out);
    out->link = lv->link;
    out->estop = (uint8_t)(in->estop_local != 0u);
    /* 主控不在：方向、电学量、模式都是断开那一刻的旧值——一律"未知 / 不可点"，不抢先也不假装实时 */
    if (out->link != UI_LINK_UP) return;
    out->manual = (uint8_t)(in->mode == 0);
    out->safe = (uint8_t)((in->alarm & UI_SAFE_ALARMS) != 0);
    out->dir[0] = ui_mode_dir_of(in->dir1);
    out->dir[1] = ui_mode_dir_of(in->dir2);
    out->elec_ok[0] = (uint8_t)(in->tec_valid & 1u);
    out->elec_ok[1] = (uint8_t)((in->tec_valid >> 1) & 1u);
    out->keys_on = (uint8_t)(out->manual && !out->safe);
    out->fan = in->fan ? 1u : 0u;
    /* 风扇联动只在手动模式（自动模式的风扇归状态机管，与首页的风扇键口径一致）*/
    out->fan_locked = (uint8_t)(out->manual && tec_running(out));
}

uint32_t ui_mode_diff(const ui_mode_state_t *was, const ui_mode_state_t *now)
{
    uint32_t m = 0;
    if (was->link != now->link)     m |= UI_M_LINK;
    if (was->manual != now->manual) m |= UI_M_MODE;
    if (was->safe != now->safe)     m |= UI_M_SAFE;
    if (was->estop != now->estop)   m |= UI_M_ESTOP;
    if (was->dir[0] != now->dir[0]) m |= UI_M_DIR1;
    if (was->dir[1] != now->dir[1]) m |= UI_M_DIR2;
    if (was->elec_ok[0] != now->elec_ok[0]) m |= UI_M_ELEC1;
    if (was->elec_ok[1] != now->elec_ok[1]) m |= UI_M_ELEC2;
    if (was->fan != now->fan || was->fan_locked != now->fan_locked) m |= UI_M_FAN;
    return m;
}

int ui_mode_btn_style(const ui_mode_state_t *s, ui_tec_dir_t btn)
{
    uint8_t i;
    if (s->link != UI_LINK_UP) return UI_MD_BTN_OFF;
    /* 高亮的是主控回读里"当前生效"的那个方向；通道开着但不是模式页设的 → 三个都不高亮 */
    for (i = 0; i < 2u; i++)
        if (s->dir[i] == btn) return UI_MD_BTN_ON;
    return UI_MD_BTN_NORMAL;
}

const char *ui_mode_dir_text(ui_tec_dir_t d)
{
    switch (d) {
        case UI_TD_PAUSE:   return "暂停";
        case UI_TD_COOL:    return "制冷中";
        case UI_TD_HEAT:    return "制热中";
        case UI_TD_EXT:     return "输出中";
        default:            return "未知";
    }
}

/* 小字里的电压 / 电流：各两位；不出现 -0.00 */
static float tidy2(float v)
{
    return (v > -0.005f && v < 0.005f) ? 0.0f : v;
}

int ui_mode_power(const ui_in_t *in, const ui_mode_state_t *s, int ch,
                  char *power, size_t pcap, char *vi, size_t vcap)
{
    float v, i, p;
    if (!s->elec_ok[ch ? 1 : 0]) {
        snprintf(power, pcap, "未知");
        snprintf(vi, vcap, "未知");
        return 0;
    }
    v = ch ? in->tec_v2 : in->tec_v1;
    i = ch ? in->tec_i2 : in->tec_i1;
    if (!(v == v) || !(i == i) || i < 0.0f) {     /* NaN / 负电流：当没数 */
        snprintf(power, pcap, "未知");
        snprintf(vi, vcap, "未知");
        return 0;
    }
    p = (v < 0.0f ? -v : v) * i;
    if (p > 999.0f) p = 999.0f;                    /* 主数字最多四位：再大就不像读数了 */
    snprintf(power, pcap, "%.1f W", (double)p);
    snprintf(vi, vcap, "%.2f V · %.2f A", (double)tidy2(v), (double)i);
    return 1;
}

const char *ui_mode_tap_block(const ui_mode_state_t *s)
{
    if (s->link != UI_LINK_UP) return "主控连接中断，现在不能操作";
    if (s->safe)               return "安全态下不可操作，请先解除报警";
    if (!s->manual)            return "只能在手动模式下调节";
    return NULL;
}

const char *ui_mode_fan_block(const ui_mode_state_t *s)
{
    /* 只有"想关"才拦：联动期间风扇本来就是开的，点它就是想关 */
    if (s->link == UI_LINK_UP && s->fan_locked && s->fan) return "TEC 工作时风扇保持开启";
    return NULL;
}

void ui_mode_cmd_start(ui_mode_cmd_t *c, const ui_in_t *in, uint8_t dir)
{
    c->waiting = 1;
    c->sent_ms = in->now_ms;
    c->seq_at_send = in->snap_seq;
    c->dir = dir;
}

ui_mode_cmd_result_t ui_mode_cmd_poll(ui_mode_cmd_t *c, const ui_in_t *in, ui_tec_dir_t now)
{
    uint32_t dt;
    if (!c->waiting) return UI_MC_DONE;
    dt = in->now_ms - c->sent_ms;
    /* 主控回读已经是这个方向（而且是点击之后来的新快照）：成了。不用等满静置时间——
     * 快照 200ms 一帧，高亮越早跟上手指越好。*/
    if (now == ui_mode_dir_of(c->dir) && in->snap_seq != c->seq_at_send) {
        c->waiting = 0;
        return UI_MC_DONE;
    }
    if (dt < UI_ACK_SETTLE_MS) return UI_MC_WAIT;      /* 与"点确认"同一个静置口径 */
    /* 点击之后链路断了：屏上残留的方向是断开那一刻的旧值，说明不了"没改成" */
    if (link_is_stale(in)) {
        c->waiting = 0;
        return UI_MC_NO_REPLY;
    }
    if (in->snap_seq == c->seq_at_send) {              /* 一帧新快照都没来 */
        if (dt < UI_ACK_TIMEOUT_MS) return UI_MC_WAIT;
        c->waiting = 0;
        return UI_MC_NO_REPLY;
    }
    /* 来了新快照、方向还是没变：按主控当前状态说原因（主控的拒绝码不透传到屏端，
     * 报警位与模式是同一帧里的事实）*/
    c->waiting = 0;
    if (in->alarm & UI_SAFE_ALARMS) return UI_MC_SAFE;
    if (in->mode != 0)               return UI_MC_NOT_MANUAL;
    return UI_MC_REJECTED;
}

const char *ui_mode_cmd_hint(ui_mode_cmd_result_t r)
{
    switch (r) {
        case UI_MC_NO_REPLY:   return "主控没有响应，请检查连接";
        case UI_MC_SAFE:       return "安全态下不可操作，请先解除报警";
        case UI_MC_NOT_MANUAL: return "只能在手动模式下调节";
        case UI_MC_REJECTED:   return "主控没有接受这次操作，请重试";
        default:               return NULL;           /* 成了 / 还在等：不出提示 */
    }
}
