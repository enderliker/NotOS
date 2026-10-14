#ifndef NOTOS_PROCESS_ABI_H
#define NOTOS_PROCESS_ABI_H
#include "base.h"
/* Wire ABI for process creation, descriptor remapping, waiting and termination. */
#define NOTOS_SPAWN_ARG_MAX 16u
#define NOTOS_SPAWN_ENV_MAX 16u
#define NOTOS_SPAWN_STR_BYTES 512u
#define NOTOS_FD_MAP_MAX 16u
#define NOTOS_FD_MAP_CLOSE (-1)
#define NOTOS_WNOHANG 1u
#define NOTOS_SIG_MAX 31
#define NOTOS_PROCESS_NAME_BYTES 24u
#define NOTOS_PROCESS_STATE_READY 1u
#define NOTOS_PROCESS_STATE_RUNNING 2u
#define NOTOS_PROCESS_STATE_BLOCKED 3u
#define NOTOS_PROCESS_STATE_ZOMBIE 4u
#define NOTOS_PROCESS_WAIT_NONE 0u
#define NOTOS_PROCESS_WAIT_TIMER 1u
#define NOTOS_PROCESS_WAIT_INPUT 2u
#define NOTOS_PROCESS_WAIT_OUTPUT 3u
#define NOTOS_PROCESS_WAIT_CHILD 4u
#define NOTOS_PROCESS_WAIT_POLL 5u
#define NOTOS_PROCESS_WAIT_SOCKET 6u
#define NOTOS_POWER_REBOOT 1u
#define NOTOS_POWER_POWEROFF 2u
#define NOTOS_POWER_HALT 3u
struct notos_fd_map { int child_fd, parent_fd; };
_Static_assert(sizeof(struct notos_fd_map) == 8, "fd_map wire ABI");
/* 44-byte process snapshot entry returned by SYS_PROCESS_LIST (49). */
struct notos_process_info {
    u32 pid;
    u32 ppid;
    u32 state;
    u32 wait_kind;
    u32 cpu_ticks;
    char name[NOTOS_PROCESS_NAME_BYTES];
};
_Static_assert(sizeof(struct notos_process_info) == 44, "process_info wire ABI");
#endif
