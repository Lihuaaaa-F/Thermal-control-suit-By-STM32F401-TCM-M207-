#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""proto_ref.py — TCS 链路协议 Python 参考实现（任务书 §4.2 / §5.1 跨端字节级对照）

帧：AA 55 | Len | Payload | CRC8(poly 0x07)；Payload = MT + Seq + body；多字节小端。
消息：0x10 STATUS(19B) / 0x11 PARAM / 0x12 SET_PARAM / 0x13 GET_REGISTRY /
      0x14 REGISTRY_ENTRY / 0x15 CMD / 0x90 ACK。

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


def frame(mt: int, seq: int, body: bytes) -> bytes:
    payload = bytes([mt, seq]) + body
    return b"\xAA\x55" + bytes([len(payload)]) + payload + bytes([crc8(bytes([len(payload)]) + payload)])


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


def describe(mt, seq, body) -> str:
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
    if mt == MT_ACK:
        acked_seq, acked_mt, status = body[0], body[1], body[2]
        ret = struct.unpack("<f", body[3:7])[0]
        return f"ACK acked_seq={acked_seq} mt=0x{acked_mt:02x} status={status} ret={ret}"
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
                    print("[ref]", describe(*r))
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
                print(describe(*r))
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
