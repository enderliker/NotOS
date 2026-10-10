#ifndef NOTOS_TASK_H
#define NOTOS_TASK_H
#include "interrupt.h"
#include "space.h"

#define TASK_EXECUTION_BUDGET 250u

enum task_state {
    TASK_UNUSED,
    TASK_READY,
    TASK_RUNNING,
    TASK_FINISHED
};

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
};

struct task {
    u32 id;
    enum task_state state;
    struct user_space space;
    struct task_result result;
    struct task_statistics statistics;
    u32 expected_marker;
    u32 started_ms;
};

int task_create(struct task *task, u32 id, const u8 *image, u32 bytes);
int task_destroy(struct task *task);
int task_run_single(struct task *task);
struct task *task_current(void);
void task_finish(u32 status, enum task_exit_reason reason) __attribute__((noreturn));
void task_handle_fault(struct interrupt_frame *frame);
void task_handle_timer(struct interrupt_frame *frame);
#endif
