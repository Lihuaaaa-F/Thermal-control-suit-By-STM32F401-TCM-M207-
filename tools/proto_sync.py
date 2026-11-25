#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""proto_sync.py — 协议单一源校验（R1 AD-6；2026-11-25 S-G l1 扩到方向）

检查：
  1) 两端 tcs_proto.h 都是薄包装，且内容逐字一致（只允许换行差异）；
  2) 包装里不含任何协议定义（#define TCS_ / typedef struct / static inline）；
  3) 唯一源 protocol/tcs_proto.h 存在且含 TCS_PROTO_VER；
  4) 全仓除 protocol/ 外没有第二处定义 TCS_PROTO_VER；
  5) 方向表（protocol/tcs_proto.h 的 MT_TCS_*_DIR，R5-01）与两端源码的收发分支逐条一致：
       接收分支 `case MT_TCS_X:` 只能出现在"对端会发 X"的那一端；
       发送点 `<组帧函数>(MT_TCS_X` 只能出现在"本端会发 X"的那一端；
  6) 两端 comm.c 的方向闸门函数 comm_rx_gate 逐字相同，且调用点在 ACK 处理 / 去重 / 交付之前；
     出队处打方向位并重算 CRC；
  7) 旧的"等长 + 发送窗"回声过滤没有任何残留符号。

用法：python tools/proto_sync.py --check        退出码 0 = 一致；1 = 漂移（构建/CI 应阻断）。
      python tools/proto_sync.py --table        打印"消息号 → 谁会发 → 发送点 / 接收分支（文件:行）"核对表。
