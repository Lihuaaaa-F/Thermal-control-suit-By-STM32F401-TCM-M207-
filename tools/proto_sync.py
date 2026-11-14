#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""proto_sync.py — 协议单一源校验（R1 AD-6）

检查：
  1) 两端 tcs_proto.h 都是薄包装，且内容逐字一致（只允许换行差异）；
  2) 包装里不含任何协议定义（#define TCS_ / typedef struct / static inline）；
  3) 唯一源 protocol/tcs_proto.h 存在且含 TCS_PROTO_VER；
  4) 全仓除 protocol/ 外没有第二处定义 TCS_PROTO_VER。

用法：python tools/proto_sync.py [--check]
退出码 0 = 一致；1 = 漂移（构建/CI 应阻断）。
"""
import re
import sys
from pathlib import Path

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

REPO = Path(__file__).resolve().parent.parent
CANON = REPO / "protocol" / "tcs_proto.h"
WRAPPERS = [
    REPO / "project_tcs" / "App" / "tcs" / "tcs_proto.h",
    REPO / "esp32" / "components" / "tcs_link" / "tcs_proto.h",
]
DEF_PAT = re.compile(r"^\s*(#\s*define\s+TCS_|typedef\s+struct|static\s+inline)")


def norm(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n").strip()


def main() -> int:
    fails = []

    if not CANON.exists():
        fails.append(f"缺少唯一源 {CANON.relative_to(REPO)}")
    else:
        c = norm(CANON)
        if "TCS_PROTO_VER" not in c:
            fails.append("唯一源缺 TCS_PROTO_VER")
        if "#ifndef TCS_PROTO_H" not in c:
            fails.append("唯一源缺 include guard TCS_PROTO_H")

    texts = []
    for w in WRAPPERS:
        if not w.exists():
            fails.append(f"缺少包装 {w.relative_to(REPO)}")
            continue
        t = norm(w)
        texts.append((w, t))
        if "protocol/tcs_proto.h" not in t:
            fails.append(f"{w.relative_to(REPO)} 未 include 唯一源")
        for i, line in enumerate(t.splitlines(), 1):
            if DEF_PAT.search(line):
                fails.append(f"{w.relative_to(REPO)}:{i} 含协议定义（应只在唯一源里）: {line.strip()[:60]}")

    if len(texts) == 2 and texts[0][1] != texts[1][1]:
        fails.append("两端包装内容不一致（除换行外必须逐字相同）")

    # 全仓扫描：除 protocol/ 外不得再定义 TCS_PROTO_VER
    for p in REPO.rglob("*.h"):
        if "protocol" in p.parts or "managed_components" in p.parts or "third_party" in p.parts:
            continue
        try:
            txt = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        if re.search(r"^\s*#\s*define\s+TCS_PROTO_VER\b", txt, re.M):
            fails.append(f"{p.relative_to(REPO)} 重复定义 TCS_PROTO_VER")

    if fails:
        print("proto_sync: FAIL")
        for f in fails:
            print("  -", f)
        return 1
    print("proto_sync: OK (单一源 + 2 包装一致，无重复定义)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
