/* tui_p4_mode.c — 模式页（m1，用户 2026-11-26 定的新功能）
 *
 * 首页的模式键点击进来。手动模式：三张温度卡（贴身水箱 / 环境水箱 / 体温，都可点 → 曲线页）
 * + 两路 TEC（界面叫 TEC1 / TEC2）各三个键"制冷 / 暂停 / 制热" + 底栏"风扇 / 切换到自动模式 / 急停"。
 * 自动模式：同样的三张温度卡；两路 TEC 变成整行只读信息；底栏"风扇 / 切换到手动模式 / 急停"。
 * 手动、自动两页都显示每路的功率（|电压| × 电流）与电压电流小字；不显示、不提供目标温度。
 *
 * 显示什么、能不能点、点了之后提示什么，全部由 ui_model 决定（ui_mode_*，主机可单测）；这里只负责画。
 * 建页时建全部控件，之后只就地改文字和样式，不整页重建；唯一重建的时机是手动 / 自动互换（版面不同）。
 * 按钮高亮以主控回读的实际输出为准（快照里的 tec_dir），不抢先高亮。
 *
 * 版面（1024×600，8px 网格）：顶栏 0–64；横幅位 64–120（版面给它留着，内容从 128 起，不用挪）；
 * 温度卡 y 128–256；TEC 两行 y 268–348 / 364–444（信息卡 x 32–488，三个键 x 504 / 676 / 848、宽 144、高 80）；
 * 底栏 y 472–568：风扇 32–264，切换模式 280–744，急停与首页同位同尺寸（TUI_ESTOP_*，顶 464）。
 * 2026-11-27（U2 用户现场）：TEC 行高度 88→80、整体上移 4/12px，TEC2 键底 444 与急停顶 464 之间留 20px。 */
#include "tui.h"
#include "ui_common.h"
#include "control.h"
#include "link_snap.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---- 版面（px）---- */
#define CARD_Y       128
#define CARD_H       128
#define TCARD_W      304
#define TCARD_X(i)   (32 + (i) * 328)          /* 32 / 360 / 688 */
#define TEC_Y(i)     (268 + (i) * 96)          /* 268 / 364（U2：与底部三键留出 20px）*/
#define TEC_H        80
#define INFO_X       32
#define INFO_W       456                       /* 手动：32..488 */
#define INFO_W_AUTO  960                       /* 自动：整行 32..992 */
#define KEY_X(k)     (504 + (k) * 172)         /* 504 / 676 / 848 */
#define KEY_W        144
#define BAR_Y        472
#define BAR_H        96
#define FAN_X        32
#define FAN_W        232
#define SWITCH_X     280
#define SWITCH_W     464
#define PAD          24

static const char *const s_key_name[3] = { "制冷", "暂停", "制热" };
static const uint8_t s_key_dir[3] = { TCS_TEC_DIR_COOL, TCS_TEC_DIR_PAUSE, TCS_TEC_DIR_HEAT };   /* 键 → 参数值 */

typedef struct {
    lv_obj_t *val, *unit;
} tcard_t;

typedef struct {
    lv_obj_t *status, *power, *vi;
    ui_button_t key[3];
    int       right;                 /* 功率 / 电压电流小字右对齐到哪 */
    uint8_t   shown;                 /* 上次画的时候有没有数（0 = 转灰）*/
} tecrow_t;

static struct {
    ui_button_t fan, bottom_switch, estop;
    tcard_t  tc[3];
    tecrow_t tec[2];
    ui_mode_state_t st;              /* 上次画上去的状态（diff 的基准）*/
    ui_mode_cmd_t cmd;               /* 点了方向键之后等主控回读 */
    uint8_t  built, manual, grayed;
} H;
static lv_obj_t *s_scr;
static uint8_t s_cmd_ch;             /* H.cmd 等的是哪一路 */

