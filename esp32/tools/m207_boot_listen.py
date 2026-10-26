# -*- coding: utf-8 -*-
"""m207_boot_listen.py — M207 断电重启全程监听（R1b 诊断；115200/57600 双波特轮询窗口）

用法: 先运行本脚本（开始监听），再给 M207 断电→等 3s→上电。任何波特下出现的
任何字节（开机横幅/探测应答）都会带时间戳打印。Ctrl+C 结束。
"""
import serial
import time

PORT = 'COM6'
BAUDS = [115200, 57600]   # 交替监听窗口，各 2s
WINDOW = 2.0

print(f'== 监听 {PORT}，波特交替 {BAUDS}，等待 M207 断电重启（Ctrl+C 结束）==')
t_start = time.time()
ser = {b: serial.Serial(PORT, b, timeout=0.1) for b in BAUDS}
active = 0
try:
    while time.time() - t_start < 120:
        b = BAUDS[active]
        active = (active + 1) % len(BAUDS)
        buf = ser[b].read(512)
        if buf:
            ts = time.time() - t_start
            print(f'[{ts:7.2f}s @{b}] {len(buf)}B {buf[:96]!r}')
except KeyboardInterrupt:
    pass
finally:
    for s in ser.values():
        s.close()
print('== 监听结束 ==')
