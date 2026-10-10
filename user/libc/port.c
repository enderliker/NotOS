/* Picolibc OS adapter. Unsupported facilities fail explicitly rather than simulate success. */
#define _DEFAULT_SOURCE 1
#define _POSIX_C_SOURCE 200809L
#include "os.h"
#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <fcntl.h>
#define HEAP_BASE 0x08400000u
#define HEAP_LIMIT 0x08800000u
static int translated_error(int error) {
    switch (error) {
    case ERROR_PERMISSION:return EPERM;
    case ERROR_IO:return EIO;
    case ERROR_BAD_FD:return EBADF;
    case ERROR_MEMORY:return ENOMEM;
    case ERROR_BAD_ADDRESS:return EFAULT;
    case ERROR_INVALID_ARGUMENT:return EINVAL;
    case ERROR_NO_SYSCALL:return ENOSYS;
    case ERROR_UNSUPPORTED:return ENOTSUP;
    default:return EIO;
    }
}
static int checked(int result) {
    if (result<0) {errno=translated_error(-result);return -1;}
    return result;
}
void _exit(int status) {
    notos_raw_call(SYS_EXIT,(uint32_t)status,0,0);
    for (;;) __asm__ volatile("ud2");
}
void *sbrk(intptr_t increment) {
    uint32_t current=(uint32_t)notos_raw_call(SYS_BRK,0,0,0),requested;
    if (increment>=0) {
        if ((uint32_t)increment>HEAP_LIMIT-current) {errno=ENOMEM;return (void *)-1;}
        requested=current+(uint32_t)increment;
    } else {
        uint32_t amount=0u-(uint32_t)increment;
        if (amount>current-HEAP_BASE) {errno=ENOMEM;return (void *)-1;}
        requested=current-amount;
    }
    if ((uint32_t)notos_raw_call(SYS_BRK,requested,0,0)!=requested) {errno=ENOMEM;return (void *)-1;}
    return (void *)current;
}
ssize_t write(int fd,const void *buffer,size_t bytes) {
    if (bytes>INT_MAX) {errno=EINVAL;return -1;}
    if (bytes>UINTPTR_MAX-(uintptr_t)buffer) {errno=EFAULT;return -1;}
    if (!bytes) return checked(notos_raw_call(SYS_FD_WRITE,(uint32_t)fd,(uint32_t)buffer,0));
    size_t written=0;
    while (written<bytes) {
        size_t n=bytes-written;if (n>NOTOS_CONSOLE_LIMIT) n=NOTOS_CONSOLE_LIMIT;
        int result=checked(notos_raw_call(SYS_FD_WRITE,(uint32_t)fd,(uint32_t)buffer+written,n));
        if (result<0) return written?(ssize_t)written:-1;
        if (!result) break;
        written+=(size_t)result;
    }
    return (ssize_t)written;
}
ssize_t read(int fd,void *buffer,size_t bytes) {
    (void)buffer;
    if (fd!=0) {errno=EBADF;return -1;}
    if (!bytes) return 0;
    errno=ENOSYS;return -1;
}
int close(int fd) {errno=(fd<0 || fd>2)?EBADF:ENOSYS;return -1;}
int open(const char *path,int flags,...) {(void)path;(void)flags;errno=ENOSYS;return -1;}
off_t lseek(int fd,off_t offset,int whence) {
    (void)offset;(void)whence;errno=(fd<0 || fd>2)?EBADF:ESPIPE;return (off_t)-1;
}
int fstat(int fd,struct stat *status) {(void)fd;(void)status;errno=ENOSYS;return -1;}
int stat(const char *path,struct stat *status) {(void)path;(void)status;errno=ENOSYS;return -1;}
int unlink(const char *path) {(void)path;errno=ENOSYS;return -1;}
int isatty(int fd) {if (fd>=0 && fd<=2) return 1;errno=EBADF;return 0;}
_Static_assert(sizeof(time_t)==8 && sizeof(struct timespec)==12 && offsetof(struct timespec,tv_nsec)==8,"timespec ABI");
_Static_assert(sizeof(struct timeval)==12 && offsetof(struct timeval,tv_usec)==8,"timeval ABI");
static int clock_number(clockid_t clock) {
    if (clock==CLOCK_REALTIME) return NOTOS_CLOCK_REALTIME;
    if (clock==CLOCK_MONOTONIC) return NOTOS_CLOCK_MONOTONIC;
    errno=EINVAL;return -1;
}
int clock_gettime(clockid_t clock,struct timespec *output) {
    int number=clock_number(clock);if (number<0) return -1;
    return checked(notos_raw_call(SYS_TIME,(uint32_t)number,(uint32_t)output,0));
}
int clock_getres(clockid_t clock,struct timespec *output) {
    if (clock_number(clock)<0) return -1;
    if (output) *output=(struct timespec){clock==CLOCK_REALTIME?1:0,clock==CLOCK_REALTIME?0:1000000};
    return 0;
}
int gettimeofday(struct timeval *output,void *timezone) {
    if (timezone) {errno=ENOTSUP;return -1;}
    if (checked(notos_raw_call(SYS_TIME,NOTOS_CLOCK_REALTIME,(uint32_t)output,0))) return -1;
    output->tv_usec/=1000;
    return 0;
}
int nanosleep(const struct timespec *request,struct timespec *remaining) {
    if (!request || request->tv_sec<0 || request->tv_sec>60 || request->tv_nsec<0
        || request->tv_nsec>=1000000000 || (request->tv_sec==60 && request->tv_nsec)) {
        errno=EINVAL;return -1;
    }
    uint32_t ms=(uint32_t)request->tv_sec*1000u+(uint32_t)(request->tv_nsec+999999)/1000000u;
    int result=checked(notos_raw_call(SYS_SLEEP,ms,0,0));
    if (!result && remaining) *remaining=(struct timespec){0,0};
    return result;
}
unsigned sleep(unsigned seconds) {
    while (seconds) {
        unsigned part=seconds>60?60:seconds;
        struct timespec request={part,0};
        if (nanosleep(&request,0)) return seconds;
        seconds-=part;
    }
    return 0;
}
int getentropy(void *buffer,size_t bytes) {
    if (bytes>NOTOS_ENTROPY_LIMIT) {errno=EIO;return -1;}
    return checked(notos_raw_call(SYS_ENTROPY,(uint32_t)buffer,(uint32_t)bytes,0));
}
pid_t getpid(void) {return (pid_t)checked(notos_raw_call(SYS_PID,0,0,0));}
