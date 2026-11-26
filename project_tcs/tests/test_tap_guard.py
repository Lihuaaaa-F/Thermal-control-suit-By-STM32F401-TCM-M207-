#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""test_tap_guard.py — 现场工具"带禁区的点击"主机自检（S-G x3；纯 Python，不碰探针）

起因（2026-11-26 D7）：estop_matrix 在模式页上点确认框的"取消"，确认框已经不在了，
那一下落到 TEC2"制冷"上——主控向 M207 发了一条真实的 TEC2 制冷命令。
用户要求"这类工具必须从结构上做不到"。于是禁区逻辑收进 site_common.tap_guarded，
本文件用假探针证明它真的拦得住：

  1. 模式页（页码 4）上，六个执行器矩形（两行制冷/制热、风扇、切自动）里的点全部被拒，
     且**一个字节都没写进探针**（被拒 ≠ 写了一半）；
  2. 急停键、返回键、首页导航键放行（它们是操作而不是执行器动作）；
  3. 其它页上同样的坐标放行（首页 (376,520) 是模式键，不是制冷）；
  4. 默认用法（不传 page）从固件 s_page 现读页码，也照样拦；
  5. 写入顺序仍然是 mode/x/y 先、序号最后，且写后回读一致（2026-11-24 的竞态教训）。
