#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""proto_ref.py — TCS 链路协议 Python 参考实现（任务书 §4.2 / §5.1 跨端字节级对照）

帧：AA 55 | Len | Payload | CRC8(poly 0x07)；Payload = MT | 方向位 | Seq | body；多字节小端。
消息：0x10 STATUS(19B) / 0x11 PARAM / 0x12 SET_PARAM / 0x13 GET_REGISTRY /
      0x14 REGISTRY_ENTRY / 0x15 CMD / 0x20 SNAPSHOT(40B) / 0x21 DELTA / 0x22 INFO / 0x90 ACK。

方向位（2026-11-25，R5-01；唯一说明在 protocol/tcs_proto.h 的「方向」一节）：
      Payload[0] 的 bit6 (0x40)：1 = 发送方是 ESP32（主控），0 = 发送方是 H743（屏）。
      frame(..., side="h743") 默认打屏方向（bit6=0，与旧线格式逐字节相同）；side="esp32" 打主控方向。
      CRC 覆盖 Len + Payload，按带方向位的字节算。decode 输出里注明发送方。
      方向表：0x11 只 ESP32→H743；0x12 只 H743→ESP32（屏写参数，等 ACK）；0x15 / 0x90 双向。

用法：
  python tools/proto_ref.py selftest                     # 独立向量自检
  python tools/proto_ref.py encode status --seq 1        # 打印 0x10 整帧 hex
  python tools/proto_ref.py encode set 1 30.5            # 0x12 帧
  python tools/proto_ref.py decode --hex AA55....        # 解码整帧
  python tools/proto_ref.py live COM8 230400             # WP7 回环：解码 H743 实时帧
  python tools/proto_ref.py live COM8 230400 --set 1 30.5  # 注入 0x12 并等 0x90 ACK
