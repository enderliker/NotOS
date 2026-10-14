#ifndef NOTOS_USER_LIBC_OS_H
#define NOTOS_USER_LIBC_OS_H
#include <stdint.h>
#include "../../kernel/syscall_numbers.h"
#include "../../kernel/platform_abi.h"
#include "../../kernel/file_abi.h"
#include "../../kernel/process_abi.h"
/* No Linux syscall numbers, libc stubs, semihosting or host I/O. */
static inline int notos_raw_call(uint32_t number,uint32_t first,uint32_t second,uint32_t third) {
    uint32_t result;
    __asm__ volatile("int $0x80":"=a"(result):"a"(number),"b"(first),"c"(second),"d"(third):"memory","cc");
    return (int)result;
}
int notos_checked(int result);
int notos_errno(int error);
/* Space-separated bounded arguments; success replaces this process and never returns. */
int notos_exec(const char *path, const char *arguments);
/* Spawns a new child process with explicit argv, envp, and fd_map; returns child PID or -1 with errno. */
int notos_spawn(const char *path, char *const argv[], char *const envp[],
    const struct notos_fd_map *fd_map, uint32_t fd_map_count, uint32_t flags);
int pipe2(int fds[2], int flags);
/* Returns 1 for one entry, 0 at EOF, or -1 with errno. */
int notos_readdir(int fd, struct notos_directory_entry *entry);
static inline int notos_raw_call4(uint32_t number,uint32_t first,uint32_t second,uint32_t third,uint32_t fourth) {
    uint32_t result;
    __asm__ volatile("int $0x80":"=a"(result):"a"(number),"b"(first),"c"(second),"d"(third),"S"(fourth):"memory","cc");
    return (int)result;
}
static inline int notos_raw_call5(uint32_t number,uint32_t first,uint32_t second,uint32_t third,uint32_t fourth,uint32_t fifth) {
    uint32_t result;
    __asm__ volatile("int $0x80":"=a"(result):"a"(number),"b"(first),"c"(second),"d"(third),"S"(fourth),"D"(fifth):"memory","cc");
    return (int)result;
}
#endif
