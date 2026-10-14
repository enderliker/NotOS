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
struct notos_fd_map { int child_fd, parent_fd; };
_Static_assert(sizeof(struct notos_fd_map) == 8, "fd_map wire ABI");
#endif
