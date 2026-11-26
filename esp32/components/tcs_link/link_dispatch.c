/**
 * link_dispatch.c — 收帧分发（M11-F-15 拆分，S 线 e1）
 *
 * 从 link_task.c 拆出的第 2 块：任务上下文里 comm 层交付的每一帧在这里分派。
 *   0x10 STATUS        → 版本比对（M11-F-09）+ 状态缓存
 *   0x14 REGISTRY_ENTRY→ 注册表落地（link_registry.c）
 *   0x12 SET_PARAM     → 应用层处理器（写权威注册表 + 回 ACK）
 *   0x15 CMD           → 命令回调（急停/确认等）
 * 0x90 在 comm 层就被消费（X01-F-03 起带 status/ret）；0x11/0x20/0x21/0x22 是本端发出的，
 * 方向闸门会丢掉它们的回声。
 */
#include "link_internal.h"
#include <string.h>
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "tcs_link";

static link_cmd_cb_t s_cmd_cb;         /* 屏幕 0x15 命令回调 */
static link_setparam_cb_t s_setparam_cb;

/* M11-F-09：STATUS 版本比对的状态（收帧侧）*/
static uint32_t s_proto_mismatch;      /* 版本不符帧数（只增）*/
static volatile uint8_t s_proto_alarm; /* 版本不符提示位（对端版本对上后清回 0）*/
static volatile uint8_t s_peer_ver;    /* 实际收到的对端协议版本（0 = 还没收到过）*/

void link_on_cmd(link_cmd_cb_t cb) { s_cmd_cb = cb; }

void link_on_setparam(link_setparam_cb_t cb)
{
    s_setparam_cb = cb;
}

uint32_t link_dispatch_proto_mismatch(void) { return s_proto_mismatch; }
uint8_t  link_dispatch_peer_ver(void)       { return s_peer_ver; }
uint16_t link_dispatch_alarm_hint(void)     { return s_proto_alarm ? (uint16_t)ALM_PROTO : 0u; }

/* —— 帧分发（任务上下文）—— */
void link_dispatch_frame(const uint8_t *payload, size_t len)
{
    if (len < 2) return;
    uint8_t mt = payload[0];
    const uint8_t *body = &payload[2];
    size_t blen = len - 2;

    switch (mt) {
    case MT_TCS_STATUS: {
        tcs_status_pl_t s;
        if (tcs_scan_status(body, blen, &s) != 0) break;
        /* M11-F-09：版本不符不能静默吞掉——留痕 + 计数 + 提示位 ALM_PROTO。
         * 修复前只看长度、不比对 ver，网页又把 ver 硬编码成常量，双方版本漂移永远看不见；
         * 错位的字段会被当成正常温度显示。提示位在版本对上以后清回 0（它是"当前状态"，
         * 不是锁存）；计数只增，给现场对账用。
         * 版本不符的帧**不 publish**：ver 变了就可能布局变了，按当前布局解出来的字段写进
         * s_status 就是拿错位数据当真值（网页/屏都会照它显示）。只把它当作"链路上还有东西
         * 在说话"来维持判活计时，屏/网页仍显示上一帧好数据或"离线"。*/
        s_peer_ver = s.ver;
        if (s.ver != TCS_PROTO_VER) {
            s_proto_mismatch++;
            s_proto_alarm = 1;
            if (s_proto_mismatch <= 3 || (s_proto_mismatch % 100u) == 0)
                ESP_LOGW(TAG, "STATUS ver=%u != local %u (count=%lu, fields not published)",
                         (unsigned)s.ver, (unsigned)TCS_PROTO_VER,
                         (unsigned long)s_proto_mismatch);
            link_status_note_alive();
            break;
        }
        s_proto_alarm = 0;
        link_internal_lock();
        {
            link_status_t *st = link_status_buf();
            st->valid = 1;
            st->ver = s.ver;
            st->seq = s.seq;
            st->tc1_act = s.tc1_act;
            st->tc2_act = s.tc2_act;
            st->body = s.body;
            st->tec1 = s.tec1;
            st->tec2 = s.tec2;
            st->fan = s.fan;
            st->mode = s.mode;
            st->alarm = s.alarm;
            st->online = s.online;
            st->last_status_us = esp_timer_get_time();
        }
        link_internal_unlock();
        link_status_note_alive();
        link_notify_event();
        break;
    }
    case MT_TCS_REGISTRY_ENTRY: {
        tcs_reg_entry_t e;
        if (tcs_scan_registry_entry(body, blen, &e) != 0) break;
        link_registry_apply_entry(&e);
        break;
    }
    case MT_TCS_SET_PARAM: {         /* 屏写参数（R5-10 起所有参数都走这一条，等 ACK）→ 本端权威注册表 → M207 verify */
        tcs_param_pl_t p;
        uint8_t seq = payload[1];
        if (tcs_scan_param(body, blen, &p) != 0) {
            link_send_ack(seq, mt, TCS_ACK_BAD_ID, 0.0f);
            break;
        }
        float v = (p.type == PARAM_TYPE_F32) ? tcs_b2f(p.value) : (float)p.value;
        /* 这里也是 web 的 pv 推送源之一：记"最近一次屏的参数写入"（R5-01 之前 0x11 的
         * 广播也走这段，本端镜像回声因此能冒充屏的写入；现在 0x11 是主控专用，进不了这里）。*/
        link_registry_note_param(p.id, v);
        /* 破组件环（tcs_core→tcs_link）：应用层注册处理器落权威注册表并回 ACK */
        if (s_setparam_cb) {
            s_setparam_cb(p.id, v, seq);
        } else {
            link_send_ack(seq, mt, TCS_ACK_BUSY, 0.0f);
        }
        link_notify_event();
        break;
    }
    case MT_TCS_CMD: {               /* v2 反向：屏幕急停/确认 → 本端执行 */
        uint8_t seq = payload[1];
        if (blen < 1) { link_send_ack(seq, mt, TCS_ACK_BAD_ID, 0.0f); break; }
        uint8_t cmd = body[0];
        if (s_cmd_cb) s_cmd_cb(cmd, seq);
        else link_send_ack(seq, mt, TCS_ACK_BUSY, 0.0f);
        break;
    }
    default:
        break;   /* 0x90 已被 comm 层消费；0x11/0x20/0x21/0x22 是本端专用（方向闸门在 comm 层就丢了）；0x13 仅本端发起 */
    }
}