"""
import os
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

# —— 5) 6) 7)（S-G l1，R5-01）——
COMM = {
    "H743": REPO / "project_tcs" / "App" / "comm" / "comm.c",
    "ESP32": REPO / "esp32" / "components" / "tcs_link" / "comm.c",
}
# 扫描收发分支的目录（只看固件源码；测试与工具可以随意组帧）
SCAN_DIRS = [REPO / "esp32" / "components", REPO / "esp32" / "main",
             REPO / "project_tcs" / "App", REPO / "project_tcs" / "Core" / "Src"]
SKIP_PARTS = {"tests", "third_party", "build", "managed_components", "host_stubs", "__pycache__"}

OTHER = {"H743": "ESP32", "ESP32": "H743"}
RX_PAT = re.compile(r"\bcase\s+(MT_TCS_\w+)\s*:")
TX_PAT = re.compile(r"\b(?:mt_frame|proto_encode_frame_with_mt|frame_with_mt|snap_send)\(\s*(MT_TCS_\w+)")
GATE_CALL = "    if (comm_rx_gate(L, payload, plen)) return 0;\n"
ACK_LINE = "        comm_on_ack(L, payload[2], tcs_frame_mt(payload[3]));\n"
MARK_LINE = "            L->tx_queue[t][3] = mt_wire;\n"
CRC_LINE = "            L->tx_queue[t][len - 1] = proto_crc8(&L->tx_queue[t][2], len - 3);\n"
LEGACY = ("s_echo_plen", "s_echo_until_ms", "TCS_ECHO_MARGIN_MS", "s_echo_drop")


def norm(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n").strip()


def read(p: Path) -> str:
    return p.read_text(encoding="utf-8", errors="replace").replace("\r\n", "\n")


def side_of(p: Path) -> str:
    return "ESP32" if "/esp32/" in p.as_posix() else "H743"


def may_send(dirdef: str, side: str) -> bool:
    """方向表允许 side 发出这一类消息号吗。"""
    return dirdef == "TCS_DIR_BOTH" or dirdef.endswith("_" + side + "_ONLY")


def firmware_sources():
    for d in SCAN_DIRS:
        for root, dirs, files in os.walk(d):
            dirs[:] = [x for x in dirs if x not in SKIP_PARTS]
            for fn in files:
                if fn.endswith((".c", ".h")):
                    yield Path(root) / fn


def check_direction(fails, table=False):
    """5) 方向表 vs 两端源码的收发分支。返回 {消息号宏: {dir, tx:[(端,文件,行)], rx:[...]}}。"""
    canon = read(CANON)
    rows = {}
    for mt, d in re.findall(r"#define\s+(MT_TCS_\w+)_DIR\s+(TCS_DIR_\w+)", canon):
        rows[mt] = {"dir": d, "tx": [], "rx": []}
    if not rows:
        fails.append("唯一源里没有 MT_TCS_*_DIR 方向表")
        return rows
    for p in firmware_sources():
        rel = p.relative_to(REPO).as_posix()
        side = side_of(p)
        for i, line in enumerate(read(p).splitlines(), 1):
            if "自环夹具" in line:
                continue                      # 开机自环自测里伪装成对端的帧
            for mt in RX_PAT.findall(line):
                if mt not in rows:
                    fails.append(f"{rel}:{i} 接收分支 case {mt}_ 没有方向表项")
                    continue
                rows[mt]["rx"].append((side, rel, i))
                if not may_send(rows[mt]["dir"], OTHER[side]):
                    fails.append(f"{rel}:{i} {side} 有 case {mt}_ 的接收分支，但方向表说 {OTHER[side]} 不会发它")
            for mt in TX_PAT.findall(line):
                if mt not in rows:
                    fails.append(f"{rel}:{i} 发送点 {mt}_ 没有方向表项")
                    continue
                rows[mt]["tx"].append((side, rel, i))
                if not may_send(rows[mt]["dir"], side):
                    fails.append(f"{rel}:{i} {side} 在发 {mt}_，但方向表说 {side} 不会发它")
    for mt, row in rows.items():
        if not row["tx"]:
            fails.append(f"方向表里的 {mt}_ 在两端源码里找不到发送点（组帧函数名变了要同步 TX_PAT）")
        if not row["rx"] and mt != "MT_TCS_ACK":
            fails.append(f"方向表里的 {mt}_ 在两端源码里找不到接收分支")
    if table:
        num = dict(re.findall(r"#define\s+(MT_TCS_\w+)\s+(0x[0-9A-Fa-f]+)", canon))
        print("| 消息号 | 宏 | 谁会发（方向表）| 发送点（文件:行，端）| 接收分支（文件:行，端）|")
        print("|---|---|---|---|---|")
        for mt in sorted(rows, key=lambda m: int(num.get(m, "0x0"), 16)):
            row = rows[mt]
            tx = "<br>".join(f"`{r}:{i}`（{s}）" for s, r, i in row["tx"]) or "—"
            rx = "<br>".join(f"`{r}:{i}`（{s}）" for s, r, i in row["rx"]) or "comm 层消费"
            who = {"TCS_DIR_BOTH": "双向", "TCS_DIR_H743_ONLY": "仅 H743", "TCS_DIR_ESP32_ONLY": "仅 ESP32"}[row["dir"]]
            print(f"| {num.get(mt, '?')} | `{mt}` | {who} | {tx} | {rx} |")
    return rows


def gate_body(text: str) -> str:
    a = text.find("static int comm_rx_gate(comm_link_t *L, const uint8_t *payload, size_t plen)")
    if a < 0:
        return ""
    b = text.find("\n}\n", a)
    return text[a:b + 3] if b > 0 else ""


def check_gate(fails):
    """6) 两端 comm.c 的闸门；7) 旧过滤残留。"""
    bodies = {}
    for side, p in COMM.items():
        rel = p.relative_to(REPO).as_posix()
        if not p.exists():
            fails.append(f"缺少 {side} 的 comm.c：{rel}")
            continue
        t = read(p)
        bodies[side] = gate_body(t)
        if not bodies[side]:
            fails.append(f"{rel} 缺少方向闸门函数 comm_rx_gate")
        if t.count(GATE_CALL) != 1:
            fails.append(f"{rel} 的闸门调用点应当恰好 1 处（现为 {t.count(GATE_CALL)}）")
        elif t.count(ACK_LINE) != 1 or t.index(GATE_CALL) > t.index(ACK_LINE):
            fails.append(f"{rel} 的闸门调用点不在 ACK 处理之前（ACK 回声会先被 comm 层消费）")
        if t.count(MARK_LINE) != 1 or t.count(CRC_LINE) != 1:
            fails.append(f"{rel} 出队处必须打方向位并重算 CRC")
    if len(bodies) == 2 and bodies["H743"] != bodies["ESP32"]:
        fails.append("两端 comm.c 的 comm_rx_gate 不再逐字相同（判定逻辑漂移）")
    for p in firmware_sources():
        t = read(p)
        for sym in LEGACY:
            if sym in t:
                fails.append(f"{p.relative_to(REPO).as_posix()} 仍有旧回声过滤的符号 {sym}")


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

    check_direction(fails, table=("--table" in sys.argv))
    check_gate(fails)

    if fails:
        print("proto_sync: FAIL")
        for f in fails:
            print("  -", f)
        return 1
    print("proto_sync: OK (单一源 + 2 包装一致，无重复定义；方向表与两端收发分支一致；两端闸门相同)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
