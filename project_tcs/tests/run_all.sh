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

run m207        tests/test_m207_parse.c       App/tcs/m207.c App/tcs/m207_txq.c   # S-G l1：m207.c 纯逻辑段的 m207_send_raw 引用队列
run m207golden  tests/test_m207_golden.c      App/tcs/m207.c App/tcs/m207_txq.c
run registry    tests/test_param_registry.c   App/tcs/param_registry.c App/proto/proto.c
run control     tests/test_control.c          App/tcs/control.c App/tcs/param_registry.c App/tcs/post.c App/proto/proto.c
run post        tests/test_post.c             App/tcs/post.c App/tcs/param_registry.c App/proto/proto.c
# S 线（2026-11-24）：现场抽出的纯逻辑——急停屏入口兜底判定、页面重建判据在 U 线的 uimodel 里测
run site_logic  tests/test_site_logic.c       App/tcs/estop_gate.c
run proto_comm  tests/test_proto_tcs.c        App/proto/proto.c App/comm/comm.c
run fuzz        tests/fuzz_m207.c             App/tcs/m207.c App/tcs/m207_txq.c   # S-G l1：同上

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
    # 2026-11-25 S 线（审查条目 X06-F-07 ⑤）：坏行与"0 行基线"原先被静默跳过或恒成立——
    # 把基线删空、删坏也能 PASS。现在都判红；卡的张数少于 7 也判红。
    if len(parts)!=4:
        print('[card_surface] FAIL 基线里有坏行: %s' % line[:60]); bad+=1; continue
    name,dirs,pat,want=parts[0],parts[1].split(),parts[2],int(parts[3].split()[0])
    if want<=0 or not dirs or not pat:
        print('[card_surface] FAIL %s: 基线为 0 或缺目录/模式' % name); bad+=1; continue
    n+=1
    out=subprocess.run([GREP,'-rnE',pat]+dirs+['--include=*.c'],
                       capture_output=True,text=True).stdout
    got=len([l for l in out.splitlines() if l])
    if got!=want:
        print('[card_surface] FAIL %s: got=%d want=%d' % (name,got,want)); bad+=1
if n<7:
    print('[card_surface] FAIL 基线里只有 %d 张卡（应 ≥ 7）' % n); bad+=1
print('[card_surface] %s (%d cards)' % ('PASS' if not bad else 'FAIL',n))
sys.exit(1 if bad else 0)
PYEOF2
    then :; else FAIL=1; fi
else
    echo "[card_surface] SKIP: 基线或 python 缺失"; FAIL=1
fi

# 研学卡 ③ 逐行核对（2026-11-25 S 线，审查条目 X06-F-07 / X06-F-09 的文档漂移部分）：
# 上面那道只数行数，看不出"文档里的行号漂了"和"文档本身比代码少列了 17 行"——这两件事当时都发生了。
if [ -n "$PYBIN" ]; then
    if "$PYBIN" tools/study_cards_check.py; then :; else FAIL=1; fi
else
    echo "[card_lines] SKIP: 没有 python"; FAIL=1
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
    # S-G e2（M12-F-12）：出货二进制 = 两端的组合。ESP32 测试入口缺失必须判红，不能 SKIP。
    echo "[esp32_tests] FAIL: ../esp32/tests/run_m207_tests.sh 缺失"; FAIL=1
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

# S-G h1a（P1-06 残余）：交付档（TCS_PROFILE=0 → TCS_H743_LINK_ONLY=1）下 control 不驱动任何本地执行器。
# 上面每一行都按台架语义（TCS_PROFILE=1）编译，走不到交付档的分支（独立复核 R5-06）。
# 这一段自带一套开关，不动既有行；源文件与 run control 同一组。
if [ "$MODE" = gcc ]; then
    if $GCC -Wall -Wextra -std=c11 -O1 -DTCS_UNIT_TEST -DTCS_PROFILE=0 $IFLAGS \
         tests/test_prod_link_only.c App/tcs/control.c App/tcs/param_registry.c App/tcs/post.c App/proto/proto.c \
         -o build/tests_prodlink.exe 2> build/tests_prodlink.err \
       && ./build/tests_prodlink.exe; then :; else
        echo "[prodlink] FAIL"; cat build/tests_prodlink.err 2>/dev/null | head -20; FAIL=1
    fi
