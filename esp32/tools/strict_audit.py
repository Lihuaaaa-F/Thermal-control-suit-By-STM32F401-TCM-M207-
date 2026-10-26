#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""严格审计：查'清单里写了但没验过'的路径 + 边界/错误处理 + 可复现性。"""
import serial, time, re

s = serial.Serial('COM3', 115200, timeout=0.3)
time.sleep(0.3); s.reset_input_buffer()
res = []
def send(cmd, wait=1.0):
    s.write((cmd+'\r\n').encode()); time.sleep(wait)
    return s.read(6000).decode('utf-8','replace')
def chk(cond, name, extra=''):
    res.append((name, bool(cond)))
    print('[CHK] %-46s %s %s' % (name, 'PASS' if cond else 'FAIL', extra))

# 1) quiet 路径：commissioned=2（清单里写了但此前从未测过）
o = send('p set commissioned 2', 1.2)
chk('= 2.000' in o and 'rc=0' in o, 'Q1 commissioned=2 写入')
o = send('status', 0.8)
chk('armed=0' in o, 'Q1b armed=0（强制不布防）', o.split('\n')[-2][:90] if o else '')
time.sleep(2.0)   # 若在 SAFE 应退出；若未布防应不因传感器缺席进 SAFE
o = send('status', 0.8)
chk('state=0' in o, 'Q1c 未布防后不驻 SAFE（state=0）')
chk('alarm=0x0000' in o or 'alarm=0x0001' in o, 'Q1d 报警位状态记录', o.split('\n')[-2][:90] if o else '')
o = send('p set commissioned 0', 1.0)
chk('rc=0' in o, 'Q1e 恢复 commissioned=0')

# 2) 参数边界与错误处理
o = send('p set tc1_set 100', 1.0)
chk('= 45.000' in o and 'rc=1' in o, 'E1 超上限钳位 100->45 rc=1')
o = send('p set tc1_set abc', 1.0)
chk('= 0.000' in o or 'rc=' in o, 'E2 非数字输入被处理', o.split('\n')[-2][:80] if o else '')
o = send('p set 0x7FFF 1', 1.0)
chk('rc=-1' in o, 'E3 未知 id -> rc=-1')
o = send('p set nosuchkey 1', 1.0)
chk('bad id/key' in o, 'E4 未知 key -> bad id/key')
o = send('p set body_cold 39', 1.0)   # body_cold 39 >= body_hot 37.5 → 约束拒绝 -2
chk('rc=-2' in o, 'E5 body_cold>=body_hot 约束拒绝 rc=-2')
o = send('p set body_cold 36.5', 1.0)
chk('rc=0' in o, 'E5b 合法值恢复')

# 3) m207 raw 错误输入
o = send('m207 raw', 0.8)
chk('usage' in o, 'E6 m207 无参 -> usage')
o = send('m207 raw TC1:NOTEXIST?', 1.0)
chk('CMD:REPLY=2' in o, 'E7 未知参数 -> =2（链路活）')

# 4) 模块 TC1 设定值现状（C 组测试遗留 15°C；记录+恢复 30）
o = send('m207 raw TC1:TCADJTEMP?', 1.0)
m = re.search(r'TCADJTEMP ch=1 = ([\d.]+)', o) or re.search(r'TCADJTEMP=([\d.]+)', o)
print('[INFO] 模块当前 TC1 TCADJTEMP =', m.group(1) if m else '?')
o = send('m207 raw TC1:TCADJTEMP=30', 1.2)
o = send('m207 raw TC1:TCADJTEMP?', 1.0)
chk('30.00' in o, 'E8 模块 TC1 设定值恢复 30.0')

# 5) 静息 5s 后再测 status（看 SAFE 重驱/自检干扰）
time.sleep(5)
o = send('status', 0.8)
print('[INFO] 静息后 status:', [l for l in o.splitlines() if 'tc1=' in l][:1])

s.close()
fail = [n for n, ok in res if not ok]
print('#### AUDIT: %d/%d PASS' % (len(res)-len(fail), len(res)))
for n in fail: print('#### FAIL:', n)
