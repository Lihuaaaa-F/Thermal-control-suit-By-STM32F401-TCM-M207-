#!/bin/bash
# tests/run_all.sh — PC 单元测试一键全绿（任务书 §5.1）
# 编译器自动选择：gcc（首选）→ MSVC cl（经 vcvars64）。
# 用法：bash tests/run_all.sh   （在 project_tcs 根目录）
set -u
cd "$(dirname "$0")/.." || exit 1
FAIL=0

IFLAGS="-IApp/tcs -IApp/proto -IApp/comm -IApp/lvgl_ui -ICore/Inc"

# 选一个带 Pillow 的 Python（2026-11-24，X06-F-08 环境假红）：shell 里先 source 过 IDF 时，
# python/python3 指向 IDF 自带解释器（没有 Pillow），二维码回读那一步会红。按候选顺序挑第一个能 import PIL 的。
pick_pybin() {
    local c
    for c in ${TCS_PYBIN:-} python3 python "py -3" \
             "/c/Users/$USERNAME/AppData/Local/Programs/Python/Python312/python.exe"; do
        # shellcheck disable=SC2086  # "py -3" 需要按词拆开
        if [ -n "$c" ] && $c -c "import PIL" >/dev/null 2>&1; then
            $c -c "import sys; sys.stdout.write(sys.executable)"
            return 0
        fi
    done
    return 1
}
INC_CL="/IApp\\tcs /IApp\\proto /IApp\\comm"
VARS64="C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat"

cl_run() {  # cl_run <out.exe> <src...> — bat 内 cd 到 build，产物/对象全落 build
    local out="$1"; shift
    local cmd_line='cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /DTCS_PROFILE=1 /I..\App\tcs /I..\App\proto /I..\App\comm /I..\App\lvgl_ui /I..\Core\Inc'
    for s in "$@"; do
        cmd_line="$cmd_line \"$(cygpath -w "$PWD/$s")\""
    done
    cmd_line="$cmd_line /Fe:$(basename "$out")"
    {
        echo '@echo off'
        echo "call \"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\VC\\Auxiliary\\Build\\vcvars64.bat\" >nul 2>&1"
        echo "cd /d \"$(cygpath -w "$PWD/build")\""
        echo "$cmd_line"
        echo 'exit /b %ERRORLEVEL%'
    } > build/cl_compile.bat
    cmd //c "build\\cl_compile.bat" > build/cl.log 2>&1
}

if command -v gcc >/dev/null 2>&1; then
    GCC=${GCC:-gcc}
    # 单测按台架语义（TCS_PROFILE=1）：本地闭环/参数持久化都在，才能测到它们。
# 交付语义（PROD=0）的差异由构建/反汇编门禁覆盖，不在主机单测里跑。
CFLAGS="-Wall -Wextra -std=c11 -O1 -DTCS_UNIT_TEST -DTCS_PROFILE=1 $IFLAGS"
    MODE=gcc
else
    MODE=cl
    mkdir -p build
    echo "[toolchain] gcc not found, using MSVC cl (vcvars64)"
fi

run() {  # run <name> <test.c> <sources...>
    local name="$1" test="$2"; shift 2
    local bin="build/tests_$name.exe"
    mkdir -p build
    if [ "$MODE" = gcc ]; then
        if ! $GCC $CFLAGS "$test" "$@" -o "$bin" 2> "build/tests_$name.err"; then
            echo "[$name] COMPILE FAIL"; cat "build/tests_$name.err"; FAIL=1; return
        fi
    else
        if ! cl_run "$bin" "$test" "$@"; then
            echo "[$name] COMPILE FAIL"; cat build/cl.log; FAIL=1; return
        fi
    fi
    if "./$bin"; then echo "[$name] PASS"; else echo "[$name] FAIL"; FAIL=1; fi
}

