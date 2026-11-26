#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""check_snap_fresh.py — X01-F-04 的接线门禁（S 线 e1，2026-11-26）。

主机用例只能验证 link_snap.h 里的纯判定；"snap_apply 真的把 0x20 的 fresh_ms/badge 写进镜像"
与"0x21 的 TCS_D_BADGE 分支真的更新角标"这两处接线在 Core/Src/main.c（依赖 HAL，主机编不了）。
修复前的缺陷正是"字段解出来了但没人用"，所以这里用逐字核对把接线钉住：

  1. main.c 的 snap_apply 里调用 tcs_link_fresh_apply(&s_fresh, s->fresh_ms, s->badge)；
  2. 0x21 分支有 TCS_D_BADGE → tcs_link_fresh_badge(&s_fresh, d.badge)；
  3. link_snap.h 声明 tcs_link_fresh_copy，main.c 实现它；
  4. 镜像结构保留 fresh_ms 原值（0xFFFF 不能被换算成毫秒数）；
  5. 不许把协议 badge 接到本地 POST 角标（post_badge_state / post_set_...）上。

用法：python -I tests/check_snap_fresh.py（只依赖标准库）
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MAIN_C = os.path.join(ROOT, "project_tcs", "Core", "Src", "main.c")
SNAP_H = os.path.join(ROOT, "project_tcs", "App", "comm", "link_snap.h")

fails = 0
checks = 0


def check(cond, msg):
    global fails, checks
    checks += 1
    if cond:
        print("  ok   " + msg)
    else:
        print("  FAIL " + msg)
        fails += 1


def read(p):
    with open(p, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def main():
    main_c = read(MAIN_C)
    snap_h = read(SNAP_H)

    # 1) snap_apply 消费 fresh_ms / badge
    m = re.search(r"static void snap_apply\(const tcs_snap_pl_t \*s\)\s*\{(.*?)\n\}", main_c, re.S)
    check(m is not None, "main.c: snap_apply found")
    body = m.group(1) if m else ""
    check(re.search(r"tcs_link_fresh_apply\(&s_fresh,\s*s->fresh_ms,\s*s->badge\)", body)
          is not None,
          "main.c: snap_apply calls tcs_link_fresh_apply(&s_fresh, s->fresh_ms, s->badge)")

    # 2) 0x21 增量的 TCS_D_BADGE 分支
    check(re.search(r"if \(mask & TCS_D_BADGE\)\s*tcs_link_fresh_badge\(&s_fresh,\s*d\.badge\)",
                    main_c) is not None,
          "main.c: 0x21 TCS_D_BADGE updates the badge mirror")

    # 3) 镜像读取口
    check("void tcs_link_fresh_copy(tcs_link_fresh_t *out)" in snap_h,
          "link_snap.h: declares tcs_link_fresh_copy")
    check("void tcs_link_fresh_copy(tcs_link_fresh_t *out)" in main_c,
          "main.c: implements tcs_link_fresh_copy")

    # 4) 0xFFFF 原值保留（不许换成毫秒/秒换算）
    check("f->fresh_ms = fresh_ms;" in snap_h,
          "link_snap.h: fresh_ms is stored verbatim (0xFFFF stays a sentinel)")

    # 5) 不许把协议 badge 接到本地 POST 角标
    bad = re.findall(r"post_badge[a-z_]*\([^)]*badge", main_c)
    check(not bad, "main.c: protocol badge is not wired into the local POST badge")

    print("check_snap_fresh: PASS=%d FAIL=%d" % (checks - fails, fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
