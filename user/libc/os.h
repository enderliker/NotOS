#ifndef NOTOS_USER_LIBC_OS_H
#define NOTOS_USER_LIBC_OS_H
#include <stdint.h>
#include "../../kernel/syscall_numbers.h"
#include "../../kernel/platform_abi.h"
#include "../../kernel/file_abi.h"
/* No Linux syscall numbers, libc stubs, semihosting or host I/O. */
static inline int notos_raw_call(uint32_t number,uint32_t first,uint32_t second,uint32_t third) {
    uint32_t result;
    __asm__ volatile("int $0x80":"=a"(result):"a"(number),"b"(first),"c"(second),"d"(third):"memory","cc");
    return (int)result;
}
int notos_checked(int result);
int notos_errno(int error);
static inline int notos_raw_call4(uint32_t number,uint32_t first,uint32_t second,uint32_t third,uint32_t fourth) {
    uint32_t result;
    __asm__ volatile("int $0x80":"=a"(result):"a"(number),"b"(first),"c"(second),"d"(third),"S"(fourth):"memory","cc");
    return (int)result;
}
#endif