run m207        tests/test_m207_parse.c       App/tcs/m207.c
run m207golden  tests/test_m207_golden.c      App/tcs/m207.c
run registry    tests/test_param_registry.c   App/tcs/param_registry.c App/proto/proto.c
run control     tests/test_control.c          App/tcs/control.c App/tcs/param_registry.c App/tcs/post.c App/proto/proto.c
run post        tests/test_post.c             App/tcs/post.c App/tcs/param_registry.c App/proto/proto.c
# S 线（2026-11-24）：现场抽出的纯逻辑——急停屏入口兜底判定、页面重建判据在 U 线的 uimodel 里测
run site_logic  tests/test_site_logic.c       App/tcs/estop_gate.c
run proto_comm  tests/test_proto_tcs.c        App/proto/proto.c App/comm/comm.c
run fuzz        tests/fuzz_m207.c             App/tcs/m207.c

run touchsession tests/test_touch_session.c   App/tcs/touch_session.c Core/Src/ft5446.c
run qr          tests/test_qr.c               App/tcs/qr/qrcodegen.c

# U 线 S2/W3：界面纯逻辑（连接视图 / 横幅选型 / 确认反馈 / 主页差分）与主控设备信息
run uimodel     tests/test_ui_model.c        App/lvgl_ui/ui_model.c
run linkinfo    tests/test_link_info.c       App/comm/link_info.c

# H13 二维码解码回读（PC 出图 → zxing-cpp 解码比对）
PYBIN=$(pick_pybin) || PYBIN=""
# S 线 D02：现场网页检查脚本自带的 WebSocket 客户端自测（本机回环，不需要设备）
if [ -n "$PYBIN" ] && "$PYBIN" tools/site/test_ws_client.py; then :; else echo "[ws_client_selftest] FAIL"; FAIL=1; fi
# S 线（2026-11-24 晚）：急停屏入口判定的变异验证——把 estop_gate.[ch] 逐个改坏，上面的单测必须每次都变红。
# 这是安全路径上唯一的判定逻辑；"全绿"不说明测试有没有牙。
if [ -n "$PYBIN" ] && PYTHONUTF8=1 "$PYBIN" tests/mutation_estop_gate.py > build/mut_estop.log 2>&1; then
    echo "[mut-estop] PASS（$(tail -1 build/mut_estop.log)）"
else
    echo "[mut-estop] FAIL"; grep -a "活下来\|编译失败\|锚点\|FAIL" build/mut_estop.log | head -12; FAIL=1
fi
if [ -n "$PYBIN" ]; then
    # 2026-11-23：原来失败不打印（"静默红"）——shell 里先 source 过 IDF 环境时，
    # python 指向 IDF 自带解释器（无 Pillow）→ 本步 import 失败却看不到原因。
    if "$PYBIN" -c "import PIL" 2>/dev/null; then :; else
        echo "[qr_verify] FAIL: 当前 python 无 Pillow（$PYBIN）——换系统 python 或别在同 shell 先 source IDF"; FAIL=1
    fi
    if "$PYBIN" tools/qr_verify.py build/test_qr.pbm "http://192.168.4.1"; then :; else echo "[qr_verify] FAIL"; FAIL=1; fi
else
    echo "[qr_verify] SKIP: python not found"; FAIL=1
fi

# TMP117 换算与健康计数（§11.2 PC 单测替代模拟器）
cat > build/test_tmp117.c <<'EOF'
#include <stdio.h>
#include "tmp117.h"
static int raise_cnt; static void raise(void){ raise_cnt++; }
int main(void){
    int fail=0;
    /* raw→°C：0.0078125 每 LSB */
    if (!(tmp117_raw_to_c(0) == 0.0f)) fail++;
    if (!(tmp117_raw_to_c(128) == 1.0f)) fail++;          /* 128 LSB = 1°C */
    if (!(tmp117_raw_to_c(2560) == 20.0f)) fail++;
    if (!(tmp117_raw_to_c(-1024) == -8.0f)) fail++;
    if (!(tmp117_raw_to_c(0x1FFF) > 63.99f && tmp117_raw_to_c(0x1FFF) < 64.01f)) fail++;
    /* 健康计数：10 连错 → 掉线沿 raise 一次；3 连成功 → 恢复数据 */
    tmp117_health_t h; tmp117_health_init(&h); raise_cnt=0;
    for (int i=0;i<10;i++) tmp117_health_step(&h,0,raise);
    if (!(h.ok==0 && raise_cnt==1)) fail++;
    tmp117_health_step(&h,0,raise);
    if (raise_cnt!=1) fail++;                              /* 掉线沿只触发一次 */
    for (int i=0;i<3;i++) tmp117_health_step(&h,1,raise);
    if (!(h.ok==1 && raise_cnt==1)) fail++;                /* 恢复不清报警位（raise 不再触发）*/
    printf("test_tmp117: %s\n", fail? "FAIL":"PASS");
    return fail;
}
EOF
run tmp117 build/test_tmp117.c App/tcs/tmp117.c