else
    mkdir -p build
    PL_CMD='cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /DTCS_PROFILE=0 /I..\App\tcs /I..\App\proto /I..\App\comm /I..\App\lvgl_ui /I..\Core\Inc'
    for s in tests/test_prod_link_only.c App/tcs/control.c App/tcs/param_registry.c App/tcs/post.c App/proto/proto.c; do
        PL_CMD="$PL_CMD \"$(cygpath -w "$PWD/$s")\""
    done
    PL_CMD="$PL_CMD /Fe:tests_prodlink.exe"
    {
        echo '@echo off'
        echo 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1'
        echo "cd /d \"$(cygpath -w "$PWD/build")\""
        echo "$PL_CMD || exit /b 1"
        echo 'tests_prodlink.exe'
        echo 'exit /b %ERRORLEVEL%'
    } > build/prodlink_compile.bat
    if cmd //c "build\\prodlink_compile.bat" > build/prodlink.log 2>&1; then
        echo "[prodlink] PASS（$(tail -1 build/prodlink.log)）"   # 成功也留一行：否则看不出这一段跑过
    else
        echo "[prodlink] FAIL"; grep -a "FAIL" build/prodlink.log | head -20; FAIL=1
    fi
fi

# S-G h1a（M01-F-08 / M02-F-05）：驱动层纯逻辑——_sbrk 堆上界、SDRAM 自检图样（故障注入）
run drvpure     tests/test_drv_pure.c     Core/Src/ltdc.c

# S-G l1（2026-11-25）：链路方向闸门（R5-01）与屏写参数走 ARQ（R5-10）——屏端 comm 层的方向用例、
# 屏写参数策略用例（App/comm/link_send.c）。ESP32 侧的对应用例在上面的 esp32_tests 里（linkdir 一行）。
run linkdir    tests/test_link_dir.c     App/proto/proto.c App/comm/comm.c
run link_send  tests/test_link_send.c    App/comm/link_send.c App/comm/comm.c App/proto/proto.c
# 它们的变异验证：协议唯一源、两端 comm.c、link_task.c、link_send.c 逐个改坏，对应那一侧的用例必须变红。
# 有变异体活下来 = 用例有盲区。约 1.5 分钟（每个变异体一次 vcvars + cl）。
if [ -n "$PYBIN" ] && PYTHONUTF8=1 "$PYBIN" tests/mutation_link_dir.py > build/mut_link.log 2>&1; then
    echo "[mut-link] PASS（$(tail -1 build/mut_link.log)）"
else
    echo "[mut-link] FAIL"; grep -a "活下来\|编译失败\|锚点\|FAIL" build/mut_link.log | head -12; FAIL=1
fi
# S-G l1：协议唯一源门禁——两端包装是薄包装、方向表与两端源码的收发分支逐条一致（M11-F-17 / X01-F-07）。
if [ -n "$PYBIN" ]; then
    if "$PYBIN" ../tools/proto_sync.py --check; then :; else echo "[proto_sync] FAIL"; FAIL=1; fi
else
    echo "[proto_sync] SKIP: python not found"; FAIL=1
fi

