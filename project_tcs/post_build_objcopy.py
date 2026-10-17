"""post_build_objcopy.py — ELF → HEX/BIN + size 报告（TCS 版）

GS 原版为绕开中文路径经英文临时目录 objcopy；本工程路径全 ASCII（D:\\Espressif\\ws\\tcs），
直接在 build 目录做，保留脚本入口以便与 GS 构建流程同构。用法：
    python post_build_objcopy.py <elf 名，默认 tcs_main.elf>   （工作目录 = build）
"""
import sys, os, subprocess

elf_name = sys.argv[1] if len(sys.argv) > 1 else "tcs_main.elf"
BUILD_DIR = os.getcwd()
ELF = os.path.join(BUILD_DIR, elf_name)
base = os.path.splitext(elf_name)[0]

OBJCOPY = r"C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\13.2 Rel1\bin\arm-none-eabi-objcopy.exe"
SIZE    = r"C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\13.2 Rel1\bin\arm-none-eabi-size.exe"

for fmt, ext in [("ihex", "hex"), ("binary", "bin")]:
    out = os.path.join(BUILD_DIR, base + "." + ext)
    r = subprocess.run([OBJCOPY, "-O", fmt, ELF, out], capture_output=True, text=True)
    if r.returncode != 0:
        print("OBJCOPY %s FAIL: %s" % (ext, r.stderr), file=sys.stderr)
        sys.exit(1)

r = subprocess.run([SIZE, ELF], capture_output=True, text=True)
print(r.stdout.strip())
print("objcopy OK: %s.hex %s.bin" % (base, base))
