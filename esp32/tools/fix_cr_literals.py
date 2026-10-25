# fix_cr_literals.py — 把 m207_service.c 字符串字面量内的原始 CR/LF 字节修复为两字符转义 \r
from pathlib import Path

p = Path(__file__).resolve().parent.parent / 'components/tcs_m207/m207_service.c'
t = p.read_bytes()
bs = bytes([92])          # '\'
r = bytes([114])          # 'r'
fixed = 0
for name in [b'TC1:TCSW?', b'TC2:TCSW?', b'TC1:TCADJTEMP?', b'TC2:TCADJTEMP?',
             b'TC1:TCSW=0', b'TC2:TCSW=0']:
    for bad in (bytes([13]) + b'"', bytes([13, 10]) + b'"', bytes([10]) + b'"'):
        good = name + bs + r + b'"'
        n = t.count(name + bad)
        if n:
            t = t.replace(name + bad, good)
            fixed += n
p.write_bytes(t)
print('fixed literals:', fixed)
# verify: no raw CR/LF inside any enqueue_simple literal remains
import re
txt = t.decode('utf-8')
bad_left = re.findall(r'enqueue_simple\("[^"]*\n[^"]*"\)', txt)
print('remaining broken:', len(bad_left))
