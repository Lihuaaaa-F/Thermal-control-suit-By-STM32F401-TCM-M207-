#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""g5_walkthrough.py — G5 全按钮走查（probe 命令钩子驱动；每个按钮点开的画面逐帧转储）
用法：TCS_ELF=build-g4lv/tcs_main.elf python tools/g5_walkthrough.py
"""
import os, subprocess, re, time

ELF = os.environ.get("TCS_ELF", "build-g4lv/tcs_main.elf")
nm = subprocess.check_output(["arm-none-eabi-nm", ELF], text=True)
CMD = re.search(r'^([0-9a-fA-F]{8}) \w+ g_ui_cmd$', nm, re.M).group(1)

def wcmd(cmd):
    subprocess.check_output(["probe-rs", "write", "--chip", "STM32H743II",
                             "--probe", "0d28:0204", "b8", "0x" + CMD, str(cmd)], text=True)

def dump(name):
    subprocess.check_output(["python", "tools/fb_dump.py", f"out/walk_{name}.png"],
                            env={**os.environ, "TCS_ELF": ELF})

STEPS = [
    (1, "dlg_estop"), (13, "home"), (2, "dlg_mode"), (13, "home"),
    (3, "p1"), (9, "p1_plus"), (10, "p1_minus"), (11, "p1_step"), (12, "p1_sel"),
    (4, "p2"), (5, "p3"), (13, "home"),
    (6, "fan_toggle"), (7, "mode_manual"),
]
for cmd, name in STEPS:
    wcmd(cmd)
    time.sleep(1.2)
    dump(name)
    print(f"[walk] cmd={cmd} -> {name}")
print("walkthrough complete")
