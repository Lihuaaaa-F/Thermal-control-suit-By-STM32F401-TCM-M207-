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
            if (s < 100u) snprintf(text, cap, "主控断开 %us", (unsigned)s);
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
        char detail[64];
        if (lv->link == UI_LINK_NEVER_SEEN)
            snprintf(detail, sizeof detail, "正在等待主控 · 连上后自动刷新");
        else {
            uint32_t s = lv->down_ms / 1000u;
            if (s < 100u) snprintf(detail, sizeof detail, "数据停在 %us 前 · 自动重连中", (unsigned)s);
            else          snprintf(detail, sizeof detail, "数据已过期 %u分 · 自动重连中",
                                    (unsigned)(s / 60u));
        }
        /* 此时屏上的报警位也是陈旧的：不拿它画横幅，等链路回来再按真值显示 */
        set_banner(out, UI_BN_LINK_LOST, 1, UI_BN_TAP_HINT, "主控连接中断", detail, 0);
        return;
    }

    uint16_t a = in->alarm;
    if (a & ALM_ESTOP) {
        set_banner(out, UI_BN_ESTOP, 1, UI_BN_TAP_ACK, "急停已触发",
                   "TEC 已停 · 风扇全速 · 点击解除", (uint8_t)(alarm_count(a) - 1u));
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
            if (b->kind == UI_BN_LINK_LOST) return "正在重新连接主控，连上后会自动刷新";
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
    a->alarm_at_send = in->alarm;
}

ui_ack_result_t ui_ack_poll(ui_ack_t *a, const ui_in_t *in, uint16_t *still)
{
    if (!a->waiting) return UI_ACK_WAIT;
    if (still) *still = 0;
    /* 先静置一段时间再下结论：点击后第一帧"新快照"往往是命令还在路上时发出的，
     * 它上面的报警位还是旧的——据此判"没解除"是误报（2026-11-24 真机）。*/
    if ((uint32_t)(in->now_ms - a->sent_ms) < UI_ACK_SETTLE_MS) return UI_ACK_WAIT;
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
    /* 主控不在时，执行器/模式/急停位都是陈旧值：保持 0，屏幕按"未知"画，不做乐观显示 */
    if (lv->link != UI_LINK_UP) return;
    out->mode = in->mode ? 1u : 0u;
    out->fan = in->fan ? 1u : 0u;
    out->tec1 = in->tec1 ? 1u : 0u;
    out->tec2 = in->tec2 ? 1u : 0u;
    out->estop = (in->alarm & ALM_ESTOP) ? 1u : 0u;
    out->safe = (in->alarm != 0) ? 1u : 0u;
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
    if (was->safe != now->safe)   m |= UI_H_SAFE;
    return m;
}
