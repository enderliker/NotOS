#ifndef NOTOS_TASK_H
#define NOTOS_TASK_H
#include "interrupt.h"
#include "space.h"
#include "scheduler_policy.h"
#include "fd.h"
enum task_wait_kind {
    TASK_WAIT_TIMER,
    TASK_WAIT_INPUT,
    TASK_WAIT_OUTPUT,
    TASK_WAIT_CHILD,
    TASK_WAIT_CONNECT,
    TASK_WAIT_RECEIVE,
    TASK_WAIT_SEND,
    TASK_WAIT_POLL
};

/* Count IRQ0 samples delivered at CPL3, not exact CPU or wall time. */
#define TASK_EXECUTION_BUDGET 250u
#define TASK_TLS_EXECUTION_BUDGET 2500u
#define TASK_KERNEL_STACK_BYTES 16384u
#define TASK_STACK_CANARY 0x5354414bu

enum task_exit_reason {
    TASK_EXIT_NORMAL,
    TASK_EXIT_FAULT,
    TASK_EXIT_BUDGET
};

struct task_result {
    enum task_exit_reason reason;
    u32 status;
    u32 fault_vector;
    u32 fault_error;
    u32 fault_address;
};

struct task_statistics {
    u32 syscalls;
    u32 observed_cpl;
    u32 observed_ss;
    u32 observed_root;
    u32 elapsed_ticks;
    u32 stack_valid;
    u32 cpu_ticks;
    u32 dispatches;
    u32 preemptions;
};

struct task {
    char cwd[NOTOS_PATH_BYTES];
    u32 execution_budget;
    u32 id;
    enum task_state state;
    struct user_space space;
    struct task_result result;
    struct task_statistics statistics;
    u32 expected_marker;
    u32 first_tick;
    u32 wait_started,wait_duration;
    enum task_wait_kind wait_kind;
    u32 pending_buffer,pending_bytes,pending_flags;
    int pending_fd;
    u32 poll_count;
    struct notos_pollfd poll_fds[NOTOS_POLL_MAX];
    struct fd_table files;
    u8 *kernel_stack;
    struct interrupt_frame *context;
};

/* Creation requires a zero-initialized/UNUSED object; failure rolls back resources. */
int task_create_elf(struct task *task,u32 id,const u8 *image,u32 bytes);
int task_create(struct task *task, u32 id, const u8 *image, u32 bytes);
/* Reject active tasks. Safe for zero-initialized and partially-created objects. */
int task_destroy(struct task *task);
/* Commit a prepared space; current CR3 is switched before releasing its old owner. */
struct interrupt_frame *task_exec(struct task *task, struct user_space *replacement);
int task_run_single(struct task *task);
struct task *task_current(void);
int task_context_valid(const struct task *task, const struct interrupt_frame *frame);
struct interrupt_frame *task_finish(struct interrupt_frame *frame, u32 status, enum task_exit_reason reason);
struct interrupt_frame *task_handle_fault(struct interrupt_frame *frame);
#endif
