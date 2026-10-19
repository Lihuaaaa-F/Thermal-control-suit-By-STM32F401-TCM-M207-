#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""touch_analyze.py — WP-M0 触摸失效模式分析（原始日志 → 三分判定 + 分布表）

输入：`touch log dump` 的 CSV（seq,t_ms,n,xr,yr,ev,id,src），首行 # 注释为日志头。
产出：
  - 失效模式三分判定：漏触 / 误杀 / 偏移（任务书 M0）
  - 幽灵会话特征表（时长/位移/坐标）
  - 帧间间隔分布（暴露 100Hz 报点 vs 10ms 轮询混叠/INT 丢失）
  - 会话级统计（驱动层复演会话机判定，交叉验证固件计数）

用法：python tools/touch_analyze.py 日志.csv [--plot 输出.png]
"""
import argparse
import csv
import sys
from collections import Counter

GHOST_MAX_MS = 50.0
JUMP_PX = 200

MAP = "GS_SWAP_V1"   # screen=(raw_y, raw_x)；本工具只做 raw 层分析，不依赖映射


def load(path):
    rows = []
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split(",")
            if parts[0] == "seq" or len(parts) < 8:
                continue
            try:
                rows.append(dict(seq=int(parts[0]), t=int(parts[1]), n=int(parts[2]),
                                 xr=int(parts[3]), yr=int(parts[4]),
                                 ev=int(parts[5], 16), id=int(parts[6]),
                                 src=int(parts[7])))
            except ValueError:
                continue
    return rows


def sessions(rows):
    """按日志序列复演会话切分（n>0 连续段 = 会话；抬起/空闲标记 n=0 结束）"""
    sess, cur = [], None
    for r in rows:
        if r["n"] == 0:
            if cur:
                sess.append(cur)
                cur = None
            continue
        if cur is None:
            cur = dict(t0=r["t"], t1=r["t"], frames=0,
                       xr_min=r["xr"], xr_max=r["xr"],
                       yr_min=r["yr"], yr_max=r["yr"],
                       evs=Counter(), ids=Counter(), src=r["src"],
                       first=(r["xr"], r["yr"]), last=(r["xr"], r["yr"]))
        cur["t1"] = r["t"]
        cur["frames"] += 1
        cur["xr_min"] = min(cur["xr_min"], r["xr"])
        cur["xr_max"] = max(cur["xr_max"], r["xr"])
        cur["yr_min"] = min(cur["yr_min"], r["yr"])
        cur["yr_max"] = max(cur["yr_max"], r["yr"])
        cur["evs"][r["ev"]] += 1
        cur["ids"][r["id"]] += 1
        cur["last"] = (r["xr"], r["yr"])
    if cur:
        sess.append(cur)
    for s in sess:
        s["dur_ms"] = s["t1"] - s["t0"]
        s["disp"] = max(abs(s["last"][0] - s["first"][0]),
                        abs(s["last"][1] - s["first"][1]))
        s["bbox_w"] = s["xr_max"] - s["xr_min"]
        s["bbox_h"] = s["yr_max"] - s["yr_min"]
        s["ghost"] = (s["dur_ms"] < GHOST_MAX_MS and s["disp"] == 0
                      and len(s["ids"]) <= 1)
    return sess


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--plot", default=None)
    args = ap.parse_args()

    rows = load(args.csv)
    if not rows:
        print("no data rows")
        return 1
    dur_total = rows[-1]["t"] - rows[0]["t"]
    contact_rows = [r for r in rows if r["n"] > 0]
    print(f"== 总览 ==")
    print(f"日志行数 {len(rows)}  触点帧 {len(contact_rows)}  跨度 {dur_total/1000:.1f}s")
    print(f"注入帧(src=1) {sum(1 for r in rows if r['src'])}")

    # 帧间间隔分布（触点帧）
    gaps = [b["t"] - a["t"] for a, b in zip(contact_rows, contact_rows[1:])
            if b["t"] > a["t"]]
    if gaps:
        gaps.sort()
        n = len(gaps)

        def pct(p):
            return gaps[min(n - 1, int(n * p))]
        print(f"\n== 触点帧间隔分布 (n={n}) ==")
        print(f"  min={gaps[0]}ms p50={pct(0.5)}ms p90={pct(0.9)}ms p99={pct(0.99)}ms max={gaps[-1]}ms")
        # 100Hz 报点 vs 10ms 轮询混叠：理想 10-12ms；>30ms 视为丢帧/饥饿
        lost = sum(1 for g in gaps if g > 30)
        print(f"  >30ms(疑似丢帧/轮询饥饿) {lost} ({100*lost/n:.1f}%)")

    sess = sessions(rows)
    ghosts = [s for s in sess if s["ghost"]]
    reals = [s for s in sess if not s["ghost"]]
    print(f"\n== 会话切分 ==")
    print(f"  会话总数 {len(sess)}  幽灵特征 {len(ghosts)}  真会话特征 {len(reals)}")

    if ghosts:
        print("\n== 幽灵会话特征表 ==")
        print("  t0(ms)  dur(ms)  disp(px)  raw(x,y)→(x,y)       ev  id  frames")
        for s in ghosts[:40]:
            print(f"  {s['t0']:<8}{s['dur_ms']:<9}{s['disp']:<10}"
                  f"{s['first']}→{s['last']}"
                  f"  {dict(s['evs'])!s:<20}{dict(s['ids'])!s:<6}{s['frames']}")

    if reals:
        print("\n== 真会话特征表（前 20）==")
        print("  t0(ms)  dur(ms)  disp(px)  bbox(raw)      raw(first)→(last)    frames")
        for s in reals[:20]:
            print(f"  {s['t0']:<8}{s['dur_ms']:<9}{s['disp']:<10}"
                  f"{s['bbox_w']}x{s['bbox_h']:<13}"
                  f"{s['first']}→{s['last']}"
                  f"  {s['frames']}")

    # 失效模式三分判定的输入证据
    print("\n== 失效模式三分输入 ==")
    valid_ev = sum(1 for r in contact_rows if r["ev"] == 0x80)
    other_ev = len(contact_rows) - valid_ev
    print(f"  contact 事件帧(ev=0x80) {valid_ev}   非法事件帧 {other_ev}")
    print(f"  会话机口径幽灵率 {len(ghosts)}/{len(sess)} = "
          f"{(100*len(ghosts)/len(sess) if sess else 0):.1f}%")

    if args.plot:
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
            fig, ax = plt.subplots(2, 1, figsize=(9, 6))
            if gaps:
                ax[0].hist(gaps, bins=60, color="#3b6ea5")
                ax[0].set_title("touch frame gap (ms)")
                ax[0].axvline(30, color="r", ls="--")
            if sess:
                ax[1].hist([s["dur_ms"] for s in sess], bins=40, color="#2a8a5a")
                ax[1].axvline(GHOST_MAX_MS, color="r", ls="--")
                ax[1].set_title("session duration (ms)")
            fig.tight_layout()
            fig.savefig(args.plot, dpi=110)
            print(f"\nplot -> {args.plot}")
        except ImportError:
            print("matplotlib 不可用，跳过出图", file=sys.stderr)

    return 0


if __name__ == "__main__":
    sys.exit(main())