# WP-P4/P6：M207 高级参数注入矩阵（PC/sim 层，五行矩阵+golden+50ms+PDF 示例）
if [ -n "$PYBIN" ]; then
    if "$PYBIN" tests/test_m207_adv.py; then :; else FAIL=1; fi
else
    echo "[m207_adv] SKIP: python not found"; FAIL=1
fi

# H3 字库缺字门禁（2026-11-15 UI 单栈：改为 LVGL 字库审计；旧 font_audit.py 已随栈删除）
PYBIN=$(pick_pybin) || PYBIN=""
if [ -n "$PYBIN" ]; then
    if "$PYBIN" tools/font_audit_lvgl.py; then :; else FAIL=1; fi
else
    echo "[font_audit] SKIP: python not found"; FAIL=1
fi

# R1 M14-F-05：网页静态资源与 .gz 同步门禁（改源未重压 → 浏览器拿旧版）
if [ -n "$PYBIN" ]; then
    if "$PYBIN" tools/web_gz_check.py; then :; else FAIL=1; fi
else
    echo "[web_gz] SKIP: python not found"; FAIL=1
fi

# RTT printf 调用点总数对账（W5 立；W4.2 vendor 口径 13→；W1a +3 日志点后=16）
# （App/rtt/ 为 vendor+薄壳域不计；16=业务面 13+W1a i2c4 三行；ltdc.c 4 处为
# HEAD 既有 G 线文件；增删调用点须同步本数并说明）
RTTN=$(grep -rn "SEGGER_RTT_printf" App Core --include=*.c | grep -v "App/rtt/" | wc -l)
if [ "$RTTN" -ne 16 ]; then
    echo "[rtt_count] FAIL: SEGGER_RTT_printf 调用点 $RTTN ≠ 16（增删须同步本门禁并说明）"
    FAIL=1
else
    echo "[rtt_count] PASS (16)"
fi

# 官方库审计门禁（W5，v13.4 §6）：未入清单/未归类文件 → FAIL
# 脚本内部自锚定仓库根（W5 改造）；新增源文件须同 commit 更新 docs/审计文件清单.txt
# 2026-11-24（X06-F-08）：改用 --check-report——门禁只读，不再每次改写机器报告；
# 报告与代码不一致（有源文件增删而没重跑审计）直接判红。
if [ -n "$PYBIN" ]; then
    if "$PYBIN" ../esp32/tools/official_lib_audit.py --check-report; then :; else FAIL=1; fi
else
    echo "[audit] SKIP: python not found"; FAIL=1
fi

# W2 转义向量单测（v13.4 §3.3）：真实 IDF cJSON 源编译（与固件同库）；
# IDF 路径机器相关（与 CMake 工具链绝对路径同性质），缺件即红不静默
CJSON_DIR="${IDF_CJSON:-/d/Espressif/frameworks/esp-idf-v5.5.5/components/json/cJSON}"
if [ -f "$CJSON_DIR/cJSON.c" ]; then
    if [ "$MODE" = gcc ]; then
        if $GCC -Wall -Wextra -std=c11 -O1 "$CJSON_DIR/cJSON.c" \
             tests/test_ws_json_escape.c -I"$CJSON_DIR" \
             -lm -o build/tests_ws_json.exe 2> build/ws_json.err \
           && ./build/tests_ws_json.exe; then :; else
            echo "[ws_json] FAIL"; cat build/ws_json.err 2>/dev/null; FAIL=1
        fi
    else
        CJW=$(cygpath -w "$CJSON_DIR")
        cat > build/ws_json.bat <<EOF
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "$(cygpath -w "$PWD/build")"
cl /nologo /W3 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /I"$CJW" "$CJW\\cJSON.c" "..\\tests\\test_ws_json_escape.c" /Fe:tests_ws_json.exe
tests_ws_json.exe
exit /b %ERRORLEVEL%
EOF
        if cmd //c "build\\ws_json.bat"; then :; else echo "[ws_json] FAIL"; FAIL=1; fi
    fi
