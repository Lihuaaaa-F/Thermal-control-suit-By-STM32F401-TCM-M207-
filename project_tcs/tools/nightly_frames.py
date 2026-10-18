#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""nightly_frames.py — H11 基准帧捕获/比对（UI 意外变化机器拦截 + 取证工具自验证）

三页基准：home / tune / qr（tests/baseline/*.png 入 git）。
- init 模式：--init 生成基线（先 p defaults + scrtest 归一化状态）。
- diff 模式：重捕获并逐像素比对；动态区（时钟/温度值）用掩码排除。
  fb_dump 若损坏（H11 工具自验证条款）→ 与基线 diff 必然 FAIL → 工具被拦截。

用法：python tools/nightly_frames.py init|diff
"""
import subprocess
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
BASE = ROOT / "tests" / "baseline"
CHIP, PROBE = "STM32H743II", "0d28:0204"
ELF = "build-sim/tcs_main.elf"
os_env = {**__import__("os").environ, "TCS_ELF": ELF}

# home 页动态区掩码 (x0,y0,x1,y1)：顶栏(时钟/状态点/横幅)、两行卡片(活动数值)
HOME_MASKS = [(0, 0, 1023, 48), (16, 56, 1008, 280), (16, 296, 1008, 448)]
PAGES = {
    "home": (["scrtest home"], HOME_MASKS),
    "tune": (["scrtest tune"], []),
    "qr":   (["sim touch 116 572", "sim touch 680 572"], []),  # home→连接
}


def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=180, **kw)


def console(*cmds):
    import time
    for c in cmds:
        for attempt in range(3):          # 探针会话竞争下重试 3 次
            r = sh(["python", "tools/mb_console.py", c], env=os_env)
            if "TIMEOUT" not in r.stdout and r.returncode == 0:
                break
            time.sleep(1.5)
        time.sleep(0.5)


def capture(path):
    r = sh(["python", "tools/fb_dump.py", str(path), "0", "600"])
    if r.returncode != 0:
        raise RuntimeError(f"fb_dump fail: {r.stderr[-200:]}")


def masked_equal(a, b, masks):
    diff = 0
    for y in range(a.height):
        for x in range(a.width):
            if any(x0 <= x <= x1 and y0 <= y <= y1 for (x0, y0, x1, y1) in masks):
                continue
            if a.getpixel((x, y)) != b.getpixel((x, y)):
                diff += 1
    return diff


def canonical_state():
    """归一化到可复现画面状态（p defaults + 默认画面）"""
    console("p defaults Y", "sim body 36.8", "ack")


def main():
    mode = sys.argv[1] if len(sys.argv) > 1 else "diff"
    BASE.mkdir(exist_ok=True)
    results = {}
    for page, (cmds, masks) in PAGES.items():
        canonical_state()
        console(*cmds)
        import time
        time.sleep(2.0)                   # 页切换+重绘稳定（探针负载下留裕量）
        tmp = BASE / f"_{page}_now.png"
        capture(tmp)
        base = BASE / f"{page}.png"
        if mode == "init":
            tmp.rename(base)
            results[page] = "baseline written"
            continue
        if not base.exists():
            results[page] = "FAIL (no baseline; run init)"
            continue
        d = masked_equal(Image.open(tmp).convert("RGB"), Image.open(base).convert("RGB"), masks)
        results[page] = f"{'OK' if d == 0 else 'FAIL'} diff={d}"
        if d != 0:
            tmp.rename(BASE / f"_{page}_FAIL.png")   # 失败帧留档分析
        else:
            tmp.unlink(missing_ok=True)
    ok = all(v.startswith(("OK", "baseline")) for v in results.values())
    print(f"nightly_frames[{mode}]: {results} → {'PASS' if ok else 'FAIL'}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
