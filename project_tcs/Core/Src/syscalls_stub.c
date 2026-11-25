/* syscalls_stub.c — newlib 系统调用桩（stm32 附录 §4 printf 三件套之一）
 *
 * _write 在 console.c（真实重定向 → 控制台 TX 环）；其余占位防链接错误。
 */
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>

#undef errno
extern int errno;

int _read(int fd, char *buf, int len)
{
    (void)fd; (void)buf; (void)len;
    errno = ENOSYS;
    return -1;
}

int _close(int fd)          { (void)fd; errno = ENOSYS; return -1; }
int _lseek(int fd, int p, int d) { (void)fd; (void)p; (void)d; errno = ENOSYS; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd)         { (void)fd; return 1; }
int _kill(int pid, int sig) { (void)pid; (void)sig; errno = ENOSYS; return -1; }
int _getpid(void)           { return 1; }
void _exit(int status)      { (void)status; while (1) {} }

/* ====== 堆上界（M01-F-08，S-G h1a）======
 * 链接脚本（gcc/stm32h743xx_flash.ld）导出两个地址：
 *   _end              堆起点（.bss / .noinit 之后）
 *   _Heap_Stack_Limit 栈区底 = DTCMRAM 顶 - 8KB 栈 - 1KB 保留（脚本里的 ASSERT 用的就是它）
 * 原来 _sbrk 没有任何边界检查：堆从 _end 一路往上长，越过保留区撞进栈里——表现是
 * 随机变量损坏，而不是 malloc 返回 NULL，崩溃点离根因很远。
 * 现在：越过上界就返回 -1 并置 errno = ENOMEM，由调用方（malloc → NULL）处理。*/
extern uint8_t _end[];
extern uint8_t _Heap_Stack_Limit[];

#ifdef TCS_UNIT_TEST
/* PC 单测（tests/test_drv_pure.c）：目标板上这两个是链接脚本给的地址，PC 上摆不出来，
 * 上下界改由测试给。只在主机单测里生效，目标构建不走这里。*/
uint8_t *sbrk_ut_base(void);
uint8_t *sbrk_ut_limit(void);
#define SBRK_BASE   (sbrk_ut_base())
#define SBRK_LIMIT  (sbrk_ut_limit())
#else
#define SBRK_BASE   (_end)
#define SBRK_LIMIT  (_Heap_Stack_Limit)
#endif

volatile uint32_t g_heap_used_max;   /* 堆水位：开机以来堆最多用到过多少字节（只读遥测）*/

/* 纯逻辑（PC 单测直接调用）：堆顶指针 brk 再往上走 inc 字节，还在不在上界之内。
 * inc <= 0（归还）恒允许。返回 0=可以，-1=越界。*/
int sbrk_fits(const uint8_t *brk, int inc, const uint8_t *limit)
{
    if (inc <= 0) return 0;
    if (brk > limit) return -1;                      /* 已经在上界之外：不许再长 */
    /* 比"还剩多少"而不是比"brk+inc"：指针相加不会溢出回绕成一个看似合法的小地址 */
    return ((size_t)inc <= (size_t)(limit - brk)) ? 0 : -1;
}

void *_sbrk(int inc)
{
    static uint8_t *brk = 0;
    if (brk == 0) brk = SBRK_BASE;
    if (sbrk_fits(brk, inc, SBRK_LIMIT) != 0) {
        errno = ENOMEM;
        return (void *)-1;
    }
    uint8_t *prev = brk;
    brk += inc;
    uint32_t used = (uint32_t)(brk - SBRK_BASE);
    if (used > g_heap_used_max) g_heap_used_max = used;
    return prev;
}
