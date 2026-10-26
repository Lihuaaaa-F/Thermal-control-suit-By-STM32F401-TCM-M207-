# -*- coding: utf-8 -*-
"""m207_pc_probe.py — PC 直连 M207 故障码/状态读取（R1b 诊断，任务书 v11.3）

用法: python tools/m207_pc_probe.py [COM口] [波特率]
默认 COM6 57600。正典《现行M207协议_v1.md》：帧尾 \\r、大小写敏感、命令间隔≥50ms
（脚本取 120ms 余量）。应答两种：`MOD:PARAM=值` 或 `CMD:REPLY=N`。
TCERRORSTATUS/LOG 位语义（EasyHost tc_hlp.xml 官方帮助，反相：正常=1 故障=0）：
bit0 外部 / bit1 过压 / bit2 过流 / bit3 过温 / bit4 传感器开路 / bit5 负载短路 / bit6 负载开路。
"""
import sys
import time

import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else 'COM6'
BAUD = int(sys.argv[2]) if len(sys.argv) > 2 else 57600

QUERIES = [
    ('TC1:TCACTTEMP?',     '链路握手+实际温度'),
    ('TC1:TCSW?',          '输出开关状态(F9 应=0)'),
    ('TC1:TCERRORSTATUS?', 'TC1 当前故障码'),
    ('TC2:TCERRORSTATUS?', 'TC2 当前故障码'),
    ('TC1:TCERRORLOG?',    'TC1 上电以来故障史'),
    ('TC2:TCERRORLOG?',    'TC2 上电以来故障史'),
    ('TC1:TCOTPSTATUS?',   'TC1 过温保护状态'),
    ('TC1:TCOCPSTATUS?',   'TC1 过流保护状态'),
    ('TC1:TCOVPSTATUS?',   'TC1 过压保护状态'),
    ('TC1:TCRTOCPSTATUS?', 'TC1 传感器开路保护状态'),
    ('PCB:TMACTTEMP?',     '模块本体温度'),
    ('PCB:TMOTPSTATUS?',   '模块过温状态'),
    ('PCB:TMDTPSTATUS?',   '模块危温状态'),
    ('SYSTEM:SYSMODEL?',   '型号变体(C3 裁定)'),
]

FAULT_BITS = {
    0: '外部错误', 1: '过压', 2: '过流', 3: '过温',
    4: '温度传感器开路', 5: '负载短路', 6: '负载开路',
}


def decode_errstatus(val):
    """反相语义：位=1 正常，位=0 故障。全正常(0-6 位全 1)=127。"""
    faults = [name for bit, name in FAULT_BITS.items() if not (val >> bit) & 1]
    return faults or None


def main():
    ser = serial.Serial(PORT, BAUD, bytesize=8, parity='N', stopbits=1,
                        timeout=0.8, write_timeout=1.0)
    print(f'== M207 PC 直读 {PORT}@{BAUD} ==')
    time.sleep(0.3)
    ser.reset_input_buffer()

    replies = {}
    for cmd, note in QUERIES:
        ser.write((cmd + '\r').encode('ascii'))
        t0 = time.time()
        line = ser.read_until(b'\r', size=128).decode('ascii', 'replace').strip()
        ms = (time.time() - t0) * 1000
        print(f'{cmd:<24} ({note}) -> {line!r:<44} [{ms:.0f}ms]')
        replies[cmd] = line
        time.sleep(0.12)

    ser.close()

    print('\n== 故障码解读 ==')
    any_fault = False
    for cmd, label in [('TC1:TCERRORSTATUS?', 'TC1 当前'), ('TC2:TCERRORSTATUS?', 'TC2 当前'),
                       ('TC1:TCERRORLOG?', 'TC1 锁存'), ('TC2:TCERRORLOG?', 'TC2 锁存')]:
        raw = replies.get(cmd, '')
        if '=' not in raw:
            print(f'{label}: 无有效应答')
            continue
        try:
            val = int(raw.split('=', 1)[1].strip(), 0)
        except ValueError:
            print(f'{label}: 非数值应答 {raw!r}')
            continue
        faults = decode_errstatus(val)
        if faults is None:
            print(f'{label}: 值={val} → 全部正常（127 语义校验{"通过" if val == 127 else "未过:非127但无故障位"}）')
        else:
            any_fault = True
            print(f'{label}: 值={val} → **故障: {"、".join(faults)}**')
    if not any_fault:
        print('（两个通道当前与锁存均无故障位）')


if __name__ == '__main__':
    main()