else
    echo "[ws_json] FAIL: IDF cJSON 源缺失（$CJSON_DIR，可用 IDF_CJSON 覆盖路径）"
    FAIL=1
fi

# W8 研学卡本地路径存在性门禁（红队建议采纳：41 条抓 1 条的实证抓力；0 bad 才绿）
if [ -n "$PYBIN" ]; then
    if "$PYBIN" - <<'PYEOF'
import io,glob,os,re,sys
bad=[]; n=0
for card in glob.glob('../docs/ref/研学卡_*.md'):
    for line in io.open(card,encoding='utf-8'):
        for m in re.finditer(r'(D:/Espressif/[\w/.-]+|/d/Espressif/[\w/.-]+)', line):
            p=m.group(1); n+=1
            win=p.replace('/d/','D:/',1) if p.startswith('/d/') else p
            win=re.sub(r'\.$','',win)
            if not os.path.exists(win): bad.append((os.path.basename(card),p))
print('[card_paths] %d checked, %d bad %s' % (n,len(bad),bad[:3] if bad else ''))
sys.exit(1 if bad else 0)
PYEOF
    then :; else FAIL=1; fi
else
    echo "[card_paths] SKIP: python not found"; FAIL=1
fi

# W4.2/R7：vendor RTT 完整性门禁（VENDOR_SHA256.txt CRLF 列 sha256sum 式机器比对；
# 终审 R-1：行尾 CR 用 tr 剥离，防 autocrlf/干净克隆把 \r 带进会话名）
VSHA=App/rtt/vendor/VENDOR_SHA256.txt
if [ -f "$VSHA" ]; then
    ok=1
    while IFS= read -r line; do
        line=$(printf '%s' "$line" | tr -d '\r')
        case "$line" in ''|\#*) continue;; esac
        h=$(printf '%s' "$line" | cut -d' ' -f1)
        name=$(printf '%s' "$line" | awk '{print $NF}')
        want=$(printf '%s' "$h" | tr -d ' ')
        got=$(sha256sum "App/rtt/vendor/$name" | cut -d' ' -f1)
        if [ "$want" != "$got" ]; then
            echo "[vendor_sha] FAIL: $name $got != $want"; ok=0
        fi
    done < "$VSHA"
    [ "$ok" = 1 ] && echo "[vendor_sha] PASS (5 files)" || FAIL=1
else
    echo "[vendor_sha] FAIL: $VSHA 缺失"; FAIL=1
fi

# MF-3（终审）：RTT printf 格式面门禁——F14(%f)/F16(%l) 归零（业务面，排除 App/rtt）
FMTF=$(grep -rn "SEGGER_RTT_printf" App Core --include=*.c | grep -v "App/rtt/" | grep -cE "%[0-9.]*[efg]")
FMTL=$(grep -rn "SEGGER_RTT_printf" App Core --include=*.c | grep -v "App/rtt/" | grep -cE "%[0-9.]*l[a-z]")
if [ "$FMTF" -eq 0 ] && [ "$FMTL" -eq 0 ]; then
    echo "[fmt_gate] PASS (%f=0 %l=0)"
else
    echo "[fmt_gate] FAIL: %f=$FMTF %l=$FMTL（浮点/长度修饰符须 snprintf 预格式化，见 F14/F16）"
    FAIL=1
fi

# MF-1（终审）：研学卡 ③ 行数常驻门禁（基线文件 docs/ref/研学卡_行数基线.txt）
CARD_BASE=../docs/ref/研学卡_行数基线.txt
if [ -f "$CARD_BASE" ] && [ -n "$PYBIN" ]; then
    if "$PYBIN" - "$CARD_BASE" <<'PYEOF2'
