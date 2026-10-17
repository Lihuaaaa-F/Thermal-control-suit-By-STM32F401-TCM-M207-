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

/* 堆：链接脚本提供 _end（bss 尾）。TCS 不用 malloc，桩仅防空引用。*/
extern uint8_t _end;
void *_sbrk(int inc)
{
    static uint8_t *brk = 0;
    if (brk == 0) brk = &_end;
    uint8_t *prev = brk;
    brk += inc;
    return prev;
}
