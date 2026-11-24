#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""site_common.py — 现场工具公共部分（路径、探针控制台加载）

所有 site 工具都从仓库根直接运行，不依赖任何临时目录：
    python project_tcs/tools/site/<脚本>.py ...
环境变量：
    TCS_SITE_DIR   桥的日志/命令文件目录（默认 %TEMP%\\tcs_site）
    TCS_ELF        H743 ELF 路径（默认 project_tcs/build/tcs_main.elf，相对 project_tcs）
"""
import importlib.util
import os
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent                      # project_tcs/
SITE_DIR = Path(os.environ.get("TCS_SITE_DIR") or (Path(os.environ.get("TEMP", ".")) / "tcs_site"))
LOG = SITE_DIR / "esp_out.log"                    # esp_bridge.py 的串口日志
CMD = SITE_DIR / "esp_cmd.txt"                    # 写一行命令进去，桥会取走并发给 ESP32


def enter_project():
    """mb_console.py 按"在 project_tcs 根目录"解析相对路径，先切过去。"""
    os.chdir(PROJECT)


def load_mb():
    """加载 tools/mb_console.py（probe 读写、符号地址、RTT 直读）。"""
    enter_project()
    spec = importlib.util.spec_from_file_location("mbc", str(PROJECT / "tools" / "mb_console.py"))
    mb = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mb)
    return mb
