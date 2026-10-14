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
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <spawn.h>
#include <dirent.h>
#include <sys/wait.h>
#define HEAP_BASE 0x08400000u
#define HEAP_LIMIT 0x08800000u
static int translated_error(int error) {
    switch (error) {
    case ERROR_NO_PROCESS:return ESRCH;
    case ERROR_CHILD:return ECHILD;
    case ERROR_BAD_EXECUTABLE:return ENOEXEC;
    case ERROR_NOT_DIRECTORY:return ENOTDIR;
    case ERROR_IS_DIRECTORY:return EISDIR;
    case ERROR_NOT_EMPTY:return ENOTEMPTY;
    case ERROR_NOT_FOUND:return ENOENT;
    case ERROR_AGAIN:return EAGAIN;
    case ERROR_ACCESS:return EACCES;
    case ERROR_EXISTS:return EEXIST;
    case ERROR_TOO_MANY_FILES:return EMFILE;
    case ERROR_FILE_TOO_LARGE:return EFBIG;
    case ERROR_NO_SPACE:return ENOSPC;
    case ERROR_ILLEGAL_SEEK:return ESPIPE;
    case ERROR_READ_ONLY:return EROFS;
    case ERROR_NAME_TOO_LONG:return ENAMETOOLONG;
    case ERROR_PERMISSION:return EPERM;
    case ERROR_CROSS_DEVICE:return EXDEV;
    case ERROR_IO:return EIO;
    case ERROR_BAD_FD:return EBADF;
    case ERROR_MEMORY:return ENOMEM;
    case ERROR_BAD_ADDRESS:return EFAULT;
    case ERROR_INVALID_ARGUMENT:return EINVAL;
    case ERROR_NO_SYSCALL:return ENOSYS;
    case ERROR_UNSUPPORTED:return ENOTSUP;
    case ERROR_BROKEN_PIPE:return EPIPE;
    case ERROR_NOT_SOCKET:return ENOTSOCK;
    case ERROR_PROTOCOL_OPTION:return ENOPROTOOPT;
    case ERROR_ADDRESS_FAMILY:return EAFNOSUPPORT;
    case ERROR_ADDRESS_IN_USE:return EADDRINUSE;
    case ERROR_NETWORK_UNREACHABLE:return ENETUNREACH;
    case ERROR_CONNECTION_RESET:return ECONNRESET;
    case ERROR_IS_CONNECTED:return EISCONN;
    case ERROR_NOT_CONNECTED:return ENOTCONN;
    case ERROR_TIMED_OUT:return ETIMEDOUT;
    case ERROR_CONNECTION_REFUSED:return ECONNREFUSED;
    case ERROR_ALREADY:return EALREADY;
    case ERROR_IN_PROGRESS:return EINPROGRESS;
    default:return EIO;
    }
}
int notos_checked(int result) {
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
    if (!bytes) return notos_checked(notos_raw_call(SYS_FD_WRITE,(uint32_t)fd,(uint32_t)buffer,0));
    size_t written=0;
    while (written<bytes) {
        size_t n=bytes-written;if (n>NOTOS_CONSOLE_LIMIT) n=NOTOS_CONSOLE_LIMIT;
        int result=notos_checked(notos_raw_call(SYS_FD_WRITE,(uint32_t)fd,(uint32_t)buffer+written,n));
        if (result<0) return written?(ssize_t)written:-1;
        if (!result) break;
        written+=(size_t)result;
    }
    return (ssize_t)written;
}
ssize_t read(int fd,void *buffer,size_t bytes) {
    if (bytes>INT_MAX) {errno=EINVAL;return -1;}
    if (bytes>UINTPTR_MAX-(uintptr_t)buffer) {errno=EFAULT;return -1;}
    size_t request=bytes>NOTOS_IO_LIMIT?NOTOS_IO_LIMIT:bytes;
    /* A short read is valid; never loop and accidentally block after delivering bytes. */
    return notos_checked(notos_raw_call(SYS_FD_READ,(uint32_t)fd,(uint32_t)buffer,(uint32_t)request));
}
int close(int fd) {return notos_checked(notos_raw_call(SYS_FD_CLOSE,(uint32_t)fd,0,0));}
static int open_flags(int flags,uint32_t *wire) {
    int accepted=O_ACCMODE|O_APPEND|O_CREAT|O_EXCL|O_TRUNC|O_NONBLOCK|O_CLOEXEC;
    if ((flags&~accepted) || (flags&O_ACCMODE)>O_RDWR) {errno=ENOTSUP;return -1;}
    *wire=(uint32_t)(flags&O_ACCMODE);
    if (flags&O_APPEND) *wire|=NOTOS_O_APPEND;
    if (flags&O_CREAT) *wire|=NOTOS_O_CREAT;
    if (flags&O_EXCL) *wire|=NOTOS_O_EXCL;
    if (flags&O_TRUNC) *wire|=NOTOS_O_TRUNC;
    if (flags&O_NONBLOCK) *wire|=NOTOS_O_NONBLOCK;
    if (flags&O_CLOEXEC) *wire|=NOTOS_O_CLOEXEC;
    return 0;
}
int open(const char *path,int flags,...) {
    uint32_t wire,mode=0;
    if (open_flags(flags,&wire)) return -1;
    if (flags&O_CREAT) {va_list arguments;va_start(arguments,flags);mode=(uint32_t)va_arg(arguments,int);va_end(arguments);}
    return notos_checked(notos_raw_call(SYS_FD_OPEN,(uint32_t)path,wire,mode));
}
off_t lseek(int fd,off_t offset,int whence) {
    if (offset<INT_MIN || offset>INT_MAX) {errno=EOVERFLOW;return (off_t)-1;}
    uint32_t origin;
    if (whence==SEEK_SET) origin=NOTOS_SEEK_SET;
    else if (whence==SEEK_CUR) origin=NOTOS_SEEK_CUR;
    else if (whence==SEEK_END) origin=NOTOS_SEEK_END;
    else {errno=EINVAL;return (off_t)-1;}
    return (off_t)notos_checked(notos_raw_call(SYS_FD_SEEK,(uint32_t)fd,(uint32_t)offset,origin));
}
static void convert_stat(struct stat *status,const struct notos_file_info *info) {
    *status=(struct stat){0};
    status->st_ino=info->inode;status->st_nlink=info->links;status->st_size=info->size;
    status->st_uid=info->uid;status->st_gid=info->gid;
    status->st_mtime=(time_t)info->mtime;
    status->st_atime=(time_t)info->mtime;
    status->st_ctime=(time_t)info->mtime;
    status->st_mode=(info->kind==NOTOS_FILE_DIRECTORY?S_IFDIR:info->kind==NOTOS_FILE_REGULAR?S_IFREG:info->kind==NOTOS_FILE_SOCKET?S_IFSOCK:info->kind==NOTOS_FILE_FIFO?S_IFIFO:S_IFCHR)|info->permissions;
    status->st_blksize=4096;status->st_blocks=((info->size+4095)/4096)*8;
}
static int stat_output(struct stat *status) {
    return notos_checked(notos_raw_call(SYS_USER_CHECK,(uint32_t)status,sizeof(*status),1));
}
int fstat(int fd,struct stat *status) {
    if (stat_output(status)) return -1;
    struct notos_file_info info;
    int result=notos_checked(notos_raw_call(SYS_FD_STAT,(uint32_t)fd,(uint32_t)&info,0));
    if (!result) convert_stat(status,&info);
    return result;
}
int stat(const char *path,struct stat *status) {
    if (stat_output(status)) return -1;
    struct notos_file_info info;
    int result=notos_checked(notos_raw_call(SYS_PATH_STAT,(uint32_t)path,(uint32_t)&info,0));
    if (!result) convert_stat(status,&info);
    return result;
}
int unlink(const char *path) {return notos_checked(notos_raw_call(SYS_PATH_UNLINK,(uint32_t)path,0,0));}
int rename(const char *old_path,const char *new_path) {
    return notos_checked(notos_raw_call(SYS_RENAME,(uint32_t)old_path,(uint32_t)new_path,0));
}
int isatty(int fd) {
    int result=notos_checked(notos_raw_call(SYS_FD_ISATTY,(uint32_t)fd,0,0));
    if (result<0) return 0;
    if (!result) errno=ENOTTY;
    return result;
}
int dup(int fd) {return notos_checked(notos_raw_call(SYS_FD_DUP,(uint32_t)fd,UINT32_MAX,0));}
int dup2(int fd,int destination) {
    if (destination<0) {errno=EBADF;return -1;}
    return notos_checked(notos_raw_call(SYS_FD_DUP,(uint32_t)fd,(uint32_t)destination,0));
}
int ftruncate(int fd,off_t length) {
    if (length<0 || (uint64_t)length>UINT32_MAX) {errno=EINVAL;return -1;}
    return notos_checked(notos_raw_call(SYS_FD_TRUNCATE,(uint32_t)fd,(uint32_t)length,0));
}
int fcntl(int fd,int command,...) {
    uint32_t number,argument=0;
    if (command==F_GETFL) number=NOTOS_F_GETFL;
    else if (command==F_GETFD) number=NOTOS_F_GETFD;
    else if (command==F_GETTTY) number=NOTOS_F_GETTTY;
    else if (command==F_SETFL || command==F_SETFD || command==F_SETTTY) {
        va_list args;va_start(args,command);int value=va_arg(args,int);va_end(args);
        if (command==F_SETFL) {
            number=NOTOS_F_SETFL;
            if (value&~(O_ACCMODE|O_APPEND|O_NONBLOCK)) {errno=EINVAL;return -1;}
            if (open_flags(value,&argument)) return -1;
        } else if (command==F_SETFD) {
            number=NOTOS_F_SETFD;
            if (value&~FD_CLOEXEC) {errno=EINVAL;return -1;}
            argument=value?NOTOS_FD_CLOEXEC:0;
        } else {
            number=NOTOS_F_SETTTY;
            if (value<0 || ((uint32_t)value&~NOTOS_TTY_RAW)) {errno=EINVAL;return -1;}
            argument=(uint32_t)value;
        }
    } else {errno=ENOTSUP;return -1;}
    int result=notos_checked(notos_raw_call(SYS_FD_CONTROL,(uint32_t)fd,number,argument));
    if (result<0) return -1;
    if (command==F_GETFL) {
        int flags=result&NOTOS_O_ACCMODE;
        if (result&NOTOS_O_APPEND) flags|=O_APPEND;
        if (result&NOTOS_O_NONBLOCK) flags|=O_NONBLOCK;
        return flags;
    }
    return command==F_GETFD?(result?FD_CLOEXEC:0):result;
}
_Static_assert(sizeof(time_t)==8 && sizeof(struct timespec)==12 && offsetof(struct timespec,tv_nsec)==8,"timespec ABI");
_Static_assert(sizeof(struct timeval)==12 && offsetof(struct timeval,tv_usec)==8,"timeval ABI");
static int clock_number(clockid_t clock) {
    if (clock==CLOCK_REALTIME) return NOTOS_CLOCK_REALTIME;
    if (clock==CLOCK_MONOTONIC) return NOTOS_CLOCK_MONOTONIC;
    errno=EINVAL;return -1;
}
int clock_gettime(clockid_t clock,struct timespec *output) {
    int number=clock_number(clock);if (number<0) return -1;
    return notos_checked(notos_raw_call(SYS_TIME,(uint32_t)number,(uint32_t)output,0));
}
int clock_getres(clockid_t clock,struct timespec *output) {
    if (clock_number(clock)<0) return -1;
    if (output) *output=(struct timespec){clock==CLOCK_REALTIME?1:0,clock==CLOCK_REALTIME?0:1000000};
    return 0;
}
int gettimeofday(struct timeval *output,void *timezone) {
    if (timezone) {errno=ENOTSUP;return -1;}
    if (notos_checked(notos_raw_call(SYS_TIME,NOTOS_CLOCK_REALTIME,(uint32_t)output,0))) return -1;
    output->tv_usec/=1000;
    return 0;
}
int nanosleep(const struct timespec *request,struct timespec *remaining) {
    if (!request || request->tv_sec<0 || request->tv_sec>60 || request->tv_nsec<0
        || request->tv_nsec>=1000000000 || (request->tv_sec==60 && request->tv_nsec)) {
        errno=EINVAL;return -1;
    }
    uint32_t ms=(uint32_t)request->tv_sec*1000u+(uint32_t)(request->tv_nsec+999999)/1000000u;
    int result=notos_checked(notos_raw_call(SYS_SLEEP,ms,0,0));
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
    return notos_checked(notos_raw_call(SYS_ENTROPY,(uint32_t)buffer,(uint32_t)bytes,0));
}
pid_t getpid(void) {return (pid_t)notos_checked(notos_raw_call(SYS_PID,0,0,0));}
pid_t getppid(void) {return (pid_t)notos_checked(notos_raw_call(SYS_GETPPID,0,0,0));}

