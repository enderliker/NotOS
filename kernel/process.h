#ifndef NOTOS_PROCESS_H
#define NOTOS_PROCESS_H
#include "base.h"
#include "process_abi.h"
struct task;
u32 process_spawn_elf_args(const char *name,const u8 *image,u32 bytes,const char *argument);
/* Return a managed PID, or zero after rolling back failed creation. */
u32 process_spawn_elf_split(const char *name,const u8 *image,u32 bytes,const char *arguments);
u32 process_spawn_elf(const char *name,const u8 *image,u32 bytes);
/* Spawn a child from a ring-3 parent with explicit argv, envp, and fd_map. */
int process_spawn_user(struct task *parent,const char *path,const char *const *argv,u32 argc,
    const char *const *envp,u32 envc,const struct notos_fd_map *maps,u32 map_count);
/* Reap a matching zombie child, return 0 for WNOHANG, or -ERROR_AGAIN to block. */
int process_wait_try(u32 parent_id,int target_pid,u32 flags,u32 *status_out);
/* Return live parent PID, or 0 if kernel-spawned or orphaned. */
u32 process_getppid(u32 pid);
/* Terminate a live non-running process with status 128+sig, or probe with sig=0. */
int process_kill(u32 pid,u32 sig);
/* Copy up to capacity live or zombie process descriptors and return the active count. */
u32 process_list(struct notos_process_info *entries,u32 capacity);
/* Suppress routine completion output; faults and cleanup errors remain visible. */
void process_quiet(u32 pid);
void process_set_foreground(u32 pid,int reprompt);
int process_foreground_active(void);
void process_foreground_input(u8 byte);
/* Update the visible slot name when SYS_EXEC replaces a running task image. */
void process_rename(u32 pid, const char *name);
/* Whether shell-requested process diagnostics are enabled. */
int process_trace_enabled(void);
void process_poll(void);
void process_input_command(const char *argument,int eof);
void process_command(const char *command, const char *argument);
/* Creates a managed ring-3 task; image copied into private code/data pages. */
u32 process_spawn_image(const char *name, const u8 *code, u32 code_bytes,
    const u8 *data, u32 data_bytes);
#endif
