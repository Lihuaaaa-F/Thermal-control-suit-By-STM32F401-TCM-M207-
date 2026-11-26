#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""test_m1_gates.py — m1"模式"页批次的源码门禁（只用标准库；python -I 运行）

守的是落在主机单测够不着的地方的接线（这些文件依赖 RTOS / IDF，主机编译不了），
逐字核对——与 S-G e2 的 test_m207_gates.py 同一做法。退出码 0 = 全部通过。
"""
import os
import sys

# 用 python -I 跑（隔离模式不认 PYTHONUTF8）：控制台可能是 cp1252 / gbk，统一按 UTF-8 输出
for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
ESP = os.path.dirname(HERE)                       # esp32/
CORE = os.path.join(ESP, "components", "tcs_core")
LINK = os.path.join(ESP, "components", "tcs_link")


def read(*parts):
    p = os.path.join(*parts)
    if not os.path.isfile(p):
        raise SystemExit("[m1gates] FAIL: 缺少 %s" % p)
    with open(p, encoding="utf-8", errors="replace") as f:
        return f.read().replace("\r\n", "\n")


def body_of(text, signature):
    """取以 signature 开头的函数的函数体（到第一个行首的 '}' 为止）。"""
    a = text.find(signature)
    if a < 0:
        return None
    b = text.find("\n}\n", a)
    return text[a:b + 3] if b > 0 else None


def main():
    fails = []

    # 1) 屏链路断开 3 秒 = 急停：control_service.c 的每个控制节拍必须问一次判定，
    #    判定要触发时必须走 tcs_emergency_stop_src（和急停命令同一条路径）。
    cs = read(CORE, "control_service.c")
    for needle in ('#include "link_loss_guard.h"', '#include "link_task.h"'):
        if needle not in cs:
            fails.append("control_service.c 缺 %s" % needle)
    task = body_of(cs, "static void control_task(void *arg)")
    if task is None or "control_poll(now_ms);" not in task:
        fails.append("control_service.c 找不到 control_task / control_poll(now_ms)")
    elif task.count("link_loss_check(now_ms);") != 1:
        fails.append("control_service.c 的控制节拍必须恰好调一次 link_loss_check(now_ms)")
    elif task.index("link_loss_check(now_ms);") < task.index("control_poll(now_ms);"):
        fails.append("link_loss_check 必须排在 control_poll 之后（先按最新状态机状态判'是否已在安全态'）")
    # 取定义（带紧随其后的 '{'），不会误取上方的前置声明
    chk = body_of(cs, "static void link_loss_check(uint32_t now_ms)\n{")
    if chk is None:
        fails.append("control_service.c 找不到 link_loss_check")
    else:
        if "link_peer_age_ms(&age)" not in chk:
            fails.append("link_loss_check 必须用 link_peer_age_ms 取'距最近一帧对端帧的毫秒数'")
        if "link_loss_guard_step(&s_link_loss, seen, age, control_state() == CTRL_SAFE)" not in chk:
            fails.append("link_loss_check 必须把 是否连上过 / 对端帧龄 / 是否已在安全态 交给 link_loss_guard_step")
        if "!= TCS_LINK_LOSS_ESTOP) return;" not in chk:
            fails.append("link_loss_check 只在判定为 TCS_LINK_LOSS_ESTOP 时继续")
        if chk.count("tcs_emergency_stop_src(PARAM_SRC_KERNEL);") != 1:
            fails.append("link_loss_check 触发时必须调一次 tcs_emergency_stop_src(PARAM_SRC_KERNEL)")
        if "SCREEN LINK LOST" not in chk:
            fails.append("link_loss_check 的日志必须写明原因（SCREEN LINK LOST）")

    # 2) "对端帧"的计时起点在方向闸门之后：comm.c 只在 comm_note_peer 里记，
    #    comm_rx_feed_byte 里闸门调用在前、comm_note_peer 在后。
    cc = read(LINK, "comm.c")
    if cc.count("L->peer_ms = L->now_ms_fn ? L->now_ms_fn() : 0u;") != 1:
        fails.append("comm.c 记对端帧时刻的语句必须恰好 1 处")
    gate_call = "    if (comm_rx_gate(L, payload, plen)) return 0;\n"
    note_call = "    comm_note_peer(L);\n"
    if cc.count(gate_call) != 1 or cc.count(note_call) != 1:
        fails.append("comm.c 的闸门调用 / comm_note_peer 调用应当各恰好 1 处")
    elif cc.index(gate_call) > cc.index(note_call):
        fails.append("comm.c 必须先过方向闸门再记对端帧时刻")
    note = body_of(cc, "static void comm_note_peer(comm_link_t *L)")
    if note is None or "L->peer_ms =" not in note or "L->peer_seen = 1;" not in note:
        fails.append("comm.c 对端帧时刻必须记在 comm_note_peer 里（闸门之后唯一的入口）")

    # 3) link_task.c 把它如实转出（时基 = link_now_ms，与 control 任务同为 esp_timer）。
    lt = read(LINK, "link_task.c")
    fn = body_of(lt, "int link_peer_age_ms(uint32_t *age_ms)")
    if fn is None:
        fails.append("link_task.c 找不到 link_peer_age_ms")
    else:
        if "comm_peer_last_ms(&s_link, &last)" not in fn:
            fails.append("link_peer_age_ms 必须读 comm_peer_last_ms(&s_link, …)")
        if "link_now_ms() - last" not in fn:
            fails.append("link_peer_age_ms 的帧龄必须用 link_now_ms() 算")
        if "return 0;" not in fn:
            fails.append("link_peer_age_ms：从未收到对端帧必须返回 0")

    # 4) 新文件进了固件构建（不只是主机单测）。
    cm = read(CORE, "CMakeLists.txt")
    for src in ("control_manual.c", "link_loss_guard.c"):
        if '"%s"' % src not in cm:
            fails.append("tcs_core/CMakeLists.txt 没有编入 %s" % src)

    # 5) m1 第 3 层（协议 TCS_PROTO_VER 2）：主控把两路输出方向与电学量装进 0x20 / 0x21
    #    （esp32/main/tcs_snap.c 依赖 RTOS，主机编译不了）。
    import re
    snap = read(ESP, "main", "tcs_snap.c")
    for needle in (
        "tcs_tec_dir_pack(control_tec_dir(1), control_tec_dir(2))",   # 方向来自控制内核
        "m207_elec_get(ch, &e)",                                       # 电学量来自 M207 轮询缓存
        "e.valid ? tcs_elec_pack_v(e.volt) : TCS_ELEC_V_UNKNOWN",      # 取不到 / 太旧 → 未知
        "e.valid ? tcs_elec_pack_i(e.cur)  : TCS_ELEC_I_UNKNOWN",
        ".tec_dir = cy.tec_dir",                                       # 0x20 末尾的 9 字节
        ".tec1_v = cy.tec1_v", ".tec1_i = cy.tec1_i",
        ".tec2_v = cy.tec2_v", ".tec2_i = cy.tec2_i",
        "mask |= TCS_D_TEC_DIR", "mask |= TCS_D_ELEC",                 # 0x21 的两个新 mask 位
    ):
        if needle not in snap:
            fails.append("tcs_snap.c 缺：%s" % needle)
    m = re.search(r"uint8_t pl\[(\d+)\];\s*/\* 全字段", snap)
    if not m or int(m.group(1)) < 31:
        fails.append("tcs_snap.c 的 0x21 载荷缓冲必须放得下全字段（含 m1 的 9 字节，共 31 字节）")

    # 6) 屏端镜像：Core/Src/main.c 在 0x20 与 0x21 两个入口都把新字段落进镜像；POST 的 VER 自检项随版本号同改
    #    （main.c / post.c 依赖 HAL，主机编译不了；换算本身在 link_snap.h，由 tests/test_proto_m1.c 直接编）。
    H7 = os.path.normpath(os.path.join(ESP, "..", "project_tcs"))
    mainc = read(H7, "Core", "Src", "main.c")
    for needle in (
        "tcs_link_tec_set_dirs(&s_tec, s->tec_dir)",
        "tcs_link_tec_set_elec(&s_tec, s->tec1_v, s->tec1_i, s->tec2_v, s->tec2_i)",
        "if (mask & TCS_D_TEC_DIR) tcs_link_tec_set_dirs(&s_tec, d.tec_dir)",
        "if (mask & TCS_D_ELEC)    tcs_link_tec_set_elec(&s_tec, d.tec1_v, d.tec1_i, d.tec2_v, d.tec2_i)",
        "s_last_snap.tec_dir != s.tec_dir",                           # 输出方向变化 = 看得见的状态变化
    ):
        if needle not in mainc:
            fails.append("H743 Core/Src/main.c 缺：%s" % needle)
    if "(TCS_PROTO_VER == 3u)" not in read(H7, "App", "tcs", "post.c"):
        fails.append("屏端 POST 的 VER 自检项必须与 TCS_PROTO_VER 同步（== 3u）")

    # 7) S 线 i3（2026-11-26；P1-09 收口；协议 TCS_PROTO_VER 3）：0x20 末尾的 3 字节（命令意图 / 模块回显 /
    #    开关状态是否可信）从控制内核取、落进快照；屏端把它落进镜像，界面据 known 位把 M207 离线的那一路
    #    画成"未知"。这些文件依赖 RTOS / HAL，主机编译不了；判定与换算本身由 tests/test_m207_force.c、
    #    tests/test_control_esp32.c（K19）、project_tcs/tests/test_ui_known.c 直接编。
    for needle in (
        "c->tec_cmd   = (uint8_t)((control_tec_cmd(1) ? 1u : 0u) | (control_tec_cmd(2) ? 2u : 0u));",
        "c->tec_act   = (uint8_t)((control_tec_act(1) ? 1u : 0u) | (control_tec_act(2) ? 2u : 0u));",
        "c->tec_known = (uint8_t)(control_tec_known() & 0x03u);",          # 现取现算，不用晚一拍的缓存副本
        ".tec_cmd = cy.tec_cmd, .tec_act = cy.tec_act, .tec_known = cy.tec_known,",
    ):
        if needle not in snap:
            fails.append("tcs_snap.c 缺（i3）：%s" % needle)
    corec = read(CORE, "control.c")
    for needle in (
        "uint8_t control_tec_act(uint8_t ch) { return (ch == 2) ? s_tec_act_on[1] : s_tec_act_on[0]; }",  # 回显是独立字段
        "void control_tec_echo_set(uint8_t ch, uint8_t on)",
        "if (!(tec_known_now() & (uint8_t)(1u << (ch - 1u)))) return TEC_DIR_PAUSE;",                    # 状态未知 → 方向是暂停
    ):
        if needle not in corec:
            fails.append("tcs_core/control.c 缺（i3）：%s" % needle)
    m207c = read(ESP, "components", "tcs_m207", "m207_core.c")
    for needle in (
        "int m207_tec_force(uint8_t ch, uint8_t on)",                       # 安全关断通道：离线也受理
        "if (s_lane[ch].hold || !cmd_on || !echo_on) {",                    # 复联先补发关断：保持位 / 命令意图 / 回显
        "control_tec_echo_set((uint8_t)r.module, (uint8_t)(r.value != 0.0f));",  # M207 服务回 TCSW? 时写回显
    ):
        if needle not in m207c:
            fails.append("tcs_m207/m207_core.c 缺（i3）：%s" % needle)
    if ".tec_force = op_tec_force," not in read(ESP, "main", "main.c"):
        fails.append("esp32/main/main.c 没有接 .tec_force（SAFE 的关断退回普通队列，模块离线时送不到执行器）")
    for needle in ("tcs_link_tec_set_state(&s_tec, s->tec_cmd, s->tec_act, s->tec_known)",):
        if needle not in mainc:
            fails.append("H743 Core/Src/main.c 缺（i3）：%s" % needle)
    tuic = read(H7, "App", "lvgl_ui", "tui.c")
    p0c = read(H7, "App", "lvgl_ui", "tui_p0.c")
    for needle in ("s_in.tec_known = t.known",):
        if needle not in tuic:
            fails.append("H743 App/lvgl_ui/tui.c 缺（i3）：%s" % needle)
    for needle in ("if (module == UI_SRC_UNKNOWN || !known)", "UI_H_TECK"):
        if needle not in p0c:
            fails.append("H743 App/lvgl_ui/tui_p0.c 缺（i3）：首页 TEC 胶囊没有跟着开关状态是否可信走：%s" % needle)

    # 8) S 线 i3（P1-12 的另一半）：link_start / web_start 带返回值并进启动汇总——原来是 void 接口，
    #    启动汇总只能记成"未核实"，链路任务 / WS 推送任务没起来时照样打 all up。
    #    link_task.c 原样编译的主机用例在 tests/test_link_dir.c 末尾；web 侧依赖 httpd，只有这条源码门禁。
    linkc = read(LINK, "link_task.c")
    for needle in (
        "esp_err_t link_start(void)",
        "return ESP_ERR_NO_MEM;",                       # 任务没建成：带出去
        "uart_driver_delete(LINK_UART);",               # 并把 UART 驱动卸掉
    ):
        if needle not in linkc:
            fails.append("tcs_link/link_task.c 缺（i3）：%s" % needle)
    wsc = read(ESP, "components", "tcs_web", "websocket.c")
    for needle in ("esp_err_t ws_init(httpd_handle_t hd)", "return ESP_ERR_NO_MEM;"):
        if needle not in wsc:
            fails.append("tcs_web/websocket.c 缺（i3）：%s" % needle)
    httpc = read(ESP, "components", "tcs_web", "http_server.c")
    for needle in ("esp_err_t web_start(void)", "return ws_init(s_httpd);"):
        if needle not in httpc:
            fails.append("tcs_web/http_server.c 缺（i3）：%s" % needle)
    if httpc.count("ws_init(") != 1:
        fails.append("tcs_web/http_server.c：web_start 只能调一次 ws_init（调两次会起两个推送任务）")
    mainesp = read(ESP, "main", "main.c")
    for needle in ("svc_note(&svc, SVC_BIT_LINK, link_start());", "svc_note(&svc, SVC_BIT_WEB, web_start());"):
        if needle not in mainesp:
            fails.append("esp32/main/main.c 缺（i3）：%s" % needle)
    for stale in ("svc_unverified(&svc, SVC_BIT_LINK)", "svc_unverified(&svc, SVC_BIT_WEB)"):
        if stale in mainesp:
            fails.append("esp32/main/main.c 还在把 link / web 记成未核实：%s" % stale)
    if mainesp.count("link_start()") != 1:
        fails.append("esp32/main/main.c：link_start 只能调一次（重入会重复安装 UART 驱动）")

    if fails:
        print("[m1gates] FAIL")
        for f in fails:
            print("  - " + f)
        return 1
    print("[m1gates] PASS (屏链路断开 3 秒 = 急停的接线：控制节拍 → 纯判定 → 急停路径；对端帧时刻在方向闸门之后；"
          "协议 ver3：主控快照带输出方向、电学量与 TEC 开关的命令 / 回显 / 是否可信，屏端落镜像并对未知的一路画未知)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