/* 数值文字：不透明底（卡底色），同首页——PARTIAL 模式下变值时连背景一起重画，不留残影 */
static lv_obj_t *value_label(lv_obj_t *p, int x, int y, lv_color_t c, const lv_font_t *f, const char *txt)
{
    lv_obj_t *l = ui_lab(p, x, y, f, c, txt);
    if (l) {
        lv_obj_set_style_bg_color(l, TKS_CARD, 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
    }
    return l;
}

/* 右对齐：文字变了才改，并按新宽度贴右缘 */
static void right_label(lv_obj_t *l, int right_x, const char *text)
{
    const lv_font_t *f = lv_obj_get_style_text_font(l, 0);
    if (strcmp(lv_label_get_text(l), text) != 0) ui_lab_set(l, text);
    lv_obj_set_x(l, right_x - ui_text_w(f, lv_label_get_text(l)));
}

static void set_color(lv_obj_t *o, lv_color_t c)
{
    if (o) lv_obj_set_style_text_color(o, c, 0);
}

/* ================= 点击 ================= */

static void clk_back(lv_event_t *e)  { LV_UNUSED(e); tui_request(TUI_HOME); }
static void clk_chart(lv_event_t *e) { LV_UNUSED(e); tui_request(TUI_CHART); }   /* 与首页水箱卡同一条路径 */

static void clk_estop(lv_event_t *e)
{
    LV_UNUSED(e);
    /* 与首页的急停键同一规则：已急停（对端锁存，或长按兜底刚触发）→ 不再弹"确认急停？"
     * （弹窗会盖住红色横幅，而横幅正是解除入口）。*/
    if (control_estop_latched() || (g_tcs.alarm_bits & ALM_ESTOP)) return;
    tui_dlg_estop();
}

/* 屏端注册表只是镜像，可能陈旧（断链期间主控把手动参数清回了暂停，0x11 没送到）：
 * 镜像里已经是这个值时 param_set 会相等短路、一帧都不发——这一下直接补发（同一条等 ACK 的 0x12）。
 * 常规情形仍走 param_set（写参数唯一路径：侧效应 + 广播 + 等确认）。*/
static void mode_write(uint16_t id, float v)
{
    if (param_get(id) != v) {
        (void)param_set(id, v, PARAM_SRC_TOUCH);
        return;
    }
    (void)tcs_link_write_param(id, PARAM_TYPE_U8, v);
}

/* 三个方向键：user = 行 × 4 + 键 */
static void clk_key(lv_event_t *e)
{
    int id = (int)(intptr_t)lv_event_get_user_data(e);
    int row = id >> 2, k = id & 3;
    const ui_in_t *in;
    const ui_link_view_t *lv;
    ui_mode_state_t now;
    const char *blk;
    tui_input_refresh();                   /* 要"此刻"的快照序号：点击前到的那一帧不算回话 */
    tui_input(&in, &lv);
    ui_mode_state(in, lv, &now);
    blk = ui_mode_tap_block(&now);
    if (blk) { ui_toast(blk); return; }    /* 主控会拒的：不发帧，直接说原因 */
    if (now.dir[row] == ui_mode_dir_of(s_key_dir[k])) return;   /* 已经是这个方向 */
    mode_write(row ? PARAM_ID_TEC2_MANUAL : PARAM_ID_TEC1_MANUAL, (float)s_key_dir[k]);
    ui_mode_cmd_start(&H.cmd, in, s_key_dir[k]);
    s_cmd_ch = (uint8_t)row;
}

static void clk_fan(lv_event_t *e)
{
    const ui_in_t *in;
    const ui_link_view_t *lv;
    ui_mode_state_t now;
    const char *blk;
    LV_UNUSED(e);
    tui_input_refresh();
    tui_input(&in, &lv);
    ui_mode_state(in, lv, &now);
    blk = ui_mode_fan_block(&now);
    if (blk) { ui_toast(blk); return; }    /* 有 TEC 在制冷 / 制热：风扇被联动，点也关不掉 */
    control_fan_toggle();                  /* 与首页同一个入口（0x15，由主控执行）*/
}

/* 切换模式：切到自动要二次确认；切到手动立即生效（与切自动之前的老规则一致，不需要确认）*/
static void clk_switch(lv_event_t *e)
{
    const ui_in_t *in;
    const ui_link_view_t *lv;
    LV_UNUSED(e);
    tui_input_refresh();
    tui_input(&in, &lv);
    if (lv->link != UI_LINK_UP) { ui_toast("主控连接中断，现在不能切换模式"); return; }
    if (in->mode) {
        if (param_set(PARAM_ID_MODE, 0.0f, PARAM_SRC_TOUCH) < 0)
            ui_toast("没有改成：这个参数现在不能修改");
    } else {
        tui_dlg_mode();                     /* 确认框里再 param_set(PARAM_ID_MODE, 1) */
    }
}

/* ================= 就地绘制 ================= */

static int key_style(const ui_mode_state_t *s, int ch, int k)
{
    ui_tec_dir_t d = (k == 0) ? UI_TD_COOL : (k == 1) ? UI_TD_PAUSE : UI_TD_HEAT;
    switch (ui_mode_btn_style(s, ch, d)) {
        case UI_MD_BTN_ON:  return (k == 0) ? UI_BTN_ON_COOL : (k == 1) ? UI_BTN_ON_PAUSE : UI_BTN_ON_HEAT;
        case UI_MD_BTN_OFF: return UI_BTN_DISABLED;
        default:            return UI_BTN_PRIMARY;
    }
}

static lv_color_t dir_color(ui_tec_dir_t d)
{
    switch (d) {
        case UI_TD_COOL:  return TKS_BLUE;
        case UI_TD_HEAT:  return TKS_YELLOW;
        case UI_TD_PAUSE: return TKS_TX2;
        case UI_TD_EXT:   return TKS_TX;
        default:          return TKS_GRAY;
    }
}

static void paint_tec(int i, const ui_mode_state_t *s)
{
    tecrow_t *r = &H.tec[i];
    ui_tec_dir_t d = s->dir[i];
    ui_lab_set(r->status, ui_mode_dir_text(d));
    set_color(r->status, dir_color(d));
    if (H.manual)
        for (int k = 0; k < 3; k++) ui_button_set_style(&r->key[k], key_style(s, i, k));
}

static void paint_fan(const ui_mode_state_t *s)
{
    uint8_t on = (uint8_t)(s->link == UI_LINK_UP);
    ui_button_set_style(&H.fan, on ? UI_BTN_PRIMARY : UI_BTN_DISABLED);
    if (on) ui_button_set_text(&H.fan, s->fan ? "风扇 开" : "风扇 关");
}

static void paint_switch(const ui_mode_state_t *s)
{
    uint8_t on = (uint8_t)(s->link == UI_LINK_UP);
    int style = on ? UI_BTN_PRIMARY : UI_BTN_DISABLED;
    /* U5（2026-11-27 用户现场）：顶栏那个重复的"切换到自动 ›"已删，只保留底栏大键 */
    ui_button_set_style(&H.bottom_switch, style);
    if (!on) return;                          /* 断链：文字保持上一次的 */
    ui_button_set_text(&H.bottom_switch, s->manual ? "切换到自动模式" : "切换到手动模式");
}

static void paint_estop(uint8_t estop)
{
    ui_button_set_style(&H.estop, estop ? UI_BTN_LATCHED : UI_BTN_DANGER);
}

static void paint_all(const ui_mode_state_t *s)
{
    paint_tec(0, s);
    paint_tec(1, s);
    paint_fan(s);
    paint_switch(s);
    paint_estop(s->estop);
}

/* 数值：三张温度卡 + 两路功率。每拍就地刷新（同文不重设）*/
static void paint_values(const ui_in_t *in, const ui_link_view_t *lv, const ui_mode_state_t *s, int force)
{
    char b[32], pw[16], vi[32];
    int gray = lv->stale;
    int i;

    if (gray) {
        /* 主控断开：数值转灰并停在断开那一刻（不刷新、不假装实时）——与首页同一口径 */
        if (H.grayed && !force) return;
        H.grayed = 1;
        for (i = 0; i < 3; i++) set_color(H.tc[i].val, TKS_GRAY);
        for (i = 0; i < 2; i++) {
            set_color(H.tec[i].power, TKS_GRAY);
            set_color(H.tec[i].vi, TKS_GRAY);
            H.tec[i].shown = 0;
        }
        if (force) {                           /* 断链状态下建页：没有可信的数可停 */
            for (i = 0; i < 3; i++) ui_lab_set(H.tc[i].val, "--.-");
            for (i = 0; i < 2; i++) {
                right_label(H.tec[i].power, H.tec[i].right, "未知");
                right_label(H.tec[i].vi, H.tec[i].right, "未知");
            }
        }
        return;
    }
    H.grayed = 0;

    for (i = 0; i < 2; i++) {
        tcard_t *t = &H.tc[i];
        int ok = (lv->module == UI_SRC_OK);
        if (ok) snprintf(b, sizeof b, "%.1f", (double)(i ? in->tc2_act : in->tc1_act));
        else    snprintf(b, sizeof b, "--.-");
        ui_lab_set(t->val, b);
        set_color(t->val, ok ? TKS_HERO : TKS_GRAY);
        lv_obj_set_x(t->unit, TCARD_X(i) + PAD + ui_text_w(&tcs_font_lv48, b) + 8);
    }
    /* 体温探头没接：--.- 转灰，沿用首页的口径（原因在横幅 / 顶栏状态条）；不画趋势箭头 */
    {
        int ok = (lv->body == UI_SRC_OK);
        if (ok) snprintf(b, sizeof b, "%.1f", (double)in->body);
        else    snprintf(b, sizeof b, "--.-");
        ui_lab_set(H.tc[2].val, b);
        set_color(H.tc[2].val, ok ? TKS_HERO : TKS_GRAY);
        lv_obj_set_x(H.tc[2].unit, TCARD_X(2) + PAD + ui_text_w(&tcs_font_lv48, b) + 8);
    }
    for (i = 0; i < 2; i++) {
        tecrow_t *r = &H.tec[i];
        int have = ui_mode_power(in, s, i, pw, sizeof pw, vi, sizeof vi);
        /* 主数字用 32px；"未知"两个字只有 24px 字库里有（32px 不放新字），没数时换成 24px */
        const lv_font_t *pf = have ? &tcs_font_lv32 : &tcs_font_lv24;
        if (lv_obj_get_style_text_font(r->power, 0) != pf) lv_obj_set_style_text_font(r->power, pf, 0);
        right_label(r->power, r->right, pw);
        right_label(r->vi, r->right, vi);
        if (have != r->shown) {
            r->shown = (uint8_t)have;
            set_color(r->power, have ? TKS_HERO : TKS_GRAY);
            set_color(r->vi, have ? TKS_TX2 : TKS_GRAY);
        }
    }
}

/* ================= 构建 ================= */

void tui_p4_build(lv_obj_t *scr)
{
    static const char *const tc_name[3] = { "贴身水箱", "环境水箱", "体温" };
    const ui_in_t *in;
    const ui_link_view_t *lv;
    int k;

    s_scr = scr;
    memset(&H, 0, sizeof H);
    s_cmd_ch = 0;
    tui_input(&in, &lv);
    ui_mode_state(in, lv, &H.st);
    /* 主控断了进来时模式是旧值：按手动版面建（键都是灰的），链路回来再按真值重建 */
    H.manual = (uint8_t)(H.st.link == UI_LINK_UP && in->mode == 0);

    ui_button(scr, 24, 8, 120, 40, "‹ 返回", UI_BTN_GHOST, &tcs_font_lv24, clk_back, NULL);
    ui_text32(scr, 176, 16, H.manual ? "手动模式" : "自动模式", TKS_TX);

    /* 三张温度卡：整卡可点 → 曲线页 */
    for (int i = 0; i < 3; i++) {
        int x = TCARD_X(i);
        ui_button(scr, x, CARD_Y, TCARD_W, CARD_H, "", UI_BTN_CARD, &tcs_font_lv24, clk_chart, NULL);
        ui_box(scr, x + 1, CARD_Y + CARD_H - 3, TCARD_W - 2, 2, TKS_CLINE, 0);   /* 卡片底饰线 */
        ui_text24(scr, x + PAD, CARD_Y + 16, tc_name[i], TKS_TX);
        H.tc[i].val = value_label(scr, x + PAD, CARD_Y + 50, TKS_GRAY, &tcs_font_lv48, "--.-");
        H.tc[i].unit = ui_text24(scr, x + PAD, CARD_Y + 70, "°C", TKS_TX2);
    }

    /* 两路 TEC */
    for (int i = 0; i < 2; i++) {
        tecrow_t *r = &H.tec[i];
        int w = H.manual ? INFO_W : INFO_W_AUTO;
        r->right = INFO_X + w - PAD;
        ui_card(scr, INFO_X, TEC_Y(i), w, TEC_H, TKS_CARD, TKS_BORDER);
        ui_text24(scr, INFO_X + PAD, TEC_Y(i) + 14, i ? "TEC2" : "TEC1", TKS_TX);
        r->status = value_label(scr, INFO_X + PAD, TEC_Y(i) + 48, TKS_GRAY, &tcs_font_lv24, "");
        r->power  = value_label(scr, r->right - 200, TEC_Y(i) + 12, TKS_GRAY, &tcs_font_lv32, "");
        r->vi     = value_label(scr, r->right - 200, TEC_Y(i) + 50, TKS_GRAY, &tcs_font_lv24, "");
    }
    for (int i = 0; i < 2 && H.manual; i++)
        for (k = 0; k < 3; k++)
            H.tec[i].key[k] = ui_button(scr, KEY_X(k), TEC_Y(i), KEY_W, TEC_H, s_key_name[k],
                                        UI_BTN_PRIMARY, &tcs_font_lv24, clk_key,
                                        (void *)(intptr_t)(i * 4 + k));

    /* 底栏：风扇 / 切换模式 / 急停（与首页同位同尺寸）*/
    H.fan = ui_button(scr, FAN_X, BAR_Y, FAN_W, BAR_H, "", UI_BTN_PRIMARY, &tcs_font_lv24, clk_fan, NULL);
    H.bottom_switch = ui_button(scr, SWITCH_X, BAR_Y, SWITCH_W, BAR_H, "", UI_BTN_PRIMARY,
                                &tcs_font_lv24, clk_switch, NULL);
    H.estop = ui_button(scr, TUI_ESTOP_X, TUI_ESTOP_Y, TUI_ESTOP_W, TUI_ESTOP_H, "急停", UI_BTN_DANGER,
                        &tcs_font_lv32, clk_estop, NULL);

    H.built = 1;
    paint_all(&H.st);
    paint_values(in, lv, &H.st, 1);
}

/* ================= 每拍更新 ================= */

int tui_p4_update(void)
{
    const ui_in_t *in;
    const ui_link_view_t *lv;
    ui_mode_state_t now;
    uint32_t m;

    if (!H.built) return 1;
    tui_input(&in, &lv);
    ui_mode_state(in, lv, &now);
    m = ui_mode_diff(&H.st, &now);
    if (m & UI_M_MODE) return 1;             /* 手动 / 自动版面不同：整页重建（tui.c 做）*/
    if (m) {
        if (m & UI_M_LINK) {
            paint_all(&now);                 /* 链路在/不在：所有控件的可信度都变了 */
        } else {
            if (m & (UI_M_DIR1 | UI_M_SAFE)) paint_tec(0, &now);
            if (m & (UI_M_DIR2 | UI_M_SAFE)) paint_tec(1, &now);
            if (m & UI_M_FAN)   paint_fan(&now);
            if (m & UI_M_ESTOP) paint_estop(now.estop);
        }
        H.st = now;
        g_ui_stat[1]++;
    }
    paint_values(in, lv, &now, 0);

    /* 点了方向键：以主控回读的实际输出为准；成了不出声（高亮自己会动），没成说原因 */
    if (H.cmd.waiting) {
        ui_mode_cmd_result_t r = ui_mode_cmd_poll(&H.cmd, in, now.dir[s_cmd_ch]);
        const char *hint = ui_mode_cmd_hint(r);
        if (hint) ui_toast(hint);
    }
    return 0;
}

/* 二级页的横幅占 y 64..120。本页内容从 128 起，版面已经给它留了位置，不需要挪——
 * 底栏急停键也因此在横幅出现时保持在原位。保留这个接口是为了和其它二级页一致。*/
void tui_p4_set_banner_room(int room)
{
    (void)room;
}
