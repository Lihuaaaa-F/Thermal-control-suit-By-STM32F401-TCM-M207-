/* tui_p0.c — 主页：两路水箱 / 体温 / 风扇 / 底栏
 * 建一次（tui_p0_build），之后每拍就地更新（tui_p0_update）：状态变了只动对应的那一块，
 * 数值变了只改文字——不再因为一个状态位翻转就整页重绘。
 * 显示什么由 ui_model 决定（ui_home_state / ui_home_diff）；这里只负责画。 */
#include "tui.h"
#include "ui_common.h"
#include "control.h"
#include <stdio.h>
#include <string.h>

/* ---- 版面（px）---- */
#define ROW1_Y      88
#define ROW1_H      240
#define ROW2_Y      352
#define ROW2_H      96
#define CARD_W      460
#define CARD1_X     32
#define CARD2_X     532
#define CARD_PAD    24
#define FAN_TRACK_X (CARD2_X + 308)   /* 风扇拨杆轨道左端 */
#define FAN_KNOB_DX 64

/* 数值文字：不透明底（卡底色）。变值时 label 连背景一起重画——PARTIAL 模式下
 * 双写帧缓冲的旧帧残影/黑带就是这么消掉的（G5-Z25 四象限矩阵实测）。*/
static lv_obj_t *value_label(lv_obj_t *p, int x, int y, lv_color_t c, const lv_font_t *f, const char *txt)
{
    lv_obj_t *l = ui_lab(p, x, y, f, c, txt);
    if (l) {
        lv_obj_set_style_bg_color(l, TKS_CARD, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }
    return l;
}

/* 单位跟在数值后面：数值变宽变窄都贴着走 */
static void place_unit(lv_obj_t *val, lv_obj_t *unit, const lv_font_t *f, int base_x, int gap)
{
    if (!val || !unit) return;
    lv_obj_set_x(unit, base_x + ui_text_w(f, lv_label_get_text(val)) + gap);
}

typedef struct {
    int cx;
    lv_obj_t *val, *unit, *set_val, *set_unit;
} tc_t;

static lv_obj_t *s_scr;
static struct {
    ui_pill_t mode;
    ui_pill_t tec[2];
    tc_t      tc[2];
    lv_obj_t *body_card, *body_val, *body_unit;
    ui_pill_t body_badge;
    lv_obj_t *fan_state, *fan_track, *fan_knob, *fan_word;
    ui_button_t b_mode, b_estop, b_fan;
    ui_home_t  st;             /* 上次画上去的状态（diff 的基准）*/
    uint8_t   built, grayed;   /* grayed：当前是否处于"主控断开、数值转灰"的样子 */
} H;

/* 上次据此算过延迟的"快照变化号"。放在 H 外面：H 每次建页都清零，而变化号只增——
 * 放在里面的话，离开首页再回来后的第一次状态变化会拿"上一次快照变化"的时刻当起点，
 * 60 秒以内都被计成延迟（独立复核 P3-3）。初值 0 = 还没为任何一次变化算过。*/
static uint32_t s_note_seq;

/* ================= 点击 ================= */

static void clk_tune(lv_event_t *e)  { LV_UNUSED(e); tui_request(TUI_TUNE); }
static void clk_chart(lv_event_t *e) { LV_UNUSED(e); tui_request(TUI_CHART); }
static void clk_conn(lv_event_t *e)  { LV_UNUSED(e); tui_request(TUI_CONN); }
/* 风扇卡片：v2 起 0x15 由主控执行。r7f（独立复核 R7-06，2026-11-26）：链路不新鲜时不发帧——
 * 主控此刻收不到 / 落不到执行器，屏上也不该报"已切换"（与模式页方向键 / 风扇键同一条口径与文案）。*/
static void clk_fan(lv_event_t *e)
{
    const ui_in_t *in;
    const ui_link_view_t *lv;
    ui_home_t now;
    const char *blk;
    LV_UNUSED(e);
    tui_input_refresh();                      /* 要"此刻"的快照年龄 */
    tui_input(&in, &lv);
    ui_home_state(in, lv, &now);
    blk = ui_home_fan_block(&now);
    if (blk) { ui_toast(blk); return; }
    control_fan_toggle();
}

/* m1（模式页，用户 2026-11-26）：首页的模式键只显示当前模式，点击进模式页——不再在首页直接切换。
 * （原先：切手动即时、切自动弹二次确认。二次确认框 tui_dlg_mode 留给模式页"切换到自动"用。）*/
static void clk_mode(lv_event_t *e)
{
    LV_UNUSED(e);
    tui_request(TUI_MODE);
}

static void clk_estop(lv_event_t *e)
{
    LV_UNUSED(e);
    /* 已急停（对端锁存，或长按兜底刚触发）→ 不再弹"确认急停？"：
     * 弹窗会盖住红色横幅，而横幅正是解除入口（2026-11-23 现场）。*/
    if (control_estop_latched() || (g_tcs.alarm_bits & ALM_ESTOP)) return;
    tui_dlg_estop();
}

/* ================= 就地绘制 ================= */

/* S 线 i3（P1-09 收口）：known = 主控说这一路的开关状态可信。M207 离线时主控只有命令意图，
 * 执行器实际在做什么无从得知——这时画"未知"，不把"已关"画出来。*/
static void paint_tec(int i, ui_src_t module, uint8_t on, uint8_t known)
{
    if (module == UI_SRC_UNKNOWN || !known)
        ui_pill_set(&H.tec[i], "未知", TKS_CARD, TKS_TX2, UI_PILL_DOT_R, TKS_GRAY);
    else
        ui_pill_set(&H.tec[i], on ? "运行" : "停止", TKS_CARD,
                    on ? TKS_GREEN : TKS_TX2, UI_PILL_DOT_R, on ? TKS_GREEN : TKS_GRAY);
}

static void paint_body(ui_src_t body)
{
    lv_obj_set_style_border_color(H.body_card, body == UI_SRC_LOST ? TKS_YELLOW : TKS_BORDER, 0);
    if (body == UI_SRC_LOST) {
        ui_pill_set(&H.body_badge, "失联", TKS_WARNBG, TKS_WARNTX, UI_PILL_DOT_R, TKS_YELLOW);
        lv_obj_set_hidden(H.body_badge.obj, false);
    } else {
        lv_obj_set_hidden(H.body_badge.obj, true);
    }
}

static void paint_mode(uint8_t online, uint8_t mode)
{
    if (online) ui_pill_set(&H.mode, mode ? "自动" : "手动", TKS_CARD, TKS_TX, UI_PILL_DOT_R,
                            mode ? TKS_GREEN : TKS_GRAY);
    else        ui_pill_set(&H.mode, "离线", TKS_CARD, TKS_TX2, UI_PILL_DOT_R, TKS_GRAY);
    /* 主控断了：模式键变灰不可点（进不了模式页，也就无从切换）。
     * 键上的文字是当前模式（"手动" / "自动"，m1 起不再写"切自动 / 切手动"）；断了保持上一次的。*/
    {
        char text[16];
        uint8_t enabled = 0;
        ui_mode_key(online, mode, text, sizeof text, &enabled);
        ui_button_set_style(&H.b_mode, enabled ? UI_BTN_PRIMARY : UI_BTN_DISABLED);
        if (online) ui_button_set_text(&H.b_mode, text);
    }
}

static void paint_fan(uint8_t online, uint8_t fan)
{
    ui_button_set_style(&H.b_fan, online ? UI_BTN_CARD : UI_BTN_DISABLED);   /* 同上：断了就不可点 */
    if (online) {
        ui_lab_set(H.fan_state, fan ? "运行中" : "已停止");
        lv_obj_set_style_text_color(H.fan_state, fan ? TKS_GREEN : TKS_TX2, 0);
        lv_obj_set_style_bg_color(H.fan_track, fan ? TKS_GREEN : TKS_GRAY, 0);
        lv_obj_set_x(H.fan_knob, FAN_TRACK_X + (fan ? FAN_KNOB_DX : 0));
        ui_lab_set(H.fan_word, fan ? "开" : "关");
    } else {
        ui_lab_set(H.fan_state, "状态未知");
        lv_obj_set_style_text_color(H.fan_state, TKS_TX2, 0);
        lv_obj_set_style_bg_color(H.fan_track, TKS_GRAY, 0);
        lv_obj_set_x(H.fan_knob, FAN_TRACK_X);
        ui_lab_set(H.fan_word, "?");
    }
}

/* 急停后按钮变灰（用户 2026-11-24 确认），解除后回红。屏端没记下急停时它永远是红的：
 * 急停必须一直可点，主控断了也一样（横幅会说明指令还没送到）。*/
static void paint_estop(uint8_t estop)
{
    ui_button_set_style(&H.b_estop, estop ? UI_BTN_LATCHED : UI_BTN_DANGER);
}

static void paint_all_state(const ui_home_t *s)
{
    uint8_t online = (uint8_t)(s->link == UI_LINK_UP);
    paint_tec(0, s->module, s->tec1, (uint8_t)(s->tec_known & 1u));
    paint_tec(1, s->module, s->tec2, (uint8_t)((s->tec_known >> 1) & 1u));
    paint_body(online ? s->body : UI_SRC_UNKNOWN);
    paint_mode(online, s->mode);
    paint_fan(online, s->fan);
    paint_estop(s->estop);           /* 主控断了也按屏端自己的急停记号画（见 ui_home_state）*/
}

static void set_value_color(lv_obj_t *o, lv_color_t c)
{
    if (o) lv_obj_set_style_text_color(o, c, 0);
}

static void paint_values(const ui_in_t *in, const ui_link_view_t *lv)
{
    char b[16];
    /* 主控断开：数值转灰并停在断开那一刻（不刷新、不假装实时）*/
    if (lv->stale != H.grayed) {
        H.grayed = lv->stale;
        for (int i = 0; i < 2; i++) {
            set_value_color(H.tc[i].val, H.grayed ? TKS_GRAY : TKS_HERO);
            set_value_color(H.tc[i].set_val, H.grayed ? TKS_GRAY : TKS_TX);
        }
        set_value_color(H.body_val, H.grayed ? TKS_GRAY : TKS_HERO);
    }
    if (lv->stale) return;

    for (int i = 0; i < 2; i++) {
        tc_t *t = &H.tc[i];
        int ok = (lv->module == UI_SRC_OK);
        if (ok) snprintf(b, sizeof b, "%.1f", (double)(i ? in->tc2_act : in->tc1_act));
        else    snprintf(b, sizeof b, "--.-");
        set_value_color(t->val, ok ? TKS_HERO : TKS_GRAY);
        ui_lab_set(t->val, b);
        place_unit(t->val, t->unit, &tcs_font_lv96, t->cx + CARD_PAD, 8);
        snprintf(b, sizeof b, "%.1f", (double)param_get(i ? PARAM_ID_TC2_SET : PARAM_ID_TC1_SET));
        ui_lab_set(t->set_val, b);
        place_unit(t->set_val, t->set_unit, &tcs_font_lv32, t->cx + CARD_PAD + 56, 4);
    }
    if (lv->body == UI_SRC_OK) snprintf(b, sizeof b, "%.1f", (double)in->body);
    else                       snprintf(b, sizeof b, "--.-");
    set_value_color(H.body_val, lv->body == UI_SRC_OK ? TKS_HERO : TKS_GRAY);
    ui_lab_set(H.body_val, b);
    place_unit(H.body_val, H.body_unit, &tcs_font_lv48, CARD1_X + CARD_PAD + 96, 6);
}

/* ================= 构建 ================= */

static void tc_build(int i, int cx, const char *title)
{
    tc_t *t = &H.tc[i];
    t->cx = cx;
    /* 整卡可点 → 曲线页；卡片样式自带按下态 */
    ui_button(s_scr, cx, ROW1_Y, CARD_W, ROW1_H, "", UI_BTN_CARD, &tcs_font_lv24, clk_chart, NULL);
    ui_box(s_scr, cx + 1, ROW1_Y + ROW1_H - 3, CARD_W - 2, 2, TKS_CLINE, 0);   /* 卡片底饰线 */
    ui_text32(s_scr, cx + CARD_PAD, 112, title, TKS_TX);
    /* "运行 / 停止 / 未知" 都是两个字：胶囊等宽，右缘不跳 */
    int pw = ui_pill_width(&tcs_font_lv24, "停止", UI_PILL_DOT_R);
    H.tec[i] = ui_pill(s_scr, cx + CARD_W - CARD_PAD - pw, 108, 40, &tcs_font_lv24);
    t->val = value_label(s_scr, cx + CARD_PAD, 160, TKS_GRAY, &tcs_font_lv96, "--.-");
    t->unit = ui_text32(s_scr, cx + CARD_PAD, 224, "°C", TKS_TX2);
    ui_text24(s_scr, cx + CARD_PAD, 272, "设定", TKS_TX2);
    t->set_val = value_label(s_scr, cx + CARD_PAD + 56, 268, TKS_GRAY, &tcs_font_lv32, "--.-");
    t->set_unit = ui_text24(s_scr, cx + CARD_PAD + 56, 276, "°C", TKS_TX2);
}

void tui_p0_build(lv_obj_t *scr)
{
    s_scr = scr;
    memset(&H, 0, sizeof H);

    /* 顶栏左：模式胶囊（右边的"主控 / 模块"状态条由 tui.c 统一建）*/
    H.mode = ui_pill(s_scr, 24, 16, 40, &tcs_font_lv24);

    /* 第一行：贴身水箱 / 环境水箱 */
    tc_build(0, CARD1_X, "贴身水箱");
    tc_build(1, CARD2_X, "环境水箱");

    /* 第二行左：体温 */
    H.body_card = ui_card(s_scr, CARD1_X, ROW2_Y, CARD_W, ROW2_H, TKS_CARD, TKS_BORDER);
    ui_text24(s_scr, CARD1_X + CARD_PAD, 364, "体温", TKS_TX);
    H.body_val = value_label(s_scr, CARD1_X + CARD_PAD + 96, 372, TKS_GRAY, &tcs_font_lv48, "--.-");
    H.body_unit = ui_aux20(s_scr, CARD1_X + CARD_PAD + 96, 400, "°C", TKS_TX2);
    int bw = ui_pill_width(&tcs_font_lv24, "失联", UI_PILL_DOT_R);
    H.body_badge = ui_pill(scr, CARD1_X + CARD_W - CARD_PAD - bw, 380, 40, &tcs_font_lv24);

    /* 第二行右：风扇（整卡可点=切换）*/
    H.b_fan = ui_button(s_scr, CARD2_X, ROW2_Y, CARD_W, ROW2_H, "", UI_BTN_CARD, &tcs_font_lv24, clk_fan, NULL);
    ui_box(s_scr, CARD2_X + 1, ROW2_Y + ROW2_H - 3, CARD_W - 2, 2, TKS_CLINE, 0);
    ui_text24(s_scr, CARD2_X + CARD_PAD, 364, "风扇", TKS_TX);
    H.fan_state = ui_text24(s_scr, CARD2_X + CARD_PAD, 402, "", TKS_TX2);
    H.fan_track = ui_box(s_scr, FAN_TRACK_X, 368, 128, 64, TKS_GRAY, 32);
    H.fan_knob = ui_box(s_scr, FAN_TRACK_X, 368, 64, 64, TKS_WHITE, 32);
    H.fan_word = ui_text_center(H.fan_knob, &tcs_font_lv24, TKS_TX, "");

    /* 底栏 */
    ui_button(s_scr, 40, 472, 208, 96, "调参", UI_BTN_PRIMARY, &tcs_font_lv32, clk_tune, NULL);
    H.b_mode = ui_button(s_scr, 272, 472, 208, 96, "", UI_BTN_PRIMARY, &tcs_font_lv32, clk_mode, NULL);
    ui_button(s_scr, 504, 472, 208, 96, "连接", UI_BTN_PRIMARY, &tcs_font_lv32, clk_conn, NULL);
    H.b_estop = ui_button(s_scr, TUI_ESTOP_X, TUI_ESTOP_Y, TUI_ESTOP_W, TUI_ESTOP_H, "急停", UI_BTN_DANGER,
                          &tcs_font_lv32, clk_estop, NULL);

    H.built = 1;
    H.grayed = 0xFF;                       /* 强制下一拍重上一次数值颜色 */
    {
        const ui_in_t *in;
        const ui_link_view_t *lv;
        tui_input(&in, &lv);
        ui_home_state(in, lv, &H.st);
        paint_all_state(&H.st);
        paint_values(in, lv);
    }
}

/* ================= 每拍更新 ================= */

int tui_p0_update(void)
{
    const ui_in_t *in;
    const ui_link_view_t *lv;
    tui_input(&in, &lv);
    if (!H.built) return 1;

    ui_home_t now;
    ui_home_state(in, lv, &now);
    uint32_t m = ui_home_diff(&H.st, &now);
    if (m) {
        if (m & UI_H_LINK) {
            paint_all_state(&now);        /* 链路在/不在：所有执行器块的可信度都变了 */
        } else {
            if (m & (UI_H_MODULE | UI_H_TEC1 | UI_H_TECK)) paint_tec(0, now.module, now.tec1, (uint8_t)(now.tec_known & 1u));
            if (m & (UI_H_MODULE | UI_H_TEC2 | UI_H_TECK)) paint_tec(1, now.module, now.tec2, (uint8_t)((now.tec_known >> 1) & 1u));
            if (m & UI_H_BODY)  paint_body(now.body);
            if (m & UI_H_MODE)  paint_mode(1, now.mode);
            if (m & UI_H_FAN)   paint_fan(1, now.fan);
            if (m & UI_H_ESTOP) paint_estop(now.estop);
        }
        H.st = now;
        g_ui_stat[1]++;
        /* "状态变化 → 上屏"的真实延迟（主循环在快照带来变化时记 g_ui_state_change_ms）*/
        /* 只给"主控快照带来的变化"算延迟：急停这类是屏幕本地先置位，不来自快照，
         * 拿上一次快照变化的时刻当起点会算出几十秒的假数（2026-11-24 真机读到 22981ms）。*/
        if (g_ui_state_change_seq != s_note_seq) {
            s_note_seq = g_ui_state_change_seq;
            uint32_t dt = in->now_ms - g_ui_state_change_ms;
            if (dt < 60000u) {
                g_ui_stat[6] = dt;
                if (dt > g_ui_stat[5]) g_ui_stat[5] = dt;
            }
        }
    }
    paint_values(in, lv);
    return 0;
}