# S-G l1：R5-06 残余——按**交付档**编译的主机用例，覆盖 control_link_follow（92899c2 加的 SAFE 退出判据）。
# 上面其余用例一律按台架档（-DTCS_PROFILE=1）编译，交付档专属的这段判定在那些编译单元里不存在，
# 此前在主机门禁里零覆盖。自带一套 CFLAGS，不改既有行。
# run_prod <名字> <用例.c> <源文件...>：按交付档（-DTCS_PROFILE=0）编译并运行一个用例。
# （M07-F-08 起第二个使用者是 auditring_prod：同一份审计用例再按交付档编译一次。）
run_prod() {  # run_prod <name> <test.c> <sources...>
    local name="$1"; shift
    local exe="build/tests_${name}_exe"
    local srcs="" s
    for s in "$@"; do srcs="$srcs \"$(cygpath -w "$PWD/$s")\""; done
    if [ "$MODE" = gcc ]; then
        if $GCC -Wall -Wextra -std=c11 -O1 -DTCS_UNIT_TEST -DTCS_PROFILE=0 $IFLAGS "$@" -o "$exe" 2> "build/tests_${name}.err" \
           && "./$exe"; then
            echo "[$name] PASS"
        else
            echo "[$name] FAIL"; cat "build/tests_${name}.err" 2>/dev/null | head -20; FAIL=1
        fi
        return
    fi
    local cmd='cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /DTCS_PROFILE=0 /I..\App\tcs /I..\App\proto /I..\App\comm'
    cmd="$cmd $srcs /Fe:$(basename "$exe")"
    {
        echo '@echo off'
        echo 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1'
        echo "cd /d \"$(cygpath -w "$PWD/build")\""
        echo "$cmd"
        echo 'exit /b %ERRORLEVEL%'
    } > "build/prod_compile_${name}.bat"
    if cmd //c "build\\prod_compile_${name}.bat" > "build/prod_compile_${name}.log" 2>&1 && "./$exe"; then
        echo "[$name] PASS"
    else
        echo "[$name] FAIL"; cat "build/prod_compile_${name}.log" | head -20; FAIL=1
    fi
}
run_prod linkfollow_prod tests/test_link_follow_prod.c App/tcs/control.c App/tcs/param_registry.c App/tcs/post.c App/proto/proto.c
# 它的回滚式验证：把判据逐个改回错误写法（不看四个标志 / 不看安全类报警 / 去掉"只在 SAFE 才判"…），
# 上面那个用例必须每次都变红。control.c 本身不改（归 h1a 批次），变异体在 build/ 下的副本里。
if [ -n "$PYBIN" ] && PYTHONUTF8=1 "$PYBIN" tests/mutation_link_follow.py > build/mut_follow.log 2>&1; then
    echo "[mut-follow] PASS（$(tail -1 build/mut_follow.log)）"
else
    echo "[mut-follow] FAIL"; grep -a "活下来\|编译失败\|锚点\|FAIL" build/mut_follow.log | head -12; FAIL=1
fi

# S-G l1（2026-11-25）：M07-F-10——M207 下发指令队列不截断过长指令。队列是纯逻辑（App/tcs/m207_txq.c），
# 用例里照抄了修复前的入队函数做对照：同一条 30 字符的指令，修复前"接受并截成半截"，现在"拒绝、队列不变"。
run m207txq     tests/test_m207_txq.c  App/tcs/m207_txq.c App/tcs/m207.c
# 它的变异验证：把队列逐个改坏（截断代替拒绝 / 上限差一 / 条数回绕算错 / 满了还写…），上面的用例必须每次都变红。
if [ -n "$PYBIN" ] && PYTHONUTF8=1 "$PYBIN" tests/mutation_m207_txq.py > build/mut_txq.log 2>&1; then
    echo "[mut-txq] PASS（$(tail -1 build/mut_txq.log)）"
else
    echo "[mut-txq] FAIL"; grep -a "活下来\|编译失败\|锚点\|FAIL" build/mut_txq.log | head -12; FAIL=1
fi

# S-G l1（2026-11-25）：M03-F-04——屏上急停入口不被主循环里的阻塞型工作挡住。这条入口的时序没法在主机上测
# （要触摸、要屏），用源码门禁钉住两件事：主循环最先看屏急停入口（在 comm_poll_tx / console_poll 之前）；
# console.c / main.c 里每个循环体不为空的 while 循环要么体内让位（console_estop_yield），要么登记了例外。
if [ -n "$PYBIN" ] && PYTHONUTF8=1 "$PYBIN" -X utf8 tests/check_estop_yield.py; then :; else echo "[estop_yield] FAIL"; FAIL=1; fi