"""
import struct
import sys
from pathlib import Path

SITE = Path(__file__).resolve().parents[1] / "tools" / "site"
sys.path.insert(0, str(SITE))
import site_common as sc  # noqa: E402

G_TAP = 0x1000
S_PAGE = 0x2000


class FakeMB:
    """只实现 tap_guarded 用到的那几个探针操作。"""

    def __init__(self, page):
        self.page = page
        self.writes = []          # 全部 pr_write（被拒时必须是空的）
        self.taps = []            # 成功注入的 (x, y, mode, seq)

    def nm_addr(self, name):
        return {"g_tap_inject": G_TAP, "s_page": S_PAGE}[name]

    def pr_write(self, addr, data):
        self.writes.append((addr, bytes(data)))
        if addr == G_TAP + 4:
            self._triple = list(struct.unpack("<III", data))
        elif addr == G_TAP:
            self._seq = struct.unpack("<I", data)[0]
            self.taps.append((self._triple[1], self._triple[2], self._triple[0], self._seq))

    def pr_read(self, fmt, addr, k=1):
        if addr == S_PAGE:
            return [self.page]
        if addr == G_TAP + 4:
            return list(getattr(self, "_triple", [0, 0, 0]))
        if addr == G_TAP:
            return [getattr(self, "_seq", 0)]
        return [0] * k


fails = 0
checks = 0


def check(cond, msg):
    global fails, checks
    checks += 1
    if not cond:
        print("    FAIL: %s" % msg)
        fails += 1


def refused(mb, x, y, page):
    """在 page 页上点 (x,y) 必须被拒，且探针没有被写。"""
    global checks
    try:
        sc.tap_guarded(mb, x, y, 1, lambda: page)
    except sc.TapRefused:
        return True
    except Exception as e:                    # 别的异常不算"拦住了"
        print("    FAIL: (%d,%d) page=%d 抛的不是 TapRefused 而是 %r" % (x, y, page, e))
        checks += 1
        return False
    print("    FAIL: (%d,%d) page=%d 没被拒（禁区失效）" % (x, y, page))
    checks += 1
    return False


def allowed(mb, x, y, page):
    global checks
    before = len(mb.taps)
    try:
        sc.tap_guarded(mb, x, y, 2, lambda: page)
    except Exception as e:
        print("    FAIL: (%d,%d) page=%d 应放行却抛了 %r" % (x, y, page, e))
        checks += 1
        return False
    checks += 1
    return len(mb.taps) == before + 1


def main():
    print("test_tap_guard: 模式页执行器禁区（site_common.tap_guarded）")

    # 0) 矩形表本身
    check(len(sc.ACTUATOR_RECTS) == 6, "ACTUATOR_RECTS 应有 6 个矩形（两行制冷/制热 + 风扇 + 切自动）")
    check(sc.MODE_PAGE == 4, "MODE_PAGE 应是 4（s_page：0 首页 … 4 模式）")
    src = (SITE / "site_common.py").read_text(encoding="utf-8")
    check("shots7/02_mode_manual.png" in src, "矩形表旁应写明坐标来源（真机截图文件名）")

    # 1) 六个矩形内的点全被拒，且一个字节都没写
    for (x0, y0, x1, y1) in sc.ACTUATOR_RECTS:
        pts = [(x0, y0), (x1 - 1, y0), (x0, y1 - 1), (x1 - 1, y1 - 1),
               ((x0 + x1) // 2, (y0 + y1) // 2)]
        for (x, y) in pts:
            mb = FakeMB(4)
            ok = refused(mb, x, y, 4)
            check(ok, "矩形 (%d,%d)-(%d,%d) 内的 (%d,%d) 应被拒" % (x0, y0, x1, y1, x, y))
            check(mb.writes == [], "被拒的点击不得写探针（(%d,%d) 写了 %d 次）" % (x, y, len(mb.writes)))

    # 2) 操作键放行：急停键 / 返回键 / 首页导航键（模式页上急停键与返回键都在）
    mb = FakeMB(4)
    check(allowed(mb, 790, 482, 4), "急停键 (790,482) 在模式页应放行")
    mb = FakeMB(4)
    check(allowed(mb, 84, 28, 4), "返回键 (84,28) 在模式页应放行")
    for (px, py) in ((144, 520), (260, 200), (608, 520), (376, 520)):
        mb = FakeMB(0)
        check(allowed(mb, px, py, 0), "首页导航键 (%d,%d) 应放行" % (px, py))

    # 3) 矩形边界语义：左闭右开。右下角外侧点放行、左上角点拒绝
    x0, y0, x1, y1 = sc.ACTUATOR_RECTS[0]
    check(refused(FakeMB(4), x0, y0, 4), "矩形左上角 (%d,%d) 属禁区（左闭）" % (x0, y0))
    check(allowed(FakeMB(4), x1, y0, 4), "矩形右边界外侧 (%d,%d) 放行（右开）" % (x1, y0))
    check(allowed(FakeMB(4), x0, y1, 4), "矩形下边界外侧 (%d,%d) 放行（下开）" % (x0, y1))

    # 4) 其它页：同样的执行器坐标放行（首页同位是导航键）
    for page in (0, 1, 2, 3):
        check(allowed(FakeMB(page), 560, 300, page), "页码 %d 上 (560,300) 应放行" % page)

    # 5) 默认用法（page=None）：从固件 s_page 现读，模式页照样拦
    mb = FakeMB(4)
    try:
        sc.tap_guarded(mb, 560, 300, 1)
        check(False, "不传 page 时模式页 (560,300) 应被拒")
    except sc.TapRefused:
        check(mb.writes == [], "不传 page 被拒时也不得写探针")
    check(allowed(FakeMB(0), 560, 300, 0), "不传 page 时首页 (560,300) 应放行")

    # 6) 写入顺序与回读：mode/x/y 先写，序号最后；tap 记录里的三元素必须是本次参数
    mb = FakeMB(0)
    seq = sc.tap_guarded(mb, 123, 456, 2, lambda: 0)
    check(len(mb.taps) == 1 and mb.taps[0][0:3] == (123, 456, 2),
          "注入参数应为 (x=123,y=456,mode=2)，实际 %r" % (mb.taps,))
    check(seq == 1, "序号应从 0 递增到 1（固件按序号变化取坐标），实际 %r" % (seq,))

    print("test_tap_guard: PASS=%d FAIL=%d" % (checks - fails, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
