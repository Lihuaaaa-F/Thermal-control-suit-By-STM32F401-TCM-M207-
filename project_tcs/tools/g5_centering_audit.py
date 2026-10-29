#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""g5_centering_audit.py — G5 居中/对齐像素断言（检查不靠目视）
用法：python tools/g5_centering_audit.py
"""
import json, numpy as np
from PIL import Image

BASE = r"D:\Espressif\ws\tcs\docs\evidence\LVGL\g5"
TH_C, TH_CW = 1, 2
TH_A, TH_AW = 2, 4
INK_DEV = 20

E = []
def add(key, frame, sr, mode, ex, note):
    E.append(dict(key=key, frame=frame, sr=tuple(sr), mode=mode,
                  ex=tuple(ex) if ex else None, note=note))

F0, F1, F2, F3, F4 = "v_home.png", "v_tune.png", "v_chart.png", "v_conn.png", "w_dlg_estop2.png"

add("p0_mode_dot",    F0, (68, 26, 18, 20),   "bbox_center", (76, 36),  "模式点圆心")
add("p0_mode_text",   F0, (86, 20, 90, 34),   "anchor_lt",   (88, 25),  "模式文字左上")
add("p0_estop_icon",  F0, (934, 14, 34, 38),  "bbox_center", (952, 32), "急停图标中心")
add("p0_c1_title",    F0, (48, 100, 200, 46), "anchor_lt",   (56, 109), "TC1 标题")
add("p0_c1_tec",      F0, (384, 114, 100, 34),"bbox_center", (432, 132),"TEC1 胶囊文字中心")
add("p0_c1_set_val",  F0, (112, 266, 152, 30),"anchor_lt",   (114, 269),"设定值 32px 左上")
add("p0_c2_tec",      F0, (984, 114, 100, 34),"bbox_center", (1032, 132),"TEC2 胶囊文字中心")
add("p0_body_title",  F0, (48, 356, 110, 42), "anchor_lt",   (56, 365), "体温标题")
add("p0_lost_badge",  F0, (412, 384, 86, 34), "bbox_center", (436, 406),"失联徽整体中心")
add("p0_fan_title",   F0, (548, 356, 110, 42),"anchor_lt",   (556, 365), "风扇标题")
add("p0_fan_state",   F0, (548, 396, 130, 40),"anchor_lt",   (556, 403), "风扇状态文字")
add("p0_fan_word",    F0, (908, 382, 60, 40), "bbox_center", (938, 408),"风扇球文字（ON 态）")
add("p0_dock_tune",   F0, (60, 480, 170, 84), "bbox_center", (144, 520),"调参钮文字中心")
add("p0_dock_mode",   F0, (292, 480, 170, 84),"bbox_center", (376, 520),"切自动钮文字中心")
add("p0_dock_conn",   F0, (524, 480, 170, 84),"bbox_center", (608, 520),"连接钮文字中心")
add("p0_dock_estop",  F0, (790, 480, 180, 80),"bbox_center", (876, 518),"急停钮文字中心（窗避开叠层）")
add("p1_back_text",   F1, (30, 12, 110, 34),  "bbox_center", (84, 28),  "返回钮文字中心")
add("p1_ctx_text",    F1, (170, 16, 500, 36), "anchor_lt",   (176, 23), "ctx 行左上")
add("p1_r1_name",     F1, (48, 60, 240, 36),  "anchor_lt",   (56, 68),  "行1 名称")
add("p1_r1_val",      F1, (52, 106, 180, 34), "anchor_lt",   (56, 112), "行1 值")
add("p1_r1_minus",    F1, (830, 116, 50, 46), "bbox_center", (856, 140),"行1 − 键文字")
add("p1_r1_plus",     F1, (910, 116, 50, 46), "bbox_center", (936, 140),"行1 + 键文字")
add("p1_r2_name",     F1, (48, 164, 240, 36), "anchor_lt",   (56, 172), "行2 名称")
add("p1_r3_name",     F1, (48, 268, 240, 36), "anchor_lt",   (56, 276), "行3 名称")
add("p1_r4_name",     F1, (48, 372, 240, 36), "anchor_lt",   (56, 380), "行4 名称")
add("p1_step_text",   F1, (176, 496, 162, 36),"bbox_center", (250, 520),"步进钮文字中心")
add("p1_zone_minus",  F1, (44, 480, 76, 84),  "bbox_center", (80, 520), "大−键文字")
add("p1_zone_plus",   F1, (910, 484, 70, 72), "bbox_center", (944, 518),"大+键文字")
add("p2_title",       F2, (170, 8, 340, 48),  "anchor_lt",   (176, 17), "曲线页标题")
add("p2_legend1",     F2, (705, 14, 130, 40), "anchor_lt",   (712, 23), "图例1 文字")
add("p2_axis_hi",     F2, (32, 78, 70, 30),   "bbox_center", None,      "Y 轴上限（只报告）")
add("p2_axis_lo",     F2, (28, 462, 80, 34),  "bbox_center", None,      "Y 轴下限（只报告）")
add("p2_axis_now",    F2, (890, 488, 110, 40),"bbox_center", None,      "X 轴现在（只报告）")
add("p3_title",       F3, (24, 8, 160, 48),   "anchor_lt",   (32, 17),  "连接标题")
add("p3_cred1",       F3, (284, 346, 120, 40),"anchor_lt",   (292, 353),"热点名称标签")
add("p3_cred2",       F3, (284, 386, 120, 40),"anchor_lt",   (292, 393),"热点密码标签")
add("p3_back_text",   F3, (60, 480, 170, 84), "bbox_center", (144, 520),"返回钮文字中心")
add("p3_fw",          F3, (880, 512, 130, 36),"bbox_center", None,      "fw 版本（只报告）")
add("dlg_title",      F4, (290, 162, 300, 46),"anchor_lt",   (296, 169),"弹窗标题（左上 40,32 rel）")
add("dlg_body",       F4, (290, 226, 400, 44),"anchor_lt",   (296, 233),"弹窗正文（定宽首字偏移=固有）")
add("dlg_ok_text",    F4, (310, 356, 170, 64),"bbox_center", (392, 388),"确认键文字中心")
add("dlg_cancel_text",F4, (560, 356, 150, 64),"bbox_center", (632, 388),"取消键文字中心")

BY_FRAME = {}
for e in E:
    BY_FRAME.setdefault(e["frame"], []).append(e)


def ink_bbox(im, sr):
    x, y, w, h = sr
    reg = im[y:y+h, x:x+w]
    med = np.median(reg)
    ys, xs = np.where(np.abs(reg - med) > INK_DEV)
    if len(ys) == 0:
        return None
    return (x + int(xs.min()), y + int(ys.min()), x + int(xs.max()), y + int(ys.max()))


def main():
    imcache = {}
    def get(f):
        if f not in imcache:
            imcache[f] = np.asarray(Image.open(str(BASE) + "\\" + f).convert("L"), dtype=np.int16)
        return imcache[f]

    rows = []
    for f, items in BY_FRAME.items():
        im = get(f)
        for e in items:
            bb = ink_bbox(im, e["sr"])
            if bb is None:
                rows.append(dict(key=e["key"], frame=f, verdict="NO-INK",
                                 dx=0, dy=0, bbox=None, note=e["note"] + "（无墨迹）"))
                continue
            x0, y0, x1, y1 = bb
            bcx, bcy = (x0+x1)/2, (y0+y1)/2
            if e["mode"] == "bbox_center" and e["ex"]:
                ecx, ecy = e["ex"]
                dx, dy = int(bcx-ecx), int(bcy-ecy)
                dev = max(abs(dx), abs(dy))
                v = "PASS" if dev <= TH_C else ("WARN" if dev <= TH_CW else "FAIL")
            elif e["ex"]:
                ex, ey = e["ex"]
                dx, dy = x0-ex, y0-ey
                dev = max(abs(dx), abs(dy))
                v = "PASS" if dev <= TH_A else ("WARN" if dev <= TH_AW else "FAIL")
            else:
                dx, dy, dev, v = 0, 0, 0, "REPORT"
            rows.append(dict(key=e["key"], frame=f, verdict=v, dx=dx, dy=dy,
                             bbox=list(bb), note=e["note"]))

    n = {}
    for r in rows:
        n[r["verdict"]] = n.get(r["verdict"], 0) + 1
    print(f"== 居中/对齐断言（{len(rows)} 元素）==")
    for k in ("PASS", "WARN", "FAIL", "REPORT", "NO-INK"):
        if k in n:
            print(f"  {k}: {n[k]}")
    for r in rows:
        if r["verdict"] not in ("PASS", "REPORT"):
            print(f"  [{r['verdict']}] {r['key']}: d=({r['dx']:+},{r['dy']:+}) {r['note']}")

    out = str(BASE) + "\\centering_report.json"
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"summary": n, "rows": rows}, f, ensure_ascii=False, indent=1)
    print("report:", out)


if __name__ == "__main__":
    main()