# S-G l1（2026-11-25）：M07-F-08——关键控制台命令的审计留痕环（App/tcs/audit_ring.c，纯逻辑，独立 512 字节）。
# 关单条件"执行 5 类命令，审计环有 5 条记录"在用例第 2 节；哪些命令留痕、环满覆盖、回放顺序与容量也在里面。
run auditring   tests/test_audit_ring.c  App/tcs/audit_ring.c
# 同一份用例再按**交付档**编译一次：交付档的审计表里不得有台架专用的命令名——否则那些命令名会出现在
# 交付档产物里，tools/site/verify_prod_clean.py 按产物查台架命令名就过不了（2026-11-26 实际踩到）。
run_prod auditring_prod tests/test_audit_ring.c App/tcs/audit_ring.c
# 它的变异验证（脚手架 tests/mutkit.py）：什么命令都不留痕 / 只读命令也留痕 / 环满计数溢出 / 回放顺序反 / 不管容量。
if [ -n "$PYBIN" ] && PYTHONUTF8=1 "$PYBIN" tests/mutation_audit_ring.py > build/mut_audit.log 2>&1; then
    echo "[mut-audit] PASS（$(tail -1 build/mut_audit.log)）"
else
    echo "[mut-audit] FAIL"; grep -a "活下来\|编译失败\|锚点\|FAIL" build/mut_audit.log | head -12; FAIL=1
fi

# S-G l1（2026-11-26）：M01-F-09——perf 的 1 秒窗口 P50 / P99 / max（App/tcs/perf_hist.c，纯逻辑）。
# 关单条件"perf 输出 p50/p99/max 三个数"里的三个数由这个模块算；一个尖峰不挪动 P99、两个尖峰会、
# 窗口清空、大 n 不溢出都在用例里。链路 TX 那一半（屏端最长的一帧在线上占多久、comm_poll_tx 一拍只发一帧）
# 在上面的 linkdir 一行里（[tx budget]）。
run perfhist    tests/test_perf_hist.c  App/tcs/perf_hist.c
# 它的变异验证（脚手架 tests/mutkit.py）：分桶差一 / 百分位用 32 位乘法 / 不向上取整 / 超预算不含等于 / 清窗口连预算一起清。
if [ -n "$PYBIN" ] && PYTHONUTF8=1 "$PYBIN" tests/mutation_perf_hist.py > build/mut_perf.log 2>&1; then
    echo "[mut-perf] PASS（$(tail -1 build/mut_perf.log)）"
else
    echo "[mut-perf] FAIL"; grep -a "活下来\|编译失败\|锚点\|FAIL" build/mut_perf.log | head -12; FAIL=1
fi

# S-G m1（模式页，2026-11-26）：链路协议 TCS_PROTO_VER 1 → 2——0x20 快照末尾 9 字节（两路输出方向 + 电压电流）、
# 0x21 增量的 0x0100 / 0x0200 两个 mask 位。字节向量与 esp32/tests/test_tcs_proto.c、tools/proto_ref.py 的
# selftest 是同一组（三处互为对照）。屏端镜像的落点（Core/Src/main.c 的 snap_apply）依赖 HAL，主机编译不了。
run proto_m1    tests/test_proto_m1.c

# S-G m1（模式页，2026-11-26）：曲线页历史环多一条体温（体温卡点进去也要有曲线）。历史环只在
# TCS_USE_LVGL 下编译（目标构建由 CMake 定义），所以既有各行走不到；这里按交付档语义
# （TCS_PROFILE=0：链路不在的格子记 NaN）单独编一次。源文件与 run control 同一组。
if [ "$MODE" = gcc ]; then
    if $GCC -Wall -Wextra -std=c11 -O1 -DTCS_UNIT_TEST -DTCS_PROFILE=0 -DTCS_USE_LVGL=1 $IFLAGS \
         tests/test_hist_body.c App/tcs/control.c App/tcs/param_registry.c App/tcs/post.c App/proto/proto.c \
         -o build/tests_histbody.exe 2> build/tests_histbody.err \
       && ./build/tests_histbody.exe; then :; else
        echo "[histbody] FAIL"; cat build/tests_histbody.err 2>/dev/null | head -20; FAIL=1
    fi
