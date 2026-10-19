#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""m207_sim.py — M207 温控模块串口模拟器 v2（任务书 v10.11 WP-P4）

v2 升级（v1 见 git 历史；§11.2 原行为保留）：
  - 完整参数字典（EasyHost 官方 OEM V6.2.0.1 全 209 参，docs/evidence/easyhost_decompile/）
  - ERRORCODE 全表应答（0-8，见 docs/现行M207协议_v1.md §3）
  - `!`=EEPROM 保存/掉电行为模拟（--state 文件持久化；未保存参数重启回旧值）
  - 50ms 命令间隔铁律模拟（违反间隔的命令可配置丢弃，验证我方队列节流）
  - 地址 @X + XOR 校验 #YY 全语法（publiccmd.c 同算法，异或含 #）
  - `^` CHANGING 拒收（正典 §6 禁用）
  - 坏帧防御：缺 \r 不成行/无冒号/坏校验 → ERROR 不崩

用法：
  python tools/m207_sim.py COM3 [57600] [--addr 0] [--state FILE] [--no-50ms-drop]
纯逻辑（tests/test_m207_adv.py 直接 import）：class M207Core
"""
import argparse
import os
import sys
import time
# 由 m207_dict_OEM_V6.2.0.1.csv 生成(2026-10-20):name -> (type, writable, savable)
DICT = {
    "TC1:TCADJTEMP": ("NUM", True, True),
    "TC1:TCACTTEMP": ("NUM", False, False),
    "TC1:TCSETTEMP": ("NUM", True, False),
    "TC1:TCRAMPSPEED": ("NUM", True, True),
    "TC1:TCMAXTEMP": ("NUM", True, True),
    "TC1:TCMINTEMP": ("NUM", True, True),
    "TC1:TCSTEPTEMP": ("NUM", True, True),
    "TC1:TCDECIMALDIGITS": ("NUM", True, True),
    "TC1:TCMAXV": ("NUM", True, True),
    "TC1:TCCOOLVRATIO": ("NUM", True, True),
    "TC1:TCMODE": ("OPT_TCMODE", True, True),
    "TC1:TCSW": ("OPT_SWSTATUS", True, True),
    "TC1:TCOE": ("OPT_ENSTATUS", True, True),
    "TC1:TCSETVOL": ("NUM", True, True),
    "TC1:TCACTVOL": ("NUM", False, False),
    "TC1:TCACTCUR": ("NUM", False, False),
    "TC1:TCLOADRES": ("NUM", True, True),
    "TC1:TCLOADDETECT": ("OPT_TCLOADDETECTMODE", True, True),
    "TC1:TCLOADRESLT": ("NUM", True, True),
    "TC1:TCLOADRESHT": ("NUM", True, True),
    "TC1:TCRREF": ("NUM", True, True),
    "TC1:TCTSTEMP": ("NUM", True, True),
    "TC1:TCTSOFFSET": ("NUM", True, True),
    "TC1:TCTSOUTPUT": ("HSTR", True, True),
    "TC1:TCTSTYPE": ("OPT_TSTYPE[HSTR]", True, True),
    "TC1:TCTSCOEFF:0": ("HSTR", True, True),
    "TC1:TCTSCOEFF:1": ("HSTR", True, True),
    "TC1:TCTSCOEFF:2": ("HSTR", True, True),
    "TC1:TCTSCOEFF:3": ("HSTR", True, True),
    "TC1:TCTSCOEFF:4": ("HSTR", True, True),
    "PAGE:NOTICE": ("STR", True, True),
    "TC1:TCTSRELOAD": ("BTN", False, False),
    "TC1:TCTSRELOAD": ("BTN", False, False),
    "TC1:TCTSSTEP": ("NUM", True, True),
    "TC1:TCAUXTSFUNC": ("OPT_TCAUXTSFUNC", True, True),
    "TC1:TCAUXTSINDEX": ("OPT_OEM_AUXTSINDEX", True, True),
    "TC1:TCAUXTSACTTEMP": ("NUM", False, False),
    "TC1:TCAUXTSSAFETY": ("OPT_TCAUXTSSAFETY", True, True),
    "TC1:TCAUXTSOTPLT": ("NUM", True, True),
    "TC1:TCAUXTSOTPHT": ("NUM", True, True),
    "TC1:TCRTOCPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC1:TCRTOCPLT": ("NUM", True, True),
    "TC1:TCRTOCPHT": ("NUM", True, True),
    "TC1:TCPIDTYPE": ("OPT_PIDTYPE", True, True),
    "TC1:TCPIDP": ("NUM", True, True),
    "TC1:TCPIDTI": ("NUM", True, True),
    "TC1:TCPIDTD": ("NUM", True, True),
    "TC1:TCCTRLINTERVAL": ("NUM", True, True),
    "TC1:TCPIDALGO": ("OPT_TCPIDCAL", True, True),
    "TC1:TCTUNESTATUS": ("NUM", False, False),
    "TC1:TCPIDWINDOW": ("NUM", True, True),
    "TC1:TCPIDACCWINDOW": ("NUM", True, True),
    "TC1:TCOVPADJTHR": ("NUM", True, True),
    "TC1:TCOVPACTTHR": ("NUM", False, False),
    "TC1:TCOVPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC1:TCOCPADJTHR": ("NUM", True, True),
    "TC1:TCOCPACTTHR": ("NUM", False, False),
    "TC1:TCOCPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC1:TCCURLIMIT": ("NUM", True, True),
    "TC1:TCOTPMODE": ("OPT_OTPMODE", True, True),
    "TC1:TCOTPLT": ("NUM", True, True),
    "TC1:TCOTPHT": ("NUM", True, True),
    "TC1:TCOTPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC1:TCOTPLHYS": ("NUM", True, True),
    "TC1:TCOTPHHYS": ("NUM", True, True),
    "TC1:TCEXTSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC1:TCEXTLOCK": ("OPT_LOCK", True, True),
    "TC1:TCEXTLOCKPOL": ("OPT_POLARITY", True, True),
    "TC1:TCEXTCTRL": ("NUM", True, True),
    "TC1:TCAR": ("NUM", True, True),
    "TC1:TCARCNT": ("NUM", False, False),
    "TC1:TCARDELAY": ("NUM", True, True),
    "TC1:TCERRORLOG": ("BINARYCODE", False, False),
    "TC1:TCERRORSTATUS": ("BINARYCODE", False, False),
    "TC1:TCERRORMASK": ("BINARYCODE", True, True),
    "TC2:TCADJTEMP": ("NUM", True, True),
    "TC2:TCACTTEMP": ("NUM", False, False),
    "TC2:TCSETTEMP": ("NUM", True, False),
    "TC2:TCRAMPSPEED": ("NUM", True, True),
    "TC2:TCMAXTEMP": ("NUM", True, True),
    "TC2:TCMINTEMP": ("NUM", True, True),
    "TC2:TCSTEPTEMP": ("NUM", True, True),
    "TC2:TCDECIMALDIGITS": ("NUM", True, True),
    "TC2:TCMAXV": ("NUM", True, True),
    "TC2:TCCOOLVRATIO": ("NUM", True, True),
    "TC2:TCMODE": ("OPT_TCMODE", True, True),
    "TC2:TCSW": ("OPT_SWSTATUS", True, True),
    "TC2:TCOE": ("OPT_ENSTATUS", True, True),
    "TC2:TCSETVOL": ("NUM", True, True),
    "TC2:TCACTVOL": ("NUM", False, False),
    "TC2:TCACTCUR": ("NUM", False, False),
    "TC2:TCLOADRES": ("NUM", True, True),
    "TC2:TCLOADDETECT": ("OPT_TCLOADDETECTMODE", True, True),
    "TC2:TCLOADRESLT": ("NUM", True, True),
    "TC2:TCLOADRESHT": ("NUM", True, True),
    "TC2:TCRREF": ("NUM", True, True),
    "TC2:TCTSTEMP": ("NUM", True, True),
    "TC2:TCTSOFFSET": ("NUM", True, True),
    "TC2:TCTSOUTPUT": ("HSTR", True, True),
    "TC2:TCTSTYPE": ("OPT_TSTYPE[HSTR]", True, True),
    "TC2:TCTSCOEFF:0": ("HSTR", True, True),
    "TC2:TCTSCOEFF:1": ("HSTR", True, True),
    "TC2:TCTSCOEFF:2": ("HSTR", True, True),
    "TC2:TCTSCOEFF:3": ("HSTR", True, True),
    "TC2:TCTSCOEFF:4": ("HSTR", True, True),
    "PAGE:NOTICE": ("STR", True, True),
    "TC2:TCTSRELOAD": ("BTN", False, False),
    "TC2:TCTSRELOAD": ("BTN", False, False),
    "TC2:TCTSSTEP": ("NUM", True, True),
    "TC2:TCAUXTSFUNC": ("OPT_TCAUXTSFUNC", True, True),
    "TC2:TCAUXTSINDEX": ("OPT_OEM_AUXTSINDEX", True, True),
    "TC2:TCAUXTSACTTEMP": ("NUM", False, False),
    "TC2:TCAUXTSSAFETY": ("OPT_TCAUXTSSAFETY", True, True),
    "TC2:TCAUXTSOTPLT": ("NUM", True, True),
    "TC2:TCAUXTSOTPHT": ("NUM", True, True),
    "TC2:TCRTOCPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC2:TCRTOCPLT": ("NUM", True, True),
    "TC2:TCRTOCPHT": ("NUM", True, True),
    "TC2:TCPIDTYPE": ("OPT_PIDTYPE", True, True),
    "TC2:TCPIDP": ("NUM", True, True),
    "TC2:TCPIDTI": ("NUM", True, True),
    "TC2:TCPIDTD": ("NUM", True, True),
    "TC2:TCCTRLINTERVAL": ("NUM", True, True),
    "TC2:TCPIDALGO": ("OPT_TCPIDCAL", True, True),
    "TC2:TCTUNESTATUS": ("NUM", False, False),
    "TC2:TCPIDWINDOW": ("NUM", True, True),
    "TC2:TCPIDACCWINDOW": ("NUM", True, True),
    "TC2:TCOVPADJTHR": ("NUM", True, True),
    "TC2:TCOVPACTTHR": ("NUM", False, False),
    "TC2:TCOVPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC2:TCOCPADJTHR": ("NUM", True, True),
    "TC2:TCOCPACTTHR": ("NUM", False, False),
    "TC2:TCOCPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC2:TCCURLIMIT": ("NUM", True, True),
    "TC2:TCOTPMODE": ("OPT_OTPMODE", True, True),
    "TC2:TCOTPLT": ("NUM", True, True),
    "TC2:TCOTPHT": ("NUM", True, True),
    "TC2:TCOTPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC2:TCOTPLHYS": ("NUM", True, True),
    "TC2:TCOTPHHYS": ("NUM", True, True),
    "TC2:TCEXTSTATUS": ("OPT_FLAGSTATUS", False, False),
    "TC2:TCEXTLOCK": ("OPT_LOCK", True, True),
    "TC2:TCEXTLOCKPOL": ("OPT_POLARITY", True, True),
    "TC2:TCEXTCTRL": ("NUM", True, True),
    "TC2:TCAR": ("NUM", True, True),
    "TC2:TCARCNT": ("NUM", False, False),
    "TC2:TCARDELAY": ("NUM", True, True),
    "TC2:TCERRORLOG": ("BINARYCODE", False, False),
    "TC2:TCERRORSTATUS": ("BINARYCODE", False, False),
    "TC2:TCERRORMASK": ("BINARYCODE", True, True),
    "NTS1:NTSACTTEMP": ("NUM", False, False),
    "NTS1:NTSSAFETY": ("OPT_NTSSAFETY", True, True),
    "NTS1:NTSSTATUS": ("OPT_TIMEOUTSTATUS", False, False),
    "NTS1:NTSTIMEOUT": ("NUM", True, True),
    "NTS1:NTSDEFTEMP": ("NUM", True, True),
    "NTS2:NTSACTTEMP": ("NUM", False, False),
    "NTS2:NTSSAFETY": ("OPT_NTSSAFETY", True, True),
    "NTS2:NTSSTATUS": ("OPT_TIMEOUTSTATUS", False, False),
    "NTS2:NTSTIMEOUT": ("NUM", True, True),
    "NTS2:NTSDEFTEMP": ("NUM", True, True),
    "SYSTEM:SYSSN": ("STR", True, True),
    "SYSTEM:SYSPVIN": ("NUM", False, False),
    "SYSTEM:SYSTIME": ("NUM", False, False),
    "SYSTEM:SYSMODEL": ("STR", True, True),
    "SYSTEM:SYSVERSION": ("STR", True, True),
    "SYSTEM:SYSCOMPANY": ("STR", True, True),
    "SYSTEM:SYSWEBSITE": ("STR", True, True),
    "SYSTEM:SYSPASSWORD": ("STR", True, True),
    "SYSTEM:SYSMODE": ("OPT_SYSMODE", True, True),
    "SYSTEM:SYSALIAS": ("STR", True, True),
    "SYS:RESET": ("BTN", False, False),
    "PCRS232:COMADDR": ("NUM", True, True),
    "PCRS232:COMBAUDRATE": ("NUM", True, True),
    "PCRS232:COMMODBUS": ("NUM", True, True),
    "UIRS232:COMADDR": ("NUM", True, True),
    "UIRS232:COMMODBUS": ("NUM", True, True),
    "MEMORY:MEMWP": ("OPT_MEMWPSTATUS", True, True),
    "PARAM:BATCH": ("OPT_SWSTATUS", True, False),
    "PARAM:BATCHDEFAULT": ("BTN", False, False),
    "PARAM:BATCHSAVE": ("BTN", False, False),
    "PCB:TMACTTEMP": ("NUM", False, False),
    "PCB:TMOTPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "PCB:TMOTPLT": ("NUM", True, True),
    "PCB:TMOTPHT": ("NUM", True, True),
    "PCB:TMOTPHHYS": ("NUM", True, True),
    "PCB:TMDTPSTATUS": ("OPT_FLAGSTATUS", False, False),
    "PCB:TMDTPLT": ("NUM", True, True),
    "PCB:TMDTPHT": ("NUM", True, True),
    "PCB:TMDTPHHYS": ("NUM", True, True),
    "MUI:LEDERRMODE": ("OPT_LEDBLINKMODE", True, True),
    "MUI:LEDSWMODE": ("OPT_LEDBLINKMODE", True, True),
    "BOARDCONFIG:TCINPARALLEL": ("OPT_OEM_TCINPARALLEL", True, True),
    "BOARDCONFIG:TC1KEYMODE": ("OPT_OEM_TCKEYMODE", True, True),
    "BOARDCONFIG:TC2KEYMODE": ("OPT_OEM_TCKEYMODE", True, True),
    "PAGE:NOTICE": ("STR", True, True),
    "SYS:RESET": ("BTN", False, False),
    "0": ("OPT_OEM_TCINPARALLEL", True, True),
    "1": ("OPT_OEM_TCINPARALLEL", True, True),
    "0": ("OPT_OEM_TCKEYMODE", True, True),
    "1": ("OPT_OEM_TCKEYMODE", True, True),
    "2": ("OPT_OEM_TCKEYMODE", True, True),
    "0": ("OPT_OEM_AUXTSINDEX", True, True),
    "1": ("OPT_OEM_AUXTSINDEX", True, True),
    "2": ("OPT_OEM_AUXTSINDEX", True, True),
    "3": ("OPT_OEM_AUXTSINDEX", True, True),
    "4": ("OPT_OEM_AUXTSINDEX", True, True),
    "5": ("OPT_OEM_AUXTSINDEX", True, True),
    "6": ("OPT_OEM_AUXTSINDEX", True, True),
    "7": ("OPT_OEM_AUXTSINDEX", True, True),
}

# ============================ 值域 ============================
# 协议语义=模块自限定（TCADJTEMP 由 TCMINTEMP/TCMAXTEMP 动态定义，正典 §8.2）。
# 显式值域仅列有物理意义的键，其余接受任意 float（C4：待真机 COMPUSHLIST 核定）。
BOUNDS = {
    "TCMAXV": (0.0, 12.0),          # 最大输出电压 V（12V 供电上限）
    "TCOCPADJTHR": (0.0, 10.0),     # 过流阈值 A
    "TCOVPADJTHR": (0.0, 24.0),     # 过压阈值 V
    "TCRAMPSPEED": (0.01, 99.0),    # 限速 ℃/s
    "TCSTEPTEMP": (0.001, 10.0),    # 步进 ℃
    "TCMINTEMP": (-50.0, 150.0),
    "TCMAXTEMP": (-50.0, 200.0),
    "TCPIDP": (0.0, 100.0),
    "TCPIDTI": (0.0, 9999.0),
    "TCPIDTD": (0.0, 9999.0),
    "TCAR": (0, 99),
    "TCARDELAY": (0, 999),
    "COMADDR": (0, 254),
    "TCCTRLINTERVAL": (0, 60000),
}

DEFAULTS = {
    "TCSW": "0", "TCADJTEMP": "25.0", "TCSETTEMP": "25.0",
    "TCMINTEMP": "-10", "TCMAXTEMP": "120",
}


class ProtocolError(Exception):
    def __init__(self, code):
        super().__init__("ERRORCODE=%d" % code)
        self.code = code


def xor_checksum(s):
    """publiccmd.c CheckSum 同算法：从首字符异或到 '#'（含），初值 0"""
    c = 0
    for ch in s:
        c ^= ord(ch)
        if ch == '#':
            break
    return c & 0xFF


def parse_frame(line, my_addr):
    """解析一行（不含 \r）→ dict(type/module/param/value/addr)。
    语法错误抛 ProtocolError(6/7)。"""
    if "#" in line:
        body, _, chk = line.rpartition("#")
        if len(chk) != 2:
            raise ProtocolError(6)
        try:
            want = int(chk, 16)
        except ValueError:
            raise ProtocolError(6)
        seg = line[:line.index('#') + 1]   # 首字符到 '#' 本身
        if xor_checksum(seg) != want:
            raise ProtocolError(7)
        if "@" not in body:
            raise ProtocolError(6)   # 校验必须配地址
        line = body
        had_chk = True
    else:
        had_chk = False
    addr = my_addr
    had_addr = False
    if "@" in line:
        base, _, a = line.rpartition("@")
        digits = ""
        for ch in a:
            if ch.isdigit():
                digits += ch
            else:
                break
        if digits == "":
            raise ProtocolError(6)
        addr = int(digits)
        line = base
        had_addr = True
    if ":" not in line:
        raise ProtocolError(6)
    mod, _, rest = line.partition(":")
    if mod == "" or rest == "":
        raise ProtocolError(6)
    for op, typ in (("?", "POLL"), ("=", "SET"), ("!", "SAVE"), ("^", "CHANGING")):
        idx = rest.find(op)
        if idx >= 0:
            return dict(type=typ, module=mod, param=rest[:idx],
                        value=rest[idx + 1:], addr=addr,
                        had_addr=had_addr, had_chk=had_chk)
    raise ProtocolError(6)


class M207Core(object):
    """M207 模块核心（纯逻辑，无串口依赖；tests 直接驱动）

    state 文件 = "已保存值" 集合（掉电保持语义）；运行值易失。"""

    def __init__(self, addr=0, state_file=None, enforce_50ms=True):
        self.addr = addr
        self.state_file = state_file
        self.enforce_50ms = enforce_50ms
        self.run = {}          # 运行值（未保存）
        self.saved = {}        # 已保存值（等价 EEPROM）
        self.min_temp = float(DEFAULTS["TCMINTEMP"])
        self.max_temp = float(DEFAULTS["TCMAXTEMP"])
        self.last_cmd_t = None
        self.violations_50ms = 0
        self.drops_50ms = 0
        if state_file and os.path.exists(state_file):
            with open(state_file, encoding="utf-8") as f:
                for ln in f:
                    k, _, v = ln.strip().partition("=")
                    if k:
                        self.saved[k] = v

    # ---------- 存取 ----------
    def get(self, module, param):
        key = module + ":" + param
        if key in self.run:
            return self.run[key]
        if key in self.saved:
            return self.saved[key]
        if key in DEFAULTS:
            return DEFAULTS[key]
        if param == "TCMINTEMP":
            return str(self.min_temp)
        if param == "TCMAXTEMP":
            return str(self.max_temp)
        if param == "TCACTTEMP":
            return "24.9"    # 模拟水温（缓变在壳层；纯核定值）
        return None

    def set(self, module, param, value):
        if param == "TCMINTEMP":
            self.min_temp = float(value)
            return
        if param == "TCMAXTEMP":
            self.max_temp = float(value)
            return
        self.run[module + ":" + param] = value

    # ---------- 命令处理：返回应答行列表（空=静默） ----------
    def handle_line(self, line, now=None):
        """line 不含 \\r。返回应答字符串列表（每条含 \\r）。"""
        if now is None:
            now = time.monotonic()
        # 4.6 时序铁律
        if self.last_cmd_t is not None and (now - self.last_cmd_t) < 0.050:
            self.violations_50ms += 1
            if self.enforce_50ms:
                self.last_cmd_t = now
                self.drops_50ms += 1
                return []          # 违反间隔：不受理（模拟下位机丢弃）
        self.last_cmd_t = now

        try:
            f = parse_frame(line, self.addr)
        except ProtocolError as e:
            return ["CMD:REPLY=%d\r" % e.code]
        if f["addr"] != self.addr:
            return []              # 7.3 地址不符：不响应

        mod, param = f["module"], f["param"]
        had_addr, had_chk = f["had_addr"], f["had_chk"]
        fmt = lambda reps: self._fmt(reps, had_addr, had_chk)   # noqa: E731
        if mod not in ("TC1", "TC2", "PCB", "MEMORY", "PARAM", "SYS", "SYSTEM",
                       "PCRS232", "UIRS232"):
            return fmt(self.reply_err(0))
        if param in ("TCMINTEMP", "TCMAXTEMP", "TCACTTEMP"):
            full = mod + ":" + param   # 动态/遥测项，字典外白名单
        else:
            full = mod + ":" + param
            if full not in DICT:
                return fmt(self.reply_err(2))

        if f["type"] == "CHANGING":
            return fmt(self.reply_err(6))   # ^ 禁用（正典 §6）
        if f["type"] == "POLL":
            v = self.get(mod, param)
            if v is None:
                return fmt(self.reply_err(2))
            return fmt([mod + ":" + param + "=" + v + "\r"])
        if f["type"] == "SET":
            typ, writable, _sav = DICT.get(
                full, ("NUM", param in ("TCMINTEMP", "TCMAXTEMP"), False))
            if not writable:
                return fmt(self.reply_err(3))
            if full == "SYS:RESET":
                return fmt(self.reply_ok(1))   # 重启语义由壳层处理
            if self._out_of_range(param, f["value"]):
                return fmt(self.reply_err(4))
            self.set(mod, param, f["value"])
            return fmt(self.reply_ok(1))
        if f["type"] == "SAVE":
            _t, writable, sav = DICT.get(
                full, ("NUM", False, False))
            if param in ("TCMINTEMP", "TCMAXTEMP"):
                writable, sav = True, True
            if not writable or not sav:
                return fmt(self.reply_err(3))
            v = self.get(mod, param)
            if v is None:
                return fmt(self.reply_err(2))
            self.saved[full] = v
            self._persist(full, v)
            return fmt(self.reply_ok(8))
        return fmt(self.reply_err(5))

    # ---------- 内部 ----------
    @staticmethod
    def _fmt(replies, had_addr, had_chk):
        # PDF 4.9/8.2：回复格式自动调整得与上位机发送格式一样（镜像 @X#YY）
        if not had_addr and not had_chk:
            return replies
        out = []
        for r in replies:
            body = r[:-1]
            if had_addr:
                body += "@0"
            if had_chk:
                body += "#"
                c = 0
                for ch in body:
                    c ^= ord(ch)
                body += "%02X" % (c & 0xFF)
            out.append(body + "\r")
        return out

    def _out_of_range(self, param, value):
        if param in ("TCADJTEMP", "TCSETTEMP"):
            try:
                v = float(value)
            except ValueError:
                return True
            return v < self.min_temp or v > self.max_temp
        b = BOUNDS.get(param)
        try:
            v = float(value)
        except ValueError:
            return True
        if b is not None:
            return v < b[0] or v > b[1]
        return False

    @staticmethod
    def reply_ok(code):
        return ["CMD:REPLY=%d\r" % code]

    @staticmethod
    def reply_err(code):
        return ["CMD:REPLY=%d\r" % code]

    def _persist(self, key, value):
        if not self.state_file:
            return
        lines = {}
        if os.path.exists(self.state_file):
            with open(self.state_file, encoding="utf-8") as f:
                for ln in f:
                    k, _, v = ln.strip().partition("=")
                    if k:
                        lines[k] = v
        lines[key] = value
        with open(self.state_file, "w", encoding="utf-8") as f:
            for k in sorted(lines):
                f.write(k + "=" + lines[k] + "\n")


# ============================ 串口壳层 ============================
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", default=None)
    ap.add_argument("baud", nargs="?", type=int, default=57600)
    ap.add_argument("--addr", type=int, default=0)
    ap.add_argument("--state", default=None, help="EEPROM 持久化文件")
    ap.add_argument("--no-50ms-drop", action="store_true",
                    help="违反间隔不丢弃只计数")
    a = ap.parse_args()

    core = M207Core(addr=a.addr, state_file=a.state,
                    enforce_50ms=not a.no_50ms_drop)
    if not a.port:
        print("dict params:", len(DICT))
        sys.exit(0)
    import serial
    ser = serial.Serial(a.port, a.baud, timeout=0.1)
    print("[sim] M207 v2 on %s @ %d addr=%d params=%d" %
          (a.port, a.baud, a.addr, len(DICT)))
    buf = b""
    try:
        while True:
            data = ser.read(256)
            if data:
                buf += data
                while b"\r" in buf:
                    raw, buf = buf.split(b"\r", 1)
                    line = raw.decode("ascii", "ignore").strip()
                    if not line:
                        continue
                    for rep in core.handle_line(line):
                        ser.write(rep.encode("ascii"))
                    print("[sim] rx:", line, "-> viol=%d drop=%d" %
                          (core.violations_50ms, core.drops_50ms))
            time.sleep(0.01)
    except KeyboardInterrupt:
        pass
    finally:
        ser.close()


if __name__ == "__main__":
    main()
