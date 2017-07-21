// Compat (darwin -> wasm32) stubs. These should disappear as soon as we
// compile mono with a proper wasm target.

#undef errno
extern int errno;
int __error(void)
{
    return errno;
}

#include <stdarg.h>

void abort(void);

void *memcpy(void *dst, void *src, int n);
void *__memcpy_chk(void *dst, void *src, long len, long dstlen)
{
    if (len > dstlen) {
        abort();
    }
    return memcpy(dst, src, len);
}

void *memmove(void *dst, void *src, int n);
void *__memmove_chk(void *dst, void *src, long len, long dstlen)
{
    if (len > dstlen) {
        abort();
    }
    return memmove(dst, src, len);
}

void *memset(void *dst, int c, int n);
void *__memset_chk(void *dst, int c, long len, long dstlen)
{
    if (len > dstlen) {
        abort();
    }
    return memset(dst, c, len);
}

char *strcpy(char *dst, char *src);
char *__strcpy_chk(char *dst, char *src, long dstlen)
{
    return strcpy(dst, src);
}

char *strncpy(char *dst, char *src, int len);
char *__strncpy_chk(char *dst, char *src, long n, long dstlen)
{
    if (n > dstlen) {
        abort();
    }
    return strncpy(dst, src, n);
}

char *strcat(char *dst, char *src);
char *__strcat_chk(char *dst, char *src, long dstlen)
{
    return strcat(dst, src);
}

int vsnprintf(char *str, int size, char *format, va_list ap);
int __vsnprintf_chk(char *str, long strlen, int flag, long len, char *format, va_list args)
{
    if (len > strlen) {
        abort();
    }
    return vsnprintf(str, len, format, args);
}

int __vsprintf_chk(char *str, int flags, long strlen, char *format, va_list va)
{
    int ret = vsnprintf(str, strlen, format, va);
    if ((long)ret >= strlen) {
        abort();
    }
    return ret;
}

int __sprintf_chk(char *str, int flag, long strlen, char *format, ...)
{
    va_list va;
    va_start(va, format);
    int ret = __vsprintf_chk(str, flag, strlen, format, va);
    va_end(va);
    return ret;
}

int __snprintf_chk(char *str, long len, int flag, long strlen, char *format, ...)
{
    va_list va;
    va_start(va, format);
    int ret = __vsnprintf_chk(str, len, flag, strlen, format, va);
    va_end(va);
    return ret;
}

#if 0
int accept(int socket, void *addr, void *addr_len);
int _accept(int socket, void *addr, void *addr_len) __attribute__((alias ("_accept$UNIX2003")));
int _accept(int socket, void *addr, void *addr_len)
{
    return accept(socket, addr, addr_len);
}


int bind(int socket, void *addr, int len);
int _bind(int socket, void *addr, int len)
{
    return bind(socket, addr, len);
}

int listen(int socket, int log);
int _listen(int socket, int log)
{
    return listen(socket, log);
}
#endif

//=> [["munmap$UNIX2003"], ["mmap$UNIX2003"], ["open$UNIX2003"], ["close$UNIX2003"], ["pthread_mutexattr_destroy$UNIX2003"], ["strerror$UNIX2003"], ["fopen$UNIX2003"], ["strftime$UNIX2003"], ["nanosleep$UNIX2003"], ["pthread_cond_init$UNIX2003"], ["pthread_cond_wait$UNIX2003"], ["write$UNIX2003"], ["fcntl$UNIX2003"], ["fwrite$UNIX2003"], ["fstat$INODE64"], ["read$UNIX2003"], ["opendir$INODE64$UNIX2003"], ["closedir$UNIX2003"], ["readdir$INODE64"], ["stat$INODE64"], ["chmod$UNIX2003"], ["lstat$INODE64"], ["fsync$UNIX2003"], ["waitpid$UNIX2003"], ["mprotect$UNIX2003"], ["kill$UNIX2003"], ["select$UNIX2003"], ["msync$UNIX2003"], ["getrlimit$UNIX2003"], ["connect$UNIX2003"], ["pthread_join$UNIX2003"], ["sleep$UNIX2003"], ["statfs$INODE64"], ["unsetenv$UNIX2003"], ["setenv$UNIX2003"], ["getfsstat$INODE64"], ["setrlimit$UNIX2003"], ["fopen$DARWIN_EXTSN"], ["strtod$UNIX2003"], ["pthread_sigmask$UNIX2003"], ["sigsuspend$UNIX2003"], ["fputs$UNIX2003"], ["accept$UNIX2003"], ["recvfrom$UNIX2003"], ["recvmsg$UNIX2003"], ["send$UNIX2003"], ["sendto$UNIX2003"], ["sendmsg$UNIX2003"], ["bind$UNIX2003"], ["getpeername$UNIX2003"], ["getsockname$UNIX2003"], ["listen$UNIX2003"]]

