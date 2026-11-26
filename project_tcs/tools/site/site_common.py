#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""site_common.py — 现场工具公共部分（路径、探针控制台加载、带禁区的触摸注入）

所有 site 工具都从仓库根直接运行，不依赖任何临时目录：
    python project_tcs/tools/site/<脚本>.py ...
环境变量：
    TCS_SITE_DIR   桥的日志/命令文件目录（默认 %TEMP%\\tcs_site）
    TCS_ELF        H743 ELF 路径（默认 project_tcs/build/tcs_main.elf，相对 project_tcs）
"""
import importlib.util
import os
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent.parent                      # project_tcs/
SITE_DIR = Path(os.environ.get("TCS_SITE_DIR") or (Path(os.environ.get("TEMP", ".")) / "tcs_site"))
LOG = SITE_DIR / "esp_out.log"                    # esp_bridge.py 的串口日志
CMD = SITE_DIR / "esp_cmd.txt"                    # 写一行命令进去，桥会取走并发给 ESP32

# ---------------------------------------------------------------------------
# 执行器禁区（S-G x3，2026-11-26；起因是 D7 事故）
#
# 2026-11-26 D7 第一次跑 estop_matrix：模式页"点按"那一行用 tap(632,388) 去点确认框的"取消"，
# 确认框已经不在了，这一下落在 TEC2"制冷"上——主控向 M207 发了一条真实的 TEC2 制冷命令
# （约 1 秒后被同一行的"按住 2 秒"急停关掉）。用户明确要求不做任何驱动 TEC 的操作：
# 这类工具必须**从结构上**做不到，而不是靠坐标碰巧对。
#
# 坐标来源：真机截图 shots7/02_mode_manual.png（2026-11-26 量取），模式页（页码 4）上
# 会让执行器动作的控件矩形 (x0, y0, x1, y1)，左闭右开：
#   TEC1 行：制冷 / 制热      TEC2 行：制冷 / 制热      风扇        切换到自动模式
# "暂停"键与"急停"键不在此表：它们是关断方向/安全入口，必须可点。
ACTUATOR_RECTS = (
    (504, 268, 648, 348), (848, 268, 992, 348),      # TEC1 行：制冷、制热
    (504, 364, 648, 444), (848, 364, 992, 444),      # TEC2 行：制冷、制热
    (32, 472, 264, 568), (280, 472, 744, 568),       # 风扇、切换到自动模式
)
# 2026-11-27（U2）：模式页 TEC 行改为 y 268–348 / 364–444（高 80），禁区同步。
# 改了模式页版面就必须先改这张表——否则护栏会把新的键区漏掉。
MODE_PAGE = 4                                        # s_page：0 首页 1 调参 2 曲线 3 连接 4 模式


class TapRefused(RuntimeError):
    """要注入的点击落在执行器禁区里——宁可整条工具不跑，也不发执行器命令。"""


def _page_reader(mb):
    """从固件的 s_page 现读页码。现场工具都配台架档固件，s_page 一定在。"""
    addr = mb.nm_addr("s_page")
    return lambda: mb.pr_read("b8", addr, 1)[0]


def tap_guarded(mb, x, y, mode=1, page=None):
    """注入一次触摸——**唯一**的实现（tools/site 下不许再有第二份写 g_tap_inject 的代码）。

    page: 返回当前页码的可调用对象；None = 从固件 s_page 现读。
    模式页（页码 4）上落在 ACTUATOR_RECTS 里 → 抛 TapRefused，一个字节都不写。
    其它页不查：同样的坐标在首页是导航键（例如 (376,520) 是模式键、(608,520) 是连接键）。
    写入顺序必须是 mode/x/y 先、序号最后——固件按"序号变化"取坐标（2026-11-24 踩过：
    先写序号会让固件读到上一次的坐标，点"切自动"却切换了风扇）。
    写入后回读校对，不一致直接抛错。
    """
    if page is None:
        page = _page_reader(mb)
    if page() == MODE_PAGE:
        for (x0, y0, x1, y1) in ACTUATOR_RECTS:
            if x0 <= x < x1 and y0 <= y < y1:
                raise TapRefused(
                    "拒绝注入 (%d,%d)：落在模式页执行器控件 (x %d..%d, y %d..%d) 里" %
                    (x, y, x0, x1 - 1, y0, y1 - 1))
    a = mb.nm_addr("g_tap_inject")
    mb.pr_write(a + 4, struct.pack("<III", mode, x, y))
    back = mb.pr_read("b32", a + 4, 3)
    if list(back) != [mode, x, y]:
        raise RuntimeError("tap: 写后回读不一致：%r != [%d, %d, %d]" % (back, mode, x, y))
    seq = (mb.pr_read("b32", a, 1)[0] + 1) & 0xFFFFFFFF
    mb.pr_write(a, struct.pack("<I", seq))
    return seq


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
