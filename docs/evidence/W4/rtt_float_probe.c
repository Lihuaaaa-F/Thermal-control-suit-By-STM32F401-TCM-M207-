/* tests/rtt_float_probe.c — F14 取证 harness（任务书 v13.4 §5.1 / W4.1）
 *
 * 目的：用 PC 主机直接链入目标机同源的 App/rtt/segger_rtt.c，复现
 *       SEGGER_RTT_printf 对 %f 的处理（switch 只有 d/i/u/x/X/s/c，'f' 落
 *       default 原样吐出字面量且 va_arg 不消费），并对照官方推荐修复模式
 *       （snprintf 预格式化 + SEGGER_RTT_WriteString，SEGGER 官方 RTT
 *       printf 同样不支持浮点，官方文档明示）。
 *
 * 复刻的 4 处点位（修复前形态，值取真实域内的代表值）：
 *   m207.c:425  "[WARN] m207 implausible ch=%u %.1f->%.1f (dt=%.1fs)\n"
 *   m207.c:461  "[WARN] m207 set NAK ch=%u set=%.1f echo=%.1f\n"
 *   tmp117.c:59 "[SIM] body temp forced %.1f"  (+WriteString("\n"))
 *   tmp117.c:161 "[WARN] tmp117 implausible sample %.2f dropped\n"
 *
 * 编译（主机，随 run_all 同款编译器策略）：
 *   cl /nologo /W4 /std:c11 /utf-8 tests\rtt_float_probe.c /Fe:rtt_float_probe.exe
 * 或 gcc -Wall -Wextra -std=c11 -IApp/rtt tests/rtt_float_probe.c -o rtt_float_probe
 */
#include <stdio.h>
#ifdef _MSC_VER
#define __attribute__(x)   /* MSVC 主机取证编译：GNU 属性不参与观测，语法放行 */
#endif
#include "segger_rtt.c"   /* 直接包含：需要访问 static rtt_up_buf 做观测 */

static void dump(const char *tag)
{
    printf("%s |%.*s|\n", tag, (int)_SEGGER_RTT.aUp[0].WrOff, rtt_up_buf);
    _SEGGER_RTT.aUp[0].WrOff = 0;   /* 清空观测窗，供下一组 */
}

int main(void)
{
    puts("=== F14 修复前后对照（同一格式串/同一实参） ===");

    /* --- 修复前形态：直接把 %f 交给自研 printf（现状即坏） --- */
    SEGGER_RTT_printf(0, "[WARN] m207 implausible ch=%u %.1f->%.1f (dt=%.1fs)\n",
                      1u, 36.5, 37.9, 2.0);
    dump("BEFORE m207.c:425 ");
    SEGGER_RTT_printf(0, "[WARN] m207 set NAK ch=%u set=%.1f echo=%.1f\n",
                      2u, 37.0, 0.0);
    dump("BEFORE m207.c:461 ");
    SEGGER_RTT_printf(0, "[SIM] body temp forced %.1f", 38.5);
    SEGGER_RTT_WriteString("\n");
    dump("BEFORE tmp117.c:59 ");
    SEGGER_RTT_printf(0, "[WARN] tmp117 implausible sample %.2f dropped\n", 55.25);
    dump("BEFORE tmp117.c:161");

    /* --- 修复后形态：官方推荐模式（浮点交给 C 库 snprintf） --- */
    {
        char b[96];
        snprintf(b, sizeof b, "[WARN] m207 implausible ch=%u %.1f->%.1f (dt=%.1fs)\n",
                 1u, 36.5, 37.9, 2.0);
        SEGGER_RTT_WriteString(b);
        dump("AFTER  m207.c:425 ");

        snprintf(b, sizeof b, "[WARN] m207 set NAK ch=%u set=%.1f echo=%.1f\n",
                 2u, 37.0, 0.0);
        SEGGER_RTT_WriteString(b);
        dump("AFTER  m207.c:461 ");

        snprintf(b, sizeof b, "[SIM] body temp forced %.1f\n", 38.5);
        SEGGER_RTT_WriteString(b);
        dump("AFTER  tmp117.c:59 ");

        snprintf(b, sizeof b, "[WARN] tmp117 implausible sample %.2f dropped\n", 55.25);
        SEGGER_RTT_WriteString(b);
        dump("AFTER  tmp117.c:161");
    }

    puts("=== 判读：BEFORE 行数值字段为字面量 %.1f/%.2f = 日志静默损坏实锤 ===");

    /* --- F16（W4.1 找茬新发现）：'l' 长度修饰符同样无 case --- */
    SEGGER_RTT_printf(0, "[POST] badge reshow %lu ms\n", 3000ul);
    dump("F16BEFORE post.c:184 ");
    SEGGER_RTT_printf(0, "RTT: tp_diag THGROUP reg=0x%02x wrote=%u read=%u %s (nak +%lu/%lu)\n",
                      0x80u, 22u, 22u, "OK", 0ul, 1ul);
    dump("F16BEFORE ft5446.c:422");
    {
        char b[96];
        snprintf(b, sizeof b, "[POST] badge reshow %lu ms\n", 3000ul);
        SEGGER_RTT_WriteString(b);
        dump("F16AFTER  post.c:184 ");
        snprintf(b, sizeof b,
                 "RTT: tp_diag THGROUP reg=0x%02x wrote=%u read=%u %s (nak +%lu/%lu)\n",
                 0x80u, 22u, 22u, "OK", 0ul, 1ul);
        SEGGER_RTT_WriteString(b);
        dump("F16AFTER  ft5446.c:422");
    }

    puts("=== F16 判读：%lu 同样吐字面量（switch 无 'l' case，va_arg 不消费） ===");
    return 0;
}