else
    mkdir -p build
    HB_CMD='cl /nologo /W4 /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS /DTCS_UNIT_TEST /DTCS_PROFILE=0 /DTCS_USE_LVGL=1 /I..\App\tcs /I..\App\proto /I..\App\comm /I..\App\lvgl_ui /I..\Core\Inc'
    for s in tests/test_hist_body.c App/tcs/control.c App/tcs/param_registry.c App/tcs/post.c App/proto/proto.c; do
        HB_CMD="$HB_CMD \"$(cygpath -w "$PWD/$s")\""
    done
    HB_CMD="$HB_CMD /Fe:tests_histbody.exe"
    {
        echo '@echo off'
        echo 'call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1'
        echo "cd /d \"$(cygpath -w "$PWD/build")\""
        echo "$HB_CMD || exit /b 1"
        echo 'tests_histbody.exe'
        echo 'exit /b %ERRORLEVEL%'
    } > build/histbody_compile.bat
    if cmd //c "build\\histbody_compile.bat" > build/histbody.log 2>&1; then
        echo "[histbody] PASS（$(tail -1 build/histbody.log)）"
    else
        echo "[histbody] FAIL"; grep -a "FAIL" build/histbody.log | head -20; FAIL=1
    fi
fi

# S-G i3（集成收口，2026-11-26）：TEC 开关状态"未知"时界面画未知、不画已关（P1-09 收口）——0x20 快照的
# tec_known 位进 ui_model；另把协议参考实现 proto_ref.py 的 selftest 接进入口（TCS_PROTO_VER 3、快照 52 字节，
# 与两端 C 用例同一组字节）。
run ui_known    tests/test_ui_known.c        App/lvgl_ui/ui_model.c
# S-G i3（P1-12 的另一半）：ALM_SERVICE 在屏上有文案（红底、说清"请断电重启"、确认不掉；急停 / 超温压过它）
run ui_svc      tests/test_ui_svc.c          App/lvgl_ui/ui_model.c
# S-G i3（条目 11）：协议版本不配对时屏上的表现——版本拒收计数在涨 + 链路不新鲜 → 横幅换文案（行为不变）
run ui_mismatch tests/test_ui_mismatch.c     App/lvgl_ui/ui_model.c
if [ -n "$PYBIN" ] && "$PYBIN" tools/proto_ref.py selftest; then :; else echo "[proto_ref_selftest] FAIL"; FAIL=1; fi


# S-G x3（X06-F-06）：恒真断言静态扫描。抓 `|| 1`、`CHECK(1,`、对无符号/枚举值 `>= 0`、
# 以及"循环条件就是 *p、体内还写 || *p == '\0'"这类死项。起因：审计在 4 处断言里找到永不红的写法，
# 其中 `comm_txq_count(...) == 0 || 1` 是字面恒真（comm 丢帧时仍然绿）。
# 先跑 --self-test（4 个构造样例必须全被抓到），再扫全仓测试源码。
if [ -n "$PYBIN" ] && "$PYBIN" -X utf8 tests/check_tautology.py --self-test; then :; else
    echo "[tautology_selftest] FAIL"; FAIL=1
fi
if [ -n "$PYBIN" ] && "$PYBIN" -X utf8 tests/check_tautology.py; then :; else
    echo "[tautology] FAIL"; FAIL=1
fi

# S-G x3（X06-F-06）：断言数 / 用例数基线门禁。逐个运行主机单测，取它自己打印的 PASS=<n>
# （或语料/模糊测试的 corpus=/fed=），与 tests/baseline/assert_counts.tsv 比较，低于基线即红。
# 防的是"删断言 / 把循环次数改小 / 抽掉一半语料"这类静默减配——run_all 全绿但覆盖变少。
if [ -n "$PYBIN" ] && "$PYBIN" -X utf8 tests/assert_count.py; then :; else
    echo "[assert_count] FAIL"; FAIL=1
fi
echo "==================="
if [ "$FAIL" = 0 ]; then echo "run_all: ALL GREEN"; else echo "run_all: HAS FAILURES"; fi
exit $FAIL
