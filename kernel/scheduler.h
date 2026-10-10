#ifndef NOTOS_SCHEDULER_H
#define NOTOS_SCHEDULER_H
#include "task.h"
#define SCHEDULER_QUANTUM_TICKS 5u

enum schedule_cause {
    SCHEDULE_START,
    SCHEDULE_TIMER,
    SCHEDULE_EXIT,
    SCHEDULE_FAULT,
    SCHEDULE_BUDGET,
    SCHEDULE_YIELD
};

struct scheduler_statistics {
    u32 switches;
    u32 preemptions;
    u32 yields;
};

/* Synchronous batch. Reject invalid input before changing any task state. */
int scheduler_run(struct task *const tasks[], unsigned count);
struct task *scheduler_current(void);
/* IRQ/syscall paths return the frame assembly must restore; may leave this stack. */
struct interrupt_frame *scheduler_tick(struct interrupt_frame *frame);
struct interrupt_frame *scheduler_finish(struct interrupt_frame *frame, enum schedule_cause cause);
struct interrupt_frame *scheduler_yield(struct interrupt_frame *frame);
const struct scheduler_statistics *scheduler_statistics(void);
void scheduler_report(void);
#endif
