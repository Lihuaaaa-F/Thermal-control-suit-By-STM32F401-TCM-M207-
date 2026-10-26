#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""LED 排查：对候选空闲脚批量发 WS2812 全黑帧（除 48 已清），再让 48 闪烁供人眼对照。
安全脚表：避开 19/20(USB) 26-37(Flash/PSRAM) 8/9(I2C) 17/18/21/47(通信) 0(strapping)。"""
import serial, time

S = serial.Serial('COM3', 115200, timeout=0.3)
time.sleep(0.4); S.reset_input_buffer()

def cmd(c, wait=0.5):
    S.write((c + '\r\n').encode()); time.sleep(wait)
    return S.read(4000).decode('utf-8', 'replace')

cands = [1,2,3,4,5,6,7,10,11,12,13,14,15,16,38,39,40,41,42,43,44,45,46]
print('# 批量黑帧到候选脚')
for p in cands:
    cmd('ledblk %d' % p, 0.15)   # 若该控制台命令不存在则跳过
print('# 完成；若灯灭请回报哪步之后灭的')
S.close()