import io,sys,subprocess,re,shutil
GREP=shutil.which('grep') or 'grep'
bad=0; n=0
for line in io.open(sys.argv[1],encoding='utf-8'):
    line=line.strip()
    if not line or line.startswith('#'): continue
    parts=line.split(';;')
    if len(parts)!=4: continue
    name,dirs,pat,want=parts[0],parts[1].split(),parts[2],int(parts[3].split()[0])
    n+=1
    out=subprocess.run([GREP,'-rnE',pat]+dirs+['--include=*.c'],
                       capture_output=True,text=True).stdout
    got=len([l for l in out.splitlines() if l])
    if got!=want:
        print('[card_surface] FAIL %s: got=%d want=%d' % (name,got,want)); bad+=1
print('[card_surface] %s (%d cards)' % ('PASS' if not bad else 'FAIL',n))
sys.exit(1 if bad else 0)
PYEOF2
    then :; else FAIL=1; fi
else
    echo "[card_surface] SKIP: 基线或 python 缺失"; FAIL=1
fi

# 源码字节门禁：NUL / 孤立 CR（"转义写成真字符"类损坏，2026-11-16 事故后加）
if [ -n "$PYBIN" ]; then
    if "$PYBIN" tools/src_byte_check.py; then :; else FAIL=1; fi
else
    echo "[src_bytes] SKIP: python not found"; FAIL=1
fi

# X06-F-02：ESP32 纯逻辑 5 套件纳入同一门禁（出货二进制=两端的组合，绿灯必须覆盖两端）
if [ -f ../esp32/tests/run_m207_tests.sh ]; then
    if bash ../esp32/tests/run_m207_tests.sh; then :; else
        echo "[esp32_tests] FAIL"; FAIL=1
    fi
else
    echo "[esp32_tests] SKIP: ../esp32/tests/run_m207_tests.sh 缺失"
fi

# S-G h1a（M01-F-10）：出现过的每个 IRQ 都必须有处理函数。
# 背景：main.c 曾在 NVIC 里使能 I2C1_EV_IRQn，而 stm32h7xx_it.c 里没有 I2C1_EV_IRQHandler——
# 真有这个中断进来会落到 Default_Handler 的死循环，主循环停摆，约 2s 后看门狗复位，
# 看起来像"莫名复位"而不是 I2C 错误。
# 口径：App 与 Core/Src 下的 *.c 里凡出现 <名字>_IRQn，<名字>_IRQHandler 就必须在同一范围里
# 有函数定义。按"出现过"查，不只查 EnableIRQ 的字面参数：串口的 IRQ 号是经变量传进去的。
if [ -n "$PYBIN" ]; then
    if "$PYBIN" - <<'PYEOF'
import io, os, re, sys
text = []
for root in ('App', 'Core/Src'):
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.endswith('.c'):
                text.append(io.open(os.path.join(d, f), encoding='utf-8', errors='replace').read())
irqs = set()
handlers = set()
for s in text:
    irqs.update(re.findall(r'\b([A-Za-z0-9_]+)_IRQn\b', s))
    handlers.update(re.findall(r'\b([A-Za-z0-9_]+)_IRQHandler\s*\([^)]*\)\s*\{', s))
bad = sorted(i for i in irqs if i not in handlers)
for i in bad:
    print('[irq_handler] FAIL: %s_IRQn 被引用，但没有 %s_IRQHandler' % (i, i))
print('[irq_handler] %s (%d 个 IRQ 被引用，%d 个没有处理函数)' %
      ('FAIL' if bad else 'PASS', len(irqs), len(bad)))
sys.exit(1 if bad else 0)
PYEOF
    then :; else FAIL=1; fi
else
    echo "[irq_handler] SKIP: python not found"; FAIL=1
fi

echo "==================="
if [ "$FAIL" = 0 ]; then echo "run_all: ALL GREEN"; else echo "run_all: HAS FAILURES"; fi
exit $FAIL