"""
import math
import struct
import sys


def crc8(data: bytes) -> int:
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


MT_STATUS, MT_PARAM, MT_SET_PARAM, MT_GET_REG, MT_REG_ENTRY, MT_CMD, MT_ACK = \
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x90
MT_SNAPSHOT, MT_DELTA, MT_INFO = 0x20, 0x21, 0x22

DIR_ESP32 = 0x40          # 消息号字节的 bit6：1 = 发送方是 ESP32（主控）
DIR_MASK = 0xBF


def frame(mt: int, seq: int, body: bytes, side: str = "h743") -> bytes:
    """组整帧。side = "h743"（屏方向，bit6=0）或 "esp32"（主控方向，bit6=1）。"""
    mt_wire = (mt & DIR_MASK) | (DIR_ESP32 if side == "esp32" else 0)
    payload = bytes([mt_wire, seq]) + body
    return b"\xAA\x55" + bytes([len(payload)]) + payload + bytes([crc8(bytes([len(payload)]) + payload)])


def sender_of(mt_wire: int) -> str:
    return "ESP32>H743" if mt_wire & DIR_ESP32 else "H743>ESP32"


def parse_frame_stream() -> "callable":
    """返回 feed(b)->(mt, seq, body, None) 生成器工厂"""
    state = {"buf": bytearray()}

    def feed(b: int):
        state["buf"].append(b)
        while True:
            buf = state["buf"]
            if len(buf) < 3:
                return None
            if buf[0] != 0xAA or buf[1] != 0x55:
                buf.pop(0)
                continue
            ln = buf[2]
            if len(buf) < 3 + ln + 1:
                return None
            body = bytes(buf[3:3 + ln])
            c = buf[3 + ln]
            del buf[:3 + ln + 1]
            if crc8(bytes([ln]) + body) == c:
                return (body[0], body[1], body[2:] if ln > 2 else b"")
            # CRC 错丢弃继续同步
    return feed


# ====== 载荷 ======
def enc_status(ver=1, seq=0, tc1=25.0, tc2=25.0, body=36.5, tec1=0, tec2=0,
               fan=0, mode=0, alarm=0, online=0, rfu=0) -> bytes:
    b = struct.pack("<BIhhh BBBB HBB", ver, seq, int(round(tc1 * 10)),
                    int(round(tc2 * 10)), int(round(body * 10)),
                    tec1, tec2, fan, mode, alarm, online, rfu)
    assert len(b) == 19, len(b)
    return b


def dec_status(b: bytes) -> dict:
    ver, seq, tc1, tc2, body, tec1, tec2, fan, mode, alarm, online, rfu = \
        struct.unpack("<BIhhh BBBB HBB", b[:19])
    return dict(ver=ver, seq=seq, tc1=tc1 / 10, tc2=tc2 / 10, body=body / 10,
                tec1=tec1, tec2=tec2, fan=fan, mode=mode, alarm=alarm,
                online=online, rfu=rfu)


def enc_param(pid: int, typ: int, value) -> bytes:
    if typ == 0:
        v = struct.unpack("<I", struct.pack("<f", float(value)))[0]
    else:
        v = int(value)
    return struct.pack("<HBI", pid, typ, v)


def dec_param(b: bytes):
    pid, typ, v = struct.unpack("<HBI", b[:7])
    if typ == 0:
        v = struct.unpack("<f", struct.pack("<I", v))[0]
    return pid, typ, v


def dec_reg_entry(b: bytes) -> dict:
    count, idx, pid, typ, flags = struct.unpack_from("<BBHBB", b, 0)
    mn, mx, st = struct.unpack_from("<fff", b, 7)
    o = 19
    kl = b[o]; o += 1
    key = b[o:o + kl].decode("utf-8"); o += kl
    nl = b[o]; o += 1
    name = b[o:o + nl].decode("utf-8"); o += nl
    ul = b[o]; o += 1
    unit = b[o:o + ul].decode("utf-8")
    return dict(count=count, idx=idx, id=pid, type=typ, flags=flags,
                min=mn, max=mx, step=st, key=key, name=name, unit=unit)


# ====== R2：0x20 SNAPSHOT（40B）/0x21 DELTA（变长位图）======
D_TC1_SET, D_TC2_SET, D_TEC, D_FAN, D_MODE, D_ALARM, D_BODY, D_BADGE =     1 << 0, 1 << 1, 1 << 2, 1 << 3, 1 << 4, 1 << 5, 1 << 6, 1 << 7


def enc_snapshot(ver=1, seq=0, ms=0, tc1_act=0.0, tc2_act=0.0, body=0.0,
                 tc1_set=0.0, tc2_set=0.0, tec1=0, tec2=0, fan=0, mode=0,
                 alarm=0, online=0, fresh_ms=0xFFFF, badge=0, rfu=0) -> bytes:
    b = struct.pack("<BIIfffff BBBBHBHBB", ver, seq, ms,
                    tc1_act, tc2_act, body, tc1_set, tc2_set,
                    tec1, tec2, fan, mode, alarm, online, fresh_ms, badge, rfu)
    assert len(b) == 40, len(b)
    return b


def dec_snapshot(b: bytes) -> dict:
    (ver, seq, ms, tc1_act, tc2_act, body, tc1_set, tc2_set,
     tec1, tec2, fan, mode, alarm, online, fresh_ms, badge, rfu) =         struct.unpack("<BIIfffff BBBBHBHBB", b[:40])
    return dict(ver=ver, seq=seq, ms=ms, tc1_act=tc1_act, tc2_act=tc2_act,
                body=body, tc1_set=tc1_set, tc2_set=tc2_set, tec1=tec1,
                tec2=tec2, fan=fan, mode=mode, alarm=alarm, online=online,
                fresh_ms=fresh_ms, badge=badge, rfu=rfu)


def enc_delta(mask: int, d: dict) -> bytes:
    b = struct.pack("<H", mask)
    if mask & D_TC1_SET: b += struct.pack("<f", d["tc1_set"])
    if mask & D_TC2_SET: b += struct.pack("<f", d["tc2_set"])
    if mask & D_TEC:     b += struct.pack("<B", d["tec"])
    if mask & D_FAN:     b += struct.pack("<B", d["fan"])
    if mask & D_MODE:    b += struct.pack("<B", d["mode"])
    if mask & D_ALARM:   b += struct.pack("<H", d["alarm"])
    if mask & D_BODY:    b += struct.pack("<f", d["body"])
    if mask & D_BADGE:   b += struct.pack("<B", d["badge"])
    return b


def dec_delta(b: bytes):
    (mask,) = struct.unpack_from("<H", b, 0)
    o = 2
    d = {}
    if mask & D_TC1_SET: d["tc1_set"] = struct.unpack_from("<f", b, o)[0]; o += 4
    if mask & D_TC2_SET: d["tc2_set"] = struct.unpack_from("<f", b, o)[0]; o += 4
    if mask & D_TEC:     d["tec"] = b[o]; o += 1
    if mask & D_FAN:     d["fan"] = b[o]; o += 1
    if mask & D_MODE:    d["mode"] = b[o]; o += 1
    if mask & D_ALARM:   d["alarm"] = struct.unpack_from("<H", b, o)[0]; o += 2
    if mask & D_BODY:    d["body"] = struct.unpack_from("<f", b, o)[0]; o += 4
    if mask & D_BADGE:   d["badge"] = b[o]; o += 1
    return mask, d


def describe(mt_wire, seq, body) -> str:
    # 方向位只用来注明发送方；消息号按去掉方向位解释（与两端 comm 层交上去的一致）
    mt = mt_wire & DIR_MASK
    if mt == MT_STATUS:
        s = dec_status(body)
        return (f"STATUS seq={s['seq']} tc1={s['tc1']} tc2={s['tc2']} body={s['body']} "
                f"tec={s['tec1']}/{s['tec2']} fan={s['fan']} mode={s['mode']} "
                f"alarm=0x{s['alarm']:04x} online=0x{s['online']:02x}")
    if mt == MT_PARAM:
        pid, typ, v = dec_param(body)
        return f"PARAM id=0x{pid:02x} type={typ} value={v}"
    if mt == MT_SET_PARAM:
        pid, typ, v = dec_param(body)
        return f"SET_PARAM id=0x{pid:02x} type={typ} value={v}"
    if mt == MT_GET_REG:
        return "GET_REGISTRY"
    if mt == MT_REG_ENTRY:
        e = dec_reg_entry(body)
        return (f"REG[{e['idx']+1}/{e['count']}] id=0x{e['id']:02x} key={e['key']} "
                f"name={e['name']} [{e['min']},{e['max']}/{e['step']}] flags=0x{e['flags']:02x}")
    if mt == MT_CMD:
        return f"CMD c={body[0]}"
    if mt == 0x20:
        s = dec_snapshot(body)
        return (f"SNAP seq={s['seq']} ms={s['ms']} tc1={s['tc1_act']} tc2={s['tc2_act']} "
                f"body={s['body']} set={s['tc1_set']}/{s['tc2_set']} "
                f"tec={s['tec1']}/{s['tec2']} fan={s['fan']} mode={s['mode']} "
                f"alarm=0x{s['alarm']:04x} online=0x{s['online']:02x} "
                f"fresh={s['fresh_ms']}ms badge={s['badge']}")
    if mt == 0x21:
        mask, d = dec_delta(body)
        return f"DELTA mask=0x{mask:04x} {d}"
    if mt == MT_INFO:
        return f"INFO {body.decode('utf-8', 'replace')}"
    if mt == MT_ACK:
        acked_seq, acked_mt, status = body[0], body[1], body[2]
        ret = struct.unpack("<f", body[3:7])[0]
        return (f"ACK acked_seq={acked_seq} mt=0x{acked_mt & DIR_MASK:02x} "
                f"status={status} ret={ret}")
    return f"MT=0x{mt:02x} len={len(body)} {body.hex()}"


# ====== CLI ======
def cmd_selftest():
    fails = 0
    if crc8(b"123456789") != 0xF4:
        fails += 1
        print("FAIL crc vector")
    s = enc_status(seq=0x01020304, tc1=28.5, tc2=30.0, body=36.8, tec1=1, fan=1,
                   mode=1, alarm=0x0004, online=0x03, rfu=0xAA)
    expect = bytes.fromhex("0104030201" + "1d01" + "2c01" + "7001" + "01000101" + "0400" + "03aa")
    if s != expect:
        fails += 1
        print(f"FAIL status hex: {s.hex()}")
    d = dec_status(s)
    if not (d["tc1"] == 28.5 and d["body"] == 36.8 and d["alarm"] == 4):
        fails += 1
        print("FAIL status roundtrip")
    # 与 C 头文件一致性：0x12 帧
    f = frame(MT_SET_PARAM, 7, enc_param(1, 0, 30.5))
    if f[0] != 0xAA or crc8(f[2:3 + f[2]]) != f[-1]:
        fails += 1
        print("FAIL frame crc")
    # 负温度
    neg = enc_status(tc1=-5.5)
    if neg[5:7] != bytes.fromhex("c9ff"):
        fails += 1
        print("FAIL negative i16")
    # 方向位（R5-01）：屏方向与旧线格式逐字节相同；主控方向 bit6=1，CRC 按带方向位的字节算；
    # 两端 comm.c 出队时打的正是这一位——对照向量与 esp32/tests/test_link_dir.c 的 [I] 阶段一致。
    if frame(MT_SET_PARAM, 7, enc_param(1, 0, 30.5))[3] != 0x12:
        fails += 1
        print("FAIL h743 frame must carry bit6=0")
    fe = frame(MT_SNAPSHOT, 0x15, enc_snapshot(), side="esp32")
    if fe[3] != 0x60 or crc8(fe[2:3 + fe[2]]) != fe[-1]:
        fails += 1
        print("FAIL esp32 frame must carry bit6=1 with matching crc")
    feed = parse_frame_stream()
    got = [feed(b) for b in fe]
    got = [g for g in got if g]
    if not (len(got) == 1 and got[0][0] == 0x60 and got[0][1] == 0x15 and sender_of(got[0][0]) == "ESP32>H743"):
        fails += 1
        print("FAIL esp32 frame decode")
    # 被确认消息号按去掉方向位解释
    if "mt=0x12" not in describe(0x90, 1, bytes([7, 0x12, 0]) + struct.pack("<f", 30.5)):
        fails += 1
        print("FAIL acked mt strip")
    print("proto_ref selftest:", "OK" if fails == 0 else f"{fails} FAILS")
    return fails


def cmd_live(port, baud, set_args):
    import serial
    ser = serial.Serial(port, baud, timeout=0.2)
    print(f"[ref] live on {port}@{baud}, Ctrl+C 退出")
    feed = parse_frame_stream()
    if set_args:
        pid, val = set_args
        f = frame(MT_SET_PARAM, 1, enc_param(pid, 0, val))
        ser.write(f)
        print(f"[ref] sent SET_PARAM id=0x{pid:02x} v={val}")
    try:
        while True:
            data = ser.read(512)
            for byte in data:
                r = feed(byte)
                if r:
                    print("[ref]", sender_of(r[0]), describe(*r))
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    cmd = sys.argv[1]
    if cmd == "selftest":
        return cmd_selftest()
    if cmd == "encode":
        sub = sys.argv[2]
        if sub == "status":
            print(frame(MT_STATUS, int(sys.argv[sys.argv.index("--seq") + 1]) if "--seq" in sys.argv else 1,
                        enc_status()).hex().upper())
        elif sub == "set":
            print(frame(MT_SET_PARAM, 1, enc_param(int(sys.argv[3], 0), 0, float(sys.argv[4]))).hex().upper())
        return 0
    if cmd == "decode":
        raw = bytes.fromhex(sys.argv[sys.argv.index("--hex") + 1])
        feed = parse_frame_stream()
        for byte in raw:
            r = feed(byte)
            if r:
                print(sender_of(r[0]), describe(*r))
        return 0
    if cmd == "live":
        set_args = None
        if "--set" in sys.argv:
            i = sys.argv.index("--set")
            set_args = (int(sys.argv[i + 1], 0), float(sys.argv[i + 2]))
        cmd_live(sys.argv[2], int(sys.argv[3]), set_args)
        return 0
    print(__doc__)
    return 1


if __name__ == "__main__":
    sys.exit(main())
