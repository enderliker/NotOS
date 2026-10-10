#ifndef NOTOS_SYSCALL_NUMBERS_H
#define NOTOS_SYSCALL_NUMBERS_H
/* Shared by C handlers and the bundled assembly program. */
#define SYS_EXIT 0
#define SYS_TICKS 1
#define SYS_PROBE 2
#define SYS_WRITE 3
#define SYS_TEST_REPORT 4
#define SYS_YIELD 5
#define SYSCALL_WRITE_LIMIT 160
#define ERROR_PERMISSION 1
#define ERROR_BAD_ADDRESS 14
#define ERROR_INVALID_ARGUMENT 22
#define ERROR_NO_SYSCALL 38
#endif
