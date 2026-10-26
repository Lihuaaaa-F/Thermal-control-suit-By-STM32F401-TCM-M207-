#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""R1e 用户清单预验收 v2（我做第一个用户）：自动判定 PASS/FAIL。
用法: python r1e_usercheck.py > docs/evidence/R1e_usercheck_transcript.log 2>&1
"""
import serial, time, re, sys

PORT = 'COM3'
results = []

def open_port():
    for _ in range(20):
        try:
            s = serial.Serial(PORT, 115200, timeout=0.3)
            time.sleep(0.3)
            return s
        except Exception:
            time.sleep(0.5)
    raise SystemExit('COM3 open failed')

def wait_console(s, timeout=15):
    """发空行直到 'tcs' 提示符出现（reset 后 console 就绪探测）"""
    t0 = time.time()
    while time.time() - t0 < timeout:
        s.write(b'\r\n')
        time.sleep(0.4)
        d = s.read(6000).decode('utf-8', 'replace')
        if re.search(r'\btcs\s*$', d, re.M) or 'tcs ' in d:
            s.reset_input_buffer()
            return True
    return False

def send(s, cmd, wait=1.2):
    s.write((cmd + '\r\n').encode('utf-8'))
    time.sleep(wait)
    out = s.read(10000).decode('utf-8', 'replace')
    print('=' * 66)
    print('>>> %s' % cmd)
    print(out.strip()[:1800])
    return out

def chk(cond, name):
    results.append((name, bool(cond)))
    print('[CHK] %s : %s' % (name, 'PASS' if cond else 'FAIL'))

s = open_port()
print('#### R1e 用户清单预验收 v2', time.strftime('%Y-%m-%d %H:%M:%S'))
if not wait_console(s):
    raise SystemExit('console not ready')

# ---------- A. M207 直查族 ----------
o = send(s, 'm207 raw TC1:TCERRORSTATUS?', 1.0)
chk('TCERRORSTATUS=255' in o, 'A1 TC1 STATUS=255（配置修正后）')
o = send(s, 'm207 raw TC2:TCERRORSTATUS?', 1.0)
chk('TCERRORSTATUS=255' in o, 'A2 TC2 STATUS=255')
o = send(s, 'm207 raw TC1:TCRTOCPHT?', 1.0)
chk('TCRTOCPHT=120' in o, 'A3 TC1 开路保护上限=120 回读')
o = send(s, 'm207 raw FOO:BAR=1', 1.0)
chk('CMD:REPLY=2' in o, 'A4 V-5 未知模块 -> =2（留痕可见）')
o = send(s, 'm207 raw GARBAGE', 1.0)
chk('CMD:REPLY=6' in o, 'A5 语法错 -> =6')
o = send(s, 'm207 raw TC1:TCSW?@0#FF', 1.0)
chk('CMD:REPLY=7' in o, 'A6 坏校验 -> =7@0#..')

# ---------- B. 参数写/自动落盘 ----------
o = send(s, 'p list', 1.0)
chk('tc1_set' in o and 'commissioned' in o, 'B1 p list 含注册表')
o = send(s, 'p set tc1_set 31', 1.0)
chk('rc=0' in o and '= 31.000' in o, 'B2 p set 31 生效')
time.sleep(2.5)                       # 2s 防抖自动落盘
send(s, 'reset', 0.6)
time.sleep(2.0)
chk(wait_console(s), 'B3 reset 后 console 恢复')
o = send(s, 'p list', 1.2)
chk('31.000' in o, 'B4 自动落盘：复位后 tc1_set 仍 31')

# ---------- C. 体温注入 + 自动模式（E-21 手工版） ----------
o = send(s, 'body 37.8', 1.4)
chk('body sim 37.80' in o, 'C1 注入 37.8')
send(s, 'ack', 0.8)
time.sleep(0.8)                        # 等 LOST->ARMED (10 healthy)
o = send(s, 'mode 1', 0.8)
chk('mode=1 rc=0' in o, 'C2 自动模式')
time.sleep(1.2)
o = send(s, 'status', 0.8)
chk('state=1' in o, 'C3 COOL（state=1）')
chk('fan=100' in o, 'C3 fan=100%')
o = send(s, 'm207 raw TC1:TCADJTEMP?', 1.0)
chk('TCADJTEMP ch=1 = 15.00' in o or 'TCADJTEMP=15' in o, 'C4 FSM 真下发 TC1=15.0 到模块')
o = send(s, 'body 37.0', 1.4)
time.sleep(1.0)
o = send(s, 'status', 0.8)
chk('state=0' in o, 'C5 回带内 -> IDLE')

# ---------- D. 急停/ack/风扇 ----------
o = send(s, 'estop', 1.0)
o = send(s, 'status', 0.8)
chk('state=3' in o and 'fan=100' in o and 'mode=0' in o, 'D1 急停 -> SAFE+fan100+手动')
o = send(s, 'fan 50', 1.0)
chk('拒绝降档' in o, 'D2 SAFE 期风扇锁全开（降档被策略门拒绝）')
o = send(s, 'status', 0.8)
chk('fan=100' in o, 'D2b 风扇仍 100%')
o = send(s, 'ack', 1.4)
o = send(s, 'status', 0.8)
chk('state=0' in o and 'alarm=0x0000' in o, 'D3 ack 清 ESTOP 回 IDLE')
o = send(s, 'fan 50', 1.0)
o = send(s, 'status', 0.8)
chk('fan=50' in o, 'D4 IDLE 期 duty 50 可调')
o = send(s, 'fan 0', 0.8)
o = send(s, 'status', 0.8)
chk('fan=0' in o, 'D5 IDLE 期 fan 0 可关')

# ---------- E. 失联 -> SAFE -> ack 才回（E-22 手工版） ----------
o = send(s, 'body off', 2.4)
o = send(s, 'status', 0.8)
chk('state=3' in o and 'alarm=0x0001' in o, 'E1 失联 -> SAFE+ALM_BODY_SENSOR')
o = send(s, 'ack', 0.8)
o = send(s, 'status', 0.8)
chk('state=3' in o and 'alarm=0x0001' in o, 'E2 源未恢复 ack 不清')

# ---------- F. COMPUSHLIST（30s 长流，放最后） ----------
o = send(s, 'm207 raw PCRS232:COMPUSHLIST=1', 3.0)
n = len(re.findall(r'\.REG=', o))
chk(n >= 5, 'F1 COMPUSHLIST 寄存器流（首 3s 捕获 %d 行）' % n)

# ---------- G. perf ----------
o = send(s, 'perf', 1.5)
chk('CPU%' in o and 'IDLE0' in o, 'G1 运行时统计表')

s.close()
print('=' * 66)
fail = [n for n, ok in results if not ok]
print('#### SUMMARY: %d/%d PASS' % (len(results) - len(fail), len(results)))
for n in fail:
    print('#### FAIL:', n)