int notos_errno(int error) {return translated_error(error);}

int chdir(const char *path) {return notos_checked(notos_raw_call(SYS_CHDIR, (uint32_t)path, 0, 0));}
int rmdir(const char *path) {return notos_checked(notos_raw_call(SYS_RMDIR, (uint32_t)path, 0, 0));}
char *getcwd(char *buffer, size_t bytes) {
    if (!buffer || bytes > UINT32_MAX) {errno = EINVAL; return 0;}
    return notos_checked(notos_raw_call(SYS_GETCWD, (uint32_t)buffer, (uint32_t)bytes, 0)) ? 0 : buffer;
}
int notos_exec(const char *path, const char *arguments) {
    return notos_checked(notos_raw_call(SYS_EXEC, (uint32_t)path, (uint32_t)arguments, 0));
}
int notos_readdir(int fd, struct notos_directory_entry *entry) {
    return notos_checked(notos_raw_call(SYS_READDIR, (uint32_t)fd, (uint32_t)entry, 0));
}
struct __notos_dir {
    int fd;
    struct dirent entry;
};
DIR *fdopendir(int fd) {
    struct stat st;
    if (fstat(fd, &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) {errno = ENOTDIR; return 0;}
    if (fcntl(fd, F_SETFD, FD_CLOEXEC) != 0) return 0;
    DIR *dir = calloc(1, sizeof(*dir));
    if (!dir) {errno = ENOMEM; return 0;}
    dir->fd = fd;
    return dir;
}
DIR *opendir(const char *path) {
    if (!path) {errno = EINVAL; return 0;}
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) != 0) {int saved = errno; close(fd); errno = saved; return 0;}
    if (!S_ISDIR(st.st_mode)) {close(fd); errno = ENOTDIR; return 0;}
    DIR *dir = calloc(1, sizeof(*dir));
    if (!dir) {close(fd); errno = ENOMEM; return 0;}
    dir->fd = fd;
    return dir;
}
struct dirent *readdir(DIR *dir) {
    if (!dir || dir->fd < 0) {errno = EBADF; return 0;}
    struct notos_directory_entry raw;
    int r = notos_readdir(dir->fd, &raw);
    if (r <= 0) return 0;
    dir->entry.d_ino = (ino_t)raw.inode;
    dir->entry.d_type = raw.kind == NOTOS_FILE_DIRECTORY ? DT_DIR :
        raw.kind == NOTOS_FILE_REGULAR ? DT_REG :
        raw.kind == NOTOS_FILE_CHARACTER ? DT_CHR :
        raw.kind == NOTOS_FILE_FIFO ? DT_FIFO :
        raw.kind == NOTOS_FILE_SOCKET ? DT_SOCK : DT_UNKNOWN;
    for (unsigned i = 0; i < sizeof(dir->entry.d_name); i++) dir->entry.d_name[i] = raw.name[i];
    dir->entry.d_name[sizeof(dir->entry.d_name) - 1] = 0;
    return &dir->entry;
}
void rewinddir(DIR *dir) {
    if (!dir || dir->fd < 0) return;
    lseek(dir->fd, 0, SEEK_SET);
}
int closedir(DIR *dir) {
    if (!dir || dir->fd < 0) {errno = EBADF; return -1;}
    int fd = dir->fd;
    dir->fd = -1;
    free(dir);
    return close(fd);
}
int dirfd(DIR *dir) {
    if (!dir || dir->fd < 0) {errno = EBADF; return -1;}
    return dir->fd;
}
int pipe2(int fds[2], int flags) {
    if (flags & ~(O_NONBLOCK | O_CLOEXEC)) {errno = EINVAL; return -1;}
    uint32_t wire = 0;
    if (flags & O_NONBLOCK) wire |= NOTOS_O_NONBLOCK;
    if (flags & O_CLOEXEC) wire |= NOTOS_O_CLOEXEC;
    return notos_checked(notos_raw_call(SYS_PIPE, (uint32_t)fds, wire, 0));
}
int pipe(int fds[2]) {return pipe2(fds, 0);}
pid_t waitpid(pid_t pid, int *status, int options) {
    if (options & ~WNOHANG) {errno = EINVAL; return -1;}
    uint32_t wire = (options & WNOHANG) ? NOTOS_WNOHANG : 0;
    return (pid_t)notos_checked(notos_raw_call(SYS_WAIT, (uint32_t)pid, (uint32_t)status, wire));
}
pid_t wait(int *status) {return waitpid(-1, status, 0);}
int kill(pid_t pid, int sig) {
    return notos_checked(notos_raw_call(SYS_KILL, (uint32_t)pid, (uint32_t)sig, 0));
}
int notos_spawn(const char *path, char *const argv[], char *const envp[],
    const struct notos_fd_map *fd_map, uint32_t fd_map_count, uint32_t flags) {
    if (fd_map_count > 0xffffu || flags > 0xffffu) {errno = EINVAL; return -1;}
    char *const *effective_envp = envp ? envp : environ;
    uint32_t packed = (flags << 16) | (fd_map_count & 0xffffu);
    return notos_checked(notos_raw_call5(SYS_SPAWN, (uint32_t)path, (uint32_t)argv,
        (uint32_t)effective_envp, (uint32_t)fd_map, packed));
}
struct __posix_spawn_file_actions {
    uint32_t count;
    struct notos_fd_map entries[NOTOS_FD_MAP_MAX];
};
int posix_spawn_file_actions_init(posix_spawn_file_actions_t *actions) {
    if (!actions) return EINVAL;
    struct __posix_spawn_file_actions *p = calloc(1, sizeof(*p));
    if (!p) return ENOMEM;
    *actions = p;
    return 0;
}
int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *actions) {
    if (!actions || !*actions) return EINVAL;
    free(*actions);
    *actions = 0;
    return 0;
}
int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *actions, int fd) {
    if (!actions || !*actions) return EINVAL;
    if (fd < 0 || fd >= (int)NOTOS_FD_MAP_MAX) return EBADF;
    if ((*actions)->count >= NOTOS_FD_MAP_MAX) return ENOMEM;
    (*actions)->entries[(*actions)->count++] = (struct notos_fd_map){fd, NOTOS_FD_MAP_CLOSE};
    return 0;
}
int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *actions, int fd, int newfd) {
    if (!actions || !*actions) return EINVAL;
    if (fd < 0 || fd >= (int)NOTOS_FD_MAP_MAX || newfd < 0 || newfd >= (int)NOTOS_FD_MAP_MAX) return EBADF;
    if ((*actions)->count >= NOTOS_FD_MAP_MAX) return ENOMEM;
    (*actions)->entries[(*actions)->count++] = (struct notos_fd_map){newfd, fd};
    return 0;
}
int posix_spawn(pid_t *pid, const char *path, const posix_spawn_file_actions_t *actions,
    const posix_spawnattr_t *attr, char *const argv[], char *const envp[]) {
    if (attr && *attr) return ENOTSUP;
    const struct notos_fd_map *map = (actions && *actions) ? (*actions)->entries : 0;
    uint32_t count = (actions && *actions) ? (*actions)->count : 0;
    int child = notos_spawn(path, argv, envp, map, count, 0);
    if (child < 0) return errno;
    if (pid) *pid = (pid_t)child;
    return 0;
}
