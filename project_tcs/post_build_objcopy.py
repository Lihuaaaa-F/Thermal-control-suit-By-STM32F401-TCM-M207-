"""post_build_objcopy.py — ELF → HEX/BIN + size 报告（TCS 版）

GS 原版为绕开中文路径经英文临时目录 objcopy；本工程路径全 ASCII（D:\\Espressif\\ws\\tcs），
直接在 build 目录做，保留脚本入口以便与 GS 构建流程同构。用法：
    python post_build_objcopy.py <elf 名，默认 tcs_main.elf> [objcopy 路径] [size 路径]   （工作目录 = build）
    后两个参数由 CMakeLists.txt 传入（取自 arm-none-eabi-gcc.cmake 找到的同一套工具链）；
    不传就在 PATH 上找。此前这里写死本机安装路径（2026-11-25 S 线，审查条目 X05-F-06）。
"""
import sys, os, shutil, subprocess

elf_name = sys.argv[1] if len(sys.argv) > 1 else "tcs_main.elf"
BUILD_DIR = os.getcwd()
ELF = os.path.join(BUILD_DIR, elf_name)
base = os.path.splitext(elf_name)[0]


def _tool(idx, name):
    if len(sys.argv) > idx and sys.argv[idx]:
        return sys.argv[idx]
    found = shutil.which(name)
    if not found:
        sys.exit("%s 不在 PATH 上：把 Arm GNU Toolchain 的 bin 目录加进 PATH" % name)
    return found


OBJCOPY = _tool(2, "arm-none-eabi-objcopy")
SIZE    = _tool(3, "arm-none-eabi-size")

for fmt, ext in [("ihex", "hex"), ("binary", "bin")]:
    out = os.path.join(BUILD_DIR, base + "." + ext)
    r = subprocess.run([OBJCOPY, "-O", fmt, ELF, out], capture_output=True, text=True)
    if r.returncode != 0:
        print("OBJCOPY %s FAIL: %s" % (ext, r.stderr), file=sys.stderr)
        sys.exit(1)

r = subprocess.run([SIZE, ELF], capture_output=True, text=True)
print(r.stdout.strip())
print("objcopy OK: %s.hex %s.bin" % (base, base))
